/*
	RECALL.cpp

	Code for the OrangeLine module RECALL

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
#include <string>
#include <vector>
#include <stdio.h>
#include <limits.h>

#include "RECALL.hpp"

// Fixed duration for the auto-sync-at-startup case only (AUTOSYNC_JSON) - there's no
// external gate to time against there, unlike GATE_INPUT/SYNC_PARAM which stay "syncing"
// for exactly as long as they're held, however long that is.
#define AUTO_SYNC_DURATION 0.5f
// Per-tick decay for the CCGridWidgets' activity flash, tuned for moduleProcess()'s ~1.1kHz
// control rate (idleSkipCounter throttling) - fades to 0 in roughly 300ms.
#define CC_ACTIVITY_DECAY 0.003f
// Rate limit for the embedded MIDI send stage, same as CV2CC's own send loop.
#define RATE_LIMITER_PERIOD (1.f / 200.f)

// CC values are inherently 7 bit (0-127), so persisting them as one byte each rather than a
// json_real() is lossless while cutting both the JSON size and (more importantly) the number
// of jansson objects allocated per save from 128 down to 1 per array.
static std::vector<uint8_t> ccFloatsToBytes(const float *values, int count)
{
	std::vector<uint8_t> bytes(count);
	for (int i = 0; i < count; i++)
		bytes[i] = (uint8_t) clamp(std::round(values[i] / 10.f * 127.f), 0.f, 127.f);
	return bytes;
}
static void bytesToCcFloats(const std::vector<uint8_t> &bytes, float *values, int count)
{
	for (int i = 0; i < count; i++)
		values[i] = (i < (int) bytes.size() ? bytes[i] : 0) / 127.f * 10.f;
}
// Same idea for the two 128-bool enable masks - packed into a 16 byte bitset instead of 128
// json_boolean() objects.
static std::vector<uint8_t> flagsToBits(const bool *flags, int count)
{
	std::vector<uint8_t> bytes((count + 7) / 8, 0);
	for (int i = 0; i < count; i++)
		if (flags[i])
			bytes[i / 8] |= (uint8_t) (1 << (i % 8));
	return bytes;
}
static void bitsToFlags(const std::vector<uint8_t> &bytes, bool *flags, int count)
{
	for (int i = 0; i < count; i++)
		flags[i] = (i / 8 < (int) bytes.size()) && (bytes[i / 8] & (1 << (i % 8))) != 0;
}

struct RECALL : Module
{

#include "OrangeLineCommon.hpp"

	bool widgetReady = false;

	/** Embedded MIDI receive/send - RECALL absorbs CC2CV's and CV2CC's own MIDI I/O directly
		rather than sitting between two separate module instances. */
	midi::InputQueue midiInput;
	midi::Output midiOutput;

	/** Raw last-known 7 bit value per CC from the embedded MIDI input, updated the instant a
		message arrives. Always live regardless of sync state - this is what Hold1 tracks
		while not syncing. Not persisted directly; reconstructed from heldRx on load. */
	uint8_t ccValues[128];
	/** Hold1: mirrors ccValues while not syncing, freezes at its last value while syncing.
		Drives RX_OUTPUT (masked by rxEnabled) and is Hold2's fallback snapshot source. */
	float heldRx[128];
	/** Hold2: takes exactly one snapshot per sync, on the rising edge of "syncing" - from
		TX_INPUT if a cable is connected for that channel, else from heldRx. Drives TX_OUTPUT
		(always, unmasked) and is the embedded MIDI-send stage's default source. */
	float hold2[128];
	/** True while GATE_INPUT/SYNC_PARAM/autosync kept "syncing" true last tick, used only to
		detect the rising edge that triggers Hold2's snapshot + a forced full resend. */
	bool wasSyncing = false;

	/** Counts down during the auto-sync-at-startup window only; <= 0 means inactive. */
	float autoSyncTimer = -1.f;
	/** Lazily decides whether to start autoSyncTimer on the first real process() tick rather
		than at construction time, so a loaded patch's AUTOSYNC_JSON value (restored via
		dataFromJson() after the constructor runs) is what actually gets checked - mirrors
		CV2CC's needsInitialSeed pattern. */
	bool needsAutoSyncCheck = true;

	/** Last 7 bit value actually sent per CC by the embedded MIDI-send stage. Sentinel 0xFF
		forces a resend; a sync's rising edge wipes this to 0xFF for every CC, guaranteeing
		Hold2's fresh snapshot goes out complete, not just the CCs that happen to differ. */
	uint8_t lastSentValues[128];
	dsp::Timer rateLimiterTimer;
	/** True until the embedded send loop actually runs for the first time after construction/
		load. Suppresses an unwanted burst on that first pass (silently seeds lastSentValues
		from the live candidate instead of sending) unless a real sync is already active on
		that very first tick (i.e. Autosync On Start fired immediately) - mirrors CV2CC's own
		needsInitialSeed pattern, with AUTOSYNC_JSON standing in for CV2CC's separate
		FLUSH_ON_START_JSON toggle (redundant here since a sync already forces a full resend). */
	bool needsInitialSeed = true;

	/** Per-CC on/off masks for the two CCGridWidgets, persisted. Disabled on the RX grid
		forces RX_OUTPUT to 0V (mutes what reaches the rest of the patch, like CC2CV's own
		grid). Disabled on the TX grid means that CC is never actually sent as MIDI (like
		CV2CC's own grid) - TX_OUTPUT itself still always shows Hold2's true snapshot
		regardless, so an external tap/infix still sees the real value even when muted. */
	bool  rxEnabled[128];
	bool  txEnabled[128];
	/** Shadow of txEnabled from the previous tick. Re-enabling a CC deliberately does NOT
		force an immediate resend/activity-flash (Dieter: "es soll einfach nicht aufblitzen
		wenn man editiert") - normal change-detection just resumes against whatever was last
		sent before it was disabled. */
	bool  txWasEnabled[128];
	/** Per-CC 0-1 activity levels for the two grids' live traffic displays, decayed every
		tick. RX flashes on every incoming MIDI message for that CC (regardless of rxEnabled,
		matching CC2CV's own behaviour); TX flashes when Hold2's snapshot actually changes
		(regardless of txEnabled). Not persisted - purely cosmetic/runtime. */
	float rxActivity[128];
	float txActivity[128];
	/** TX grid color escalation, not persisted (purely cosmetic/runtime, like txActivity).
		txSnapshotOverride[cc] is latched only at a sync's rising edge (Hold2 only changes
		there): true when TX_INPUT was actually connected AND supplied a value different from
		what plain passthrough (heldRx) would have given - i.e. the snapshot was genuinely
		overridden, not just "something is patched". Stays as-is between syncs; recomputing it
		continuously against the live (still-moving) heldRx would be wrong, since heldRx keeps
		changing after the freeze independent of whether an override actually happened. */
	bool txSnapshotOverride[128];
	/** txSendOverride[cc] is live, recomputed every 200Hz send tick (RX_INPUT's effect on the
		actually-sent candidate is continuous, unlike TX_INPUT's one-shot snapshot): true when
		RX_INPUT is connected AND the resulting send candidate differs from Hold2. Takes visual
		priority over txSnapshotOverride when both apply (see CCGridWidget's draw()). */
	bool txSendOverride[128];
	/** Whether each CCGridWidget ignores clicks/drags - protects against accidentally toggling
		CCs while rearranging cables/modules nearby. Deliberately *not* persisted (Dieter: "das
		Persistieren der Locks ist overdesigned") - always start locked on add/load/reset, the
		lights (red = unlocked/careful, off = locked/safe) only reflect clicks since. */
	bool rxGridLocked = true;
	bool txGridLocked = true;

	RECALL()
	{
		for (int cc = 0; cc < 128; cc++)
		{
			ccValues[cc] = 0;
			heldRx[cc] = 0.f;
			hold2[cc] = 0.f;
			lastSentValues[cc] = 0xFF;
			rxEnabled[cc] = true;
			txEnabled[cc] = true;
			txWasEnabled[cc] = true;
			rxActivity[cc] = 0.f;
			txActivity[cc] = 0.f;
			txSnapshotOverride[cc] = false;
			txSendOverride[cc] = false;
		}

		moduleExtraDataToJson = [this](json_t *rootJ)
		{
			json_object_set_new(rootJ, "heldRx", json_string(string::toBase64(ccFloatsToBytes(heldRx, 128)).c_str()));
			json_object_set_new(rootJ, "hold2", json_string(string::toBase64(ccFloatsToBytes(hold2, 128)).c_str()));
			json_object_set_new(rootJ, "rxEnabled", json_string(string::toBase64(flagsToBits(rxEnabled, 128)).c_str()));
			json_object_set_new(rootJ, "txEnabled", json_string(string::toBase64(flagsToBits(txEnabled, 128)).c_str()));
			json_object_set_new(rootJ, "midiIn", midiInput.toJson());
			json_object_set_new(rootJ, "midiOut", midiOutput.toJson());
		};
		moduleExtraDataFromJson = [this](json_t *rootJ)
		{
			// json_is_string() guards matter here: an autosave written by an older version of
			// this module (before this field was a base64 string) would still have the key
			// present but as a JSON array - json_string_value() on the wrong type returns NULL,
			// and string::fromBase64(NULL) constructs a std::string from a null pointer, which
			// aborts. Treat a wrong-typed field the same as a missing one (fall back to
			// whatever the constructor already initialized) rather than crashing on load.
			json_t *heldRxJ = json_object_get(rootJ, "heldRx");
			if (heldRxJ && json_is_string(heldRxJ))
				bytesToCcFloats(string::fromBase64(json_string_value(heldRxJ)), heldRx, 128);
			json_t *hold2J = json_object_get(rootJ, "hold2");
			if (hold2J && json_is_string(hold2J))
				bytesToCcFloats(string::fromBase64(json_string_value(hold2J)), hold2, 128);
			json_t *rxEnabledJ = json_object_get(rootJ, "rxEnabled");
			if (rxEnabledJ && json_is_string(rxEnabledJ))
				bitsToFlags(string::fromBase64(json_string_value(rxEnabledJ)), rxEnabled, 128);
			json_t *txEnabledJ = json_object_get(rootJ, "txEnabled");
			if (txEnabledJ && json_is_string(txEnabledJ))
			{
				bitsToFlags(string::fromBase64(json_string_value(txEnabledJ)), txEnabled, 128);
				for (int cc = 0; cc < 128; cc++)
					txWasEnabled[cc] = txEnabled[cc];
			}
			json_t *midiInJ = json_object_get(rootJ, "midiIn");
			if (midiInJ)
				midiInput.fromJson(midiInJ);
			json_t *midiOutJ = json_object_get(rootJ, "midiOut");
			if (midiOutJ)
				midiOutput.fromJson(midiOutJ);

			// Reconstruct ccValues from the just-restored heldRx so the very first tick's
			// "heldRx tracks ccValues while not syncing" doesn't stomp the loaded value with 0
			// before any fresh MIDI has arrived.
			for (int cc = 0; cc < 128; cc++)
				ccValues[cc] = (uint8_t) clamp(std::round(heldRx[cc] / 10.f * 127.f), 0.f, 127.f);
		};

		initializeInstance();
	}

	bool moduleSkipProcess()
	{
		return (idleSkipCounter != 0);
	}

	void moduleInitStateTypes()
	{
		for (int n = 0; n < 8; n++)
		{
			setInPoly (RX_INPUT + n, true);
			setInPoly (TX_INPUT + n, true);
			setOutPoly (RX_OUTPUT + n, true);
			setOutPoly (TX_OUTPUT + n, true);
		}
	}

	/**
		Initialize json configuration by defining the lables used form json state variables
	*/
	inline void moduleInitJsonConfig()
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		setJsonLabel(STYLE_JSON, "style");
		setJsonLabel(AUTOSYNC_JSON, "autosync");

#pragma GCC diagnostic pop
	}

	inline void moduleParamConfig()
	{
		configParam(SYNC_PARAM, 0.f, 1.f, 0.f, "Sync / force resend (hold)");
		configInput(GATE_INPUT, "Sync / force resend gate");
		configParam(RX_GRID_LOCK_PARAM, 0.f, 1.f, 0.f, "Lock RX grid editing (click to toggle)");
		configParam(TX_GRID_LOCK_PARAM, 0.f, 1.f, 0.f, "Lock TX grid editing (click to toggle)");
		for (int n = 0; n < 8; n++)
		{
			configInput(RX_INPUT + n, string::f("Send override %d-%d", n * 16, n * 16 + 15));
			configOutput(RX_OUTPUT + n, string::f("RX %d-%d", n * 16, n * 16 + 15));
			configInput(TX_INPUT + n, string::f("TX inject %d-%d", n * 16, n * 16 + 15));
			configOutput(TX_OUTPUT + n, string::f("TX %d-%d", n * 16, n * 16 + 15));
		}
	}

	inline void moduleCustomInitialize() {}
	inline void moduleInitialize() {}

	void moduleReset()
	{
		for (int cc = 0; cc < 128; cc++)
		{
			ccValues[cc] = 0;
			heldRx[cc] = 0.f;
			hold2[cc] = 0.f;
			lastSentValues[cc] = 0xFF;
			rxEnabled[cc] = true;
			txEnabled[cc] = true;
			txWasEnabled[cc] = true;
			rxActivity[cc] = 0.f;
			txActivity[cc] = 0.f;
			txSnapshotOverride[cc] = false;
			txSendOverride[cc] = false;
		}
		wasSyncing = false;
		autoSyncTimer = -1.f;
		needsAutoSyncCheck = true;
		needsInitialSeed = true;
		rateLimiterTimer.reset();
		midiInput.reset();
		midiOutput.reset();
		setStateJson(AUTOSYNC_JSON, 1.f);
		rxGridLocked = true;
		txGridLocked = true;

		styleChanged = true;
	}

	/**
		Normal operation (GATE_INPUT low, SYNC_PARAM not held, no active auto-sync window):
		incoming MIDI CCs drain into ccValues every tick, Hold1 (heldRx) tracks them live and
		reaches RX_OUTPUT (masked by the RX grid) - the "receive-only" path to the rest of the
		patch. Hold2/TX_OUTPUT/the embedded MIDI-send stage all stay exactly where the last
		sync left them; the send stage still runs (rate-limited to 200Hz) so a live TX_INPUT
		injection or an external override on RX_INPUT still reaches the controller immediately,
		independent of sync state.

		On a sync's rising edge (GATE_INPUT/SYNC_PARAM/autosync): Hold2 takes exactly one
		snapshot per channel - from TX_INPUT if a cable is connected there, else from Hold1's
		current (about-to-freeze) value - *before* lastSentValues is wiped, so the guaranteed
		full resend that follows actually sends the fresh snapshot, not the stale one from
		before this sync. While syncing stays high, Hold1 stops tracking (freezes RX_OUTPUT).

		RX_INPUT is normally unconnected per channel, in which case the embedded MIDI-send
		stage reads Hold2 directly; patching a cable into it (e.g. from an external Infix
		tapping TX_OUTPUT) overrides that channel with the external value instead, live, every
		tick - independent of sync.
	*/
	inline void moduleProcess(const ProcessArgs &args)
	{
		if (styleChanged && widgetReady)
		{
			switch (int(getStateJson(STYLE_JSON)))
			{
			case STYLE_ORANGE: brightPanel->visible = false; darkPanel->visible = false; break;
			case STYLE_BRIGHT: brightPanel->visible = true;  darkPanel->visible = false; break;
			case STYLE_DARK:   brightPanel->visible = false; darkPanel->visible = true;  break;
			}
			styleChanged = false;
		}

		if (inChangeParam(RX_GRID_LOCK_PARAM))
			rxGridLocked = !rxGridLocked;
		if (inChangeParam(TX_GRID_LOCK_PARAM))
			txGridLocked = !txGridLocked;
		setStateLight(RX_GRID_LOCK_LIGHT, rxGridLocked ? 0.f : 255.f);	// red = unlocked (careful!), off = locked (safe)
		setStateLight(TX_GRID_LOCK_LIGHT, txGridLocked ? 0.f : 255.f);

		if (needsAutoSyncCheck)
		{
			needsAutoSyncCheck = false;
			if (getStateJson(AUTOSYNC_JSON) != 0.f)
				autoSyncTimer = AUTO_SYNC_DURATION;
		}

		float elapsed = args.sampleTime * float(samplesSkipped + 1);
		if (autoSyncTimer > 0.f)
			autoSyncTimer -= elapsed;

		bool gateHeld = getStateInput(GATE_INPUT) > 5.f;
		bool buttonHeld = getStateParam(SYNC_PARAM) > 0.f;
		bool autoSyncActive = autoSyncTimer > 0.f;
		bool syncing = gateHeld || buttonHeld || autoSyncActive;
		bool syncRisingEdge = syncing && !wasSyncing;
		wasSyncing = syncing;

		// --- embedded MIDI IN: drain queue into ccValues, flash RX activity ---
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame))
		{
			if (msg.getStatus() == 0xb && msg.getSize() >= 3)
			{
				uint8_t cc = msg.getNote();
				ccValues[cc] = msg.getValue();
				rxActivity[cc] = 1.f;
			}
		}

		// --- Hold1: live while not syncing, frozen while syncing ---
		for (int cc = 0; cc < 128; cc++)
		{
			rxActivity[cc] = std::max(0.f, rxActivity[cc] - CC_ACTIVITY_DECAY);
			txActivity[cc] = std::max(0.f, txActivity[cc] - CC_ACTIVITY_DECAY);
			if (!syncing)
				heldRx[cc] = ccValues[cc] / 127.f * 10.f;
		}

		// --- Hold2: one snapshot per sync, before the guaranteed resend below ---
		if (syncRisingEdge)
		{
			for (int n = 0; n < 8; n++)
			{
				bool connected = getInputConnected(TX_INPUT + n);
				int  channels  = connected ? inputs[TX_INPUT + n].getChannels() : 0;
				for (int c = 0; c < 16; c++)
				{
					int cc = n * 16 + c;
					float newValue = (c < channels) ? OL_statePoly[(TX_INPUT + n) * POLY_CHANNELS + c] : heldRx[cc];
					if (newValue != hold2[cc])
						txActivity[cc] = 1.f;
					hold2[cc] = newValue;
					txSnapshotOverride[cc] = connected && (c < channels) && (newValue != heldRx[cc]);
				}
			}
			for (int cc = 0; cc < 128; cc++)
				lastSentValues[cc] = 0xFF;
			needsInitialSeed = false;
		}

		// --- physical CV outputs: Ring B (to patch) and Column 2 (Hold2 display) ---
		for (int n = 0; n < 8; n++)
		{
			for (int c = 0; c < 16; c++)
			{
				int cc = n * 16 + c;
				setStateOutPoly(RX_OUTPUT + n, c, rxEnabled[cc] ? heldRx[cc] : 0.f);
				setStateOutPoly(TX_OUTPUT + n, c, hold2[cc]);
			}
			setOutPolyChannels(RX_OUTPUT + n, 16);
			setOutPolyChannels(TX_OUTPUT + n, 16);
		}

		// --- embedded MIDI OUT: rate-limited (200Hz) send stage ---
		if (rateLimiterTimer.process(elapsed) < RATE_LIMITER_PERIOD)
			return;
		rateLimiterTimer.time -= RATE_LIMITER_PERIOD;

		bool suppressInitialFlush = needsInitialSeed && !syncing;
		needsInitialSeed = false;

		for (int n = 0; n < 8; n++)
		{
			bool connected = getInputConnected(RX_INPUT + n);
			int  channels  = connected ? inputs[RX_INPUT + n].getChannels() : 0;
			for (int c = 0; c < 16; c++)
			{
				int cc = n * 16 + c;

				// Re-enabling a CC does NOT force an immediate resend/activity-flash
				// (Dieter: "es soll einfach nicht aufblitzen wenn man editiert") - it just
				// resumes normal change-detection against whatever was last sent before it
				// was disabled.
				txWasEnabled[cc] = txEnabled[cc];

				float candidate = (c < channels) ? OL_statePoly[(RX_INPUT + n) * POLY_CHANNELS + c] : hold2[cc];
				uint8_t value = (uint8_t) clamp(std::round(candidate / 10.f * 127.f), 0.f, 127.f);
				txSendOverride[cc] = connected && (c < channels) && (candidate != hold2[cc]);

				if (suppressInitialFlush)
				{
					lastSentValues[cc] = value;
					continue;
				}

				bool changed = (value != lastSentValues[cc]);

				if (!txEnabled[cc])
				{
					if (changed)
					{
						lastSentValues[cc] = value;
						txActivity[cc] = 1.f;
					}
					continue;
				}

				if (!changed)
					continue;
				lastSentValues[cc] = value;
				txActivity[cc] = 1.f;

				midi::Message outMsg;
				outMsg.setStatus(0xb);
				outMsg.setNote(cc);
				outMsg.setValue(value);
				outMsg.setFrame(args.frame);
				midiOutput.sendMessage(outMsg);
			}
		}
	}

	inline void moduleProcessState() {}
	inline void moduleReflectChanges() {}
};

/**
	Main Module Widget

	Coordinates extracted from res/RecallWork.svg's "Controls" layer. Two 8-jack rings and two
	8-jack columns: Ring A (right, x~80-112) = RX_INPUT, an override input for the embedded
	MIDI-send stage (normally unconnected - fed internally by Hold2). Ring B (left, x~9-41) =
	RX_OUTPUT, to the rest of the patch. Column 1 ("1. Spalte", x=55.118) = TX_INPUT, an
	optional override feeding Hold2's snapshot. Column 2 ("2. Spalte", x=66.802) = TX_OUTPUT,
	always displaying Hold2's snapshot. Both columns run top-to-bottom for bank 0-7. The two
	43.688x28.194mm reference boxes at y~10.4 hold the embedded MIDI IN (left) / MIDI OUT
	(right) MidiDisplay widgets, same size as CC2CV's/CV2CC's own.
*/
struct RECALLWidget : ModuleWidget
{
	RECALLWidget(RECALL *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RecallOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RecallBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RecallDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		MidiDisplay *inDisplay = createWidget<MidiDisplay>(calculateCoordinates(2.032f, 10.415f, 0.f));
		inDisplay->box.size = mm2px(Vec(43.688f, 28.194f));
		inDisplay->setMidiPort(module ? &module->midiInput : NULL);
		addChild(inDisplay);

		MidiDisplay *outDisplay = createWidget<MidiDisplay>(calculateCoordinates(76.196f, 10.411f, 0.f));
		outDisplay->box.size = mm2px(Vec(43.688f, 28.194f));
		outDisplay->setMidiPort(module ? &module->midiOutput : NULL);
		addChild(outDisplay);

		// Ring A (right) - RX_INPUT, bank 0-7 clockwise from top.
		static const float ringAX[8] = { 96.520f, 107.950f, 112.522f, 107.950f, 96.520f, 85.090f, 80.518f, 85.344f };
		static const float ringAY[8] = { 80.291f, 84.863f, 96.293f, 107.723f, 112.295f, 107.723f, 96.293f, 85.117f };
		for (int n = 0; n < 8; n++)
			addInput(createInputCentered<PJ301MPort>(calculateCoordinates(ringAX[n], ringAY[n], 0.f), module, RX_INPUT + n));

		// Ring B (left) - RX_OUTPUT, bank 0-7 clockwise from top.
		static const float ringBX[8] = { 25.400f, 36.830f, 41.402f, 36.830f, 25.400f, 13.970f, 9.398f, 13.970f };
		static const float ringBY[8] = { 80.265f, 84.837f, 96.267f, 107.697f, 112.269f, 107.697f, 96.267f, 84.837f };
		for (int n = 0; n < 8; n++)
			addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(ringBX[n], ringBY[n], 0.f), module, RX_OUTPUT + n));

		// Both columns - bank 0-7 top to bottom.
		static const float colY[8] = { 46.483f, 56.135f, 65.787f, 75.439f, 85.091f, 94.743f, 104.628f, 114.301f };
		for (int n = 0; n < 8; n++)
			addInput(createInputCentered<PJ301MPort>(calculateCoordinates(55.118f, colY[n], 0.f), module, TX_INPUT + n));
		for (int n = 0; n < 8; n++)
			addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(66.802f, colY[n], 0.f), module, TX_OUTPUT + n));

		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(60.960f, 24.512f, 0.f), module, GATE_INPUT));
		addParam(createParamCentered<LEDButton>(calculateCoordinates(60.960f, 33.323f, 0.f), module, SYNC_PARAM));

		CCGridWidget *rxGrid = CCGridWidget::create(calculateCoordinates(2.032f, 42.673f, 0.f), mm2px(Vec(43.688f, 22.0f)),
			module ? &module->rxEnabled[0] : NULL, module ? &module->rxActivity[0] : NULL, module ? &module->heldRx[0] : NULL,
			module ? &module->rxGridLocked : NULL);
		addChild(rxGrid);

		CCGridWidget *txGrid = CCGridWidget::create(calculateCoordinates(76.200f, 42.673f, 0.f), mm2px(Vec(43.688f, 22.0f)),
			module ? &module->txEnabled[0] : NULL, module ? &module->txActivity[0] : NULL, module ? &module->hold2[0] : NULL,
			module ? &module->txGridLocked : NULL, NULL,
			module ? &module->txSnapshotOverride[0] : NULL, module ? &module->txSendOverride[0] : NULL);
		addChild(txGrid);

		addParam(createParamCentered<VCVLatch>(calculateCoordinates(4.063f, 70.867f, 0.f), module, RX_GRID_LOCK_PARAM));
		addChild(createLightCentered<LargeLight<RedLight>>(calculateCoordinates(4.063f, 70.867f, 0.f), module, RX_GRID_LOCK_LIGHT));
		addParam(createParamCentered<VCVLatch>(calculateCoordinates(117.855f, 70.867f, 0.f), module, TX_GRID_LOCK_PARAM));
		addChild(createLightCentered<LargeLight<RedLight>>(calculateCoordinates(117.855f, 70.867f, 0.f), module, TX_GRID_LOCK_LIGHT));

		if (module)
			module->widgetReady = true;
	}

	struct RECALLStyleItem : MenuItem
	{
		RECALL *module;
		int style;
		void onAction(const event::Action &e) override
		{
			module->OL_setOutState(STYLE_JSON, float(style));
			module->styleChanged = true;
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[STYLE_JSON] == style) ? "✔" : "";
		}
	};

	struct RECALLAutosyncItem : MenuItem
	{
		RECALL *module;
		void onAction(const event::Action &e) override
		{
			module->setStateJson(AUTOSYNC_JSON, module->getStateJson(AUTOSYNC_JSON) == 0.f ? 1.f : 0.f);
		}
		void step() override
		{
			if (module)
				rightText = (module->getStateJson(AUTOSYNC_JSON) != 0.f) ? "✔" : "";
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		RECALL *module = dynamic_cast<RECALL *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		RECALLStyleItem *style1Item = new RECALLStyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		RECALLStyleItem *style2Item = new RECALLStyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		RECALLStyleItem *style3Item = new RECALLStyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		RECALLAutosyncItem *autosyncItem = new RECALLAutosyncItem();
		autosyncItem->module = module;
		autosyncItem->text = "Autosync On Start";
		menu->addChild(autosyncItem);
	}
};

Model *modelRECALL = createModel<RECALL, RECALLWidget>("RECALL");
