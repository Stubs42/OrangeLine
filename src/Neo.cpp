/*
	Neo.cpp

	Code for the OrangeLine module NEO

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
#include <functional>
#include <stdio.h>
#include <limits.h>
#include <cmath>
#include <cstring>
#include <algorithm>

#include "Neo.hpp"
// All NEO layout/geometry constants (row height, column width, global-area width, per-row
// control offsets/sizes, content-frame margins, neoMinWidthHp()/neoMaxWidthHp()) now live in
// Neo.hpp - see its own "NEO layout constants" section, the single source of truth for all of it.

struct Neo : Module, XExpanderInterface, XOExpanderInterface, NeoExpanderInterface, ExpanderBridgeInterface
{
#include "OrangeLineCommon.hpp"

	// Resolved every tick via the generic touch-once-then-persist mechanism (both sides -
	// ExpanderBridge.hpp), same as XO-family/LANES now use.
	NeoHostInterface *neoHost = nullptr;
	// The raw neighbor Module* that satisfied neoHost above - kept separately so the
	// XExpanderInterface/XOExpanderInterface relay methods below can re-cast the SAME instance to
	// whichever other interface a chained X-/XO-family Expander is looking for (Morpheus already
	// implements all three interfaces on one instance).
	Module *neoHostModule = nullptr;

	dsp::SchmittTrigger leftTrigger[NEO_NUM_ROWS];
	dsp::SchmittTrigger rightTrigger[NEO_NUM_ROWS];

	// Persistent per-instance label buffers - setJsonLabel() stores the raw char* handed to it
	// rather than copying the string, so a temporary std::string's c_str() would dangle the
	// instant the temporary is destroyed (see CLAUDE.md-adjacent lesson from XR8/XR16 today).
	// rowChannelLabelBuf/rowMemTapeLabelBuf are gone - ROW_CHANNEL_JSON/ROW_MEMTAPE_JSON were
	// both replaced by real Params (ROW_TRACK_PARAM/ROW_CHANNEL_PARAM, 2026-07-20), which Rack's
	// own base Module persistence handles without needing a jsonLabel at all.
	char rowFollowLabelBuf[NEO_NUM_ROWS][20];
	char rowPageLabelBuf[NEO_NUM_ROWS][20];
	char rowCellTypeLabelBuf[NEO_NUM_ROWS][20];

	// Persisted "NFC touch once, stays connected" target Host id - a real int64_t, not an
	// OL_state float slot, see ExpanderBridge.hpp's resolveBridgeHostId() and
	// XOShared.hpp's resolveXOHostBridge() for why that distinction matters (observed Rack
	// module ids run into the quadrillions - a float silently corrupts them). Persisted via
	// this module's own moduleExtraDataToJson/FromJson below.
	int64_t neoConnectedHostId = -1;

	/*
		Channel identity - name/color (and, later, cell-editor type/config) are properties of the
		underlying (TRACK, CHANNEL) pair, not of whichever NEO row happens to be looking at it
		right now (a row's own identity is the combination of track and channel, not channel
		alone - confirmed 2026-07-21, superseding the original 2026-07-20 channel-only design:
		NEO is meant to work with any future sequencer Host, not just Morpheus, so there is no
		assumed relationship between the same channel index across different tracks - e.g.
		channel 3 on M-05 can be named/colored completely differently than channel 3 on TAPE;
		track is treated as a fully opaque dimension, no special-casing MSEL to "inherit" whatever
		M-slot it currently mirrors). This data only ever changes at sequencer-setup time, never
		while running. Stored on the HOST itself (shared automatically across every NEO instance
		attached to it) via the generic, opaque, slug-keyed storage every Host offers for free
		(ExpanderBridgeInterface::writeExpanderData()/readExpanderData(), see ExpanderBridge.hpp) -
		Morpheus (or any future NeoHostInterface implementer) never parses or understands any of
		this, it's just a string NEO itself reads/writes under its own slug ("Neo").

		Schema (NEO's own, living, private contract - update this comment whenever a field is
		added, this IS the documentation). Renamed the top-level key from "channels" to "tracks"
		2026-07-21 when this became (track, channel)-keyed - deliberate breaking change, no
		migration: NEO hasn't shipped/released yet, so an old flat "channels" blob simply has no
		"tracks" key and every identity falls back to defaults, same as "never configured":

		{
		  "tracks": [
		    { "channels": [
		        { "name": "Kick", "color": 16711680, "cellType": "gate",  "cellConfig": {} },
		        { "name": "Lead", "color": 65280,    "cellType": "value", "cellConfig": { "default": 0.0, "min": -10, "max": 10 } },
		        ...
		      ]
		    },
		    ...
		  ]
		}

		Up to NEO_MAX_TRACKS (32) track entries, each holding up to POLY_CHANNELS (16) channel
		entries - outer array index IS the track index, inner array index IS the channel index, no
		separate "track"/"channel" keys needed. "cellType"/"cellConfig" are RESERVED for the
		abstract cell-editor system (2026-07-20 design discussion, still unspecified beyond this
		placeholder) - not read or written by any code yet, only "name" and "color" are live today.
		Every read/write below treats an entry as a whole JSON object and only ever touches the ONE
		key it cares about, so a future version that adds cellType/cellConfig will never clobber
		name/color written by this version, and vice versa.
	*/
	std::string channelName[NEO_MAX_TRACKS][POLY_CHANNELS];
	int channelColor[NEO_MAX_TRACKS][POLY_CHANNELS] = {};
	// -1 = never refreshed yet, forces the very first refreshChannelTable() call to actually
	// read - see its own comment for why comparing against the Host's cheap timestamp (rather
	// than re-parsing the JSON string every tick) is the whole point of this cache.
	int64_t channelTableSeenTimestampMs = -1;

	// Resolves neoHostModule to ExpanderBridgeInterface* - every channel-table read/write goes
	// through this, never neoHost directly, since NeoHostInterface itself has no idea the generic
	// opaque-storage mechanism exists (nor should it - see ExpanderBridge.hpp's own "deliberately
	// dumb" framing for why that separation is intentional).
	ExpanderBridgeInterface* neoHostBridge()
	{
		return neoHostModule ? dynamic_cast<ExpanderBridgeInterface*>(neoHostModule) : nullptr;
	}

	// Refreshes the local channelName[]/channelColor[] cache from the Host's own stored JSON -
	// cheap no-op in the common case (a plain int64 compare) unless another NEO instance (or this
	// one, via setChannelName()/setChannelColor() below) has actually written since we last
	// looked. Called once per moduleProcess() tick while connected - see its own call site.
	void refreshChannelTable()
	{
		ExpanderBridgeInterface *host = neoHostBridge();
		if (!host)
			return;
		int64_t ts = getHostDataTimestampMs(host);
		if (ts == channelTableSeenTimestampMs)
			return;
		channelTableSeenTimestampMs = ts;

		// Reset to sane defaults first - covers both "never configured at all" (brand new patch)
		// and "this (track, channel) entry doesn't exist/doesn't have this field yet" uniformly,
		// without needing a separate has-value check at every read site elsewhere.
		for (int t = 0; t < NEO_MAX_TRACKS; t++)
			for (int c = 0; c < POLY_CHANNELS; c++)
			{
				channelName[t][c] = "";
				channelColor[t][c] = 0xff6600; // ORANGE, matches the old CHANNEL_COLOR_JSON default
			}

		std::string raw = readHostData(host);
		if (raw.empty())
			return;
		json_error_t error;
		json_t *rootJ = json_loads(raw.c_str(), 0, &error);
		if (!rootJ)
			return;
		json_t *tracksJ = json_object_get(rootJ, "tracks");
		if (tracksJ && json_is_array(tracksJ))
		{
			size_t trackIndex;
			json_t *trackEntryJ;
			json_array_foreach(tracksJ, trackIndex, trackEntryJ)
			{
				if (trackIndex >= (size_t) NEO_MAX_TRACKS)
					break;
				json_t *channelsJ = json_object_get(trackEntryJ, "channels");
				if (!channelsJ || !json_is_array(channelsJ))
					continue;
				size_t index;
				json_t *entryJ;
				json_array_foreach(channelsJ, index, entryJ)
				{
					if (index >= (size_t) POLY_CHANNELS)
						break;
					json_t *nameJ = json_object_get(entryJ, "name");
					if (nameJ && json_is_string(nameJ))
						channelName[trackIndex][index] = json_string_value(nameJ);
					json_t *colorJ = json_object_get(entryJ, "color");
					if (colorJ && json_is_integer(colorJ))
						channelColor[trackIndex][index] = (int) json_integer_value(colorJ);
				}
			}
		}
		json_decref(rootJ);
	}

	// Shared read-modify-write for a single (track, channel)/single field - reads the CURRENT full
	// blob (not our own local cache, which may be stale relative to another instance's own more
	// recent write), mutates only the one key requested on the one entry requested, and writes the
	// whole blob back. Every other key on every entry (including ones this code doesn't know
	// about, e.g. a future cellType/cellConfig) round-trips completely untouched, since entries
	// are mutated in place rather than rebuilt from scratch. Takes ownership of `value`.
	void writeChannelField(int track, int channel, const char *key, json_t *value)
	{
		ExpanderBridgeInterface *host = neoHostBridge();
		if (!host || track < 0 || track >= NEO_MAX_TRACKS || channel < 0 || channel >= POLY_CHANNELS)
		{
			json_decref(value);
			return;
		}

		std::string raw = readHostData(host);
		json_error_t error;
		json_t *rootJ = raw.empty() ? nullptr : json_loads(raw.c_str(), 0, &error);
		if (!rootJ)
			rootJ = json_object();
		json_t *tracksJ = json_object_get(rootJ, "tracks");
		if (!tracksJ || !json_is_array(tracksJ))
		{
			tracksJ = json_array();
			json_object_set_new(rootJ, "tracks", tracksJ);
		}
		// Pad with template {"channels":[]} objects up to this track's own index - leaves every
		// earlier track entry's own existing content completely untouched.
		while (json_array_size(tracksJ) <= (size_t) track)
		{
			json_t *trackTemplateJ = json_object();
			json_object_set_new(trackTemplateJ, "channels", json_array());
			json_array_append_new(tracksJ, trackTemplateJ);
		}
		json_t *trackEntryJ = json_array_get(tracksJ, track);
		json_t *channelsJ = json_object_get(trackEntryJ, "channels");
		if (!channelsJ || !json_is_array(channelsJ))
		{
			channelsJ = json_array();
			json_object_set_new(trackEntryJ, "channels", channelsJ);
		}
		// Pad with empty objects up to this channel's own index - leaves every earlier entry's
		// own existing content completely untouched.
		while (json_array_size(channelsJ) <= (size_t) channel)
			json_array_append_new(channelsJ, json_object());
		json_t *entryJ = json_array_get(channelsJ, channel);
		json_object_set_new(entryJ, key, value); // mutates in place - every other key untouched

		char *serialized = json_dumps(rootJ, JSON_COMPACT);
		writeHostData(host, serialized ? serialized : "");
		free(serialized);
		json_decref(rootJ);

		// Force our own next refreshChannelTable() to actually re-read, rather than trusting a
		// timestamp we might already have seen (this same write bumps the Host's own timestamp,
		// but polling that on the very next tick already happens naturally either way - this just
		// makes it unconditional so this instance sees its own edit immediately, not on whatever
		// tick happens to poll next).
		channelTableSeenTimestampMs = -1;
	}

	void setChannelName(int track, int channel, const std::string &name) { writeChannelField(track, channel, "name", json_string(name.c_str())); }
	void setChannelColor(int track, int channel, int packedColor) { writeChannelField(track, channel, "color", json_integer(packedColor)); }

	/*
		Lock - lets multiple NEO instances attached to the same Host agree on a common Grid
		Rows/Full Height/width, so a stack of them (same HP position, different rack rows) reads
		as one continuous grid (2026-07-20 design, worked out together with Dieter). ANY locked-in
		instance can change the group's config; every other locked instance adopts it live -
		locking in is joining a group that stays in sync, not a one-time snapshot. Width is
		opportunistic: a locked instance always adopts the common row count, and tries to match
		the common width too, but only as far as free space allows (requestModulePos() is Rack's
		own collision test) - it keeps retrying every tick, so it grows (or shrinks) into the
		target the moment room frees up, never clipping columns in the meantime.

		Schema addition (same top-level object "tracks" lives in, see its own comment above):
		{
		  "tracks": [...],
		  "lock": {
		    "ids": [123, 456],   // module ids of every instance currently locked in - each
		                         // instance adds itself on lock, removes itself on unlock/removal
		    "rows": 6,
		    "fullHeight": true,
		    "widthHp": 32,
		    "dragging": false   // 2026-07-21: true while ANY locked instance is actively being
		                        // drag-resized - every locked instance (dragged or following) reads
		                        // this to freeze its own displayed header/column layout for the
		                        // duration (see NeoWidget::step()'s cached-layout refresh gate),
		                        // avoiding the whole-grid jitter a continuous per-tick reflow would
		                        // otherwise cause during a live drag. Only the dragged instance
		                        // writes it (onDragStart()/onDragEnd()), same single-writer rule as
		                        // widthHp.
		  }
		}
	*/
	struct NeoLockData
	{
		std::vector<int64_t> ids;
		int rows = NEO_ROWS_DEFAULT;
		bool fullHeight = false;
		int widthHp = NEO_DEFAULT_WIDTH_HP;
		bool dragging = false;
	};

	NeoLockData readLockData()
	{
		NeoLockData result;
		ExpanderBridgeInterface *host = neoHostBridge();
		if (!host)
			return result;
		std::string raw = readHostData(host);
		if (raw.empty())
			return result;
		json_error_t error;
		json_t *rootJ = json_loads(raw.c_str(), 0, &error);
		if (!rootJ)
			return result;
		json_t *lockJ = json_object_get(rootJ, "lock");
		if (lockJ)
		{
			json_t *idsJ = json_object_get(lockJ, "ids");
			if (idsJ && json_is_array(idsJ))
			{
				size_t index;
				json_t *idJ;
				json_array_foreach(idsJ, index, idJ)
					if (json_is_integer(idJ))
						result.ids.push_back(json_integer_value(idJ));
			}
			json_t *rowsJ = json_object_get(lockJ, "rows");
			if (rowsJ && json_is_integer(rowsJ))
				result.rows = (int) json_integer_value(rowsJ);
			json_t *fhJ = json_object_get(lockJ, "fullHeight");
			if (fhJ && json_is_boolean(fhJ))
				result.fullHeight = json_is_true(fhJ);
			json_t *widthJ = json_object_get(lockJ, "widthHp");
			if (widthJ && json_is_integer(widthJ))
				result.widthHp = (int) json_integer_value(widthJ);
			json_t *draggingJ = json_object_get(lockJ, "dragging");
			if (draggingJ && json_is_boolean(draggingJ))
				result.dragging = json_is_true(draggingJ);
		}
		json_decref(rootJ);
		return result;
	}

	// Same shared read-modify-write-whole-blob convention as writeChannelField() - "channels"
	// (and any other future top-level field) round-trips completely untouched. Unlike
	// writeChannelField() this replaces the WHOLE "lock" object at once rather than mutating a
	// single key - safe here since every field in it is owned end-to-end by this same code, no
	// other writer ever touches any part of it (unlike a channel entry, which cellType/cellConfig
	// will eventually also write into).
	void writeLockData(const NeoLockData &data)
	{
		ExpanderBridgeInterface *host = neoHostBridge();
		if (!host)
			return;
		std::string raw = readHostData(host);
		json_error_t error;
		json_t *rootJ = raw.empty() ? nullptr : json_loads(raw.c_str(), 0, &error);
		if (!rootJ)
			rootJ = json_object();

		json_t *lockJ = json_object();
		json_t *idsJ = json_array();
		for (int64_t lockedId : data.ids)
			json_array_append_new(idsJ, json_integer(lockedId));
		json_object_set_new(lockJ, "ids", idsJ);
		json_object_set_new(lockJ, "rows", json_integer(data.rows));
		json_object_set_new(lockJ, "fullHeight", json_boolean(data.fullHeight));
		json_object_set_new(lockJ, "widthHp", json_integer(data.widthHp));
		json_object_set_new(lockJ, "dragging", json_boolean(data.dragging));
		json_object_set_new(rootJ, "lock", lockJ);

		char *serialized = json_dumps(rootJ, JSON_COMPACT);
		writeHostData(host, serialized ? serialized : "");
		free(serialized);
		json_decref(rootJ);

		channelTableSeenTimestampMs = -1; // this write also bumped the shared timestamp - force
		                                   // our own next refreshChannelTable() to re-read promptly
	}

	// Session-only (never persisted - see NeoWidget::step()'s own lock-sync block for how these
	// are used): the width/rows/fullHeight this instance last physically CONFIRMED matching,
	// distinguishing "I just made a local edit" (propagate) from "the group's target changed"
	// (adopt) from "I'm still converging toward an already-known target" (keep retrying). -1 =
	// not yet synced, so the first locked tick always adopts rather than misreading as an edit.
	float neoLockLastSyncedRows = -1.f;
	float neoLockLastSyncedFullHeight = -1.f;
	// neoLockLastSyncedWidthHp is gone (2026-07-21) - width sync no longer tracks "did my own
	// width change" at all, see NeoWidget::step()'s own "Lock sync (width half)" comment for why.

	// neoPendingColumnTarget ("preserve column count across a Full Height toggle") is gone
	// (2026-07-22, Dieter's own KISS simplification) - it was the mechanism auto-resizing the
	// module whenever Normal and Full Height needed different widths for the same column count,
	// which directly violated the stronger rule Dieter set: the panel's own width must NEVER
	// change except from an actual user drag (or a locked instance converging toward one). See
	// NEO_DEFAULT_WIDTH_HP's own comment (Neo.hpp) for what replaced the guarantee this served.

	// Which row's color dot is currently being click-dragged, if any (2026-07-21) - transient,
	// never persisted, session-only UI signal. Set by NeoRowColorDotWidget::onButton()/
	// onDragEnd(), read by that same row's own NeoRowNameField so it can show the swatch's
	// printable NAME in place of the channel name text for the duration of the drag (Dieter's own
	// request - live feedback on which of the 22 Kelly colors is currently selected, without
	// needing a separate popup/tooltip). -1 = nothing being dragged.
	int neoColorDragRow = -1;

	// Called only from the rare, one-time "am I first or joining" moment in toggleLock() - a real
	// UI click, not a per-tick lookup, so APP->engine->getModule() here is safe (the deadlock
	// pattern this codebase already hit and fixed was specifically about calling it from every
	// tick of moduleProcess()/widget step(), not an occasional user action - see CLAUDE.md's
	// Pitfalls). Drops any id that no longer resolves to a real, still-in-the-patch NEO instance -
	// an automatic cleanup safety net against a stale id left behind by, say, a crash that
	// skipped a normal onRemove() (Dieter's own instruction, 2026-07-20).
	void pruneStaleLockIds(NeoLockData &data)
	{
		std::vector<int64_t> alive;
		for (int64_t lockedId : data.ids)
		{
			Module *m = APP->engine->getModule(lockedId);
			if (m && dynamic_cast<NeoExpanderInterface*>(m))
				alive.push_back(lockedId);
		}
		data.ids = alive;
	}

	// Right-click-free - a real panel widget (the LOCK/UNLOCK button, OLLabelButton) toggles this.
	// Handles both "I'm the first to lock in" (my own current config becomes the group's) and
	// "joining an existing group" (adopt its rows/fullHeight immediately; width converges
	// gradually via the per-tick sync in NeoWidget::step(), so a blocked instance still locks in
	// successfully at whatever width it can currently manage rather than failing the lock
	// outright).
	void toggleLock()
	{
		bool wasLocked = OL_state[LOCKED_JSON] > 0.5f;
		ExpanderBridgeInterface *host = neoHostBridge();
		if (!host)
			return; // no Host connected - nothing to lock into

		NeoLockData data = readLockData();
		if (!wasLocked)
		{
			pruneStaleLockIds(data);
			if (data.ids.empty())
			{
				// First to lock in - my own current config becomes the group's.
				data.rows = clamp((int) OL_state[ROWS_DISPLAYED_JSON], NEO_ROWS_MIN, NEO_ROWS_MAX);
				data.fullHeight = OL_state[FULL_HEIGHT_JSON] > 0.5f;
				data.widthHp = (int) std::round(OL_state[PANEL_WIDTH_HP_JSON]);
			}
			else
			{
				// Joining an existing group - adopt its rows/fullHeight now; width is left to
				// the per-tick convergence in NeoWidget::step().
				OL_setOutState(ROWS_DISPLAYED_JSON, (float) data.rows);
				OL_setOutState(FULL_HEIGHT_JSON, data.fullHeight ? 1.f : 0.f);
			}
			neoLockLastSyncedRows = (float) data.rows;
			neoLockLastSyncedFullHeight = data.fullHeight ? 1.f : 0.f;
			if (std::find(data.ids.begin(), data.ids.end(), (int64_t) id) == data.ids.end())
				data.ids.push_back((int64_t) id);
			writeLockData(data);
			OL_setOutState(LOCKED_JSON, 1.f);
		}
		else
		{
			data.ids.erase(std::remove(data.ids.begin(), data.ids.end(), (int64_t) id), data.ids.end());
			writeLockData(data);
			OL_setOutState(LOCKED_JSON, 0.f);
			neoLockLastSyncedRows = -1.f;
			neoLockLastSyncedFullHeight = -1.f;
		}
	}

	// Global FOLLOW (2026-07-21) - one toggle for every row at once, applies regardless of Host
	// connection state (unlike LOCK, which needs a Host to mean anything). See moduleProcess()'s
	// own row loop for where this is actually read.
	void toggleGlobalFollow() { OL_state[GLOBAL_FOLLOW_JSON] = (OL_state[GLOBAL_FOLLOW_JSON] > 0.5f) ? 0.f : 1.f; }

	// Full Height toggle (2026-07-22) - moved here from the right-click "Full Height" menu item.
	// Just flips the state now - no longer captures/preserves a column-count target (2026-07-22
	// KISS simplification, see neoPendingColumnTarget's own removal comment above): the panel's
	// own width never changes here at all, whatever column count the new mode's own available
	// width naturally produces is simply accepted. neoCachedLayout recomputes fresh from
	// neoComputeLayout() on the very next NeoWidget::step() tick regardless, so nothing else needs
	// doing here.
	void toggleFullHeight()
	{
		OL_setOutState(FULL_HEIGHT_JSON, OL_state[FULL_HEIGHT_JSON] > 0.5f ? 0.f : 1.f);
	}

	// Called from onRemove()/invalidateBridgeCache() below - proactively drops this instance's
	// own id from the shared list using the ALREADY-CACHED host pointer (no getModule() lookup,
	// same reasoning as disconnectNeoHost()'s own sibling cleanup) so the list never accumulates
	// a stale id for an instance that's actually gone. Safe to call even while unlocked/
	// disconnected (both branches degrade to a no-op).
	void leaveLockGroupOnRemoval()
	{
		if (OL_state[LOCKED_JSON] <= 0.5f)
			return;
		ExpanderBridgeInterface *host = neoHostBridge();
		if (!host)
			return;
		NeoLockData data = readLockData();
		data.ids.erase(std::remove(data.ids.begin(), data.ids.end(), (int64_t) id), data.ids.end());
		writeLockData(data);
	}

	Neo()
	{
		initializeInstance();

		moduleExtraDataToJson = [this](json_t *rootJ)
		{
			json_object_set_new(rootJ, "connectedHostId", json_integer(neoConnectedHostId));
		};
		moduleExtraDataFromJson = [this](json_t *rootJ)
		{
			json_t *idJ = json_object_get(rootJ, "connectedHostId");
			if (idJ && json_is_integer(idJ))
				neoConnectedHostId = json_integer_value(idJ);
		};
	}

	// The current square cell size in mm - driven ONLY by Grid Rows/Full Height, deliberately
	// NEVER by the panel's own current width (Dieter's own instruction, 2026-07-20: cell size
	// must stay visually fixed while resizing - only the visible column COUNT changes).
	float getColumnWidthMm()
	{
		bool fullHeight = OL_state[FULL_HEIGHT_JSON] > 0.5f;
		int rowsDisplayed = clamp((int) OL_state[ROWS_DISPLAYED_JSON], NEO_ROWS_MIN, NEO_ROWS_MAX);
		return neoColumnWidthMm(fullHeight, rowsDisplayed);
	}

	// The actual per-column footprint (cell + gap) - horizontal padding matches vertical padding
	// (Dieter's own instruction, 2026-07-20), so this always equals the current row pitch. Every
	// width-BUDGET computation uses this, never getColumnWidthMm() (that one's for drawing).
	float getColumnPitchMm()
	{
		bool fullHeight = OL_state[FULL_HEIGHT_JSON] > 0.5f;
		int rowsDisplayed = clamp((int) OL_state[ROWS_DISPLAYED_JSON], NEO_ROWS_MIN, NEO_ROWS_MAX);
		return neoColumnPitchMm(fullHeight, rowsDisplayed);
	}

	// THE one place a row's own current channel/track selection is read - every call site
	// (moduleProcess(), NeoRowCellsWidget, NeoRowColorDotWidget/NeoRowNameField, track/channel displays)
	// must go through these rather than reading ROW_CHANNEL_PARAM/ROW_TRACK_PARAM directly, so
	// the clamping rules can never drift between them (2026-07-20 track/channel knob redesign).
	int getRowChannel(int row)
	{
		return clamp((int) std::round(getStateParam(ROW_CHANNEL_PARAM + row)), 0, POLY_CHANNELS - 1);
	}
	// See NEO_MAX_TRACKS's own comment (Neo.hpp) for why the knob's own raw value is clamped to
	// the connected Host's actual track count here rather than reconfiguring the Param's range.
	int getRowTrack(int row)
	{
		int raw = (int) std::round(getStateParam(ROW_TRACK_PARAM + row));
		int maxTrack = neoHost ? std::max(0, neoHost->getTrackCount() - 1) : 0;
		return clamp(raw, 0, maxTrack);
	}

	// Session-only UI cache (2026-07-21 redesign) - the module's own current header width/controls
	// width/visible column count, as a single NeoLayoutResult. Refreshed by NeoWidget::step() via
	// neoComputeLayout() every tick EXCEPT while a drag (this instance's own, or any other locked
	// instance's, per NeoLockData::dragging) is in progress - see step()'s own comment for why
	// that freeze exists. Every consumer (this module's own getRowHeaderWidthMm()/
	// getControlsWidthMm()/getVisibleColumns(), NeoRowCellsWidget's draw()/click-handling) reads
	// from this SAME cache rather than recomputing independently, so they can never disagree
	// about what's actually on screen during a drag. Default-initialized against
	// NEO_DEFAULT_WIDTH_HP so a sensible value exists even before the first step() tick runs.
	NeoLayoutResult neoCachedLayout = neoComputeLayout((float) NEO_DEFAULT_WIDTH_HP * 5.08f, false, NEO_ROWS_DEFAULT);

	float getRowHeaderWidthMm() { return neoCachedLayout.headerWidthMm; }
	float getControlsWidthMm() { return neoCachedLayout.controlsWidthMm; }
	int getVisibleColumns() { return neoCachedLayout.visibleCols; }

	bool moduleSkipProcess() { return (idleSkipCounter != 0); }
	void moduleInitStateTypes() {}

	inline void moduleInitJsonConfig()
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		setJsonLabel(STYLE_JSON, "style");
		setJsonLabel(PANEL_WIDTH_HP_JSON, "panelWidthHp");
		setJsonLabel(ROWS_DISPLAYED_JSON, "rowsDisplayed");
		setJsonLabel(FULL_HEIGHT_JSON, "fullHeight");
		setJsonLabel(LOCKED_JSON, "locked");
		// ROW_HEADER_WIDTH_MM_JSON is unused (2026-07-21) - OL_state at that slot is never read
		// or written anymore (see its own enum comment, Neo.hpp), but it still needs a real
		// jsonLabel here: dataToJson()/dataFromJson() (OrangeLineCommon.hpp) index OL_jsonLabel[]
		// unconditionally for every slot up to NUM_JSONS, with no null-check - an unlabeled slot
		// is uninitialized/garbage memory, not a safe no-op, and crashes the instant a fresh
		// instance is first serialized (confirmed live, 2026-07-21: this exact omission crashed
		// Rack on module-browser creation). Keeping the label just means a permanently-0.0 dead
		// field in every save - harmless, and the only safe way to leave a slot unused without
		// deleting the enum entry outright (which every OTHER retired json field in this file
		// does instead - see CONNECTED_HOST_ID_JSON/CHANNEL_COLOR_JSON/ROW_MEMTAPE_JSON's own
		// comments - that's the real established precedent, not an unlabeled slot).
		setJsonLabel(ROW_HEADER_WIDTH_MM_JSON, "rowHeaderWidthMm");
		// CONNECTED_HOST_ID_JSON is gone - neoConnectedHostId is a real int64_t now, persisted
		// via this module's own moduleExtraDataToJson/FromJson instead (see its own comment).
		for (int r = 0; r < NEO_NUM_ROWS; r++)
		{
			snprintf(rowFollowLabelBuf[r], sizeof(rowFollowLabelBuf[r]), "rowFollow%d", r);
			setJsonLabel(ROW_FOLLOW_JSON + r, rowFollowLabelBuf[r]);
			snprintf(rowPageLabelBuf[r], sizeof(rowPageLabelBuf[r]), "rowPage%d", r);
			setJsonLabel(ROW_PAGE_JSON + r, rowPageLabelBuf[r]);
			snprintf(rowCellTypeLabelBuf[r], sizeof(rowCellTypeLabelBuf[r]), "rowCellType%d", r);
			setJsonLabel(ROW_CELLTYPE_JSON + r, rowCellTypeLabelBuf[r]);
		}
		// GLOBAL_FOLLOW_JSON was missing its own setJsonLabel call (found 2026-07-21 while
		// chasing the same crash the ROW_HEADER_WIDTH_MM_JSON comment above describes) - it's
		// actively read in moduleProcess(), so this wasn't even a "harmless unused slot" case,
		// just a plain omission from whenever this field was added.
		setJsonLabel(GLOBAL_FOLLOW_JSON, "globalFollow");

#pragma GCC diagnostic pop
	}

	inline void moduleParamConfig()
	{
		for (int r = 0; r < NEO_NUM_ROWS; r++)
		{
			// Real discrete knobs (2026-07-20), matching MidiBus's own RX/TX_CHANNEL_PARAM
			// precedent - see NEO_MAX_TRACKS's own comment for why the track knob's range is a
			// fixed ceiling rather than the connected Host's own actual track count.
			configParam(ROW_TRACK_PARAM + r, 0.f, (float) (NEO_MAX_TRACKS - 1), 0.f, string::f("Row %d Track", r + 1));
			paramQuantities[ROW_TRACK_PARAM + r]->snapEnabled = true;
			configParam(ROW_CHANNEL_PARAM + r, 0.f, (float) (POLY_CHANNELS - 1), (float) (r % POLY_CHANNELS), string::f("Row %d Channel", r + 1));
			paramQuantities[ROW_CHANNEL_PARAM + r]->snapEnabled = true;
			configParam(ROW_FOLLOW_PARAM + r, 0.f, 1.f, 0.f, string::f("Row %d Follow", r + 1));
			configParam(ROW_LEFT_PARAM + r, 0.f, 1.f, 0.f, string::f("Row %d Page Back", r + 1));
			configParam(ROW_RIGHT_PARAM + r, 0.f, 1.f, 0.f, string::f("Row %d Page Forward", r + 1));
		}
	}

	inline void moduleCustomInitialize() {}
	inline void moduleInitialize() {}

	// Unregisters from the Host's listener registry before dropping the cached pointer(s)/id -
	// see XOModuleCommon.hpp's identical disconnectXOHost() for the full reasoning. Used by
	// moduleReset(), the right-click Disconnect item, and invalidateBridgeCache() below.
	void disconnectNeoHost()
	{
		if (neoHostModule)
		{
			ExpanderBridgeInterface *hostBridge = dynamic_cast<ExpanderBridgeInterface*>(neoHostModule);
			if (hostBridge)
				hostBridge->unregisterBridgeListener(this);
		}
		neoHost = nullptr;
		neoHostModule = nullptr;
		neoConnectedHostId = -1;
	}
	// Called BY the cached Host itself, right before it's destroyed (see
	// ExpanderBridgeInterface's own comment, ExpanderBridge.hpp) - just drop the connection
	// directly (no unregister needed, the Host is already tearing down its own registry). No
	// point trying to leaveLockGroupOnRemoval() here - the shared storage is going away WITH the
	// Host, so there's nothing left to write into; just reset this instance's own local lock
	// state so it doesn't think it's still part of a group that no longer has anywhere to live.
	void invalidateBridgeCache() override
	{
		neoHost = nullptr;
		neoHostModule = nullptr;
		neoConnectedHostId = -1;
		OL_state[LOCKED_JSON] = 0.f;
		neoLockLastSyncedRows = -1.f;
		neoLockLastSyncedFullHeight = -1.f;
	}
	// Symmetric with the Host's own onRemove() (Morpheus.cpp) - proactively tells the cached
	// Host to forget this Neo instance before it's actually destroyed, using only the pointer
	// already held (no engine lookup of any kind). leaveLockGroupOnRemoval() first, while
	// neoHostModule is still valid - same reasoning, own cached pointer, no engine lookup.
	void onRemove(const RemoveEvent &e) override
	{
		leaveLockGroupOnRemoval();
		if (neoHostModule)
		{
			ExpanderBridgeInterface *hostBridge = dynamic_cast<ExpanderBridgeInterface*>(neoHostModule);
			if (hostBridge)
				hostBridge->unregisterBridgeListener(this);
		}
		Module::onRemove(e);
	}

	void moduleReset()
	{
		styleChanged = true;
		OL_state[PANEL_WIDTH_HP_JSON] = (float) NEO_DEFAULT_WIDTH_HP;
		OL_state[ROWS_DISPLAYED_JSON] = (float) NEO_ROWS_DEFAULT;
		OL_state[FULL_HEIGHT_JSON] = 0.f;
		OL_state[LOCKED_JSON] = 0.f;
		// The row header's width is no longer stored - neoComputeLayout() (Neo.hpp) derives it
		// fresh from the panel width just set above, so it's already correctly padded from the
		// very first frame with no separate settling step needed (2026-07-21 redesign - see
		// NeoLayoutResult's own comment for why this replaced the older incremental-absorb model).
		neoCachedLayout = neoComputeLayout((float) NEO_DEFAULT_WIDTH_HP * 5.08f, false, NEO_ROWS_DEFAULT);
		disconnectNeoHost();
		OL_state[GLOBAL_FOLLOW_JSON] = 1.f; // auto-follow the play cursor, on by default
		for (int r = 0; r < NEO_NUM_ROWS; r++)
		{
			// Row r's default track/channel (row r shows channel r, track TAPE) come from
			// ROW_TRACK_PARAM/ROW_CHANNEL_PARAM's own configParam() defaults now, not OL_state.
			// ROW_FOLLOW_JSON is unused (see moduleProcess()'s own note) - no default needed here.
			OL_state[ROW_PAGE_JSON + r] = 0.f;
			OL_state[ROW_CELLTYPE_JSON + r] = 1.f;      // Value (more generally useful default)
		}
		// Channel name/color are Host-shared, not this instance's own state (see their own
		// member comment) - resetting THIS NEO must never wipe out data every other NEO instance
		// attached to the same Host still relies on, so there's deliberately nothing to reset
		// here anymore.
	}

	inline void moduleProcess(const ProcessArgs &args)
	{
		// No brightPanel/darkPanel SvgPanel swap here (unlike every other module) - NeoPanelWidget
		// draws its own theme-aware flat background directly every frame instead, since a baked
		// SVG panel can't stretch to match this module's resizable width. styleChanged is left
		// alone (never consumed) - harmless, nothing depends on it being cleared.

		// Touch-once-then-persist connection (both sides now) - see resolveBridgeHostId()'s own
		// comment (ExpanderBridge.hpp). Only ever attempts a fresh touch while not yet connected;
		// once connected, stays put regardless of later physical movement until an explicit
		// "Disconnect".
		if (neoConnectedHostId == -1)
		{
			neoHost = nullptr;
			neoHostModule = nullptr;
			int64_t newId = resolveBridgeHostId({ FAMILY_NEO }, leftExpander.module, rightExpander.module);
			if (newId != -1)
				neoConnectedHostId = newId;
		}
		// Resolved via APP->engine->getModule() at most ONCE per actual connection - right after
		// the fresh touch above, or on the first tick after a patch load where
		// neoConnectedHostId is already restored from JSON but neoHost is still null on this
		// freshly-constructed instance - never on any other tick. An unconditional per-tick
		// getModule() call here used to run every single tick regardless of connection state -
		// confirmed (gdb, live freeze, 2026-07-19) to be the same class of engine deadlock found
		// and fixed elsewhere the same session (a share-locking getModule() call from inside
		// moduleProcess(), racing a queued exclusive lock request during a module add/remove).
		// Registers with the Host's listener registry the moment the pointer is first cached, so
		// the Host's own onRemove() can proactively invalidate it before being destroyed - see
		// ExpanderBridgeInterface's own comment (ExpanderBridge.hpp) for the full mechanism.
		if (neoConnectedHostId != -1 && !neoHost)
		{
			Module *m = APP->engine->getModule(neoConnectedHostId);
			NeoHostInterface *host = m ? resolveNeoHost(m) : nullptr;
			if (!host)
				neoConnectedHostId = -1; // target vanished/never existed - clear stale id
			else
			{
				neoHost = host;
				neoHostModule = m;
				ExpanderBridgeInterface *hostBridge = dynamic_cast<ExpanderBridgeInterface*>(m);
				if (hostBridge)
					hostBridge->registerBridgeListener(this);
			}
		}
		if (neoHost)
			refreshChannelTable();

		int visibleCols = getVisibleColumns();
		// One global FOLLOW applies to every row (2026-07-21) - per-row FOLLOW is deferred for now
		// (the old ROW_FOLLOW_JSON/PARAM per-row toggle is unused dead infrastructure, kept only so
		// re-introducing it later doesn't need to renumber every Param after it - see Neo.hpp).
		bool follow = OL_state[GLOBAL_FOLLOW_JSON] > 0.5f;

		for (int r = 0; r < NEO_NUM_ROWS; r++)
		{
			if (neoHost)
			{
				int channel = getRowChannel(r);
				if (follow)
				{
					// visibleCols can genuinely be 0 now (2026-07-22, no minimum column count
					// guarantee anymore) - paging is meaningless with nothing visible to page
					// through, so just leave ROW_PAGE_JSON wherever it already was rather than
					// dividing by zero.
					if (visibleCols > 0)
					{
						int cursor = neoHost->getPlayCursor(channel);
						OL_state[ROW_PAGE_JSON + r] = (float) (cursor / visibleCols);
					}
				}
				else
				{
					int loopLen = std::max(1, neoHost->getLoopLen(channel));
					int maxPage = (visibleCols > 0) ? (loopLen - 1) / visibleCols : 0;
					if (leftTrigger[r].process(params[ROW_LEFT_PARAM + r].getValue()))
						OL_state[ROW_PAGE_JSON + r] = std::max(0.f, OL_state[ROW_PAGE_JSON + r] - 1.f);
					if (rightTrigger[r].process(params[ROW_RIGHT_PARAM + r].getValue()))
						OL_state[ROW_PAGE_JSON + r] = std::min((float) maxPage, OL_state[ROW_PAGE_JSON + r] + 1.f);
				}
			}
			else
			{
				leftTrigger[r].process(params[ROW_LEFT_PARAM + r].getValue());
				rightTrigger[r].process(params[ROW_RIGHT_PARAM + r].getValue());
			}
		}
	}

	inline void moduleProcessState() {}
	inline void moduleReflectChanges() {}

	// XExpanderInterface - pure relay, NEO never uses any of these itself; only implemented so
	// an X-family Expander chained further along the rack (e.g. "X8 | NEO | Morpheus") keeps
	// resolving its own Host straight through NEO exactly as it would through any other
	// X-family member. Every method below except getXHost()/getXStyle() is genuinely dead code
	// from NEO's own perspective but must still compile.
	XHostInterface* getXHost() override { return neoHostModule ? dynamic_cast<XHostInterface*>(neoHostModule) : nullptr; }
	void setXBoundHostId(int64_t hostId, XHostInterface *hostPtr = nullptr) override {}
	int64_t getXBoundHostId() override { return -1; }
	int64_t getXSelfId() override { return -1; } // never a clone-recovery target - see the pure-
	                                              // relay comment above; -1 also correctly never
	                                              // satisfies xIsFreshClone() for any real id
	float getXStyle() override { return OL_state[STYLE_JSON]; }
	int getXKnobCount() override { return 0; }
	float getXKnobValue(int channel) override { return 0.f; }
	int getXBrowseIndex() override { return 0; }
	XParamType getXBrowsedParamType() override { return X_PARAM_CONTINUOUS; }
	NVGcolor getXBrowsedParamColor() override { return ORANGE; }
	XAlign getXBrowsedParamAlign() override { return X_ALIGN_LEFT; }
	std::string formatXValue(float raw) override { return ""; }
	void requestXValueClick(int channel) override {}
	void getXEngagedSummary(int &hostCount, int &slotCount) override { hostCount = 0; slotCount = 0; }
	bool isXKnobReady(int index) override { return true; }
	float getXBrowsedParamDisplayMin() override { return 0.f; }
	float getXBrowsedParamDisplayMax() override { return 1.f; }
	float getXBrowsedParamDisplayDefault() override { return 0.5f; }
	bool getXBrowsedParamSnap() override { return false; }
	const char* getXBrowsedParamUnit() override { return ""; }

	// XOExpanderInterface - same pure-relay reasoning as above, for "NEO | XO8"-shaped chains.
	XOHostInterface* getXOHost() override { return neoHostModule ? dynamic_cast<XOHostInterface*>(neoHostModule) : nullptr; }
	int64_t getXOConnectedHostId() override { return -1; }
	void disconnectXOHost() override {}
	float getXOStyle() override { return OL_state[STYLE_JSON]; }
	int getXOCapacity() override { return 0; }
	int getXOBrowseIndex() override { return 0; }
	XOType getXOBrowsedType() override { return XO_TYPE_CONTINUOUS; }
	NVGcolor getXOBrowsedColor() override { return ORANGE; }
	XAlign getXOBrowsedAlign() override { return X_ALIGN_LEFT; }
	std::string formatXOBrowsedValue(float raw) override { return ""; }
	int getXOBrowsedChannelCount() override { return 0; }
	float getXOBrowsedChannelValue(int channel) override { return 0.f; }
	bool getXOBrowsedChannelGateLit(int channel) override { return false; }

	// ExpanderBridgeInterface (ExpanderBridge.hpp) - the persisted connection IS this Expander's
	// own bridge id (NEO has no exclusivity concept, same as XO-family/LANES).
	int64_t getBridgeHostId() override { return neoConnectedHostId; }
	std::vector<ExpanderFamily> getBridgeFamilies() override { return getModuleFamilies(model->slug); }
	std::string getBridgeHostName() override { return ""; } // Expander, not a Host
};

/**
	Resize handle - own implementation inspired by VCV core's own Blank.cpp/ModuleResizeHandle
	(read directly from /c/msys64/home/Dieter/RackDevelopment/Rack/src/core/Blank.cpp) and
	SubmarineFree's SizeableModuleWidget/ResizeHandle (github.com/david-c14/SubmarineFree, GPL -
	pattern reused, not copied verbatim). Right-edge only for v1 (simpler case both references
	support identically).
*/
struct NeoResizeHandle : OpaqueWidget
{
	Neo *module = nullptr;
	Vec dragPos;
	Rect originalBox;
	// True for the exact duration of a live drag gesture (2026-07-21) - lets NeoWidget::step()'s
	// own cached-layout refresh gate freeze the displayed header width/visible column count for
	// the duration, rather than recomputing (and visibly jittering) every frame. Mirrored
	// group-wide via NeoLockData::dragging so every OTHER locked instance freezes its own display
	// too, not just this one (see onDragStart()/onDragEnd()).
	bool isDragging = false;

	// box.size/pos are set fresh every NeoWidget::step() (mode-dependent - see its own comment),
	// so no initial value is needed here beyond a harmless placeholder before the first step().
	NeoResizeHandle() {}

	void onDragStart(const DragStartEvent &e) override
	{
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		isDragging = true;
		dragPos = APP->scene->rack->getMousePos();
		ModuleWidget *mw = getAncestorOfType<ModuleWidget>();
		assert(mw);
		originalBox = mw->box;

		// Nothing to snap anymore (2026-07-21 redesign) - the row header is no longer a stored
		// value, it's recomputed fresh via neoComputeLayout() the next time NeoWidget::step() is
		// allowed to refresh its cache (the tick after this drag ends). Just tell every other
		// locked instance to freeze its own display for the duration too, same reason this
		// instance does (see NeoLockData::dragging's own comment).
		if (module && module->OL_state[LOCKED_JSON] > 0.5f)
		{
			Neo::NeoLockData lockData = module->readLockData();
			if (!lockData.dragging)
			{
				lockData.dragging = true;
				module->writeLockData(lockData);
			}
		}
	}

	void onDragMove(const DragMoveEvent &e) override
	{
		ModuleWidget *mw = getAncestorOfType<ModuleWidget>();
		assert(mw);

		Vec newDragPos = APP->scene->rack->getMousePos();
		float deltaX = newDragPos.x - dragPos.x;

		Rect newBox = originalBox;
		Rect oldBox = mw->box;
		bool fullHeight = module && module->OL_state[FULL_HEIGHT_JSON] > 0.5f;
		int rowsDisplayedForClamp = module ? clamp((int) module->OL_state[ROWS_DISPLAYED_JSON], NEO_ROWS_MIN, NEO_ROWS_MAX) : NEO_ROWS_DEFAULT;
		float columnPitchMm = neoColumnPitchMm(fullHeight, rowsDisplayedForClamp);
		// Always measured against the header's own MINIMUM width, never a "current" one (there is
		// no such stored value anymore) - the true floor/ceiling this module can ever reach is the
		// same regardless of wherever the header happens to currently sit (2026-07-21 redesign).
		float controlsWidthAtMinMm = neoRowAreaControlsWidthMm(fullHeight, NEO_ROW_HEADER_MIN_WIDTH_MM);
		// Both-modes worst case - never let a drag go narrower than what Full Height alone would
		// need, even while dragging in Normal mode - see neoMinWidthHpAnyMode()'s own comment.
		float minWidth = neoMinWidthHpAnyMode(NEO_ROW_HEADER_MIN_WIDTH_MM) * RACK_GRID_WIDTH;
		float maxWidth = neoMaxWidthHp(module ? module->neoHost : nullptr, columnPitchMm, controlsWidthAtMinMm) * RACK_GRID_WIDTH;
		// Kept as cheap insurance (2026-07-21 fix, still relevant even with no column-count floor
		// anymore) - Rack's own clamp() returns `a` (the min) whenever `b < a`, so if maxWidth
		// ever ended up smaller than minWidth for any reason, every drag frame would silently snap
		// to the same fixed minWidth regardless of mouse position. Flooring maxWidth to minWidth
		// here guarantees that can never happen.
		maxWidth = std::max(maxWidth, minWidth);
		newBox.size.x += deltaX;
		newBox.size.x = clamp(newBox.size.x, minWidth, maxWidth);
		newBox.size.x = std::round(newBox.size.x / RACK_GRID_WIDTH) * RACK_GRID_WIDTH;

		// No search (2026-07-21, Dieter's own KISS simplification): try this one width, and if
		// Rack's own collision test blocks it, just stay at the previous frame's width - no
		// decrement-and-retry loop. A live drag only ever moves the target by a small amount each
		// frame (matching natural mouse movement), so the fits/doesn't-fit boundary is always
		// found this way, one step at a time - a search was only ever needed to jump straight to a
		// potentially-far-away target in one step, which a live drag never does.
		mw->box = newBox;
		if (!APP->scene->rack->requestModulePos(mw, newBox.pos))
			mw->box = oldBox;

		if (module)
		{
			float hp = std::round(mw->box.size.x / RACK_GRID_WIDTH);
			module->OL_setOutState(PANEL_WIDTH_HP_JSON, hp);

			// KISS rule (2026-07-21, Dieter's own instruction after the lock-sync width fight
			// bug): ONLY the instance actively being resized ever writes the group's shared
			// lockData.widthHp - every other locked instance only ever READS it and tries to
			// match it (see NeoWidget::step()'s own "Lock sync (width half)" block, which no
			// longer writes at all). This IS that one write, live during the drag, so every
			// other locked instance picks up the new target on its very next tick.
			if (module->OL_state[LOCKED_JSON] > 0.5f)
			{
				Neo::NeoLockData lockData = module->readLockData();
				if (lockData.widthHp != (int) hp)
				{
					lockData.widthHp = (int) hp;
					module->writeLockData(lockData);
				}
			}
		}
	}

	void onDragEnd(const DragEndEvent &e) override
	{
		isDragging = false;
		// Un-freeze the group too - the very next NeoWidget::step() tick (this instance's and
		// every other locked instance's) will recompute its own cached layout fresh via
		// neoComputeLayout(), settling the header/visible-column count for the new width.
		if (module && module->OL_state[LOCKED_JSON] > 0.5f)
		{
			Neo::NeoLockData lockData = module->readLockData();
			if (lockData.dragging)
			{
				lockData.dragging = false;
				module->writeLockData(lockData);
			}
		}
	}

	void draw(const DrawArgs &args) override
	{
		// One glyph, same small corner-icon box, for both Normal and Full Height mode now
		// (2026-07-22) - the dedicated full-height grip-strip look was removed: it lived inside
		// its own reserved 1 HP column (still reserved in the width math, see
		// NEO_RESIZE_RESERVED_WIDTH_MM's own comment), but its faint semi-transparent lines
		// against the panel edge/Ext-strip clutter there turned out to be effectively invisible in
		// practice (Dieter's own catch, live-testing) - not worth a second, mode-specific glyph.
		//
		// Also deliberately NOT the old diagonal-corner-resize lines anymore - those visually imply
		// a two-axis (width AND height) resize, which NEO never actually does (width-only). Two
		// short vertical grip lines read as "drag me left/right" without that implication - the
		// same shape the old full-height strip used, just scaled down to this small icon box
		// instead of spanning the full panel height.
		float x1 = box.size.x * 0.35f;
		float x2 = box.size.x * 0.65f;
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, x1, box.size.y * 0.15f);
		nvgLineTo(args.vg, x1, box.size.y * 0.85f);
		nvgMoveTo(args.vg, x2, box.size.y * 0.15f);
		nvgLineTo(args.vg, x2, box.size.y * 0.85f);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStrokeColor(args.vg, nvgRGBAf(0.5f, 0.5f, 0.5f, 0.6f));
		nvgStroke(args.vg);
	}
};

/*
	Abstract cell editor - one "kind" of step-cell display/interaction. NEO's own JSON schema
	(Neo module's own comment, near channelName[]/channelColor[]) already reserves "cellType"/
	"cellConfig" for this; ROW_CELLTYPE_JSON (0=Gate, 1=Value) is the concrete per-row selector
	for now. Each concrete editor owns everything about how ONE step's raw float value is drawn
	and edited - NeoRowCellsWidget itself no longer knows what "gate" or "value" even mean, it
	just asks whichever editor the row's own cellType selects to draw/drag/reset each visible
	cell. Adding a future kind (Pitch, Curve, ... - see the plan) means writing one new
	NeoCellEditor subclass and registering it in neoCellEditorForType() - nothing else in
	NeoRowCellsWidget needs to change (2026-07-20 infrastructure, Dieter's own request).
*/
struct NeoCellEditor
{
	virtual ~NeoCellEditor() {}

	// Draws ONE cell's own content at local (x, 0, cellWidthPx, cellHeightPx) - the shared
	// per-cell backdrop behind every cell is already drawn generically by the caller; this only
	// draws the value itself on top of it.
	virtual void drawCell(const Widget::DrawArgs &args, float x, float cellWidthPx, float cellHeightPx, float value, NVGcolor color) = 0;

	// What a double-click on a cell resets it to (NeoRowCellsWidget::onDoubleClick()).
	virtual float defaultValue() { return 0.f; }

	// Turns a drag gesture (the value the drag started from, the accumulated vertical pixel
	// delta since, and the cell's own current pixel height) into a new value - lets a future
	// editor define its own sensitivity/range/quantization independently of the others.
	virtual float dragValue(float startValue, float deltaY, float cellHeightPx) = 0;
};

struct NeoGateCellEditor : NeoCellEditor
{
	void drawCell(const Widget::DrawArgs &args, float x, float cellWidthPx, float cellHeightPx, float value, NVGcolor color) override
	{
		bool on = value > 5.f; // plain 5V threshold on a real Host voltage - see CLAUDE.md's
		                       // pitfall on X8-style dual-convention issues, doesn't apply here
		                       // since this always reads a real Host value directly, never a raw
		                       // 0..1 knob
		if (!on)
			return;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, x, 0.f, cellWidthPx, cellHeightPx);
		nvgFillColor(args.vg, color);
		nvgFill(args.vg);
	}
	float defaultValue() override { return 0.f; } // off
	float dragValue(float startValue, float deltaY, float cellHeightPx) override
	{
		float sensitivity = 20.f / cellHeightPx; // full cell height ~= 20V of travel
		return clamp(startValue - deltaY * sensitivity, -10.f, 10.f);
	}
};

struct NeoValueCellEditor : NeoCellEditor
{
	void drawCell(const Widget::DrawArgs &args, float x, float cellWidthPx, float cellHeightPx, float value, NVGcolor color) override
	{
		float t = clamp((value + 10.f) / 20.f, 0.f, 1.f); // -10..+10V -> 0..1
		float barHeight = t * cellHeightPx;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, x, cellHeightPx - barHeight, cellWidthPx, barHeight);
		nvgFillColor(args.vg, color);
		nvgFill(args.vg);
	}
	float defaultValue() override { return 0.f; } // center
	float dragValue(float startValue, float deltaY, float cellHeightPx) override
	{
		float sensitivity = 20.f / cellHeightPx;
		return clamp(startValue - deltaY * sensitivity, -10.f, 10.f);
	}
};

// Replicates MorpheusDisplayWidget's own value-to-color technique (Morpheus.cpp) at NEO's own
// per-cell scale, instead of an on/off square or a bar height - see NEO_MORPHEUS_CELL_LOW_COLOR's
// own comment (Neo.hpp) for the full reasoning. First pass: just the color-lerp itself: the
// match/dirty distinction and transient event-flash Morpheus's own display also has are left for
// a later tuning pass, once this base technique is confirmed live (Dieter's own scoping,
// 2026-07-20).
struct NeoMorpheusStyleCellEditor : NeoCellEditor
{
	void drawCell(const Widget::DrawArgs &args, float x, float cellWidthPx, float cellHeightPx, float value, NVGcolor color) override
	{
		float t = clamp((value + 10.f) / 20.f, 0.f, 1.f); // -10..+10V -> 0..1, same as Morpheus's own display
		NVGcolor fill = nvgLerpRGBA(NEO_MORPHEUS_CELL_LOW_COLOR, NEO_MORPHEUS_CELL_HIGH_COLOR, t);
		nvgBeginPath(args.vg);
		nvgRect(args.vg, x, 0.f, cellWidthPx, cellHeightPx);
		nvgFillColor(args.vg, fill);
		nvgFill(args.vg);
	}
	float defaultValue() override { return 0.f; } // center
	float dragValue(float startValue, float deltaY, float cellHeightPx) override
	{
		float sensitivity = 20.f / cellHeightPx;
		return clamp(startValue - deltaY * sensitivity, -10.f, 10.f);
	}
};

// The one place a cellType number maps to its own editor - the entire registry a future cell
// type needs to join. Static instances since editors are stateless (pure functions of whatever
// cell they're currently asked to draw/edit), so there's never a need for more than one of each.
inline NeoCellEditor* neoCellEditorForType(int cellType)
{
	static NeoGateCellEditor gate;
	static NeoValueCellEditor value;
	static NeoMorpheusStyleCellEditor morpheusStyle;
	if (cellType <= 0)
		return &gate;
	if (cellType == 1)
		return &value;
	return &morpheusStyle;
}

/**
	Draws (and handles single-cell drag/double-click-edit for) one row's currently-visible step
	cells, entirely through whichever NeoCellEditor the row's own ROW_CELLTYPE_JSON selects - see
	its own comment for the full abstraction. One widget per row rather than one child widget per
	cell, since the visible column count changes with the resizable panel width - simpler to just
	recompute what's visible each draw() call than to create/destroy child widgets on every
	resize.
*/
struct NeoRowCellsWidget : TransparentWidget
{
	Module *module = nullptr;
	int row = 0;
	int dragStep = -1;
	float dragStartValue = 0.f;

	Neo* neo() { return module ? dynamic_cast<Neo*>(module) : nullptr; }

	int stepAtLocalX(float x, float pitchPx, int visibleCols)
	{
		// -1 = no valid step (2026-07-22) - visibleCols can genuinely be 0 now (no minimum column
		// count guarantee anymore), which would otherwise make visibleCols-1 go negative -
		// clamp(v, 0, -1) doesn't crash (Rack's own clamp() just returns the lower bound), but it
		// would silently return step 0 as if it were valid, which it isn't.
		if (visibleCols <= 0)
			return -1;
		return clamp((int) (x / pitchPx), 0, visibleCols - 1);
	}

	// Deferred interface idea, noted 2026-07-20, not implemented: clicking in the GAP between two
	// cells and dragging right should copy the value of the step immediately to the left of the
	// drag start across every cell the drag comes to fully cover (a "paint the last value
	// forward" gesture) - and, symmetrically, dragging left from a gap should copy the value of
	// the step immediately to the RIGHT of the drag start across every cell it covers going that
	// direction. Distinct from the existing single-cell onButton()/onDragMove() behavior above,
	// which only ever edits the one cell where the press began.

	void draw(const DrawArgs &args) override
	{
		Neo *m = neo();
		if (!m)
			return;

		// Cell size/pitch are fixed by Grid Rows/Full Height alone, never by the row-cells
		// widget's own current box.size.x - so they never change while resizing; only how many
		// whole columns fit (visibleCols, already floored) does. This widget's own box.size.y is
		// ALREADY exactly one cell's height with no gap baked in - the inter-row gap is external,
		// from how NeoWidget::step() spaces consecutive row-cells widgets apart - so a cell fills
		// its own full height with zero vertical inset. Horizontally it gets the matching
		// treatment (Dieter's own instruction, 2026-07-20 - horizontal padding must equal
		// vertical padding): inset by half the actual current gap on each side within its own
		// pitch slot, the same half-gap-edge convention neoRowLayout() already uses for rows.
		float cellWidthPx = mm2px(m->getColumnWidthMm());
		float pitchPx = mm2px(m->getColumnPitchMm());
		float gapPx = pitchPx - cellWidthPx;
		int visibleCols = m->getVisibleColumns();

		// Always-visible per-cell backdrop - drawn for every visible column regardless of
		// content, so individual cell boundaries read clearly even at rest (Dieter's own
		// instruction, 2026-07-20, for visual support while testing).
		for (int i = 0; i < visibleCols; i++)
		{
			float x = gapPx / 2.f + (float) i * pitchPx;
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x, 0.f, cellWidthPx, box.size.y);
			nvgFillColor(args.vg, NEO_CELL_BG_COLOR);
			nvgFill(args.vg);
		}

		if (!m->neoHost)
			return;

		int channel = m->getRowChannel(row);
		int track = m->getRowTrack(row);
		NeoCellEditor *editor = neoCellEditorForType((int) m->OL_state[ROW_CELLTYPE_JSON + row]);
		int page = (int) m->OL_state[ROW_PAGE_JSON + row];
		int loopLen = m->neoHost->getLoopLen(channel);
		int colorPacked = m->channelColor[track][channel]; // (track,channel)-owned, Host-shared - see its own comment
		NVGcolor color = nvgRGB((colorPacked >> 16) & 0xff, (colorPacked >> 8) & 0xff, colorPacked & 0xff);

		for (int i = 0; i < visibleCols; i++)
		{
			int step = page * visibleCols + i;
			if (step >= loopLen)
				break;
			float value = m->neoHost->getTrackStep(track, channel, step);
			float x = gapPx / 2.f + (float) i * pitchPx;
			editor->drawCell(args, x, cellWidthPx, box.size.y, value, color);
		}
	}

	void onButton(const event::Button &e) override
	{
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS)
		{
			Neo *m = neo();
			if (m && m->neoHost)
			{
				int visibleCols = m->getVisibleColumns();
				float pitchPx = mm2px(m->getColumnPitchMm());
				int candidateStep = stepAtLocalX(e.pos.x, pitchPx, visibleCols);
				// candidateStep < 0 means no valid step at all (visibleCols is 0 - 2026-07-22, no
				// minimum column count guarantee anymore) - nothing to click on, start no drag.
				if (candidateStep >= 0)
				{
					int channel = m->getRowChannel(row);
					int page = (int) m->OL_state[ROW_PAGE_JSON + row];
					int step = page * visibleCols + candidateStep;
					// "It does not make sense to edit anything beyond LEN" (Dieter's own instruction,
					// 2026-07-20) - a step this widget would otherwise never even draw (see draw()'s
					// own loopLen break above) must not be editable either, so a click landing in
					// that trailing dead space on the loop's last page simply starts no drag at all.
					if (step < m->neoHost->getLoopLen(channel))
					{
						dragStep = candidateStep;
						int track = m->getRowTrack(row);
						dragStartValue = m->neoHost->getTrackStep(track, channel, step);
					}
				}
			}
			e.consume(this);
		}
		TransparentWidget::onButton(e);
	}

	// Single-cell only (confirmed explicitly during spec discussion) - dragStep is fixed at the
	// moment of the initial press, never re-evaluated mid-drag, so the gesture always edits
	// exactly one step regardless of how far the mouse strays horizontally afterward. Vertical
	// drag (up = higher value) matches the bar's own vertical orientation.
	void onDragMove(const DragMoveEvent &e) override
	{
		Neo *m = neo();
		if (!m || !m->neoHost || dragStep < 0)
			return;
		int visibleCols = m->getVisibleColumns();
		int channel = m->getRowChannel(row);
		int track = m->getRowTrack(row);
		int page = (int) m->OL_state[ROW_PAGE_JSON + row];
		int step = page * visibleCols + dragStep;

		// e.mouseDelta is already zoom-corrected by Rack - accumulate it directly rather than
		// re-deriving from absolute position, simplest correct approach for a continuous drag.
		NeoCellEditor *editor = neoCellEditorForType((int) m->OL_state[ROW_CELLTYPE_JSON + row]);
		float newValue = editor->dragValue(dragStartValue, e.mouseDelta.y, box.size.y);
		dragStartValue = newValue;

		m->neoHost->setTrackStep(track, channel, step, newValue);
	}

	void onDragEnd(const DragEndEvent &e) override
	{
		dragStep = -1;
		TransparentWidget::onDragEnd(e);
	}

	// Resets whichever cell was last pressed (dragStep, still valid - Rack's own double-click
	// detection dispatches this right after the second onButton() press, which already
	// recomputed dragStep from that same click's own position) to its editor's own default.
	void onDoubleClick(const DoubleClickEvent &e) override
	{
		Neo *m = neo();
		if (m && m->neoHost && dragStep >= 0)
		{
			int visibleCols = m->getVisibleColumns();
			int channel = m->getRowChannel(row);
			int track = m->getRowTrack(row);
			int page = (int) m->OL_state[ROW_PAGE_JSON + row];
			int step = page * visibleCols + dragStep;
			float resetValue = neoCellEditorForType((int) m->OL_state[ROW_CELLTYPE_JSON + row])->defaultValue();
			m->neoHost->setTrackStep(track, channel, step, resetValue);
		}
		TransparentWidget::onDoubleClick(e);
	}
};

/**
	OrangeLine-style small digital-look text display - a thin NEO-specific wrapper around
	OrangeLine.hpp's shared olDrawDisplayFrame()/olDrawDisplayText() (2026-07-21 extraction, see
	CLAUDE.md's "Code-drawn digital displays and knob rings" section) - this struct's own job is
	just resolving NEO's theme (OL_state[STYLE_JSON]) into concrete colors and calling getText()/
	getText2() to get the actual content; the drawing itself is shared, common code now, not
	duplicated per module. Takes a live getText() callback (returning arbitrary text, not just a
	formatted number) rather than OrangeLine.hpp's own NumberWidget's persistent float-pointer +
	printf-format-string shape, since NEO's own displays (track name, channel number, page/
	position) are cheap to recompute fresh from the module's real state every draw() call and
	track needs non-numeric content ("TAPE"/"MSEL"/"M-01".."M-16").
*/
struct NeoRowTextDisplayWidget : TransparentWidget
{
	Module *module = nullptr;
	std::function<std::string(Neo*)> getText;
	// Optional second stacked line (2026-07-20 experiment, Dieter's own request - a step toward
	// the "pack two rows of controls into one taller cell" idea noted for later) - unset (default)
	// keeps the original single centered line; once set, both lines instead split the box into
	// top/bottom halves, each centered within its own half the same way the single line used to
	// center within the whole box.
	std::function<std::string(Neo*)> getText2;
	float fontSize = 1.f; // set by the caller right after construction

	void draw(const DrawArgs &args) override
	{
		Neo *m = module ? dynamic_cast<Neo*>(module) : nullptr;
		float style = m ? m->OL_state[STYLE_JSON] : STYLE_ORANGE;
		NVGcolor bg = (style == STYLE_DARK) ? OL_DISPLAY_BG_DARK : (style == STYLE_BRIGHT) ? OL_DISPLAY_BG_BRIGHT : OL_DISPLAY_BG_ORANGE;
		NVGcolor frame = (style == STYLE_DARK) ? X_FRAME_DARK : (style == STYLE_BRIGHT) ? X_FRAME_BRIGHT : X_FRAME_ORANGE;
		olDrawDisplayFrame(args.vg, box.size, bg, frame, mm2px(NEO_ROW_DISPLAY_RADIUS_MM), mm2px(NEO_ROW_DISPLAY_STROKE_MM));
		TransparentWidget::draw(args);
	}

	void drawLayer(const DrawArgs &args, int layer) override
	{
		if (layer != 1)
		{
			Widget::drawLayer(args, layer);
			return;
		}
		Neo *m = module ? dynamic_cast<Neo*>(module) : nullptr;
		if (m && getText)
		{
			NVGcolor textColor = (m->OL_state[STYLE_JSON] == STYLE_ORANGE) ? ORANGE : WHITE;
			std::string line1 = getText(m);
			std::string line2 = getText2 ? getText2(m) : std::string();
			olDrawDisplayText(args.vg, box.size, textColor, line1, line2,
				"res/repetition-scrolling.regular.ttf", fontSize,
				mm2px(NEO_ROW_DISPLAY_TEXT_INSET_MM), mm2px(NEO_ROW_DISPLAY_TEXT_Y_OFFSET_MM),
				mm2px(NEO_ROW_DISPLAY_LINE2_Y_NUDGE_MM));
		}
		Widget::drawLayer(args, 1);
	}
};

/**
	Themed ring drawn around a knob (2026-07-20), matching NeoRowTextDisplayWidget's own frame
	styling - lifted from Dieter's own reference SVG (res/DisplaysWithKnobsInFrame.svg). A
	separate, self-contained widget (not baked into the knob) specifically so it stays an
	optional add-on per knob - only NEO's own new track/channel knobs get one for now (Dieter's
	own instruction: "add this here but do not draw them elsewhere for now, will use the drawn
	version in future because it makes layout much easier"). Always drawn (plain draw(), not
	layer-1-gated), same reasoning as every other frame/decoration widget in this file.
*/
struct NeoRowKnobRingWidget : TransparentWidget
{
	Module *module = nullptr;

	void draw(const DrawArgs &args) override
	{
		Neo *m = module ? dynamic_cast<Neo*>(module) : nullptr;
		float style = m ? m->OL_state[STYLE_JSON] : STYLE_ORANGE;
		NVGcolor frame = (style == STYLE_DARK) ? X_FRAME_DARK : (style == STYLE_BRIGHT) ? X_FRAME_BRIGHT : X_FRAME_ORANGE;
		float r = box.size.x / 2.f;

		nvgBeginPath(args.vg);
		nvgCircle(args.vg, r, r, r);
		nvgStrokeWidth(args.vg, mm2px(NEO_ROW_KNOB_RING_STROKE_MM));
		nvgStrokeColor(args.vg, frame);
		nvgStroke(args.vg);
		TransparentWidget::draw(args);
	}
};

// Shared color preset list (2026-07-21) - the predefined colors NeoRowColorDotWidget's own
// click-drag cycles through. No menu/popup involved at all (see the widget's own comment) - kept
// as a plain array purely so the color set lives in one place. All 22 of Kelly's "Colors of
// Maximum Contrast" - the established, purpose-built answer for "N colors that stay visually
// distinguishable from each other," still used in cartography/dataviz today. Full credit to their
// original author:
//
//   Kenneth L. Kelly, "Twenty-two colors of maximum contrast," Color Engineering, vol. 3, no. 6,
//   1965, pp. 26-27.
//
// The `name` field below is NOT Kelly's own official name - it's a common, recognizable SYNONYM
// chosen to fit within NEO_ROW_NAME_MAX_CHARS (8), so it never needs truncating in the row's name
// field while its color dot is being dragged (NeoRowNameField::drawLayer(), gated by
// Neo::neoColorDragRow). Kelly's own official name is kept as a trailing comment on each entry
// for documentation/reference, and tabulated here for a quick side-by-side:
//
//   hex      Kelly's official name       short name used here
//   F2F3F4   White                       White
//   222222   Black                       Black
//   F3C300   Vivid Yellow                Yellow
//   875692   Strong Purple               Purple
//   F38400   Vivid Orange                Orange
//   A1CAF1   Very Light Blue             Sky Blue
//   BE0032   Vivid Red                   Red
//   C2B280   Grayish Yellow (Buff)       Tan
//   848482   Medium Gray                 Gray
//   008856   Vivid Green                 Green
//   E68FAC   Strong Purplish Pink        Orchid
//   0067A5   Strong Blue                 Blue
//   F99379   Strong Yellowish Pink       Salmon
//   604E97   Strong Violet               Violet
//   F6A600   Vivid Orange Yellow         Amber
//   B3446C   Strong Purplish Red         Berry
//   DCD300   Vivid Greenish Yellow       Citron
//   882D17   Strong Reddish Brown        Rust
//   8DB600   Vivid Yellowish Green       Lime
//   654522   Deep Yellowish Brown        Khaki
//   E25822   Vivid Reddish Orange        Coral
//   2B3D26   Dark Olive Green            Olive
//
// The scissor clip in that same drawLayer() is kept regardless of this fit, as cheap insurance
// against any future name change.
struct NeoColorSwatch { int color; const char *name; };
static const NeoColorSwatch NEO_COLOR_SWATCHES[] = {
	{ 0xF2F3F4, "White" },    // Kelly: White
	{ 0x222222, "Black" },    // Kelly: Black
	{ 0xF3C300, "Yellow" },   // Kelly: Vivid Yellow
	{ 0x875692, "Purple" },   // Kelly: Strong Purple
	{ 0xF38400, "Orange" },   // Kelly: Vivid Orange
	{ 0xA1CAF1, "Sky Blue" }, // Kelly: Very Light Blue
	{ 0xBE0032, "Red" },      // Kelly: Vivid Red
	{ 0xC2B280, "Tan" },      // Kelly: Grayish Yellow (Buff)
	{ 0x848482, "Gray" },     // Kelly: Medium Gray
	{ 0x008856, "Green" },    // Kelly: Vivid Green
	{ 0xE68FAC, "Orchid" },   // Kelly: Strong Purplish Pink
	{ 0x0067A5, "Blue" },     // Kelly: Strong Blue
	{ 0xF99379, "Salmon" },   // Kelly: Strong Yellowish Pink
	{ 0x604E97, "Violet" },   // Kelly: Strong Violet
	{ 0xF6A600, "Amber" },    // Kelly: Vivid Orange Yellow
	{ 0xB3446C, "Berry" },    // Kelly: Strong Purplish Red
	{ 0xDCD300, "Citron" },   // Kelly: Vivid Greenish Yellow
	{ 0x882D17, "Rust" },     // Kelly: Strong Reddish Brown
	{ 0x8DB600, "Lime" },     // Kelly: Vivid Yellowish Green
	{ 0x654522, "Khaki" },    // Kelly: Deep Yellowish Brown
	{ 0xE25822, "Coral" },    // Kelly: Vivid Reddish Orange
	{ 0x2B3D26, "Olive" },    // Kelly: Dark Olive Green
};
static const int NEO_NUM_COLOR_SWATCHES = sizeof(NEO_COLOR_SWATCHES) / sizeof(NEO_COLOR_SWATCHES[0]);

/**
	Colored dot preceding each row's own editable name field (2026-07-21) - filled with
	channelColor[track][channel] (the row's OWN current track/channel, via getRowTrack()/
	getRowChannel(), never cached - always reflects whatever the row is showing right now).
	Deliberately NOT a button/menu/popup of any kind (Dieter's own explicit direction, after trying
	and rejecting both a floating swatch menu and an in-panel picker panel idea) - it behaves like
	a hidden knob: click-and-drag vertically cycles through NEO_COLOR_SWATCHES directly, same
	"accumulate e.mouseDelta.y, step on crossing a threshold" technique NeoRowCellsWidget's own
	value-cell drag editing already uses (see its onDragMove() above), just discrete-stepped
	through a fixed color list instead of a continuous value. Up = next color, matching this
	codebase's "up = higher value" drag convention elsewhere.
*/
struct NeoRowColorDotWidget : OpaqueWidget
{
	Module *module = nullptr;
	int row = 0;
	float dragAccumPx = 0.f;

	void draw(const DrawArgs &args) override
	{
		Neo *m = module ? dynamic_cast<Neo*>(module) : nullptr;
		int colorPacked = m ? m->channelColor[m->getRowTrack(row)][m->getRowChannel(row)] : 0xff6600;
		NVGcolor color = nvgRGB((colorPacked >> 16) & 0xff, (colorPacked >> 8) & 0xff, colorPacked & 0xff);
		float r = box.size.x / 2.f;
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, r, r, r);
		nvgFillColor(args.vg, color);
		nvgFill(args.vg);
		OpaqueWidget::draw(args);
	}

	void onButton(const event::Button &e) override
	{
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT)
		{
			dragAccumPx = 0.f;
			// Tell this row's own NeoRowNameField to show the swatch name instead of the channel
			// name for the duration of the drag (2026-07-21, Dieter's own request) - cleared in
			// onDragEnd() below.
			Neo *m = module ? dynamic_cast<Neo*>(module) : nullptr;
			if (m)
				m->neoColorDragRow = row;
			e.consume(this);
		}
		OpaqueWidget::onButton(e);
	}

	void onDragMove(const event::DragMove &e) override
	{
		Neo *m = module ? dynamic_cast<Neo*>(module) : nullptr;
		if (!m)
			return;
		int track = m->getRowTrack(row);
		int channel = m->getRowChannel(row);
		int currentColor = m->channelColor[track][channel];
		int index = 0;
		for (int i = 0; i < NEO_NUM_COLOR_SWATCHES; i++)
			if (NEO_COLOR_SWATCHES[i].color == currentColor)
			{
				index = i;
				break;
			}

		// e.mouseDelta is already zoom-corrected by Rack - accumulate it directly, same technique
		// NeoRowCellsWidget::onDragMove() uses for continuous value drags, just stepped here.
		dragAccumPx += e.mouseDelta.y;
		float stepPx = mm2px(NEO_ROW_NAME_DOT_DRAG_STEP_MM);
		while (dragAccumPx <= -stepPx)
		{
			index = (index + 1) % NEO_NUM_COLOR_SWATCHES;
			dragAccumPx += stepPx;
		}
		while (dragAccumPx >= stepPx)
		{
			index = (index - 1 + NEO_NUM_COLOR_SWATCHES) % NEO_NUM_COLOR_SWATCHES;
			dragAccumPx -= stepPx;
		}
		m->setChannelColor(track, channel, NEO_COLOR_SWATCHES[index].color);
	}

	void onDragEnd(const event::DragEnd &e) override
	{
		Neo *m = module ? dynamic_cast<Neo*>(module) : nullptr;
		if (m && m->neoColorDragRow == row)
			m->neoColorDragRow = -1;
		OpaqueWidget::onDragEnd(e);
	}
};

/**
	On-panel editable channel-identity name field (2026-07-21) - OrangeLine's first inline-
	editable text control (every prior ui::TextField use in this codebase lives inside a menu, see
	CLAUDE.md). Bound to channelName[track][channel] for the row's OWN current track/channel
	(never cached - resolved fresh via getRowTrack()/getRowChannel() every call, same as the color
	dot above). ui::TextField's own draw() is opaque to us (compiled into the SDK, no readable
	source in this checkout) - overridden completely rather than fighting an unknown default look:
	draw() paints the background/frame via the shared olDrawDisplayFrame() (matching every other
	NEO display), drawLayer(layer==1) paints the text itself via the shared olDrawDisplayText()
	plus a manually-drawn cursor/selection highlight on top, using the inherited text/cursor/
	selection members TextField's own event handlers (onSelectText/onSelectKey/onDragHover/
	onButton, all inherited unchanged) already maintain correctly. Cursor/selection are only drawn
	while this field actually has keyboard focus (APP->event->getSelectedWidget() == this).
*/
struct NeoRowNameField : ui::TextField
{
	Module *module = nullptr;
	int row = 0;

	void onChange(const ChangeEvent &e) override
	{
		TextField::onChange(e);
		// Hard length cap (Dieter's own catch, 2026-07-21: nothing was stopping the user from
		// typing arbitrarily far past the field's own drawn width). Clamping cursor/selection
		// here too is required, not optional - leaving either pointing past the now-shorter
		// text's end is exactly what caused a real crash (std::string::substr(pos) throws
		// std::out_of_range if pos > size(), and drawLayer() below measures cursor position via
		// text.substr(0, cursor)).
		if ((int) text.size() > NEO_ROW_NAME_MAX_CHARS)
		{
			text = text.substr(0, NEO_ROW_NAME_MAX_CHARS);
			cursor = std::min(cursor, (int) text.size());
			selection = std::min(selection, (int) text.size());
		}
		Neo *m = module ? dynamic_cast<Neo*>(module) : nullptr;
		if (m)
			m->setChannelName(m->getRowTrack(row), m->getRowChannel(row), text);
	}

	void step() override
	{
		// Skip the resync entirely while THIS field is the one being typed into (2026-07-21,
		// real bug found live) - m->channelName[] is only refreshed by refreshChannelTable() on
		// the DSP thread (moduleProcess()), a separate thread from this UI-side step(). Right
		// after a keystroke, the very next step() call can still see a stale, one-character-
		// shorter liveText (the DSP thread hasn't caught up yet), which used to unconditionally
		// reset text back to that stale value and clamp cursor DOWN to its shorter length. Once
		// the DSP thread caught up a moment later, text corrected itself again - but cursor never
		// grew back (std::min() only ever shrinks), so it stayed stuck one short of the true end
		// forever after - symptom: appending a character at the end of the field left the cursor
		// sitting BEFORE the just-typed character instead of advancing past it. This resync's
		// real job is picking up an edit made by ANOTHER NEO instance (or a fresh (track,channel)
		// pair) - it should never fight this same field's own in-flight local edit. Same
		// focus check drawLayer() already uses for the same reason (this IS the authoritative
		// source of truth while focused, nothing external should override it).
		bool focused = (APP->event->getSelectedWidget() == this);
		Neo *m = (!focused && module) ? dynamic_cast<Neo*>(module) : nullptr;
		if (m)
		{
			const std::string &liveText = m->channelName[m->getRowTrack(row)][m->getRowChannel(row)];
			if (text != liveText)
			{
				text = liveText; // direct member assign, not setText() - keeps cursor/selection as-is...
				// ...except cursor/selection MUST still be clamped to the new (possibly shorter -
				// e.g. a fresh (track,channel) pair, or another instance's shorter edit) text's
				// own length right here - the same out-of-bounds substr() crash as above, just
				// triggered by a remote/cross-instance resync instead of local typing.
				cursor = std::min(cursor, (int) text.size());
				selection = std::min(selection, (int) text.size());
			}
		}
		TextField::step();
	}

	void draw(const DrawArgs &args) override
	{
		Neo *m = module ? dynamic_cast<Neo*>(module) : nullptr;
		float style = m ? m->OL_state[STYLE_JSON] : STYLE_ORANGE;
		NVGcolor bg = (style == STYLE_DARK) ? OL_DISPLAY_BG_DARK : (style == STYLE_BRIGHT) ? OL_DISPLAY_BG_BRIGHT : OL_DISPLAY_BG_ORANGE;
		NVGcolor frame = (style == STYLE_DARK) ? X_FRAME_DARK : (style == STYLE_BRIGHT) ? X_FRAME_BRIGHT : X_FRAME_ORANGE;
		olDrawDisplayFrame(args.vg, box.size, bg, frame, mm2px(NEO_ROW_DISPLAY_RADIUS_MM), mm2px(NEO_ROW_DISPLAY_STROKE_MM));
	}

	void drawLayer(const DrawArgs &args, int layer) override
	{
		if (layer != 1)
		{
			Widget::drawLayer(args, layer);
			return;
		}
		Neo *m = module ? dynamic_cast<Neo*>(module) : nullptr;
		// Text color reflects the channel's own identity color (same value the dot shows and
		// NeoRowCellsWidget's own step-cell rendering uses) - NOT the theme's plain ORANGE/WHITE
		// text color every other NEO display uses (Dieter's own catch, 2026-07-21: the dot
		// visibly changes the identity color, but the name text itself was staying theme-colored
		// instead of following it).
		int colorPacked = m ? m->channelColor[m->getRowTrack(row)][m->getRowChannel(row)] : 0xff6600;
		NVGcolor textColor = nvgRGB((colorPacked >> 16) & 0xff, (colorPacked >> 8) & 0xff, colorPacked & 0xff);
		float insetX = mm2px(NEO_ROW_DISPLAY_TEXT_INSET_MM);

		// While this row's own color dot is being dragged, show the swatch's printable Kelly-color
		// name instead of the channel name - live feedback on which of the 22 is currently
		// selected (Dieter's own request, 2026-07-21). Cursor/selection are suppressed for the
		// same duration - they reflect real text-editing focus, which isn't relevant here.
		bool showingColorName = (m && m->neoColorDragRow == row);
		bool focused = !showingColorName && (APP->event->getSelectedWidget() == this);
		std::string displayText = text;
		if (showingColorName)
		{
			displayText = "?";
			for (int i = 0; i < NEO_NUM_COLOR_SWATCHES; i++)
				if (NEO_COLOR_SWATCHES[i].color == colorPacked)
				{
					displayText = NEO_COLOR_SWATCHES[i].name;
					break;
				}
		}

		if (focused && selection != cursor)
		{
			std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, mm2px(NEO_ROW_NAME_FONT_SIZE_MM));
			int selStart = std::min(cursor, selection);
			int selEnd = std::max(cursor, selection);
			float xStart = insetX + nvgTextBounds(args.vg, 0.f, 0.f, text.substr(0, selStart).c_str(), nullptr, nullptr);
			float xEnd = insetX + nvgTextBounds(args.vg, 0.f, 0.f, text.substr(0, selEnd).c_str(), nullptr, nullptr);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, xStart, mm2px(0.5f), xEnd - xStart, box.size.y - mm2px(1.f));
			nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x50));
			nvgFill(args.vg);
		}

		// Scissor-clipped to this field's own box while showing a color name (2026-07-21) - several
		// Kelly names ("Greenish Yellow", "Yellowish Brown", etc.) are longer than the field's
		// fixed 8-character width, and olDrawDisplayText() doesn't clip on its own - an unclipped
		// long name would bleed into the LEFT/RIGHT paging buttons right next to it. The normal
		// channel-name case never needs this (already capped at NEO_ROW_NAME_MAX_CHARS).
		if (showingColorName)
			nvgScissor(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		olDrawDisplayText(args.vg, box.size, textColor, displayText, "",
			"res/repetition-scrolling.regular.ttf", mm2px(NEO_ROW_NAME_FONT_SIZE_MM),
			insetX, mm2px(NEO_ROW_DISPLAY_TEXT_Y_OFFSET_MM));
		if (showingColorName)
			nvgResetScissor(args.vg);

		if (focused)
		{
			std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, mm2px(NEO_ROW_NAME_FONT_SIZE_MM));
			float cursorX = insetX + nvgTextBounds(args.vg, 0.f, 0.f, text.substr(0, cursor).c_str(), nullptr, nullptr);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, cursorX, mm2px(0.5f), mm2px(0.2f), box.size.y - mm2px(1.f));
			nvgFillColor(args.vg, textColor);
			nvgFill(args.vg);
		}
		Widget::drawLayer(args, 1);
	}
};

/**
	Frame around one row's own "header data" (name/track/channel/paging) - 2026-07-20, Dieter's
	own spec: own outer height matches the current cell height, left corners match the outer
	content frame's own radius, right corners get a smaller radius emphasizing that this frame
	leads into the step-cell grid rather than standing apart from it. Stroke-only (no fill), so
	it draws cleanly behind or in front of the real controls it frames either way - positioned/
	sized by NeoWidget::step(), same as every other per-row widget.
*/
struct NeoRowHeaderFrameWidget : TransparentWidget
{
	Module *module = nullptr;

	void draw(const DrawArgs &args) override
	{
		Neo *m = module ? dynamic_cast<Neo*>(module) : nullptr;
		float style = m ? m->OL_state[STYLE_JSON] : STYLE_ORANGE;
		NVGcolor frame = (style == STYLE_DARK) ? X_FRAME_DARK : (style == STYLE_BRIGHT) ? X_FRAME_BRIGHT : X_FRAME_ORANGE;

		nvgBeginPath(args.vg);
		nvgRoundedRectVarying(args.vg, 0.f, 0.f, box.size.x, box.size.y,
			mm2px(NEO_ROW_HEADER_LEFT_RADIUS_MM), mm2px(NEO_ROW_HEADER_RIGHT_RADIUS_MM),
			mm2px(NEO_ROW_HEADER_RIGHT_RADIUS_MM), mm2px(NEO_ROW_HEADER_LEFT_RADIUS_MM));
		nvgStrokeWidth(args.vg, mm2px(NEO_FRAME_STROKE_MM));
		nvgStrokeColor(args.vg, frame);
		nvgStroke(args.vg);
	}
};

/**
	Flat, code-drawn panel background - an SVG panel (the usual `setPanel()`/`SvgPanel` convention
	every other OrangeLine module uses) has a fixed baked width and can't stretch to match a
	resizable module, so NEO draws its own background directly instead, sized to `box.size`
	every frame (mirrors VCV core's own `BlankPanel`, `Blank.cpp`). Deliberately simple graphics
	per Dieter's own "function first, polish later" instruction - a themed flat fill + border, no
	hand-authored art at all yet.
*/
// Frame/accent/title geometry constants now live in Neo.hpp's own "NEO layout constants"
// section (NEO_FRAME_MARGIN_MM etc.) - the single source of truth for all of NEO's own layout.
struct NeoPanelWidget : Widget
{
	Module *module = nullptr;

	// Draws, in order: (1) one background rectangle spanning the panel's full width, (2) a
	// PanelBorder-style stroke reproducing Rack's own module-seam look, (3) two peer frames -
	// a fixed-width global sidebar frame (always drawn) and a row-area frame (skipped in Full
	// Height - see below), (4) the accent stripe + "NEO"/"ORANGE LINE" brand text.
	//
	// Full Height mode (FULL_HEIGHT_JSON) makes the row area a chrome-free rectangle spanning the
	// full 0..box.size.y with no frame/border/brand text crossing it, so two instances stacked at
	// the same HP position (different rack row) read as one seamless grid. Every element below
	// that cares about this is driven by the SAME `top`/`bottom` pair computed once up front -
	// they used to be computed separately per element (background, frame, border each had their
	// own slightly different top/bottom logic), which both read as more complicated than the
	// actual rule ("everything shares one seamless top/bottom span in Full Height, one inset span
	// otherwise") and hid a real bug: the background's own global-area half kept its inset
	// regardless of Full Height, leaving a one-pixel seam right where it met the row area's half
	// (Dieter's own catch, 2026-07-21).
	void draw(const DrawArgs &args) override
	{
		Neo *m = module ? dynamic_cast<Neo*>(module) : nullptr;
		float style = m ? m->OL_state[STYLE_JSON] : STYLE_ORANGE;
		bool fullHeight = m && m->OL_state[FULL_HEIGHT_JSON] > 0.5f;
		NVGcolor bg = (style == STYLE_DARK) ? X_STRIP_BG_DARK : (style == STYLE_BRIGHT) ? X_STRIP_BG_BRIGHT : X_STRIP_BG_ORANGE;
		NVGcolor frameColor = (style == STYLE_DARK) ? X_FRAME_DARK : (style == STYLE_BRIGHT) ? X_FRAME_BRIGHT : X_FRAME_ORANGE;
		NVGcolor text = xThemedTextColor(style);

		float bgInsetPx = mm2px(NEO_BACKGROUND_INSET_MM);
		float marginPx = mm2px(NEO_FRAME_MARGIN_MM);
		float globalAreaEdgePx = mm2px(NEO_GLOBAL_AREA_WIDTH_MM);
		float halfGapPx = mm2px(NEO_FRAME_GAP_MM) / 2.f;

		// Seamless in Full Height (flush to the panel's real 0..box.size.y), inset by the same
		// margin Rack's own SvgPanel convention leaves around every other OrangeLine module's
		// baked panel art otherwise - NEO draws its own background directly, so it has to apply
		// that inset by hand rather than getting it for free.
		float top = fullHeight ? 0.f : bgInsetPx;
		float bottom = fullHeight ? box.size.y : box.size.y - bgInsetPx;

		// (1) Background - ONE rectangle, full width. addXExtStrip()/addXExtStripLeft() (below)
		// are the only things allowed to paint into the resulting left/right margin, and only
		// while actually bridgeConnected().
		nvgBeginPath(args.vg);
		nvgRect(args.vg, bgInsetPx, top, box.size.x - 2.f * bgInsetPx, bottom - top);
		nvgFillColor(args.vg, bg);
		nvgFill(args.vg);

		// (2) PanelBorder-style stroke, same width as the background inset so it exactly fills
		// the gap the inset leaves. nvgStroke() centers on its own path, so each edge is drawn
		// half a stroke-width in from the true boundary - otherwise the outer half bleeds past
		// box.size and gets clipped away. Top/bottom are skipped ENTIRELY in Full Height (not just
		// shortened - a shortened line that stops abruptly mid-panel at globalAreaEdgePx was the
		// actual visible artifact Dieter caught, 2026-07-21: even after the background fix above,
		// this partial line was still being drawn right on top of the now-seamless background).
		// Full Height's whole point is no border/frame/brand element interrupting the panel top-
		// to-bottom, so top/bottom simply don't exist there at all. Left/right reproduce Rack's
		// own horizontal rack-row seam (unrelated to vertical stacking, so still drawn either way)
		// - but with top/bottom now gone in Full Height, their own usual half-stroke inset from
		// the very top/bottom would leave a small visible gap at each corner with nothing there to
		// close it off (Dieter's own follow-up catch, 2026-07-21) - so in Full Height they run the
		// TRUE full 0..box.size.y instead of the inset span Normal mode still uses.
		float strokeHalfPx = bgInsetPx / 2.f;
		float sideTopPx = fullHeight ? 0.f : strokeHalfPx;
		float sideBottomPx = fullHeight ? box.size.y : box.size.y - strokeHalfPx;
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, strokeHalfPx, sideTopPx);
		nvgLineTo(args.vg, strokeHalfPx, sideBottomPx); // left
		nvgMoveTo(args.vg, box.size.x - strokeHalfPx, sideTopPx);
		nvgLineTo(args.vg, box.size.x - strokeHalfPx, sideBottomPx); // right
		if (!fullHeight)
		{
			nvgMoveTo(args.vg, strokeHalfPx, strokeHalfPx);
			nvgLineTo(args.vg, box.size.x - strokeHalfPx, strokeHalfPx); // top
			nvgMoveTo(args.vg, strokeHalfPx, box.size.y - strokeHalfPx);
			nvgLineTo(args.vg, box.size.x - strokeHalfPx, box.size.y - strokeHalfPx); // bottom
		}
		nvgStrokeWidth(args.vg, bgInsetPx);
		nvgStrokeColor(args.vg, NEO_PANEL_BORDER_COLOR);
		nvgStroke(args.vg);

		// (3) Two independent, fully-rounded peer frames (not a shared edge), separated by
		// PanelDesignGuide.md's own nested-frame padding (NEO_FRAME_GAP_MM) instead of touching
		// directly - see NeoWork.svg's own file comment for the full reasoning. Both always use
		// the SAME fixed vertical span (NEO_FRAME_TOP_MM..PANELHEIGHT-NEO_FRAME_BOTTOM_MM) - unlike
		// the background above, the frame's own top/bottom never changes with Full Height, since
		// the global sidebar frame is always drawn regardless. Only the row-area frame itself is
		// skipped entirely in Full Height (a stacked pair needs a chrome-free rectangle there, not
		// a frame that would visibly interrupt the seam).
		float frameTopPx = mm2px(NEO_FRAME_TOP_MM);
		float frameBottomPx = box.size.y - mm2px(NEO_FRAME_BOTTOM_MM);
		float radiusPx = mm2px(NEO_FRAME_RADIUS_MM);

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, marginPx, frameTopPx, globalAreaEdgePx - halfGapPx - marginPx, frameBottomPx - frameTopPx, radiusPx);
		nvgStrokeWidth(args.vg, mm2px(NEO_FRAME_STROKE_MM));
		nvgStrokeColor(args.vg, frameColor);
		nvgStroke(args.vg);

		if (!fullHeight)
		{
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, globalAreaEdgePx + halfGapPx, frameTopPx, box.size.x - marginPx - globalAreaEdgePx - halfGapPx, frameBottomPx - frameTopPx, radiusPx);
			nvgStrokeWidth(args.vg, mm2px(NEO_FRAME_STROKE_MM));
			nvgStrokeColor(args.vg, frameColor);
			nvgStroke(args.vg);
		}

		// (4) Accent stripe + "NEO"/"ORANGE LINE" brand text - centered on the full current
		// box.size.x (recomputed every draw() call so it tracks any resize; a baked SVG's fixed x
		// can't do this on its own). In Full Height this retreats to span only the global area's
		// own content width instead, matching the row-area frame being skipped above - nothing
		// brand-related crosses the seam when two instances are stacked.
		float accentX1 = marginPx;
		float accentX2 = fullHeight ? (globalAreaEdgePx - halfGapPx) : (box.size.x - marginPx);
		float centerX = (accentX1 + accentX2) / 2.f;

		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, accentX1, mm2px(NEO_ACCENT_Y_MM));
		nvgLineTo(args.vg, accentX2, mm2px(NEO_ACCENT_Y_MM));
		nvgStrokeWidth(args.vg, mm2px(NEO_FRAME_STROKE_MM));
		nvgStrokeColor(args.vg, ORANGE);
		nvgLineCap(args.vg, NVG_ROUND);
		nvgStroke(args.vg);

		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/RobotoCondensed-Bold.ttf"));
		if (font && font->handle >= 0)
		{
			nvgFontFaceId(args.vg, font->handle);
			nvgFillColor(args.vg, text);

			nvgFontSize(args.vg, mm2px(NEO_TITLE_SIZE_MM));
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, centerX, mm2px(NEO_TITLE_CENTER_Y_MM), "NEO", nullptr);

			nvgFontSize(args.vg, mm2px(NEO_WORDMARK_SIZE_MM));
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
			nvgText(args.vg, centerX, mm2px(NEO_WORDMARK_ORANGE_Y_MM), "ORANGE", nullptr);
			nvgText(args.vg, centerX, mm2px(NEO_WORDMARK_LINE_Y_MM), "LINE", nullptr);
		}

		Widget::draw(args);
	}
};

/**
	Small square/pill toggle button shared by the per-row LEFT/RIGHT paging Params (FOLLOW moved
	to a single global button, 2026-07-21 - see NeoWidget's own globalFollowButton) - file-scope
	(not a constructor-local type) so NeoWidget can hold typed arrays of these and reposition them
	every step() as the row layout changes (Grid Rows / Full Height).
*/
struct NeoRowButton : ParamWidget
{
	NVGcolor onColor = ORANGE;
	void draw(const DrawArgs &args) override
	{
		engine::ParamQuantity *pq = getParamQuantity();
		bool on = pq && pq->getValue() > 0.5f;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.f);
		nvgFillColor(args.vg, on ? onColor : nvgRGB(0x30, 0x30, 0x30));
		nvgFill(args.vg);
	}
	void onButton(const event::Button &e) override
	{
		engine::ParamQuantity *pq = getParamQuantity();
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && pq)
		{
			pq->setValue(pq->getValue() > 0.5f ? 0.f : 1.f);
			e.consume(this);
		}
		ParamWidget::onButton(e);
	}
};

/**
	Main Module Widget - resizable panel, up to 16 row-slots, NEO_ROWS_MIN..NEO_ROWS_MAX (4..8) of
	them actually shown at once (right-click "Grid Rows"). Function first, styling later per
	Dieter's own instruction - simple graphics throughout.
*/
struct NeoWidget : ModuleWidget
{
	NeoResizeHandle *resizeHandle = nullptr;
	NeoPanelWidget *panelWidget = nullptr;
	NeoRowCellsWidget *rowCells[NEO_NUM_ROWS] = {};
	NeoRowColorDotWidget *rowNameDots[NEO_NUM_ROWS] = {};
	NeoRowNameField *rowNameFields[NEO_NUM_ROWS] = {};
	NeoRowHeaderFrameWidget *rowHeaderFrames[NEO_NUM_ROWS] = {};
	// Track/channel select knobs + their own numeric displays (2026-07-20) - replace the old
	// memTapeBtns entirely (see ROW_MEMTAPE_PARAM's own removal note, Neo.hpp).
	NeoRowTextDisplayWidget *trackDisplays[NEO_NUM_ROWS] = {};
	ParamWidget *trackKnobs[NEO_NUM_ROWS] = {};
	NeoRowKnobRingWidget *trackKnobRings[NEO_NUM_ROWS] = {};
	NeoRowTextDisplayWidget *channelDisplays[NEO_NUM_ROWS] = {};
	ParamWidget *channelKnobs[NEO_NUM_ROWS] = {};
	NeoRowKnobRingWidget *channelKnobRings[NEO_NUM_ROWS] = {};
	// Right-aligned page/position display (2026-07-20) - see NEO_ROW_POSITION_DISPLAY_WIDTH_MM's
	// own comment (Neo.hpp).
	NeoRowTextDisplayWidget *positionDisplays[NEO_NUM_ROWS] = {};
	NeoRowButton *leftBtns[NEO_NUM_ROWS] = {};
	NeoRowButton *rightBtns[NEO_NUM_ROWS] = {};
	// Seam-bridging strips, reused verbatim from XShared.hpp (XExtStripWidget is already fully
	// family-agnostic - Neo already implements XExpanderInterface::getXStyle() for the X-family
	// relay above, which is all getXNeighborStyle()/updateXExtStrip() need to work here too).
	// Both edges get one (NEO's own bridged Host can sit on either side, no fixed direction) -
	// the right one's own x-position needs re-deriving every step() since, unlike every other
	// module that owns one of these, NEO's own panel width isn't fixed.
	XExtStripWidget *extStripRight = nullptr;
	XExtStripWidget *extStripLeft = nullptr;
	OLLabelButton *lockButton = nullptr;
	OLLabelButton *globalFollowButton = nullptr;
	OLLabelButton *fullHeightButton = nullptr;
	OLLabelButton *unbindButton = nullptr;

	NeoWidget(Neo *module)
	{
		setModule(module);

		// NEO_DEFAULT_WIDTH_HP IS the module's fixed minimum (see its own comment, Neo.hpp) - a
		// patch saved before that constant was corrected (2026-07-20) could still carry a stored
		// width narrower than it now guarantees, so clamp up on load rather than re-deriving the
		// floor at runtime.
		float widthHp = module ? module->OL_state[PANEL_WIDTH_HP_JSON] : NEO_DEFAULT_WIDTH_HP;
		if (widthHp < NEO_DEFAULT_WIDTH_HP)
			widthHp = NEO_DEFAULT_WIDTH_HP;
		box.size = Vec(widthHp * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		panelWidget = new NeoPanelWidget();
		panelWidget->module = module;
		panelWidget->box.size = box.size;
		addChild(panelWidget);

		extStripRight = addXExtStrip(this, widthHp * 5.08f);
		extStripLeft = addXExtStripLeft(this); // both already include X_STRIP_FRAME_NUDGE_MM (XShared.hpp)

		// Spans the global area's own frame width, inset by one frame-padding unit on left/top/
		// right from that frame's own edges (see NEO_LOCK_BUTTON_X/Y/WIDTH_MM's own comment).
		// Built on the shared OLLabelButton (OrangeLine.hpp, 2026-07-20 partial refactor,
		// Dieter's own instruction: "let just NEO use this common code for now... roll it out to
		// the rest of the modules... another day") - three-state grey/themed/green accent color
		// and the "LOCK"/"FREE" label are wired here via callbacks, the shared widget only owns
		// the actual drawing (fill/stroke/centered-label-via-nvgTextBounds).
		lockButton = new OLLabelButton();
		lockButton->fontSize = mm2px(Vec(NEO_GLOBAL_AREA_BUTTON_FONT_SIZE_MM, 0.f)).x;
		lockButton->cornerRadiusPx = mm2px(NEO_ROW_DISPLAY_RADIUS_MM);
		lockButton->strokeWidthPx = mm2px(NEO_FRAME_STROKE_MM);
		lockButton->textYNudgePx = mm2px(0.2f); // first guess at the residual nudge, live-tuning
		lockButton->getFillColor = [module]() {
			float style = module ? module->OL_state[STYLE_JSON] : STYLE_ORANGE;
			return (style == STYLE_DARK) ? X_BUTTON_FILL_DARK : (style == STYLE_BRIGHT) ? X_BUTTON_FILL_BRIGHT : X_BUTTON_FILL_ORANGE;
		};
		lockButton->getAccentColor = [module]() {
			if (!module || !module->neoHost)
				return X_COLOR_INACTIVE_GREY;
			bool locked = module->OL_state[LOCKED_JSON] > 0.5f;
			if (locked)
				return NEO_LOCK_ON_COLOR;
			return xThemedTextColor(module->OL_state[STYLE_JSON]);
		};
		lockButton->getLabel = [module]() {
			// "FREE" is now reserved for the UNBIND button's own Host-disconnect action below -
			// this button's own "locked" label is "UNLOCK" instead (Dieter's own terminology
			// catch, 2026-07-21: the two buttons must never share a label for two unrelated
			// actions).
			bool locked = module && module->OL_state[LOCKED_JSON] > 0.5f;
			return std::string(locked ? "UNLOCK" : "LOCK");
		};
		lockButton->onClick = [module]() {
			if (module)
				module->toggleLock();
		};
		lockButton->box.pos = mm2px(Vec(NEO_LOCK_BUTTON_X_MM, NEO_LOCK_BUTTON_Y_MM));
		lockButton->box.size = mm2px(Vec(NEO_LOCK_BUTTON_WIDTH_MM, NEO_LOCK_BUTTON_HEIGHT_MM));
		addChild(lockButton);

		// Global FOLLOW (2026-07-21) - applies to every row at once, replacing the deferred
		// per-row toggle (see moduleProcess()'s own comment). Same button shape as LOCK/UNLOCK,
		// stacked directly below it. Static label, two-state color only (no grey/disconnected
		// state - Dieter's own spec: "off color is default text color... green if enabled").
		globalFollowButton = new OLLabelButton();
		globalFollowButton->fontSize = mm2px(Vec(NEO_GLOBAL_AREA_BUTTON_FONT_SIZE_MM, 0.f)).x;
		globalFollowButton->cornerRadiusPx = mm2px(NEO_ROW_DISPLAY_RADIUS_MM);
		globalFollowButton->strokeWidthPx = mm2px(NEO_FRAME_STROKE_MM);
		globalFollowButton->textYNudgePx = mm2px(0.2f);
		globalFollowButton->getFillColor = [module]() {
			float style = module ? module->OL_state[STYLE_JSON] : STYLE_ORANGE;
			return (style == STYLE_DARK) ? X_BUTTON_FILL_DARK : (style == STYLE_BRIGHT) ? X_BUTTON_FILL_BRIGHT : X_BUTTON_FILL_ORANGE;
		};
		globalFollowButton->getAccentColor = [module]() {
			if (!module)
				return xThemedTextColor(STYLE_ORANGE);
			bool on = module->OL_state[GLOBAL_FOLLOW_JSON] > 0.5f;
			return on ? NEO_LOCK_ON_COLOR : xThemedTextColor(module->OL_state[STYLE_JSON]);
		};
		globalFollowButton->getLabel = []() { return std::string("FOLLOW"); };
		globalFollowButton->onClick = [module]() {
			if (module)
				module->toggleGlobalFollow();
		};
		globalFollowButton->box.pos = mm2px(Vec(NEO_FOLLOW_BUTTON_X_MM, NEO_FOLLOW_BUTTON_Y_MM));
		globalFollowButton->box.size = mm2px(Vec(NEO_FOLLOW_BUTTON_WIDTH_MM, NEO_FOLLOW_BUTTON_HEIGHT_MM));
		addChild(globalFollowButton);

		// Global FULL HEIGHT (2026-07-22) - moved from the right-click "Full Height" menu item
		// (Neo::toggleFullHeight() has the unchanged toggle logic). Same button shape, stacked
		// directly below FOLLOW. Label reads the action a click performs, same convention as
		// LOCK/UNLOCK - "FULL" while in Normal mode, "NORMAL" while in Full Height mode. Plain
		// orange either way for now (Dieter's own call) - NEO_FULLHEIGHT_ON_COLOR/OFF_COLOR are
		// still two separate constants so either can change independently later.
		fullHeightButton = new OLLabelButton();
		fullHeightButton->fontSize = mm2px(Vec(NEO_GLOBAL_AREA_BUTTON_FONT_SIZE_MM, 0.f)).x;
		fullHeightButton->cornerRadiusPx = mm2px(NEO_ROW_DISPLAY_RADIUS_MM);
		fullHeightButton->strokeWidthPx = mm2px(NEO_FRAME_STROKE_MM);
		fullHeightButton->textYNudgePx = mm2px(0.2f);
		fullHeightButton->getFillColor = [module]() {
			float style = module ? module->OL_state[STYLE_JSON] : STYLE_ORANGE;
			return (style == STYLE_DARK) ? X_BUTTON_FILL_DARK : (style == STYLE_BRIGHT) ? X_BUTTON_FILL_BRIGHT : X_BUTTON_FILL_ORANGE;
		};
		fullHeightButton->getAccentColor = [module]() {
			bool fullHeight = module && module->OL_state[FULL_HEIGHT_JSON] > 0.5f;
			return fullHeight ? NEO_FULLHEIGHT_ON_COLOR : NEO_FULLHEIGHT_OFF_COLOR;
		};
		fullHeightButton->getLabel = [module]() {
			bool fullHeight = module && module->OL_state[FULL_HEIGHT_JSON] > 0.5f;
			return std::string(fullHeight ? "NORMAL" : "FULL");
		};
		fullHeightButton->onClick = [module]() {
			if (module)
				module->toggleFullHeight();
		};
		fullHeightButton->box.pos = mm2px(Vec(NEO_FULLHEIGHT_BUTTON_X_MM, NEO_FULLHEIGHT_BUTTON_Y_MM));
		fullHeightButton->box.size = mm2px(Vec(NEO_FULLHEIGHT_BUTTON_WIDTH_MM, NEO_FULLHEIGHT_BUTTON_HEIGHT_MM));
		addChild(fullHeightButton);

		// Global UNBIND (2026-07-21) - a placeholder for now (Dieter's own words: "we do not yet
		// know whether we will keep this button"). Calls the same disconnectNeoHost() the
		// right-click "Disconnect" menu item already uses - that menu item is deliberately left in
		// place alongside this, not replaced. Anchored to the bottom of the global frame rather
		// than stacked with LOCK/FOLLOW at the top.
		unbindButton = new OLLabelButton();
		unbindButton->fontSize = mm2px(Vec(NEO_GLOBAL_AREA_BUTTON_FONT_SIZE_MM, 0.f)).x;
		unbindButton->cornerRadiusPx = mm2px(NEO_ROW_DISPLAY_RADIUS_MM);
		unbindButton->strokeWidthPx = mm2px(NEO_FRAME_STROKE_MM);
		unbindButton->textYNudgePx = mm2px(0.2f);
		unbindButton->getFillColor = [module]() {
			float style = module ? module->OL_state[STYLE_JSON] : STYLE_ORANGE;
			return (style == STYLE_DARK) ? X_BUTTON_FILL_DARK : (style == STYLE_BRIGHT) ? X_BUTTON_FILL_BRIGHT : X_BUTTON_FILL_ORANGE;
		};
		unbindButton->getAccentColor = [module]() {
			if (!module || !module->neoHost)
				return X_COLOR_INACTIVE_GREY;
			return xThemedTextColor(module->OL_state[STYLE_JSON]);
		};
		unbindButton->getLabel = []() { return std::string("FREE"); };
		unbindButton->onClick = [module]() {
			if (module)
				module->disconnectNeoHost();
		};
		unbindButton->box.pos = mm2px(Vec(NEO_UNBIND_BUTTON_X_MM, NEO_UNBIND_BUTTON_Y_MM));
		unbindButton->box.size = mm2px(Vec(NEO_UNBIND_BUTTON_WIDTH_MM, NEO_UNBIND_BUTTON_HEIGHT_MM));
		addChild(unbindButton);

		// All real geometry (position, size, visibility) for every one of these is set fresh
		// every step() below, driven by the current Grid Rows count / Full Height state - it can
		// change at runtime via the right-click menu, so nothing here can be a fixed one-time
		// layout the way it used to be when NEO always showed a fixed 16 rows. What's created
		// here is only the widgets themselves plus whatever's genuinely constant (box.size for
		// the buttons - their own size never depends on row height, only their position does).
		for (int r = 0; r < NEO_NUM_ROWS; r++)
		{
			NeoRowHeaderFrameWidget *headerFrame = new NeoRowHeaderFrameWidget();
			headerFrame->module = module;
			addChild(headerFrame);
			rowHeaderFrames[r] = headerFrame;

			NeoRowColorDotWidget *nameDot = new NeoRowColorDotWidget();
			nameDot->module = module;
			nameDot->row = r;
			nameDot->box.size = mm2px(Vec(NEO_ROW_NAME_DOT_DIAMETER_MM, NEO_ROW_NAME_DOT_DIAMETER_MM));
			addChild(nameDot);
			rowNameDots[r] = nameDot;

			NeoRowNameField *nameField = new NeoRowNameField();
			nameField->module = module;
			nameField->row = r;
			nameField->box.size = mm2px(Vec(NEO_ROW_NAME_TEXT_WIDTH_MM, NEO_ROW_NUMBER_DISPLAY_HEIGHT_MM));
			addChild(nameField);
			rowNameFields[r] = nameField;

			// Track select (2026-07-20) - display + knob, replacing the old MEM/TAPE toggle
			// button entirely (see ROW_MEMTAPE_PARAM's own removal note, Neo.hpp).
			NeoRowTextDisplayWidget *trackDisplay = new NeoRowTextDisplayWidget();
			trackDisplay->module = module;
			trackDisplay->fontSize = mm2px(Vec(NEO_ROW_NUMBER_DISPLAY_FONT_SIZE_MM, 0.f)).x;
			trackDisplay->box.size = mm2px(Vec(NEO_ROW_TRACK_DISPLAY_WIDTH_MM, NEO_ROW_NUMBER_DISPLAY_HEIGHT_MM));
			trackDisplay->getText = [r](Neo *m) {
				return (m->neoHost) ? m->neoHost->getTrackName(m->getRowTrack(r)) : std::string("----");
			};
			addChild(trackDisplay);
			trackDisplays[r] = trackDisplay;

			NeoRowKnobRingWidget *trackKnobRing = new NeoRowKnobRingWidget();
			trackKnobRing->module = module;
			trackKnobRing->box.size = mm2px(Vec(NEO_ROW_SELECT_KNOB_SIZE_MM, NEO_ROW_SELECT_KNOB_SIZE_MM));
			addChild(trackKnobRing);
			trackKnobRings[r] = trackKnobRing;

			ParamWidget *trackKnob = createParam<RoundSmallBlackKnob>(Vec(), module, ROW_TRACK_PARAM + r);
			addParam(trackKnob);
			trackKnobs[r] = trackKnob;

			// Channel select (2026-07-20) - display + knob, replacing the old right-click-only
			// "Rows -> Row N -> channel" submenu entirely (same reasoning as the track knob
			// replacing MEM/TAPE - a separate menu would be redundant and could disagree).
			NeoRowTextDisplayWidget *channelDisplay = new NeoRowTextDisplayWidget();
			channelDisplay->module = module;
			channelDisplay->fontSize = mm2px(Vec(NEO_ROW_NUMBER_DISPLAY_FONT_SIZE_MM, 0.f)).x;
			channelDisplay->box.size = mm2px(Vec(NEO_ROW_CHANNEL_DISPLAY_WIDTH_MM, NEO_ROW_NUMBER_DISPLAY_HEIGHT_MM));
			channelDisplay->getText = [r](Neo *m) {
				char buf[8];
				// Space-padded, right-aligned within a 2-char field (Dieter's own instruction,
				// 2026-07-20 - same "%Nd already right-aligns with blanks" convention as the
				// position display's own fields).
				snprintf(buf, sizeof(buf), "%2d", m->getRowChannel(r) + 1);
				return std::string(buf);
			};
			addChild(channelDisplay);
			channelDisplays[r] = channelDisplay;

			NeoRowKnobRingWidget *channelKnobRing = new NeoRowKnobRingWidget();
			channelKnobRing->module = module;
			channelKnobRing->box.size = mm2px(Vec(NEO_ROW_SELECT_KNOB_SIZE_MM, NEO_ROW_SELECT_KNOB_SIZE_MM));
			addChild(channelKnobRing);
			channelKnobRings[r] = channelKnobRing;

			ParamWidget *channelKnob = createParam<RoundSmallBlackKnob>(Vec(), module, ROW_CHANNEL_PARAM + r);
			addParam(channelKnob);
			channelKnobs[r] = channelKnob;

			// Page/position display (2026-07-20) - two stacked lines, right-aligned against the
			// header's own actual current right edge (set in step(), unlike every left-anchored
			// control above, since header width isn't fixed): line 1 is "pp/PP" (page/pages),
			// line 2 is "sss/SSS" (step/steps) - split out of the original single-line
			// "pp/PP:sss/SSS" spec once the two-line stack itself looked good. sss is read as the
			// live play cursor position (first-pass interpretation of Dieter's own spec -
			// "current step pos" - not yet visually confirmed).
			NeoRowTextDisplayWidget *positionDisplay = new NeoRowTextDisplayWidget();
			positionDisplay->module = module;
			positionDisplay->fontSize = mm2px(Vec(NEO_ROW_POSITION_DISPLAY_FONT_SIZE_MM, 0.f)).x;
			positionDisplay->box.size = mm2px(Vec(NEO_ROW_POSITION_DISPLAY_WIDTH_MM, NEO_ROW_POSITION_DISPLAY_HEIGHT_MM));
			positionDisplay->getText = [r](Neo *m) {
				if (!m->neoHost)
					return std::string("HELLO");
				int channel = m->getRowChannel(r);
				int visibleCols = m->getVisibleColumns();
				int page = (int) m->OL_state[ROW_PAGE_JSON + r];
				int loopLen = std::max(1, m->neoHost->getLoopLen(channel));
				// visibleCols can genuinely be 0 now (2026-07-22, no minimum column count
				// guarantee anymore) - nothing to page through, so just show 1 page rather than
				// dividing by zero.
				int numPages = (visibleCols > 0) ? std::max(1, (loopLen + visibleCols - 1) / visibleCols) : 1;
				// Right-align the WHOLE "page/pages" string within a 7-char field using spaces,
				// not each number padded individually (Dieter's own correction, 2026-07-20 -
				// "____1/1" and "__12/64", 7 chars total either way).
				char inner[16];
				snprintf(inner, sizeof(inner), "%d/%d", page + 1, numPages);
				char buf[20];
				snprintf(buf, sizeof(buf), "%7s", inner);
				return std::string(buf);
			};
			positionDisplay->getText2 = [r](Neo *m) {
				if (!m->neoHost)
					return std::string("HELLO");
				int channel = m->getRowChannel(r);
				int loopLen = std::max(1, m->neoHost->getLoopLen(channel));
				int cursor = m->neoHost->getPlayCursor(channel);
				char inner[16];
				snprintf(inner, sizeof(inner), "%d/%d", cursor + 1, loopLen);
				char buf[20];
				snprintf(buf, sizeof(buf), "%7s", inner);
				return std::string(buf);
			};
			addChild(positionDisplay);
			positionDisplays[r] = positionDisplay;

			// Per-row FOLLOW button removed 2026-07-21 (Dieter's own call, deferred in favor of
			// the single global FOLLOW button in the global area - see moduleProcess()'s comment).
			NeoRowButton *leftBtn = createParam<NeoRowButton>(Vec(), module, ROW_LEFT_PARAM + r);
			leftBtn->box.size = mm2px(Vec(NEO_ROW_PAGEBTN_SIZE_MM, NEO_ROW_PAGEBTN_SIZE_MM));
			addParam(leftBtn);
			leftBtns[r] = leftBtn;

			NeoRowButton *rightBtn = createParam<NeoRowButton>(Vec(), module, ROW_RIGHT_PARAM + r);
			rightBtn->box.size = mm2px(Vec(NEO_ROW_PAGEBTN_SIZE_MM, NEO_ROW_PAGEBTN_SIZE_MM));
			addParam(rightBtn);
			rightBtns[r] = rightBtn;

			NeoRowCellsWidget *cells = new NeoRowCellsWidget();
			cells->module = module;
			cells->row = r;
			cells->box.size = mm2px(Vec(1.f, 1.f)); // fixed up in step()
			addChild(cells);
			rowCells[r] = cells;
		}

		resizeHandle = new NeoResizeHandle();
		resizeHandle->module = module;
		addChild(resizeHandle);
	}

	void step() override
	{
		Neo *neoModule = dynamic_cast<Neo *>(module);
		if (neoModule)
		{
			// Lock sync (rows/fullHeight half) - see Neo::NeoLockData's own comment for the full
			// mechanism. Runs BEFORE the row-layout computation below, so any adoption this tick
			// is already reflected in it (Dieter's own instruction, 2026-07-20: any locked
			// instance can change the group's config, and it propagates live to every other
			// locked instance; width itself is synced separately, further down, after the normal
			// cell-size-driven auto-resize has already settled for this tick).
			bool locked = neoModule->OL_state[LOCKED_JSON] > 0.5f;
			Neo::NeoLockData lockData;
			bool lockDataValid = false;
			if (locked && neoModule->neoHost)
			{
				lockData = neoModule->readLockData();
				bool amIRegistered = std::find(lockData.ids.begin(), lockData.ids.end(), (int64_t) neoModule->id) != lockData.ids.end();
				if (!amIRegistered)
				{
					// My own id isn't actually in the shared group - almost certainly a preset
					// was just loaded (Rack module ids aren't stable across a save/reload, so a
					// persisted "lock" object can never correspond to a freshly-loaded instance's
					// own id) or the group otherwise no longer exists. Auto-unlock rather than
					// silently pretending to be part of a group I'm not really registered in -
					// Dieter's own instruction, 2026-07-20: the user re-locks with one click once
					// they've confirmed which instances should actually be grouped.
					neoModule->OL_setOutState(LOCKED_JSON, 0.f);
					neoModule->neoLockLastSyncedRows = -1.f;
					neoModule->neoLockLastSyncedFullHeight = -1.f;
					locked = false;
				}
				else
				{
					lockDataValid = true;
					float myRows = neoModule->OL_state[ROWS_DISPLAYED_JSON];
					bool myFullHeight = neoModule->OL_state[FULL_HEIGHT_JSON] > 0.5f;
					bool rowsLocallyChanged = (myRows != neoModule->neoLockLastSyncedRows) ||
						(myFullHeight != (neoModule->neoLockLastSyncedFullHeight > 0.5f));
					bool lockNeedsWrite = false;
					if (rowsLocallyChanged)
					{
						// My own value diverged from what I last confirmed matching - a genuine
						// local edit (menu click), not something this sync block itself just
						// did - propagate it as the group's new target.
						lockData.rows = (int) myRows;
						lockData.fullHeight = myFullHeight;
						lockNeedsWrite = true;
						neoModule->neoLockLastSyncedRows = myRows;
						neoModule->neoLockLastSyncedFullHeight = myFullHeight ? 1.f : 0.f;
					}
					else if ((int) myRows != lockData.rows || myFullHeight != lockData.fullHeight)
					{
						// I'm still at what I last confirmed, but the group's own target has
						// since moved (someone else changed it) - adopt it.
						neoModule->OL_setOutState(ROWS_DISPLAYED_JSON, (float) lockData.rows);
						neoModule->OL_setOutState(FULL_HEIGHT_JSON, lockData.fullHeight ? 1.f : 0.f);
						neoModule->neoLockLastSyncedRows = (float) lockData.rows;
						neoModule->neoLockLastSyncedFullHeight = lockData.fullHeight ? 1.f : 0.f;
					}
					if (lockNeedsWrite)
						neoModule->writeLockData(lockData);
				}
			}

			int rowsDisplayed = clamp((int) neoModule->OL_state[ROWS_DISPLAYED_JSON], NEO_ROWS_MIN, NEO_ROWS_MAX);
			bool fullHeight = neoModule->OL_state[FULL_HEIGHT_JSON] > 0.5f;
			float firstRowYMm, cellHeightMm, rowPitchMm;
			neoRowLayout(fullHeight, rowsDisplayed, firstRowYMm, cellHeightMm, rowPitchMm);
			float columnPitchMm = rowPitchMm; // square cells + matching horizontal/vertical padding (2026-07-20) -
			                                   // the per-column footprint always equals the row pitch exactly.
			// "Needed width for N columns" always uses the header's own MINIMUM width - there is no
			// "current" header value to reference anymore (2026-07-21 redesign, see
			// NeoLayoutResult's own comment, Neo.hpp) - the tightest possible total width for N
			// columns is always header-at-minimum plus exactly N columns' worth of pitch.
			float controlsWidthAtMinMm = neoRowAreaControlsWidthMm(fullHeight, NEO_ROW_HEADER_MIN_WIDTH_MM);

			// KISS (Dieter's own instruction, 2026-07-20, reaffirmed/strengthened 2026-07-22): NEO
			// never auto-resizes itself just because Grid Rows/Full Height changed the cell pitch
			// or the mode's own controlsWidthMm - it simply shows as many whole columns as
			// currently fit (getVisibleColumns(), already floor-based, possibly zero), padded to
			// the frame the same way every other edge already is. The module's own width ONLY
			// EVER changes from an explicit user drag (NeoResizeHandle) or the Lock group's own
			// target-width convergence below - never as an automatic side effect of anything else.
			// The earlier "auto-widen to preserve column count across a Full Height toggle"
			// mechanism that used to live here is gone (2026-07-22, Dieter's own KISS
			// simplification) - it was itself the thing violating this same rule, resizing the
			// module every time Normal and Full Height needed different widths for the same
			// column count. See NEO_DEFAULT_WIDTH_HP's own comment (Neo.hpp) for what replaced the
			// guarantee it used to serve.

			// Lock sync (width half) - width, unlike rows, is "weak" (Dieter's own framing) - a
			// locked instance always keeps its own adopted row count, but only matches the common
			// width as far as free space allows, remembering the target and retrying every tick
			// so it grows (or shrinks) into it the moment room frees up, rather than failing the
			// lock outright.
			//
			// KISS rule (2026-07-21, Dieter's own instruction, after a real bug where this block's
			// old "propagate my own width if it changed" logic could write a follower's own
			// incidental/blocked width back as the group's new target, fighting a manual drag on a
			// completely different instance): this block is now READ-ONLY with respect to
			// lockData.widthHp - it only ever reads the target and tries to match it, never writes
			// it. The ONLY writer of lockData.widthHp is the instance actually being resized
			// (NeoResizeHandle::onDragMove(), live during a manual drag) - Full Height/Grid Rows no
			// longer write it at all (2026-07-22, see neoPendingColumnTarget's own removal comment
			// above: the panel's own width now only ever changes from an actual drag). Every other
			// locked instance - a pure follower - just keeps retrying to grow
			// or shrink toward whatever the target currently is, same as before; if blocked, its own
			// column count just stays reduced for now and it grabs the space automatically the
			// moment room frees up (Dieter's own spec: "wait means they will grab space until they
			// get the locked HP automatically if they can").
			//
			// Growing steps ONE HP at a time toward the target, not a single all-or-nothing jump
			// straight to it (2026-07-21, Dieter's own catch: with two locked instances and one
			// blocked by a neighbor several HP away, moving that neighbor only partway did nothing
			// at all - the follower stayed put until the ENTIRE remaining gap opened up in one go,
			// instead of grabbing whatever became free immediately, "it should always try to
			// resize itself even if it's one hp it can get"). Still no search - exactly one
			// candidate width is tried per tick, same principle as everywhere else in this file;
			// the candidate is just "one HP closer" instead of "the whole target," so it advances
			// one step per tick as room frees up, converging over a few frames instead of needing
			// the full gap free all at once. Shrinking is different - it can never be blocked by a
			// neighbor (a smaller footprint can only ever reduce overlap, never introduce a new
			// collision), so it still jumps straight to the target in one tick, no reason to
			// animate it out over several frames.
			if (locked && lockDataValid)
			{
				int myWidthHp = (int) std::round(neoModule->OL_state[PANEL_WIDTH_HP_JSON]);
				if (myWidthHp != lockData.widthHp)
				{
					// Both-modes worst case - see neoMinWidthHpAnyMode()'s own comment. Measured
					// against the header's own MINIMUM width, same reasoning as the drag handle's
					// own clamp above - there is no "current" header value.
					float minWidthPx = neoMinWidthHpAnyMode(NEO_ROW_HEADER_MIN_WIDTH_MM) * RACK_GRID_WIDTH;
					float maxWidthPx = neoMaxWidthHp(neoModule->neoHost, columnPitchMm, controlsWidthAtMinMm) * RACK_GRID_WIDTH;
					// Kept as cheap insurance, same reasoning as the drag handle's own comment -
					// guarantees maxWidthPx can never end up smaller than minWidthPx.
					maxWidthPx = std::max(maxWidthPx, minWidthPx);
					int minWidthHp = (int) std::round(minWidthPx / RACK_GRID_WIDTH);
					int maxWidthHp = (int) std::round(maxWidthPx / RACK_GRID_WIDTH);
					int clampedTargetHp = clamp(lockData.widthHp, minWidthHp, maxWidthHp);
					int stepHp = (clampedTargetHp <= myWidthHp) ? clampedTargetHp : myWidthHp + 1;

					if (stepHp != myWidthHp)
					{
						float stepWidthPx = (float) stepHp * RACK_GRID_WIDTH;
						Rect oldBox = box;
						Rect newBox = box;
						newBox.size.x = stepWidthPx;
						box = newBox;
						if (APP->scene->rack->requestModulePos(this, newBox.pos))
							neoModule->OL_setOutState(PANEL_WIDTH_HP_JSON, (float) stepHp);
						else
							box = oldBox; // even one more HP is blocked - stay put, retry next tick
					}
				}
			}

			float widthMm = neoModule->OL_state[PANEL_WIDTH_HP_JSON] * 5.08f; // may have just changed above

			// Hybrid live/frozen split during a drag (2026-07-22, Dieter's own refinement): the
			// HEADER's own width stays frozen for the duration of a drag - this instance's own, or
			// any other locked instance's, via NeoLockData::dragging - so its right edge (and thus
			// every column's own x-position) never shifts mid-drag, which is what actually caused
			// the visible jitter (not the column count changing). The visible COLUMN COUNT still
			// updates live throughout, though - columns simply appear/disappear at the grid's own
			// trailing edge as the current total width changes, computed fresh every tick against
			// that same frozen header (no leftover absorbed into the header itself yet). Only once
			// the drag actually ends does the header do its one real "right alignment" pass -
			// absorbing whatever leftover space is left over into itself - via the normal, full
			// neoComputeLayout() settle.
			bool groupDragging = locked && lockDataValid && lockData.dragging;
			bool dragging = resizeHandle->isDragging || groupDragging;
			if (dragging)
			{
				float frozenControlsWidthMm = neoRowAreaControlsWidthMm(fullHeight, neoModule->neoCachedLayout.headerWidthMm);
				int liveVisibleCols;
				float liveLeftoverMm;
				neoColumnFit(widthMm, frozenControlsWidthMm, columnPitchMm, liveVisibleCols, liveLeftoverMm);
				neoModule->neoCachedLayout.visibleCols = liveVisibleCols;
				neoModule->neoCachedLayout.cellsWidthMm = (float) liveVisibleCols * columnPitchMm;
				// headerWidthMm/controlsWidthMm/leftoverMm deliberately left untouched here - they
				// only change once the drag ends and the full settle below runs.
			}
			else
				neoModule->neoCachedLayout = neoComputeLayout(widthMm, fullHeight, rowsDisplayed);

			box.size.x = mm2px(widthMm);
			// Read from the cache (not a fresh recompute) so this widget's own positioning always
			// agrees with what NeoRowCellsWidget's own draw()/click-handling sees via
			// Neo::getRowHeaderWidthMm()/getControlsWidthMm()/getVisibleColumns() - all of these
			// read the exact same neoCachedLayout, so they can never disagree about what's shown.
			NeoLayoutResult layout = neoModule->neoCachedLayout;
			float rowHeaderWidthMm = layout.headerWidthMm;
			float cellsWidthMm = layout.cellsWidthMm;

			for (int r = 0; r < NEO_NUM_ROWS; r++)
			{
				bool rowVisible = r < rowsDisplayed;
				rowHeaderFrames[r]->visible = rowVisible;
				rowNameDots[r]->visible = rowVisible;
				rowNameFields[r]->visible = rowVisible;
				trackDisplays[r]->visible = rowVisible;
				trackKnobs[r]->visible = rowVisible;
				trackKnobRings[r]->visible = rowVisible;
				channelDisplays[r]->visible = rowVisible;
				channelKnobs[r]->visible = rowVisible;
				channelKnobRings[r]->visible = rowVisible;
				positionDisplays[r]->visible = rowVisible;
				leftBtns[r]->visible = rowVisible;
				rightBtns[r]->visible = rowVisible;
				rowCells[r]->visible = rowVisible;
				if (!rowVisible)
					continue;

				float y = firstRowYMm + (float) r * rowPitchMm;
				float centerY = y + cellHeightMm / 2.f;

				// Header-data frame: this is a NESTED frame inside the row area's own outer frame
				// (matches NEO_ROW_HEADER_LEFT_RADIUS_MM's own comment, Neo.hpp - "separated from
				// it by NEO_FRAME_GAP_MM"), not a packed cell - so its left edge needs a FULL
				// NEO_FRAME_GAP_MM inset from the row area's own frame edge, not half of one
				// (Dieter's own catch, 2026-07-20: the first attempt used half the row gap here,
				// matching how step-cells pad themselves, and ended up visibly too far left/
				// under-padded against the row area's own frame line). rowAreaFrameLeftMm mirrors
				// NeoPanelWidget::draw()'s own row-area frame edge (globalAreaEdgePx + halfGapPx)
				// exactly. Right edge approximates the same on the other side with NEO_FRAME_GAP_MM
				// (see its own comment, Neo.hpp, for why that side isn't exact) - Dieter's own
				// spec, 2026-07-20. headerBoundaryMm (global area + the header's own CURRENT
				// width, deliberately NOT including Full Height's own resize-strip reservation -
				// that's reserved at the panel's far right edge, unrelated to where the header/
				// step-cells boundary sits) is where the step-cell grid actually begins.
				float headerBoundaryMm = NEO_GLOBAL_AREA_WIDTH_MM + rowHeaderWidthMm;
				float rowAreaFrameLeftMm = NEO_GLOBAL_AREA_WIDTH_MM + NEO_FRAME_GAP_MM / 2.f;
				float headerFrameLeftMm = rowAreaFrameLeftMm + NEO_FRAME_GAP_MM;
				// Full Height mode never draws the row area's own outer frame at all
				// (NeoPanelWidget::draw() - "chrome-free rectangle") - the nested one-frame-gap
				// inset just above assumed nesting INSIDE that (now-nonexistent) frame, so left
				// as-is it leaves a visibly dead, ugly gap between the global area's own frame and
				// the row-header-frame's own left edge, right where a connecting frame line used to
				// be (Dieter's own catch, 2026-07-22, live-testing). Reclaim exactly the two things
				// that used to justify that space - one frame-gap-unit of nested padding, plus the
				// row-area frame's own stroke width - by pulling the row-header-frame's own left
				// edge back by that amount. headerFrameRightMm below is untouched, so the frame
				// simply grows wider to fill the reclaimed space - none of the actual controls
				// inside it (track/channel/name) need to move, they're independently anchored to
				// NEO_GLOBAL_AREA_WIDTH_MM with their own padding, unrelated to this frame's own
				// decorative boundary.
				if (fullHeight)
					headerFrameLeftMm -= (NEO_FRAME_GAP_MM + NEO_FRAME_STROKE_MM);
				// Right edge inset from headerBoundaryMm by HALF a column gap, not a full
				// NEO_FRAME_GAP_MM (Dieter's own catch, 2026-07-21) - the step-cell grid already
				// insets its own first cell by half a column gap from ITS OWN origin
				// (NeoRowCellsWidget::draw(), "inset by half the actual current gap on each side
				// within its own pitch slot"), so a full extra frame-gap here was stacking on top
				// of that and making the header-to-first-cell gap visibly bigger than the gap
				// between any two cells. Using the same half-column-gap here instead makes the two
				// add up to exactly one column gap, matching cell-to-cell spacing exactly. This
				// only moves where the header-DATA-frame's own decorative boundary is drawn -
				// headerBoundaryMm itself (where the grid actually starts, and every resize/
				// column-fit calculation's own idea of "the row header's width") is untouched, so
				// none of neoColumnFit()/neoMinWidthHp()/neoMaxWidthHp() need updating for this.
				float columnGapMm = columnPitchMm - neoModule->getColumnWidthMm();
				float headerFrameRightMm = headerBoundaryMm - columnGapMm / 2.f;
				rowHeaderFrames[r]->box.pos = calculateCoordinates(headerFrameLeftMm, y, 0.f);
				rowHeaderFrames[r]->box.size = mm2px(Vec(std::max(1.f, headerFrameRightMm - headerFrameLeftMm), cellHeightMm));

				// Track/channel displays are vertically centered on the row (Dieter's own
				// follow-up spec, 2026-07-20) - X stays a plain left edge, only Y shifts up by
				// half the display's own height from centerY, same convention every
				// center-positioned control in this loop already uses via .minus(size.div(2)).
				trackDisplays[r]->box.pos = calculateCoordinates(NEO_ROW_TRACK_DISPLAY_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(Vec(0.f, trackDisplays[r]->box.size.y / 2.f));
				trackKnobRings[r]->box.pos = calculateCoordinates(NEO_ROW_TRACK_KNOB_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(trackKnobRings[r]->box.size.div(2.f));
				trackKnobs[r]->box.pos = calculateCoordinates(NEO_ROW_TRACK_KNOB_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(trackKnobs[r]->box.size.div(2.f));
				channelDisplays[r]->box.pos = calculateCoordinates(NEO_ROW_CHANNEL_DISPLAY_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(Vec(0.f, channelDisplays[r]->box.size.y / 2.f));
				channelKnobRings[r]->box.pos = calculateCoordinates(NEO_ROW_CHANNEL_KNOB_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(channelKnobRings[r]->box.size.div(2.f));
				channelKnobs[r]->box.pos = calculateCoordinates(NEO_ROW_CHANNEL_KNOB_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(channelKnobs[r]->box.size.div(2.f));

				// Dot is a small fixed-size control, vertically centered on the row like the track/
				// channel displays above (Dieter's own spec, 2026-07-21).
				rowNameDots[r]->box.pos = calculateCoordinates(NEO_ROW_NAME_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(Vec(0.f, rowNameDots[r]->box.size.y / 2.f));

				// Height matches every other NEO display at this font size (NEO_ROW_NUMBER_DISPLAY_
				// HEIGHT_MM), NOT the full row cell height (Dieter's own catch, 2026-07-21) - a
				// taller-than-the-font box made the text cursor render hugely tall and looked wrong;
				// vertically centered on the row like every other fixed-height display here.
				float nameFieldXMm = NEO_ROW_NAME_X_MM + NEO_ROW_NAME_DOT_DIAMETER_MM + NEO_ROW_NAME_DOT_GAP_MM;
				// Fixed width, same as every other row display - no longer grows to absorb leftover
				// header width (Dieter's own reversal, 2026-07-21, of the original 2026-07-20 spec):
				// that growth made padding/spacing after the field inconsistent whenever the header
				// itself had grown past its own default target, which is confusing more than useful.
				// Only the right-aligned controls (position display) still track the header's own
				// current width - everything else in the row, name field included, stays fixed.
				float nameFieldWidthMm = NEO_ROW_NAME_TEXT_WIDTH_MM;
				rowNameFields[r]->box.size = mm2px(Vec(nameFieldWidthMm, NEO_ROW_NUMBER_DISPLAY_HEIGHT_MM));
				rowNameFields[r]->box.pos = calculateCoordinates(nameFieldXMm + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(Vec(0.f, rowNameFields[r]->box.size.y / 2.f));

				// LEFT/RIGHT positions derived from the name field's own actual current right edge
				// (nameFieldXMm + nameFieldWidthMm) rather than a separately-maintained static
				// offset, so they can never drift out of sync with the field's real edge. FOLLOW no
				// longer sits between them - it moved to the global area (2026-07-21, see
				// moduleProcess()'s own comment) - LEFT now starts directly after the name field.
				//
				// A real bug (Dieter's own catch via screenshot, 2026-07-21):
				// calculateCoordinates(...).minus(box.size.div(2)) treats the X passed in as the
				// button's own CENTER, but the *Mm values below were computed as a LEFT edge
				// (previous element's right edge + one gap) with no correction for that - silently
				// shifting each button left by half its own width, eating most of the intended gap.
				// Fixed by explicitly computing each button's own LEFT edge first (previous left
				// edge + previous width + gap, chained), then converting to a center by adding back
				// that SAME button's own half-width only at the point where it's fed into the
				// centering call - never skip that step.
				float leftLeftMm = nameFieldXMm + nameFieldWidthMm + NEO_FRAME_GAP_MM;
				float leftCenterMm = leftLeftMm + NEO_ROW_PAGEBTN_SIZE_MM / 2.f;
				leftBtns[r]->box.pos = calculateCoordinates(leftCenterMm + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(leftBtns[r]->box.size.div(2.f));
				float rightLeftMm = leftLeftMm + NEO_ROW_PAGEBTN_SIZE_MM + NEO_FRAME_GAP_MM;
				float rightCenterMm = rightLeftMm + NEO_ROW_PAGEBTN_SIZE_MM / 2.f;
				rightBtns[r]->box.pos = calculateCoordinates(rightCenterMm + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(rightBtns[r]->box.size.div(2.f));

				// Right-aligned against the header's own actual current right edge (headerFrameRightMm,
				// already computed above) minus its own width and a small margin - unlike every
				// other row control, this one's x genuinely depends on the header's current
				// (possibly grown-past-Tw) width, not a fixed left-edge offset.
				float positionDisplayXMm = headerFrameRightMm - NEO_ROW_POSITION_DISPLAY_WIDTH_MM - NEO_ROW_POSITION_DISPLAY_MARGIN_MM;
				positionDisplays[r]->box.pos = calculateCoordinates(positionDisplayXMm, centerY, 0.f).minus(Vec(0.f, positionDisplays[r]->box.size.y / 2.f));

				rowCells[r]->box.pos = calculateCoordinates(headerBoundaryMm, y, 0.f);
				rowCells[r]->box.size = mm2px(Vec(cellsWidthMm, cellHeightMm));
			}
			// addXExtStrip() positions the right strip against the panel width it's given at
			// construction time only - unlike every other module that owns one of these, NEO's
			// own width isn't fixed, so its x needs re-deriving here every time too.
			if (extStripRight)
			{
				extStripRight->box.pos.x = mm2px(widthMm - X_STRIP_SEEM_WIDTH_MM / 2.f + X_STRIP_FRAME_NUDGE_MM); // matches addXExtStrip()'s own formula (XShared.hpp) - re-derived every tick since NEO's own width isn't fixed
				updateXExtStrip(extStripRight, neoModule, neoModule->rightExpander.module);
			}
			if (extStripLeft)
				updateXExtStripLeft(extStripLeft, neoModule, neoModule->leftExpander.module);
		}
		if (panelWidget)
			panelWidget->box.size = box.size;
		if (resizeHandle)
		{
			// Same small icon, same corner position, in BOTH Normal and Full Height mode now
			// (2026-07-22) - the small icon alone is sufficient (Dieter's own call, live-testing:
			// the dedicated full-height grip-strip look turned out effectively invisible in
			// practice - see NeoResizeHandle::draw()'s own comment). NEO_TITLE_CENTER_Y_MM is still
			// the right anchor even in Full Height mode - the title text itself still renders
			// there too (just confined to the global area's own width, not the full panel), so this
			// keeps the icon vertically aligned with it exactly as Normal mode already does.
			//
			// NEO_RESIZE_RESERVED_WIDTH_MM (Neo.hpp) is UNCHANGED and still reserved in the column-
			// fit math for Full Height mode - only the drawn glyph and its own box moved, not the
			// width budget it used to visually justify. Live-testing confirmed this reservation was
			// already invisible/harmless before this change (no visible dead gap), so it's left
			// alone rather than risk re-deriving NEO_DEFAULT_WIDTH_HP for a purely cosmetic fix.
			float iconPx = mm2px(NEO_RESIZE_ICON_SIZE_MM);
			float marginPx = mm2px(NEO_FRAME_MARGIN_MM);
			resizeHandle->box.size = Vec(iconPx, iconPx);
			resizeHandle->box.pos = Vec(box.size.x - marginPx - iconPx, mm2px(NEO_TITLE_CENTER_Y_MM) - iconPx / 2.f);
		}
		ModuleWidget::step();
	}

	struct XOStyleItem : MenuItem
	{
		Neo *module;
		int style;
		void onAction(const event::Action &e) override
		{
			module->OL_setOutState(STYLE_JSON, float(style));
			module->styleChanged = true;
		}
		void step() override
		{
			if (module)
				rightText = (module->OL_state[STYLE_JSON] == style) ? "✔" : "";
		}
	};

	// How many of the 16 row-slots are actually shown at once (NEO_ROWS_MIN..NEO_ROWS_MAX) -
	// named "Grid Rows" specifically to avoid colliding with NeoRowsItem's own "Rows" menu below
	// (per-row channel assignment - a completely different setting).
	struct NeoGridRowsItem : MenuItem
	{
		Neo *module;

		struct NeoGridRowsCountItem : MenuItem
		{
			Neo *module;
			int count;
			void onAction(const event::Action &e) override
			{
				// No validation needed (2026-07-22, Dieter's own KISS simplification) - NEO no
				// longer guarantees any minimum visible column count (see NEO_DEFAULT_WIDTH_HP's
				// own comment, Neo.hpp), so reducing Grid Rows can no longer "retroactively"
				// violate anything - however many columns the current width produces at the new
				// row count is simply accepted, same as everywhere else in this file.
				module->OL_setOutState(ROWS_DISPLAYED_JSON, float(count));
			}
			void step() override
			{
				if (module)
					rightText = ((int) module->OL_state[ROWS_DISPLAYED_JSON] == count) ? "✔" : "";
			}
		};

		Menu *createChildMenu() override
		{
			Menu *menu = new Menu;
			for (int count = NEO_ROWS_MIN; count <= NEO_ROWS_MAX; count++)
			{
				NeoGridRowsCountItem *item = new NeoGridRowsCountItem();
				item->module = module;
				item->count = count;
				item->text = string::f("%d", count);
				item->setSize(Vec(50, 20));
				menu->addChild(item);
			}
			return menu;
		}
	};

	// NeoFullHeightItem removed 2026-07-22 (Dieter's own instruction) - "Full Height" moved from
	// this right-click menu item to a global-area button (see NeoWidget's own fullHeightButton,
	// and Neo::toggleFullHeight() for the same logic this item's own onAction() used to run).

	// Per-row "Cell Type" submenu - two-level, "Rows" -> "Row N" -> the cell-type choices, same
	// setSize()-required pattern as CLAUDE.md's documented "Channels" submenu convention. Used to
	// also hold a "which channel" choice here (NeoRowChannelItem) - removed 2026-07-20, replaced
	// entirely by the new on-panel ROW_CHANNEL_PARAM knob (a separate right-click menu for the
	// exact same setting would be redundant and could disagree with the knob).
	struct NeoRowsItem : MenuItem
	{
		Neo *module;

		struct NeoRowItem : MenuItem
		{
			Neo *module;
			int row;

			struct NeoRowCellTypeItem : MenuItem
			{
				Neo *module;
				int row;
				int cellType;
				void onAction(const event::Action &e) override
				{
					module->OL_setOutState(ROW_CELLTYPE_JSON + row, float(cellType));
				}
				void step() override
				{
					if (module)
						rightText = (module->OL_state[ROW_CELLTYPE_JSON + row] == cellType) ? "✔" : "";
				}
			};

			Menu *createChildMenu() override
			{
				Menu *menu = new Menu;

				MenuLabel *typeLabel = new MenuLabel();
				typeLabel->text = "Cell Type";
				menu->addChild(typeLabel);
				const char *typeNames[3] = { "Gate", "Value", "Morpheus" };
				for (int t = 0; t < 3; t++)
				{
					NeoRowCellTypeItem *item = new NeoRowCellTypeItem();
					item->module = module;
					item->row = row;
					item->cellType = t;
					item->text = typeNames[t];
					item->setSize(Vec(90, 20));
					menu->addChild(item);
				}
				return menu;
			}
		};

		Menu *createChildMenu() override
		{
			Menu *menu = new Menu;
			for (int r = 0; r < NEO_NUM_ROWS; r++)
			{
				NeoRowItem *item = new NeoRowItem();
				item->module = module;
				item->row = r;
				item->text = string::f("Row %d", r + 1);
				item->rightText = RIGHT_ARROW;
				item->setSize(Vec(90, 20));
				menu->addChild(item);
			}
			return menu;
		}
	};

	// Non-adjacent Connect/Disconnect - lists every module in the patch implementing
	// Non-adjacent connection is auto-remembered (see moduleProcess()'s own comment) - no manual
	// "Connect" selection needed, just a way to explicitly forget the remembered target and fall
	// back to pure physical adjacency again.
	struct NeoDisconnectItem : MenuItem
	{
		Neo *module;
		void onAction(const event::Action &e) override
		{
			module->disconnectNeoHost();
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		Neo *module = dynamic_cast<Neo *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		XOStyleItem *style1Item = new XOStyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		XOStyleItem *style2Item = new XOStyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		XOStyleItem *style3Item = new XOStyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		NeoGridRowsItem *gridRowsItem = new NeoGridRowsItem();
		gridRowsItem->module = module;
		gridRowsItem->text = "Grid Rows";
		gridRowsItem->rightText = RIGHT_ARROW;
		menu->addChild(gridRowsItem);
		// "Full Height" menu item removed 2026-07-22 - now a global-area button (fullHeightButton).

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		NeoRowsItem *rowsItem = new NeoRowsItem();
		rowsItem->module = module;
		rowsItem->text = "Rows";
		rowsItem->rightText = RIGHT_ARROW;
		menu->addChild(rowsItem);

		if (module->neoConnectedHostId >= 0)
		{
			spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			NeoDisconnectItem *disconnectItem = new NeoDisconnectItem();
			disconnectItem->module = module;
			disconnectItem->text = "Disconnect";
			menu->addChild(disconnectItem);
		}
	}
};

Model *modelNeo = createModel<Neo, NeoWidget>("Neo");
