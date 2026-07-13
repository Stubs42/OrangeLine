/*
	LanesVoiceAllocator.hpp

	Author: Dieter Stubler

Copyright (C) 2019 Dieter Stubler

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef LANES_VOICE_ALLOCATOR_HPP
#define LANES_VOICE_ALLOCATOR_HPP

#include "LanesShared.hpp"

/*
	Reusable per-expander voice-stealing allocator. Each Lanes expander (LanesCV, LanesMidi,
	...) owns its own instance, sized to ITS OWN CAPACITY (max simultaneous voices per lane)
	- independent of any other expander's capacity, and independent of the Hub (LANES),
	which no longer merges or steals voices itself - see LanesShared.hpp's
	LanesHubInterface, which only exposes raw, already-quantized/defaulted per-source state.

	process() re-derives this tick's per-lane slot assignment from the Hub's raw per-source
	state every call, running the identical merge/grow/steal-oldest algorithm LANES itself
	used before the Hub/Expander split (same "merge into existing same-pitch slot" / "reuse
	a freed slot" / "grow up to CAPACITY" / "steal the oldest slot" ladder, and the same
	"lane or pitch changed while gate held: release then reacquire" edge handling).

	Not thread-shared: each expander module owns exactly one instance per capacity it needs
	(in practice one, since a module has one CAPACITY), all state below is private to it.
*/
template <int CAPACITY>
struct LanesVoiceAllocator
{
	// Per-source-channel edge-detection state (which lane-slot, if any, each of the
	// NUM_SOURCES * POLY_CHANNELS source-channels currently contributes to).
	bool  oldGate [NUM_SOURCES][POLY_CHANNELS];
	int   oldLane [NUM_SOURCES][POLY_CHANNELS];
	float oldPitch[NUM_SOURCES][POLY_CHANNELS];
	int   srcSlot [NUM_SOURCES][POLY_CHANNELS];

	// Per-lane slot state, sized to this allocator's own CAPACITY.
	bool  slotActive      [NUM_LANES][CAPACITY];
	float slotPitch       [NUM_LANES][CAPACITY];
	float slotVelocity    [NUM_LANES][CAPACITY];
	int   slotContributors[NUM_LANES][CAPACITY];
	unsigned long slotAge [NUM_LANES][CAPACITY];
	int   laneChannelCount[NUM_LANES];
	bool  laneOverflow    [NUM_LANES];
	unsigned long ageCounter;

	void reset()
	{
		memset (oldGate,           0, sizeof(oldGate));
		memset (oldPitch,        0.f, sizeof(oldPitch));
		memset (slotActive,        0, sizeof(slotActive));
		memset (slotPitch,       0.f, sizeof(slotPitch));
		memset (slotVelocity,    0.f, sizeof(slotVelocity));
		memset (slotContributors,  0, sizeof(slotContributors));
		memset (laneChannelCount,  0, sizeof(laneChannelCount));
		memset (slotAge,           0, sizeof(slotAge));
		memset (laneOverflow,      0, sizeof(laneOverflow));
		ageCounter = 0;
		for (int s = 0; s < NUM_SOURCES; s++)
		{
			for (int c = 0; c < POLY_CHANNELS; c++)
			{
				oldLane[s][c] = -1;
				srcSlot[s][c] = -1;
			}
		}
	}

	/**
		Try to acquire (or merge into) a slot in the given lane for the given (already
		quantized) pitch. Always succeeds: if the lane is full (CAPACITY slots all active),
		steals the oldest active slot - the evicted source-channel(s) are NOT reassigned
		automatically even if still held (see process()'s "held but unslotted" handling).
	*/
	inline int acquire (int lane, float pitch, float velocity)
	{
		for (int slot = 0; slot < laneChannelCount[lane]; slot++)
		{
			if (slotActive[lane][slot] && slotPitch[lane][slot] == pitch)
			{
				slotContributors[lane][slot]++;
				return slot;
			}
		}
		for (int slot = 0; slot < laneChannelCount[lane]; slot++)
		{
			if (!slotActive[lane][slot])
			{
				slotActive[lane][slot]       = true;
				slotPitch[lane][slot]        = pitch;
				slotVelocity[lane][slot]     = velocity;
				slotContributors[lane][slot] = 1;
				slotAge[lane][slot]          = ageCounter;
				return slot;
			}
		}
		if (laneChannelCount[lane] < CAPACITY)
		{
			int slot = laneChannelCount[lane]++;
			slotActive[lane][slot]       = true;
			slotPitch[lane][slot]        = pitch;
			slotVelocity[lane][slot]     = velocity;
			slotContributors[lane][slot] = 1;
			slotAge[lane][slot]          = ageCounter;
			return slot;
		}
		int oldestSlot = 0;
		for (int slot = 1; slot < laneChannelCount[lane]; slot++)
		{
			if (slotAge[lane][slot] < slotAge[lane][oldestSlot])
				oldestSlot = slot;
		}
		for (int s2 = 0; s2 < NUM_SOURCES; s2++)
		{
			for (int c2 = 0; c2 < POLY_CHANNELS; c2++)
			{
				if (oldLane[s2][c2] == lane && srcSlot[s2][c2] == oldestSlot)
					srcSlot[s2][c2] = -1;
			}
		}
		slotPitch[lane][oldestSlot]        = pitch;
		slotVelocity[lane][oldestSlot]     = velocity;
		slotContributors[lane][oldestSlot] = 1;
		slotAge[lane][oldestSlot]          = ageCounter;
		return oldestSlot;
	}

	/**
		Release a contribution to a lane slot. Only clears the slot (gate/velocity to 0)
		once the last contributor has released it.
	*/
	inline void release (int lane, int slot)
	{
		if (slot < 0)
			return;
		if (--slotContributors[lane][slot] <= 0)
		{
			slotActive[lane][slot]   = false;
			slotVelocity[lane][slot] = 0.f;
			slotContributors[lane][slot] = 0;
		}
	}

	/**
		Re-derive this tick's per-lane slot assignment from the Hub's raw per-source state.
		Call once per control-rate tick (skip - or call reset() instead - while hub is
		nullptr, so a disconnected expander doesn't hold stale voices forever).
	*/
	void process (LanesHubInterface *hub)
	{
		for (int lane = 0; lane < NUM_LANES; lane++)
			while (laneChannelCount[lane] > 0 && !slotActive[lane][laneChannelCount[lane] - 1])
				laneChannelCount[lane]--;

		bool overflowThisTick[NUM_LANES];
		memset (overflowThisTick, 0, sizeof(overflowThisTick));
		ageCounter++;

		for (int s = 0; s < NUM_SOURCES; s++)
		{
			for (int c = 0; c < POLY_CHANNELS; c++)
			{
				bool  gateIn  = hub->getSourceGate (s, c);
				float pitchIn = hub->getSourcePitch (s, c);
				float velIn   = hub->getSourceVelocity (s, c);
				int   laneIn  = hub->getSourceLane (s, c);

				bool  wasGate   = oldGate[s][c];
				int   prevLane  = oldLane[s][c];
				float prevPitch = oldPitch[s][c];

				bool needRelease = false;
				bool needAcquire = false;

				if (gateIn && !wasGate)
				{
					needAcquire = true;
				}
				else if (gateIn && wasGate && (laneIn != prevLane || pitchIn != prevPitch))
				{
					needRelease = true;
					needAcquire = true;
				}
				else if (!gateIn && wasGate)
				{
					needRelease = true;
				}

				if (needRelease)
				{
					release (prevLane, srcSlot[s][c]);
					srcSlot[s][c] = -1;
				}
				if (needAcquire)
				{
					srcSlot[s][c] = acquire (laneIn, pitchIn, velIn);
				}

				if (gateIn && srcSlot[s][c] < 0)
					overflowThisTick[laneIn] = true;

				oldGate[s][c]  = gateIn;
				oldLane[s][c]  = laneIn;
				oldPitch[s][c] = pitchIn;
			}
		}

		memcpy (laneOverflow, overflowThisTick, sizeof(laneOverflow));
	}

	int   getLaneChannelCount (int lane) { return laneChannelCount[lane]; }
	float getLaneVoct (int lane, int slot) { return slotPitch[lane][slot]; }
	bool  getLaneGate (int lane, int slot) { return slotActive[lane][slot]; }
	float getLaneVelocity (int lane, int slot) { return slotVelocity[lane][slot]; }
	bool  getLaneOverflow (int lane) { return laneOverflow[lane]; }
};

#endif
