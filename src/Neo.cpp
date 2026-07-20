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

	dsp::SchmittTrigger memTapeTrigger[NEO_NUM_ROWS];
	dsp::SchmittTrigger followTrigger[NEO_NUM_ROWS];
	dsp::SchmittTrigger leftTrigger[NEO_NUM_ROWS];
	dsp::SchmittTrigger rightTrigger[NEO_NUM_ROWS];

	// Persistent per-instance label buffers - setJsonLabel() stores the raw char* handed to it
	// rather than copying the string, so a temporary std::string's c_str() would dangle the
	// instant the temporary is destroyed (see CLAUDE.md-adjacent lesson from XR8/XR16 today).
	char rowChannelLabelBuf[NEO_NUM_ROWS][20];
	char rowMemTapeLabelBuf[NEO_NUM_ROWS][20];
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
		underlying Morpheus CHANNEL, not of whichever NEO row happens to be looking at it right
		now (confirmed 2026-07-20: a channel shouldn't have an ambiguous identity depending on
		which row/instance is viewing it, and this data only ever changes at sequencer-setup time,
		never while running - both point at the same place, not per-row/per-instance state).
		Stored on the HOST itself (shared automatically across every NEO instance attached to it)
		via the generic, opaque, slug-keyed storage every Host offers for free
		(ExpanderBridgeInterface::writeExpanderData()/readExpanderData(), see ExpanderBridge.hpp) -
		Morpheus (or any future NeoHostInterface implementer) never parses or understands any of
		this, it's just a string NEO itself reads/writes under its own slug ("Neo").

		Schema (NEO's own, living, private contract - update this comment whenever a field is
		added, this IS the documentation):

		{
		  "channels": [
		    { "name": "Kick", "color": 16711680, "cellType": "gate",  "cellConfig": {} },
		    { "name": "Lead", "color": 65280,    "cellType": "value", "cellConfig": { "default": 0.0, "min": -10, "max": 10 } },
		    ...
		  ]
		}

		One entry per POLY_CHANNELS (16) - array index IS the channel index, no separate "channel"
		key needed. "cellType"/"cellConfig" are RESERVED for the abstract cell-editor system
		(2026-07-20 design discussion, still unspecified beyond this placeholder) - not read or
		written by any code yet, only "name" and "color" are live today. Every read/write below
		treats an entry as a whole JSON object and only ever touches the ONE key it cares about,
		so a future version that adds cellType/cellConfig will never clobber name/color written by
		this version, and vice versa.
	*/
	std::string channelName[POLY_CHANNELS];
	int channelColor[POLY_CHANNELS] = {};
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
		// and "this channel's own entry doesn't exist/doesn't have this field yet" uniformly,
		// without needing a separate has-value check at every read site elsewhere.
		for (int c = 0; c < POLY_CHANNELS; c++)
		{
			channelName[c] = "";
			channelColor[c] = 0xff6600; // ORANGE, matches the old CHANNEL_COLOR_JSON default
		}

		std::string raw = readHostData(host);
		if (raw.empty())
			return;
		json_error_t error;
		json_t *rootJ = json_loads(raw.c_str(), 0, &error);
		if (!rootJ)
			return;
		json_t *channelsJ = json_object_get(rootJ, "channels");
		if (channelsJ && json_is_array(channelsJ))
		{
			size_t index;
			json_t *entryJ;
			json_array_foreach(channelsJ, index, entryJ)
			{
				if (index >= (size_t) POLY_CHANNELS)
					break;
				json_t *nameJ = json_object_get(entryJ, "name");
				if (nameJ && json_is_string(nameJ))
					channelName[index] = json_string_value(nameJ);
				json_t *colorJ = json_object_get(entryJ, "color");
				if (colorJ && json_is_integer(colorJ))
					channelColor[index] = (int) json_integer_value(colorJ);
			}
		}
		json_decref(rootJ);
	}

	// Shared read-modify-write for a single channel/single field - reads the CURRENT full blob
	// (not our own local cache, which may be stale relative to another instance's own more recent
	// write), mutates only the one key requested on the one entry requested, and writes the whole
	// blob back. Every other key on every entry (including ones this code doesn't know about,
	// e.g. a future cellType/cellConfig) round-trips completely untouched, since entries are
	// mutated in place rather than rebuilt from scratch. Takes ownership of `value`.
	void writeChannelField(int channel, const char *key, json_t *value)
	{
		ExpanderBridgeInterface *host = neoHostBridge();
		if (!host || channel < 0 || channel >= POLY_CHANNELS)
		{
			json_decref(value);
			return;
		}

		std::string raw = readHostData(host);
		json_error_t error;
		json_t *rootJ = raw.empty() ? nullptr : json_loads(raw.c_str(), 0, &error);
		if (!rootJ)
			rootJ = json_object();
		json_t *channelsJ = json_object_get(rootJ, "channels");
		if (!channelsJ || !json_is_array(channelsJ))
		{
			channelsJ = json_array();
			json_object_set_new(rootJ, "channels", channelsJ);
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

	void setChannelName(int channel, const std::string &name) { writeChannelField(channel, "name", json_string(name.c_str())); }
	void setChannelColor(int channel, int packedColor) { writeChannelField(channel, "color", json_integer(packedColor)); }

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

		Schema addition (same top-level object "channels" lives in, see its own comment above):
		{
		  "channels": [...],
		  "lock": {
		    "ids": [123, 456],   // module ids of every instance currently locked in - each
		                         // instance adds itself on lock, removes itself on unlock/removal
		    "rows": 6,
		    "fullHeight": true,
		    "widthHp": 32
		  }
		}
	*/
	struct NeoLockData
	{
		std::vector<int64_t> ids;
		int rows = NEO_ROWS_DEFAULT;
		bool fullHeight = false;
		int widthHp = NEO_DEFAULT_WIDTH_HP;
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
	float neoLockLastSyncedWidthHp = -1.f;

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

	// Right-click-free - a real panel widget (NeoLockButton) toggles this. Handles both "I'm the
	// first to lock in" (my own current config becomes the group's) and "joining an existing
	// group" (adopt its rows/fullHeight immediately; width converges gradually via the per-tick
	// sync in NeoWidget::step(), so a blocked instance still locks in successfully at whatever
	// width it can currently manage rather than failing the lock outright).
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
			neoLockLastSyncedWidthHp = OL_state[PANEL_WIDTH_HP_JSON]; // current, NOT data.widthHp -
			                                                          // the per-tick sync's own
			                                                          // convergence check picks up
			                                                          // the gap to data.widthHp
			                                                          // from here if not yet equal
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
			neoLockLastSyncedWidthHp = -1.f;
		}
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

	// How much width is spent before the step-column grid begins right now - NEO_CONTROLS_WIDTH_MM
	// plus Full Height's own reserved resize-handle strip when that's active.
	float getControlsWidthMm()
	{
		return neoRowAreaControlsWidthMm(OL_state[FULL_HEIGHT_JSON] > 0.5f);
	}

	// How many step-columns currently fit, given the panel's own current width - "however many
	// fit," deliberately simple (Dieter's own "kiss" instruction from the spec conversation).
	int getVisibleColumns()
	{
		float widthMm = OL_state[PANEL_WIDTH_HP_JSON] * 5.08f;
		int cols = (int) ((widthMm - getControlsWidthMm()) / getColumnPitchMm());
		return std::max(1, cols);
	}

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
		// CONNECTED_HOST_ID_JSON is gone - neoConnectedHostId is a real int64_t now, persisted
		// via this module's own moduleExtraDataToJson/FromJson instead (see its own comment).
		for (int r = 0; r < NEO_NUM_ROWS; r++)
		{
			snprintf(rowChannelLabelBuf[r], sizeof(rowChannelLabelBuf[r]), "rowChannel%d", r);
			setJsonLabel(ROW_CHANNEL_JSON + r, rowChannelLabelBuf[r]);
			snprintf(rowMemTapeLabelBuf[r], sizeof(rowMemTapeLabelBuf[r]), "rowMemTape%d", r);
			setJsonLabel(ROW_MEMTAPE_JSON + r, rowMemTapeLabelBuf[r]);
			snprintf(rowFollowLabelBuf[r], sizeof(rowFollowLabelBuf[r]), "rowFollow%d", r);
			setJsonLabel(ROW_FOLLOW_JSON + r, rowFollowLabelBuf[r]);
			snprintf(rowPageLabelBuf[r], sizeof(rowPageLabelBuf[r]), "rowPage%d", r);
			setJsonLabel(ROW_PAGE_JSON + r, rowPageLabelBuf[r]);
			snprintf(rowCellTypeLabelBuf[r], sizeof(rowCellTypeLabelBuf[r]), "rowCellType%d", r);
			setJsonLabel(ROW_CELLTYPE_JSON + r, rowCellTypeLabelBuf[r]);
		}

#pragma GCC diagnostic pop
	}

	inline void moduleParamConfig()
	{
		for (int r = 0; r < NEO_NUM_ROWS; r++)
		{
			configParam(ROW_MEMTAPE_PARAM + r, 0.f, 1.f, 0.f, string::f("Row %d Mem/Tape", r + 1));
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
		neoLockLastSyncedWidthHp = -1.f;
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
		disconnectNeoHost();
		for (int r = 0; r < NEO_NUM_ROWS; r++)
		{
			OL_state[ROW_CHANNEL_JSON + r] = (float) r; // row r shows channel r by default
			OL_state[ROW_MEMTAPE_JSON + r] = 0.f;       // TAPE
			OL_state[ROW_FOLLOW_JSON + r] = 1.f;        // auto-follow the play cursor
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

		for (int r = 0; r < NEO_NUM_ROWS; r++)
		{
			if (memTapeTrigger[r].process(params[ROW_MEMTAPE_PARAM + r].getValue()))
				OL_state[ROW_MEMTAPE_JSON + r] = (OL_state[ROW_MEMTAPE_JSON + r] > 0.5f) ? 0.f : 1.f;
			if (followTrigger[r].process(params[ROW_FOLLOW_PARAM + r].getValue()))
				OL_state[ROW_FOLLOW_JSON + r] = (OL_state[ROW_FOLLOW_JSON + r] > 0.5f) ? 0.f : 1.f;

			bool follow = OL_state[ROW_FOLLOW_JSON + r] > 0.5f;
			if (neoHost)
			{
				int channel = clamp((int) OL_state[ROW_CHANNEL_JSON + r], 0, POLY_CHANNELS - 1);
				if (follow)
				{
					int cursor = neoHost->getPlayCursor(channel);
					OL_state[ROW_PAGE_JSON + r] = (float) (cursor / visibleCols);
				}
				else
				{
					int loopLen = std::max(1, neoHost->getLoopLen(channel));
					int maxPage = (loopLen - 1) / visibleCols;
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

	// box.size/pos are set fresh every NeoWidget::step() (mode-dependent - see its own comment),
	// so no initial value is needed here beyond a harmless placeholder before the first step().
	NeoResizeHandle() {}

	void onDragStart(const DragStartEvent &e) override
	{
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		dragPos = APP->scene->rack->getMousePos();
		ModuleWidget *mw = getAncestorOfType<ModuleWidget>();
		assert(mw);
		originalBox = mw->box;
	}

	void onDragMove(const DragMoveEvent &e) override
	{
		ModuleWidget *mw = getAncestorOfType<ModuleWidget>();
		assert(mw);

		Vec newDragPos = APP->scene->rack->getMousePos();
		float deltaX = newDragPos.x - dragPos.x;

		Rect newBox = originalBox;
		Rect oldBox = mw->box;
		float columnPitchMm = module ? module->getColumnPitchMm() : neoColumnPitchMm(false, NEO_ROWS_DEFAULT);
		float controlsWidthMm = module ? module->getControlsWidthMm() : neoRowAreaControlsWidthMm(false);
		float minWidth = neoMinWidthHp(columnPitchMm, controlsWidthMm) * RACK_GRID_WIDTH;
		float maxWidth = neoMaxWidthHp(module ? module->neoHost : nullptr, columnPitchMm, controlsWidthMm) * RACK_GRID_WIDTH;
		newBox.size.x += deltaX;
		newBox.size.x = clamp(newBox.size.x, minWidth, maxWidth);
		newBox.size.x = std::round(newBox.size.x / RACK_GRID_WIDTH) * RACK_GRID_WIDTH;

		mw->box = newBox;
		if (!APP->scene->rack->requestModulePos(mw, newBox.pos))
			mw->box = oldBox;

		if (module)
		{
			float hp = std::round(mw->box.size.x / RACK_GRID_WIDTH);
			module->OL_setOutState(PANEL_WIDTH_HP_JSON, hp);
		}
	}

	void draw(const DrawArgs &args) override
	{
		bool fullHeight = module && module->OL_state[FULL_HEIGHT_JSON] > 0.5f;
		if (fullHeight)
		{
			// Full-height grip strip - same look as before, just now confined to its own
			// dedicated reserved column instead of overlapping the grid.
			for (float x = 5.f; x <= 10.f; x += 5.f)
			{
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, x + 0.5f, 5.5f);
				nvgLineTo(args.vg, x + 0.5f, box.size.y - 4.5f);
				nvgStrokeWidth(args.vg, 1.f);
				nvgStrokeColor(args.vg, nvgRGBAf(0.5f, 0.5f, 0.5f, 0.5f));
				nvgStroke(args.vg);
			}
		}
		else
		{
			// Small header-corner resize glyph - standard diagonal-lines resize affordance,
			// anchored to the icon's own bottom-right corner.
			for (float d = 0.32f; d <= 0.82f; d += 0.25f)
			{
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, box.size.x * d, box.size.y);
				nvgLineTo(args.vg, box.size.x, box.size.y * d);
				nvgStrokeWidth(args.vg, 1.f);
				nvgStrokeColor(args.vg, nvgRGBAf(0.5f, 0.5f, 0.5f, 0.6f));
				nvgStroke(args.vg);
			}
		}
	}
};

/**
	Draws (and handles single-cell drag-edit for) one row's currently-visible step cells - Gate
	(on/off square) or Value (unidirectional bar) depending on that row's own cell-type choice.
	One widget per row rather than one child widget per cell, since the visible column count
	changes with the resizable panel width - simpler to just recompute what's visible each draw()
	call than to create/destroy child widgets on every resize. Deliberately simple graphics per
	Dieter's own "function first, polish later" instruction - fancier rendering (line-vs-rect
	styles, gradients) is explicitly deferred, see the plan.
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
		return clamp((int) (x / pitchPx), 0, visibleCols - 1);
	}

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
		// gate/value content, so individual cell boundaries read clearly even at rest (Dieter's
		// own instruction, 2026-07-20, for visual support while testing).
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

		int channel = clamp((int) m->OL_state[ROW_CHANNEL_JSON + row], 0, POLY_CHANNELS - 1);
		bool mem = m->OL_state[ROW_MEMTAPE_JSON + row] > 0.5f;
		bool gate = m->OL_state[ROW_CELLTYPE_JSON + row] < 0.5f;
		int page = (int) m->OL_state[ROW_PAGE_JSON + row];
		int loopLen = m->neoHost->getLoopLen(channel);
		int colorPacked = m->channelColor[channel]; // channel-owned, Host-shared - see its own comment
		NVGcolor color = nvgRGB((colorPacked >> 16) & 0xff, (colorPacked >> 8) & 0xff, colorPacked & 0xff);

		for (int i = 0; i < visibleCols; i++)
		{
			int step = page * visibleCols + i;
			if (step >= loopLen)
				break;
			float value = mem ? m->neoHost->getMemStep(channel, step) : m->neoHost->getTapeStep(channel, step);
			float x = gapPx / 2.f + (float) i * pitchPx;

			if (gate)
			{
				bool on = value > 5.f; // plain 5V threshold on a real Host voltage - see
				                       // CLAUDE.md's pitfall on X8-style dual-convention issues,
				                       // doesn't apply here since this always reads a real Host
				                       // value directly, never a raw 0..1 knob
				if (on)
				{
					nvgBeginPath(args.vg);
					nvgRect(args.vg, x, 0.f, cellWidthPx, box.size.y);
					nvgFillColor(args.vg, color);
					nvgFill(args.vg);
				}
			}
			else
			{
				float t = clamp((value + 10.f) / 20.f, 0.f, 1.f); // -10..+10V -> 0..1
				float barHeight = t * box.size.y;
				nvgBeginPath(args.vg);
				nvgRect(args.vg, x, box.size.y - barHeight, cellWidthPx, barHeight);
				nvgFillColor(args.vg, color);
				nvgFill(args.vg);
			}
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
				dragStep = stepAtLocalX(e.pos.x, pitchPx, visibleCols);
				int channel = clamp((int) m->OL_state[ROW_CHANNEL_JSON + row], 0, POLY_CHANNELS - 1);
				bool mem = m->OL_state[ROW_MEMTAPE_JSON + row] > 0.5f;
				int page = (int) m->OL_state[ROW_PAGE_JSON + row];
				int step = page * visibleCols + dragStep;
				dragStartValue = mem ? m->neoHost->getMemStep(channel, step) : m->neoHost->getTapeStep(channel, step);
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
		int channel = clamp((int) m->OL_state[ROW_CHANNEL_JSON + row], 0, POLY_CHANNELS - 1);
		bool mem = m->OL_state[ROW_MEMTAPE_JSON + row] > 0.5f;
		int page = (int) m->OL_state[ROW_PAGE_JSON + row];
		int step = page * visibleCols + dragStep;

		// e.mouseDelta is already zoom-corrected by Rack - accumulate it directly rather than
		// re-deriving from absolute position, simplest correct approach for a continuous drag.
		float sensitivity = 20.f / box.size.y; // full row height ~= 20V of travel
		float newValue = clamp(dragStartValue - e.mouseDelta.y * sensitivity, -10.f, 10.f);
		dragStartValue = newValue;

		if (mem)
			m->neoHost->setMemStep(channel, step, newValue);
		else
			m->neoHost->setTapeStep(channel, step, newValue);
	}

	void onDragEnd(const DragEndEvent &e) override
	{
		dragStep = -1;
		TransparentWidget::onDragEnd(e);
	}
};

/**
	Plain row-number label (v1) - full channel renaming via right-click is deferred (see
	NeoChannelNameField's own comment below), so this just shows which channel the row currently
	displays. Simple direct nvgText() draw, mirroring the rest of the codebase's own small label
	widgets rather than OrangeLine.hpp's own TextWidget (a much more specialized scrolling-display
	widget tied to module-specific animation state, not a fit for a plain static/row-number label).
*/
struct NeoRowNameWidget : TransparentWidget
{
	Module *module = nullptr;
	int row = 0;

	void drawLayer(const DrawArgs &args, int layer) override
	{
		if (layer != 1)
		{
			Widget::drawLayer(args, layer);
			return;
		}
		Neo *m = module ? dynamic_cast<Neo*>(module) : nullptr;
		int channel = m ? clamp((int) m->OL_state[ROW_CHANNEL_JSON + row], 0, POLY_CHANNELS - 1) : row;

		float fontSizePx = mm2px(Vec(3.5f, 0.f)).x;
		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, fontSizePx);
		nvgFillColor(args.vg, ORANGE);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		char buffer[8];
		snprintf(buffer, sizeof(buffer), "%d", channel + 1);
		nvgText(args.vg, 0.f, box.size.y / 2.f, buffer, nullptr);
		Widget::drawLayer(args, 1);
	}
};

/**
	Frame around one row's own "header data" (name/MEM-TAPE/FOLLOW/paging) - 2026-07-20, Dieter's
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

	void draw(const DrawArgs &args) override
	{
		Neo *m = module ? dynamic_cast<Neo*>(module) : nullptr;
		float style = m ? m->OL_state[STYLE_JSON] : STYLE_ORANGE;
		bool fullHeight = m && m->OL_state[FULL_HEIGHT_JSON] > 0.5f;
		NVGcolor bg = (style == STYLE_DARK) ? X_STRIP_BG_DARK : (style == STYLE_BRIGHT) ? X_STRIP_BG_BRIGHT : X_STRIP_BG_ORANGE;
		NVGcolor frame = (style == STYLE_DARK) ? X_FRAME_DARK : (style == STYLE_BRIGHT) ? X_FRAME_BRIGHT : X_FRAME_ORANGE;
		NVGcolor text = xThemedTextColor(style);

		float marginPx = mm2px(NEO_FRAME_MARGIN_MM);
		float bgInsetPx = mm2px(NEO_BACKGROUND_INSET_MM);
		float globalAreaEdgePx = mm2px(NEO_GLOBAL_AREA_WIDTH_MM);
		float halfGapPx = mm2px(NEO_FRAME_GAP_MM) / 2.f;

		// Rack's own SvgPanel convention never paints a module's true background flush to the
		// panel's literal edges - the real panel content stays inset by this same margin, and
		// Rack's own PanelBorder/rail rendering shows through the remaining sliver as the
		// natural "seam" between two adjacent, unconnected modules. Every other OrangeLine
		// module gets this for free since its baked SvgPanel is loaded through Rack's own
		// panel-drawing path; NEO draws its own background directly and has to respect the same
		// inset by hand. addXExtStrip()/addXExtStripLeft() (below) are the ONLY things allowed
		// to paint into the LEFT/RIGHT margin, and only while actually bridgeConnected(). In
		// Full Height mode the row area additionally drops its own TOP/BOTTOM inset entirely
		// (see FULL_HEIGHT_JSON's own comment) - the global area's own inset never changes.
		nvgBeginPath(args.vg);
		nvgRect(args.vg, bgInsetPx, bgInsetPx, globalAreaEdgePx - bgInsetPx, box.size.y - bgInsetPx * 2.f);
		nvgFillColor(args.vg, bg);
		nvgFill(args.vg);

		float rowTopPx = fullHeight ? 0.f : bgInsetPx;
		float rowBottomPx = fullHeight ? box.size.y : box.size.y - bgInsetPx;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, globalAreaEdgePx, rowTopPx, box.size.x - bgInsetPx - globalAreaEdgePx, rowBottomPx - rowTopPx);
		nvgFillColor(args.vg, bg);
		nvgFill(args.vg);

		// Reproduces Rack's own PanelBorder - a stroke right at the panel's true bounds, the
		// same width as the background inset above, so it fills exactly the gap the inset
		// leaves rather than a plain transparent sliver. nvgStroke() centers a straight line on
		// its own path, so each edge is drawn inset by half the stroke width from the true
		// boundary - otherwise half the stroke bleeds past box.size and gets clipped away,
		// leaving the outer half of the intended border missing instead of reaching the true
		// edge. Left/right edges always run the panel's full height (they're unrelated to
		// vertical stacking); top/bottom edges only span the global area's own width in Full
		// Height mode, since nothing should cross the row area there at all in that mode.
		float borderHalfStrokePx = bgInsetPx / 2.f;
		float topBottomRightPx = fullHeight ? globalAreaEdgePx : box.size.x;
		auto strokeBorderEdge = [&](float x1, float y1, float x2, float y2)
		{
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, x1, y1);
			nvgLineTo(args.vg, x2, y2);
			nvgStrokeWidth(args.vg, bgInsetPx);
			nvgStrokeColor(args.vg, NEO_PANEL_BORDER_COLOR);
			nvgStroke(args.vg);
		};
		strokeBorderEdge(borderHalfStrokePx, borderHalfStrokePx, borderHalfStrokePx, box.size.y - borderHalfStrokePx); // left
		strokeBorderEdge(box.size.x - borderHalfStrokePx, borderHalfStrokePx, box.size.x - borderHalfStrokePx, box.size.y - borderHalfStrokePx); // right
		strokeBorderEdge(borderHalfStrokePx, borderHalfStrokePx, topBottomRightPx, borderHalfStrokePx); // top
		strokeBorderEdge(borderHalfStrokePx, box.size.y - borderHalfStrokePx, topBottomRightPx, box.size.y - borderHalfStrokePx); // bottom

		float topPx = mm2px(NEO_FRAME_TOP_MM);
		float bottomPx = box.size.y - mm2px(NEO_FRAME_BOTTOM_MM);
		float radiusPx = mm2px(NEO_FRAME_RADIUS_MM);

		// Two independent, fully-rounded peer frames (not a shared edge) - a fixed-width
		// "global" sidebar frame on the left (always drawn, unaffected by Full Height) and a
		// "row area" frame that resizes with the module on the right, separated by
		// PanelDesignGuide.md's own nested-frame padding value (NEO_FRAME_GAP_MM) instead of
		// touching directly. See NeoWork.svg's own file comment for the full reasoning. In Full
		// Height mode the row area's own frame is skipped entirely (Dieter's own instruction,
		// 2026-07-20) - a stacked pair needs a chrome-free rectangle there, not a frame that
		// would visibly interrupt the seam.
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, marginPx, topPx, globalAreaEdgePx - halfGapPx - marginPx, bottomPx - topPx, radiusPx);
		nvgStrokeWidth(args.vg, mm2px(NEO_FRAME_STROKE_MM));
		nvgStrokeColor(args.vg, frame);
		nvgStroke(args.vg);

		if (!fullHeight)
		{
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, globalAreaEdgePx + halfGapPx, topPx, box.size.x - marginPx - globalAreaEdgePx - halfGapPx, bottomPx - topPx, radiusPx);
			nvgStrokeWidth(args.vg, mm2px(NEO_FRAME_STROKE_MM));
			nvgStrokeColor(args.vg, frame);
			nvgStroke(args.vg);
		}

		// Title + "ORANGE LINE" wordmark/accent stripe. Normally centered on the full current
		// box.size.x, recomputed every draw() call so it stays horizontally centered through any
		// resize (a baked SVG's fixed x, correct at one width, cannot do this on its own). In
		// Full Height mode all of this retreats to span only the global area's own content
		// width instead - the row area becomes a pure chrome-free grid (see FULL_HEIGHT_JSON's
		// own comment) so nothing brand-related crosses the seam when two instances are stacked.
		float accentX1 = marginPx;
		float accentX2 = fullHeight ? (globalAreaEdgePx - halfGapPx) : (box.size.x - marginPx);
		float centerX = fullHeight ? (accentX1 + accentX2) / 2.f : box.size.x / 2.f;

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
	Small square/pill toggle button shared by all four per-row Params (MEM/TAPE, FOLLOW, LEFT,
	RIGHT) - file-scope (not a constructor-local type) so NeoWidget can hold typed arrays of
	these and reposition them every step() as the row layout changes (Grid Rows / Full Height).
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
	Lock button - global area, join/leave the Host-shared "common config" group (see the Neo
	module's own NeoLockData/toggleLock() for the full mechanism). A plain clickable square, not
	a Rack Param - locking has complex cross-instance side effects that don't fit the automatable-
	parameter model. Same flat-fill style as NeoRowButton (the row paging buttons) and X-family's
	own Bind/Free button - grey when unlocked, green when locked; a locked/unlocked icon on top of
	this same square is a later styling pass (Dieter's own instruction, 2026-07-20).
*/
struct NeoLockButton : OpaqueWidget
{
	Neo *module = nullptr;

	void draw(const DrawArgs &args) override
	{
		bool locked = module && module->OL_state[LOCKED_JSON] > 0.5f;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.f);
		nvgFillColor(args.vg, locked ? NEO_LOCK_ON_COLOR : NEO_LOCK_OFF_COLOR);
		nvgFill(args.vg);
	}

	void onButton(const event::Button &e) override
	{
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && module)
		{
			module->toggleLock();
			e.consume(this);
		}
		OpaqueWidget::onButton(e);
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
	NeoRowNameWidget *rowNames[NEO_NUM_ROWS] = {};
	NeoRowHeaderFrameWidget *rowHeaderFrames[NEO_NUM_ROWS] = {};
	NeoRowButton *memTapeBtns[NEO_NUM_ROWS] = {};
	NeoRowButton *followBtns[NEO_NUM_ROWS] = {};
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
	NeoLockButton *lockButton = nullptr;
	// Last column/controls width the auto-resize reconciliation (step(), below) actually ran
	// against - negative means "not yet initialized," so the very first step() call just adopts
	// the current values without attempting a resize (a freshly loaded/created module shouldn't
	// immediately jerk its own width around). Tracking both (not just column width) so toggling
	// Full Height still triggers a reconciliation even in the rare case its own resize-handle
	// reservation is the only thing that changed.
	float lastColumnPitchMm = -1.f;
	float lastControlsWidthMm = -1.f;

	NeoWidget(Neo *module)
	{
		setModule(module);

		float widthHp = module ? module->OL_state[PANEL_WIDTH_HP_JSON] : NEO_DEFAULT_WIDTH_HP;
		float columnPitchMmInit = module ? module->getColumnPitchMm() : neoColumnPitchMm(false, NEO_ROWS_DEFAULT);
		float controlsWidthMmInit = module ? module->getControlsWidthMm() : neoRowAreaControlsWidthMm(false);
		if (widthHp < neoMinWidthHp(columnPitchMmInit, controlsWidthMmInit))
			widthHp = NEO_DEFAULT_WIDTH_HP;
		box.size = Vec(widthHp * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		panelWidget = new NeoPanelWidget();
		panelWidget->module = module;
		panelWidget->box.size = box.size;
		addChild(panelWidget);

		extStripRight = addXExtStrip(this, widthHp * 5.08f);
		extStripLeft = addXExtStripLeft(this); // both already include X_STRIP_FRAME_NUDGE_MM (XShared.hpp)

		// Upper left corner of the global area's own frame (NEO_FRAME_MARGIN_MM left edge,
		// NEO_FRAME_TOP_MM top edge), spaced by PanelDesignGuide.md's own "Positioning controls"
		// target (NEO_LOCK_BUTTON_SPACING_MM) from both.
		lockButton = new NeoLockButton();
		lockButton->module = module;
		lockButton->box.pos = mm2px(Vec(NEO_FRAME_MARGIN_MM + NEO_LOCK_BUTTON_SPACING_MM, NEO_FRAME_TOP_MM + NEO_LOCK_BUTTON_SPACING_MM));
		lockButton->box.size = mm2px(Vec(NEO_LOCK_BUTTON_SIZE_MM, NEO_LOCK_BUTTON_SIZE_MM));
		addChild(lockButton);

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

			NeoRowNameWidget *name = new NeoRowNameWidget();
			name->module = module;
			name->row = r;
			name->box.size = mm2px(Vec(NEO_ROW_NAME_WIDTH_MM, 1.f));
			addChild(name);
			rowNames[r] = name;

			NeoRowButton *memTapeBtn = createParam<NeoRowButton>(Vec(), module, ROW_MEMTAPE_PARAM + r);
			memTapeBtn->box.size = mm2px(Vec(NEO_ROW_TOGGLE_WIDTH_MM, NEO_ROW_TOGGLE_HEIGHT_MM));
			memTapeBtn->onColor = nvgRGB(0x00, 0x99, 0xff);
			addParam(memTapeBtn);
			memTapeBtns[r] = memTapeBtn;

			NeoRowButton *followBtn = createParam<NeoRowButton>(Vec(), module, ROW_FOLLOW_PARAM + r);
			followBtn->box.size = mm2px(Vec(NEO_ROW_TOGGLE_WIDTH_MM, NEO_ROW_TOGGLE_HEIGHT_MM));
			followBtn->onColor = nvgRGB(0x00, 0xdd, 0x44);
			addParam(followBtn);
			followBtns[r] = followBtn;

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
					neoModule->neoLockLastSyncedWidthHp = -1.f;
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
			float controlsWidthMm = neoRowAreaControlsWidthMm(fullHeight);

			// Auto-adapt width to the current cell pitch - a Grid Rows/Full Height change
			// indirectly changes it too (square cells), so the panel's own width may need to
			// follow. Width, unlike height, is a WEAK constraint here - NEO controls its own
			// left edge (the global area) and can freely grow/shrink rightward, unlike height
			// which is Rack's own fixed 128.5mm no module can ever adapt (Dieter's own framing,
			// 2026-07-20) - so there's no need for height's own HP-snap-then-absorb-leftover
			// trick here; just resize to fit exactly. Never leaves a clipped or dead-space
			// column: grow rightward to preserve the previously-visible column count if cells
			// got wider (only if free space allows - requestModulePos() is Rack's own collision
			// test, used exactly the same way NeoResizeHandle::onDragMove() already uses it for
			// manual drags); if cells got narrower, leave the width alone and let more columns
			// show, UNLESS that would exceed the Host's own max channel length, in which case
			// shrink away the resulting dead space instead (Dieter's own spec, 2026-07-20).
			if (lastColumnPitchMm < 0.f)
			{
				lastColumnPitchMm = columnPitchMm; // first tick - adopt without resizing
				lastControlsWidthMm = controlsWidthMm;
			}
			else if (columnPitchMm != lastColumnPitchMm || controlsWidthMm != lastControlsWidthMm)
			{
				float currentWidthMm = neoModule->OL_state[PANEL_WIDTH_HP_JSON] * 5.08f;
				int maxLoopLen = neoModule->neoHost ? neoModule->neoHost->getMaxLoopLen() : 100000;
				int targetCols;
				if (columnPitchMm > lastColumnPitchMm || controlsWidthMm > lastControlsWidthMm)
				{
					// cells grew, or the resize-handle reservation just grew (Full Height turned
					// on) - try to preserve the column count that was visible before, so nothing
					// already on screen just disappears.
					targetCols = std::max(1, (int) ((currentWidthMm - lastControlsWidthMm) / lastColumnPitchMm));
				}
				else
				{
					// cells shrank (and the reservation didn't grow) - let the page size grow to
					// fill the same width, capped at however many steps the Host could ever have.
					targetCols = std::max(1, (int) ((currentWidthMm - controlsWidthMm) / columnPitchMm));
				}
				targetCols = clamp(targetCols, NEO_MIN_VISIBLE_COLS, maxLoopLen);

				float desiredWidthMm = controlsWidthMm + (float) targetCols * columnPitchMm;
				float desiredWidthPx = std::round(mm2px(desiredWidthMm) / RACK_GRID_WIDTH) * RACK_GRID_WIDTH;
				float minWidthPx = neoMinWidthHp(columnPitchMm, controlsWidthMm) * RACK_GRID_WIDTH;
				float maxWidthPx = neoMaxWidthHp(neoModule->neoHost, columnPitchMm, controlsWidthMm) * RACK_GRID_WIDTH;
				desiredWidthPx = clamp(desiredWidthPx, minWidthPx, maxWidthPx);

				if (desiredWidthPx != box.size.x)
				{
					Rect oldBox = box;
					Rect newBox = box;
					newBox.size.x = desiredWidthPx;
					box = newBox;
					if (APP->scene->rack->requestModulePos(this, newBox.pos))
						neoModule->OL_setOutState(PANEL_WIDTH_HP_JSON, std::round(box.size.x / RACK_GRID_WIDTH));
					else
						box = oldBox; // blocked by a neighbor - keep current width, columns just show as-is
				}
				lastColumnPitchMm = columnPitchMm;
				lastControlsWidthMm = controlsWidthMm;
			}

			// Lock sync (width half) - runs after the cell-size-driven auto-resize above has
			// already settled for this tick, so it converges toward the GROUP's target width
			// rather than fighting the column-preserving resize that may have just happened.
			// Width, unlike rows, is "weak" (Dieter's own framing) - a locked instance always
			// keeps its own adopted row count, but only matches the common width as far as free
			// space allows, remembering the target and retrying every tick so it grows (or
			// shrinks) into it the moment room frees up, rather than failing the lock outright.
			if (locked && lockDataValid)
			{
				int myWidthHp = (int) std::round(neoModule->OL_state[PANEL_WIDTH_HP_JSON]);
				bool lockNeedsWrite = false;
				if (myWidthHp != (int) std::round(neoModule->neoLockLastSyncedWidthHp))
				{
					// My own physical width changed since I last confirmed it - either a manual
					// drag, or the row-count reconciliation above just moved me as a side effect
					// of adopting a new row count. Either way this IS my new confirmed state;
					// propagate it as the group's target if it doesn't already match.
					if (myWidthHp != lockData.widthHp)
					{
						lockData.widthHp = myWidthHp;
						lockNeedsWrite = true;
					}
					neoModule->neoLockLastSyncedWidthHp = (float) myWidthHp;
				}
				if (myWidthHp != lockData.widthHp)
				{
					// Still (or newly) diverged from the group's target - try to converge,
					// exactly the same collision-checked resize NeoResizeHandle::onDragMove()
					// and the cell-size reconciliation above already use.
					float minWidthPx = neoMinWidthHp(columnPitchMm, controlsWidthMm) * RACK_GRID_WIDTH;
					float maxWidthPx = neoMaxWidthHp(neoModule->neoHost, columnPitchMm, controlsWidthMm) * RACK_GRID_WIDTH;
					float targetWidthPx = clamp((float) lockData.widthHp * RACK_GRID_WIDTH, minWidthPx, maxWidthPx);
					if (targetWidthPx != box.size.x)
					{
						Rect oldBox = box;
						Rect newBox = box;
						newBox.size.x = targetWidthPx;
						box = newBox;
						if (APP->scene->rack->requestModulePos(this, newBox.pos))
						{
							int newHp = (int) std::round(box.size.x / RACK_GRID_WIDTH);
							neoModule->OL_setOutState(PANEL_WIDTH_HP_JSON, (float) newHp);
							neoModule->neoLockLastSyncedWidthHp = (float) newHp;
						}
						else
							box = oldBox; // still blocked - keep current width, retry next tick
					}
				}
				if (lockNeedsWrite)
					neoModule->writeLockData(lockData);
			}

			float widthMm = neoModule->OL_state[PANEL_WIDTH_HP_JSON] * 5.08f; // may have just changed above
			box.size.x = mm2px(widthMm);
			float cellsWidthMm = std::max(1.f, widthMm - controlsWidthMm);

			for (int r = 0; r < NEO_NUM_ROWS; r++)
			{
				bool rowVisible = r < rowsDisplayed;
				rowHeaderFrames[r]->visible = rowVisible;
				rowNames[r]->visible = rowVisible;
				memTapeBtns[r]->visible = rowVisible;
				followBtns[r]->visible = rowVisible;
				leftBtns[r]->visible = rowVisible;
				rightBtns[r]->visible = rowVisible;
				rowCells[r]->visible = rowVisible;
				if (!rowVisible)
					continue;

				float y = firstRowYMm + (float) r * rowPitchMm;
				float centerY = y + cellHeightMm / 2.f;

				// Header-data frame: left edge matches the row area's own frame left edge
				// (NEO_FRAME_GAP_MM/2 in from the global area boundary) plus the same cell
				// padding (half the current row gap) the step-cells themselves use; right edge
				// approximates the same on the other side with NEO_FRAME_GAP_MM (see its own
				// comment, Neo.hpp, for why that side isn't exact) - Dieter's own spec, 2026-07-20.
				float rowGapMm = rowPitchMm - cellHeightMm;
				float headerFrameLeftMm = NEO_GLOBAL_AREA_WIDTH_MM + NEO_FRAME_GAP_MM / 2.f + rowGapMm / 2.f;
				float headerFrameRightMm = NEO_CONTROLS_WIDTH_MM - NEO_FRAME_GAP_MM;
				rowHeaderFrames[r]->box.pos = calculateCoordinates(headerFrameLeftMm, y, 0.f);
				rowHeaderFrames[r]->box.size = mm2px(Vec(std::max(1.f, headerFrameRightMm - headerFrameLeftMm), cellHeightMm));

				rowNames[r]->box.pos = calculateCoordinates(NEO_ROW_NAME_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, y, 0.f);
				rowNames[r]->box.size = mm2px(Vec(NEO_ROW_NAME_WIDTH_MM, cellHeightMm));

				memTapeBtns[r]->box.pos = calculateCoordinates(NEO_ROW_MEMTAPE_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(memTapeBtns[r]->box.size.div(2.f));
				followBtns[r]->box.pos = calculateCoordinates(NEO_ROW_FOLLOW_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(followBtns[r]->box.size.div(2.f));
				leftBtns[r]->box.pos = calculateCoordinates(NEO_ROW_LEFT_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(leftBtns[r]->box.size.div(2.f));
				rightBtns[r]->box.pos = calculateCoordinates(NEO_ROW_RIGHT_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(rightBtns[r]->box.size.div(2.f));

				rowCells[r]->box.pos = calculateCoordinates(NEO_CONTROLS_WIDTH_MM, y, 0.f);
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
			// Normal mode: a small icon confined to the header band, right upper corner,
			// vertically centered on the same y the title text uses - entirely above the grid
			// content, so it needs no reserved width. Full Height: the dedicated full-height 1HP
			// strip at the right edge that neoRowAreaControlsWidthMm() already reserved room for
			// in the grid's own width above (Dieter's own instruction, 2026-07-20).
			bool fullHeightForHandle = neoModule && neoModule->OL_state[FULL_HEIGHT_JSON] > 0.5f;
			if (fullHeightForHandle)
			{
				resizeHandle->box.size = Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
				resizeHandle->box.pos = Vec(box.size.x - resizeHandle->box.size.x, 0.f);
			}
			else
			{
				float iconPx = mm2px(NEO_RESIZE_ICON_SIZE_MM);
				float marginPx = mm2px(NEO_FRAME_MARGIN_MM);
				resizeHandle->box.size = Vec(iconPx, iconPx);
				resizeHandle->box.pos = Vec(box.size.x - marginPx - iconPx, mm2px(NEO_TITLE_CENTER_Y_MM) - iconPx / 2.f);
			}
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

	// See FULL_HEIGHT_JSON's own comment (Neo.hpp) for what this actually does to the layout.
	struct NeoFullHeightItem : MenuItem
	{
		Neo *module;
		void onAction(const event::Action &e) override
		{
			module->OL_setOutState(FULL_HEIGHT_JSON, module->OL_state[FULL_HEIGHT_JSON] > 0.5f ? 0.f : 1.f);
		}
		void step() override
		{
			if (module)
				rightText = (module->OL_state[FULL_HEIGHT_JSON] > 0.5f) ? "✔" : "";
		}
	};

	// Per-row "which channel" submenu - two-level, "Rows" -> "Row N" -> the 16 channel choices,
	// same setSize()-required pattern as CLAUDE.md's documented "Channels" submenu convention.
	struct NeoRowsItem : MenuItem
	{
		Neo *module;

		struct NeoRowItem : MenuItem
		{
			Neo *module;
			int row;

			struct NeoRowChannelItem : MenuItem
			{
				Neo *module;
				int row;
				int channel;
				void onAction(const event::Action &e) override
				{
					module->OL_setOutState(ROW_CHANNEL_JSON + row, float(channel));
				}
				void step() override
				{
					if (module)
						rightText = (module->OL_state[ROW_CHANNEL_JSON + row] == channel) ? "✔" : "";
				}
			};

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

				MenuLabel *channelLabel = new MenuLabel();
				channelLabel->text = "Channel";
				menu->addChild(channelLabel);
				for (int c = 0; c < POLY_CHANNELS; c++)
				{
					NeoRowChannelItem *item = new NeoRowChannelItem();
					item->module = module;
					item->row = row;
					item->channel = c;
					item->text = string::f("Channel %d", c + 1);
					item->setSize(Vec(90, 20));
					menu->addChild(item);
				}

				MenuLabel *typeLabel = new MenuLabel();
				typeLabel->text = "Cell Type";
				menu->addChild(typeLabel);
				const char *typeNames[2] = { "Gate", "Value" };
				for (int t = 0; t < 2; t++)
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

	// Per-channel name/color submenu - name and color belong to the CHANNEL, not the row
	// (confirmed explicitly, they travel with the channel wherever it's reassigned).
	struct NeoChannelsItem : MenuItem
	{
		Neo *module;

		struct NeoChannelNameField : ui::TextField
		{
			Neo *module;
			int channel;
			void onChange(const ChangeEvent &e) override
			{
				TextField::onChange(e);
				if (module)
					module->setChannelName(channel, text);
			}
		};

		struct NeoChannelColorItem : MenuItem
		{
			Neo *module;
			int channel;
			int color;
			void onAction(const event::Action &e) override
			{
				module->setChannelColor(channel, color);
			}
			void step() override
			{
				if (module)
					rightText = (module->channelColor[channel] == color) ? "✔" : "";
			}
		};

		struct NeoChannelItem : MenuItem
		{
			Neo *module;
			int channel;

			Menu *createChildMenu() override
			{
				Menu *menu = new Menu;

				NeoChannelNameField *nameField = new NeoChannelNameField();
				nameField->module = module;
				nameField->channel = channel;
				nameField->text = module->channelName[channel];
				nameField->box.size = Vec(140.f, 20.f);
				menu->addChild(nameField);

				MenuLabel *colorSpacer = new MenuLabel();
				menu->addChild(colorSpacer);

				// Simple preset-swatch grid for v1 (per the plan - "a simple preset-swatch grid
				// submenu is enough for v1, a full custom color wheel is not necessary yet").
				static const int swatches[8] = {
					0xff6600, 0xff0000, 0x00cc44, 0x00aaff,
					0xffcc00, 0xcc00ff, 0xffffff, 0x888888
				};
				static const char *swatchNames[8] = {
					"Orange", "Red", "Green", "Blue", "Yellow", "Purple", "White", "Grey"
				};
				for (int i = 0; i < 8; i++)
				{
					NeoChannelColorItem *item = new NeoChannelColorItem();
					item->module = module;
					item->channel = channel;
					item->color = swatches[i];
					item->text = swatchNames[i];
					item->setSize(Vec(70, 20));
					menu->addChild(item);
				}
				return menu;
			}
		};

		Menu *createChildMenu() override
		{
			Menu *menu = new Menu;
			for (int c = 0; c < POLY_CHANNELS; c++)
			{
				NeoChannelItem *item = new NeoChannelItem();
				item->module = module;
				item->channel = c;
				item->text = string::f("Channel %d Color", c + 1);
				item->rightText = RIGHT_ARROW;
				item->setSize(Vec(110, 20));
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

		NeoFullHeightItem *fullHeightItem = new NeoFullHeightItem();
		fullHeightItem->module = module;
		fullHeightItem->text = "Full Height";
		menu->addChild(fullHeightItem);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		NeoRowsItem *rowsItem = new NeoRowsItem();
		rowsItem->module = module;
		rowsItem->text = "Rows";
		rowsItem->rightText = RIGHT_ARROW;
		menu->addChild(rowsItem);

		NeoChannelsItem *channelsItem = new NeoChannelsItem();
		channelsItem->module = module;
		channelsItem->text = "Channels";
		channelsItem->rightText = RIGHT_ARROW;
		menu->addChild(channelsItem);

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
