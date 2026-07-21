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

	dsp::SchmittTrigger followTrigger[NEO_NUM_ROWS];
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

	// Right-click-free - a real panel widget (the LOCK/FREE button, OLLabelButton) toggles this. Handles both "I'm the
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

	// The row header's own ACTUAL current width - see NEO_ROW_HEADER_TARGET_WIDTH_MM's own
	// comment (Neo.hpp) for the full drag-lifecycle mechanism that can grow it past its default.
	float getRowHeaderWidthMm()
	{
		float w = OL_state[ROW_HEADER_WIDTH_MM_JSON];
		return w > 0.f ? w : NEO_ROW_HEADER_TARGET_WIDTH_MM; // 0 = never set (e.g. a very old save)
	}

	// THE one place a row's own current channel/track selection is read - every call site
	// (moduleProcess(), NeoRowCellsWidget, NeoRowNameWidget, the new track/channel displays)
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

	// How much width is spent before the step-column grid begins right now - the global area,
	// the row header's own current width, plus Full Height's own reserved resize-handle strip
	// when that's active.
	float getControlsWidthMm()
	{
		return neoRowAreaControlsWidthMm(OL_state[FULL_HEIGHT_JSON] > 0.5f, getRowHeaderWidthMm());
	}

	// How many step-columns currently fit, given the panel's own current width - "however many
	// fit," deliberately simple (Dieter's own "kiss" instruction from the spec conversation).
	// Goes through the one shared neoColumnFit() (Neo.hpp) - see its own comment for why this
	// must never be reimplemented inline again at any call site.
	int getVisibleColumns()
	{
		float widthMm = OL_state[PANEL_WIDTH_HP_JSON] * 5.08f;
		int visibleCols; float leftoverMm;
		neoColumnFit(widthMm, getControlsWidthMm(), getColumnPitchMm(), visibleCols, leftoverMm);
		return visibleCols;
	}

	// Grows the row header to absorb whatever leftover space the floor-based column count can't
	// use, instead of leaving it as a dead gap after the last visible column (Dieter's own spec,
	// 2026-07-20). Uses the header's own CURRENT width as the base (not a hardcoded target), so
	// it's idempotent - calling it again once already settled (leftover 0) is a harmless no-op,
	// and it stays correct even if some earlier step didn't manage to reset the header to Tw
	// first. Called both from NeoResizeHandle::onDragEnd() (the instance actually being dragged)
	// and from the Lock group's own width-convergence success (NeoWidget::step()) - a locked
	// instance that only followed via the lock sync, never dragged directly, still needs this
	// same settling step once ITS OWN width actually lands somewhere new. Goes through the same
	// shared neoColumnFit() getVisibleColumns() does, so the column count/leftover this settles
	// on can never disagree with what was actually shown live during the drag that led here.
	void absorbLeftoverIntoHeader()
	{
		bool fullHeight = OL_state[FULL_HEIGHT_JSON] > 0.5f;
		float widthMm = OL_state[PANEL_WIDTH_HP_JSON] * 5.08f;
		float headerWidthMm = getRowHeaderWidthMm();
		float controlsWidthMm = neoRowAreaControlsWidthMm(fullHeight, headerWidthMm);
		int visibleCols; float leftoverMm;
		neoColumnFit(widthMm, controlsWidthMm, getColumnPitchMm(), visibleCols, leftoverMm);
		OL_setOutState(ROW_HEADER_WIDTH_MM_JSON, headerWidthMm + leftoverMm);
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
		OL_state[ROW_HEADER_WIDTH_MM_JSON] = NEO_ROW_HEADER_TARGET_WIDTH_MM;
		// NEO_DEFAULT_WIDTH_HP is whole-HP-rounded UP from the exact minimum, so it always
		// leaves a small leftover past Tw - absorb it now (same settling pass onDragEnd() uses)
		// so a freshly-placed module starts already correctly padded instead of showing that
		// leftover as a visible gap on the right until the first resize drag (Dieter's own
		// catch, 2026-07-20 - "the header size has to be adapted to show a correctly padded 4
		// column grid at startup, this does not happen").
		absorbLeftoverIntoHeader();
		disconnectNeoHost();
		for (int r = 0; r < NEO_NUM_ROWS; r++)
		{
			// Row r's default track/channel (row r shows channel r, track TAPE) come from
			// ROW_TRACK_PARAM/ROW_CHANNEL_PARAM's own configParam() defaults now, not OL_state.
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
			if (followTrigger[r].process(params[ROW_FOLLOW_PARAM + r].getValue()))
				OL_state[ROW_FOLLOW_JSON + r] = (OL_state[ROW_FOLLOW_JSON + r] > 0.5f) ? 0.f : 1.f;

			bool follow = OL_state[ROW_FOLLOW_JSON + r] > 0.5f;
			if (neoHost)
			{
				int channel = getRowChannel(r);
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

		// Snap the row header back to exactly its target width (Tw) - a flat state reset, and
		// nothing else. The module's own width is deliberately left untouched (Dieter's own
		// correction, 2026-07-20, after an earlier, much more complicated version of this also
		// resized the module by a rounded delta "so the column count doesn't jump" - that turned
		// out to be the actual SOURCE of a real column-count bug, not a fix for one): whatever
		// the header had grown to absorb is by construction always LESS than one column pitch
		// (Neo::absorbLeftoverIntoHeader() only ever grows the header by a leftover remainder,
		// never a whole extra pitch), so shrinking the header back down can never free up enough
		// room for one more column - the same columns keep showing, just left-aligned against
		// the now-narrower header, with that same leftover reappearing as a plain visible gap on
		// the right until onDragEnd()'s own absorb pass consumes it again.
		if (module)
			module->OL_setOutState(ROW_HEADER_WIDTH_MM_JSON, NEO_ROW_HEADER_TARGET_WIDTH_MM);
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
		float controlsWidthMm = module ? module->getControlsWidthMm() : neoRowAreaControlsWidthMm(false, NEO_ROW_HEADER_TARGET_WIDTH_MM);
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

	// The header stayed at exactly its target width (Tw) throughout the drag (onDragMove never
	// touches it), so this is the one moment that width actually changes - see
	// Neo::absorbLeftoverIntoHeader()'s own comment for the full reasoning.
	void onDragEnd(const DragEndEvent &e) override
	{
		if (!module)
			return;
		module->absorbLeftoverIntoHeader();

		// A locked follower never gets its own onDragStart/onDragEnd - it only tracks this
		// drag live via NeoWidget::step()'s own lock-width-sync, which deliberately just pins
		// its header at Tw the whole time instead of absorbing on every intermediate tick (see
		// that block's own comment). This is therefore the one real "the drag is truly over"
		// event for the WHOLE group - finalize every other locked instance's header here too,
		// not just this one's own. A single, one-shot getModule() lookup triggered by a real
		// mouse-release, same already-established-safe pattern as toggleLock()/
		// pruneStaleLockIds() above - not a per-tick moduleProcess()/onRemove() call, so none of
		// CLAUDE.md's Pitfalls deadlock concerns about getModule() apply here.
		if (module->OL_state[LOCKED_JSON] > 0.5f)
		{
			Neo::NeoLockData lockData = module->readLockData();
			for (int64_t lockedId : lockData.ids)
			{
				if (lockedId == (int64_t) module->id)
					continue;
				Module *m = APP->engine->getModule(lockedId);
				Neo *other = m ? dynamic_cast<Neo*>(m) : nullptr;
				if (other)
					other->absorbLeftoverIntoHeader();
			}
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
		int colorPacked = m->channelColor[channel]; // channel-owned, Host-shared - see its own comment
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

/**
	Plain row-name label (v1) - full channel renaming via right-click is deferred (see
	NeoChannelNameField's own comment below). Used to also draw the row's own channel number as a
	stand-in (there was no dedicated channel display yet) - that's now redundant and removed
	(2026-07-20) now that a real channel display+knob exists (NEO_ROW_CHANNEL_DISPLAY_X_MM,
	NeoWidget::step()), so this currently draws nothing at all until real per-row naming exists;
	kept as an empty placeholder widget (not deleted outright) since its box/position are already
	wired into NeoWidget::step()'s own layout and it's the natural home for a future name.
*/
struct NeoRowNameWidget : TransparentWidget
{
	Module *module = nullptr;
	int row = 0;
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
	OLLabelButton *lockButton = nullptr;

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
			bool locked = module && module->OL_state[LOCKED_JSON] > 0.5f;
			return std::string(locked ? "FREE" : "LOCK");
		};
		lockButton->onClick = [module]() {
			if (module)
				module->toggleLock();
		};
		lockButton->box.pos = mm2px(Vec(NEO_LOCK_BUTTON_X_MM, NEO_LOCK_BUTTON_Y_MM));
		lockButton->box.size = mm2px(Vec(NEO_LOCK_BUTTON_WIDTH_MM, NEO_LOCK_BUTTON_HEIGHT_MM));
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
				int numPages = std::max(1, (loopLen + visibleCols - 1) / visibleCols);
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
			float rowHeaderWidthMm = neoModule->getRowHeaderWidthMm();
			float controlsWidthMm = neoRowAreaControlsWidthMm(fullHeight, rowHeaderWidthMm);

			// KISS (Dieter's own instruction, 2026-07-20): NEO never auto-resizes itself just
			// because Grid Rows/Full Height changed the cell pitch - it simply shows as many
			// whole columns as currently fit (getVisibleColumns(), already floor-based), padded
			// to the frame the same way every other edge already is. The module's own width only
			// ever changes from an explicit user drag (NeoResizeHandle) or the Lock group's own
			// target-width convergence below - never as an automatic side effect of a cell-size
			// change.

			// Lock sync (width half) - width, unlike rows, is "weak" (Dieter's own framing) - a
			// locked instance always keeps its own adopted row count, but only matches the common
			// width as far as free space allows, remembering the target and retrying every tick
			// so it grows (or shrinks) into it the moment room frees up, rather than failing the
			// lock outright.
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
					// Still (or newly) diverged from the group's target - the header stays
					// pinned at its own target width (Tw) for as long as I'm still chasing the
					// group's width, exactly like the actively-dragged instance's own header
					// during a live drag (Dieter's own spec, 2026-07-20: "while dragging the
					// header has to be targetWidth and this does not change"). This instance
					// never gets its own onDragStart/onDragEnd (it only follows via this lock
					// sync, never dragged directly) - the ONE place its header ever gets the
					// "fill the leftover gap" treatment is when the actually-dragged instance's
					// own onDragEnd() finalizes the whole locked group at once
					// (NeoResizeHandle::onDragEnd() below), never here mid-chase - doing it here
					// on every successful convergence step instead grows the header a little on
					// every intermediate tick of a live drag (each one based on whatever the
					// previous tick already grew it to, since nothing ever resets it back down in
					// between) instead of staying flat at Tw until the drag truly ends.
					if (rowHeaderWidthMm != NEO_ROW_HEADER_TARGET_WIDTH_MM)
					{
						neoModule->OL_setOutState(ROW_HEADER_WIDTH_MM_JSON, NEO_ROW_HEADER_TARGET_WIDTH_MM);
						rowHeaderWidthMm = NEO_ROW_HEADER_TARGET_WIDTH_MM;
						controlsWidthMm = neoRowAreaControlsWidthMm(fullHeight, rowHeaderWidthMm);
					}

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
			// Goes through the same shared neoColumnFit() (Neo.hpp) that Neo::getVisibleColumns()/
			// Neo::absorbLeftoverIntoHeader() use, and sizes the widget to EXACTLY visibleCols
			// worth of pitch (not the raw, possibly-fractional widthMm-controlsWidthMm) - so this
			// widget's own box can never show a sliver of dead trailing space beyond the last
			// column draw()/click-handling actually treats as real (Dieter's own catch,
			// 2026-07-20: these used to be separate reimplementations of the same arithmetic that
			// could silently disagree - "ugly and has to be resolved... by better coding").
			int visibleColsThisFrame; float leftoverMmThisFrame;
			neoColumnFit(widthMm, controlsWidthMm, columnPitchMm, visibleColsThisFrame, leftoverMmThisFrame);
			float cellsWidthMm = (float) visibleColsThisFrame * columnPitchMm;

			for (int r = 0; r < NEO_NUM_ROWS; r++)
			{
				bool rowVisible = r < rowsDisplayed;
				rowHeaderFrames[r]->visible = rowVisible;
				rowNames[r]->visible = rowVisible;
				trackDisplays[r]->visible = rowVisible;
				trackKnobs[r]->visible = rowVisible;
				trackKnobRings[r]->visible = rowVisible;
				channelDisplays[r]->visible = rowVisible;
				channelKnobs[r]->visible = rowVisible;
				channelKnobRings[r]->visible = rowVisible;
				positionDisplays[r]->visible = rowVisible;
				followBtns[r]->visible = rowVisible;
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
				float headerFrameRightMm = headerBoundaryMm - NEO_FRAME_GAP_MM;
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

				rowNames[r]->box.pos = calculateCoordinates(NEO_ROW_NAME_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, y, 0.f);
				// The name field is the one thing that actually grows to fill whatever extra
				// width the header has beyond its own default target (Dieter's own spec,
				// 2026-07-20) - every other control in the row keeps its own fixed size/position.
				float nameWidthMm = NEO_ROW_NAME_WIDTH_MM + std::max(0.f, rowHeaderWidthMm - NEO_ROW_HEADER_TARGET_WIDTH_MM);
				rowNames[r]->box.size = mm2px(Vec(nameWidthMm, cellHeightMm));

				followBtns[r]->box.pos = calculateCoordinates(NEO_ROW_FOLLOW_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(followBtns[r]->box.size.div(2.f));
				leftBtns[r]->box.pos = calculateCoordinates(NEO_ROW_LEFT_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(leftBtns[r]->box.size.div(2.f));
				rightBtns[r]->box.pos = calculateCoordinates(NEO_ROW_RIGHT_X_MM + NEO_GLOBAL_AREA_WIDTH_MM, centerY, 0.f).minus(rightBtns[r]->box.size.div(2.f));

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
