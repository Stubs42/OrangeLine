/*
	Styx.cpp

	Code for the OrangeLine module STYX

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

#include "Styx.hpp"

#define STYX_DEFAULT_WIDTH_HP 24
#define STYX_CONTROLS_WIDTH_MM 40.f  // reserved left-hand width for name/toggle/page controls
#define STYX_COLUMN_WIDTH_MM   4.f   // width of one step-column cell
#define STYX_ROW_HEIGHT_MM     6.5f
#define STYX_FIRST_ROW_Y_MM    12.f

struct Styx : Module, XExpanderInterface, XOExpanderInterface, StyxExpanderInterface
{
#include "OrangeLineCommon.hpp"

	// Resolved every tick by trying both sides (unlike the strictly one-sided X/XO families) -
	// the non-adjacent/detached-connection idea was explicitly withdrawn, so this is a plain
	// immediate-neighbor dynamic_cast, not a chain-walk.
	StyxHostInterface *styxHost = nullptr;
	// The raw neighbor Module* that satisfied styxHost above - kept separately so the
	// XExpanderInterface/XOExpanderInterface relay methods below can re-cast the SAME instance to
	// whichever other interface a chained X-/XO-family Expander is looking for (Morpheus already
	// implements all three interfaces on one instance).
	Module *styxHostModule = nullptr;

	dsp::SchmittTrigger memTapeTrigger[STYX_NUM_ROWS];
	dsp::SchmittTrigger followTrigger[STYX_NUM_ROWS];
	dsp::SchmittTrigger leftTrigger[STYX_NUM_ROWS];
	dsp::SchmittTrigger rightTrigger[STYX_NUM_ROWS];

	// Persistent per-instance label buffers - setJsonLabel() stores the raw char* handed to it
	// rather than copying the string, so a temporary std::string's c_str() would dangle the
	// instant the temporary is destroyed (see CLAUDE.md-adjacent lesson from XR8/XR16 today).
	char rowChannelLabelBuf[STYX_NUM_ROWS][20];
	char channelColorLabelBuf[POLY_CHANNELS][20];
	char rowMemTapeLabelBuf[STYX_NUM_ROWS][20];
	char rowFollowLabelBuf[STYX_NUM_ROWS][20];
	char rowPageLabelBuf[STYX_NUM_ROWS][20];
	char rowCellTypeLabelBuf[STYX_NUM_ROWS][20];

	Styx()
	{
		initializeInstance();
	}

	// How many step-columns currently fit, given the panel's own current width - "however many
	// fit," deliberately simple (Dieter's own "kiss" instruction from the spec conversation).
	int getVisibleColumns()
	{
		float widthMm = OL_state[PANEL_WIDTH_HP_JSON] * 5.08f;
		int cols = (int) ((widthMm - STYX_CONTROLS_WIDTH_MM) / STYX_COLUMN_WIDTH_MM);
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
		setJsonLabel(CONNECTED_HOST_ID_JSON, "connectedHostId");
		for (int r = 0; r < STYX_NUM_ROWS; r++)
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
		for (int c = 0; c < POLY_CHANNELS; c++)
		{
			snprintf(channelColorLabelBuf[c], sizeof(channelColorLabelBuf[c]), "channelColor%d", c);
			setJsonLabel(CHANNEL_COLOR_JSON + c, channelColorLabelBuf[c]);
		}

#pragma GCC diagnostic pop
	}

	inline void moduleParamConfig()
	{
		for (int r = 0; r < STYX_NUM_ROWS; r++)
		{
			configParam(ROW_MEMTAPE_PARAM + r, 0.f, 1.f, 0.f, string::f("Row %d Mem/Tape", r + 1));
			configParam(ROW_FOLLOW_PARAM + r, 0.f, 1.f, 0.f, string::f("Row %d Follow", r + 1));
			configParam(ROW_LEFT_PARAM + r, 0.f, 1.f, 0.f, string::f("Row %d Page Back", r + 1));
			configParam(ROW_RIGHT_PARAM + r, 0.f, 1.f, 0.f, string::f("Row %d Page Forward", r + 1));
		}
	}

	inline void moduleCustomInitialize() {}
	inline void moduleInitialize() {}

	void moduleReset()
	{
		styleChanged = true;
		OL_state[PANEL_WIDTH_HP_JSON] = (float) STYX_DEFAULT_WIDTH_HP;
		OL_state[CONNECTED_HOST_ID_JSON] = -1.f;
		for (int r = 0; r < STYX_NUM_ROWS; r++)
		{
			OL_state[ROW_CHANNEL_JSON + r] = (float) r; // row r shows channel r by default
			OL_state[ROW_MEMTAPE_JSON + r] = 0.f;       // TAPE
			OL_state[ROW_FOLLOW_JSON + r] = 1.f;        // auto-follow the play cursor
			OL_state[ROW_PAGE_JSON + r] = 0.f;
			OL_state[ROW_CELLTYPE_JSON + r] = 1.f;      // Value (more generally useful default)
		}
		for (int c = 0; c < POLY_CHANNELS; c++)
			OL_state[CHANNEL_COLOR_JSON + c] = (float) 0xff6600; // ORANGE, packed 24-bit RGB
	}

	inline void moduleProcess(const ProcessArgs &args)
	{
		// No brightPanel/darkPanel SvgPanel swap here (unlike every other module) - StyxPanelWidget
		// draws its own theme-aware flat background directly every frame instead, since a baked
		// SVG panel can't stretch to match this module's resizable width. styleChanged is left
		// alone (never consumed) - harmless, nothing depends on it being cleared.

		// Auto-remembered non-adjacent connection: whenever ordinary physical adjacency resolves
		// a Host, its module id is saved automatically - no manual "Connect" selection needed.
		// That remembered id then takes over once STYX is no longer physically adjacent (moved
		// elsewhere in the rack), letting it keep watching the same Morpheus. "Disconnect"
		// (right-click) clears the remembered id back to -1, forcing pure adjacency again.
		Module *neighbor = leftExpander.module;
		styxHost = resolveStyxHost(neighbor);
		if (!styxHost)
		{
			neighbor = rightExpander.module;
			styxHost = resolveStyxHost(neighbor);
		}
		if (styxHost)
		{
			// Remember this connection for later, unless the user explicitly disconnected.
			int64_t neighborId = neighbor->id;
			if ((int64_t) OL_state[CONNECTED_HOST_ID_JSON] != neighborId)
				OL_state[CONNECTED_HOST_ID_JSON] = (float) neighborId;
		}
		else
		{
			// Not physically adjacent to anything right now - fall back to whatever was
			// remembered, if it's still a valid StyxHostInterface implementer.
			int64_t connectedId = (int64_t) OL_state[CONNECTED_HOST_ID_JSON];
			if (connectedId >= 0)
			{
				Module *m = APP->engine->getModule(connectedId);
				StyxHostInterface *host = m ? dynamic_cast<StyxHostInterface*>(m) : nullptr;
				if (host)
				{
					styxHost = host;
					neighbor = m;
				}
				else
					OL_state[CONNECTED_HOST_ID_JSON] = -1.f; // target vanished - clear the stale id
			}
		}
		styxHostModule = styxHost ? neighbor : nullptr;
		setStateLight(CONN_LIGHT, styxHost ? 255.f : 0.f);

		int visibleCols = getVisibleColumns();

		for (int r = 0; r < STYX_NUM_ROWS; r++)
		{
			if (memTapeTrigger[r].process(params[ROW_MEMTAPE_PARAM + r].getValue()))
				OL_state[ROW_MEMTAPE_JSON + r] = (OL_state[ROW_MEMTAPE_JSON + r] > 0.5f) ? 0.f : 1.f;
			if (followTrigger[r].process(params[ROW_FOLLOW_PARAM + r].getValue()))
				OL_state[ROW_FOLLOW_JSON + r] = (OL_state[ROW_FOLLOW_JSON + r] > 0.5f) ? 0.f : 1.f;

			bool follow = OL_state[ROW_FOLLOW_JSON + r] > 0.5f;
			if (styxHost)
			{
				int channel = clamp((int) OL_state[ROW_CHANNEL_JSON + r], 0, POLY_CHANNELS - 1);
				if (follow)
				{
					int cursor = styxHost->getPlayCursor(channel);
					OL_state[ROW_PAGE_JSON + r] = (float) (cursor / visibleCols);
				}
				else
				{
					int loopLen = std::max(1, styxHost->getLoopLen(channel));
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

	// XExpanderInterface - pure relay, STYX never uses any of these itself; only implemented so
	// an X-family Expander chained further along the rack (e.g. "X8 | STYX | Morpheus") keeps
	// resolving its own Host straight through STYX exactly as it would through any other
	// X-family member. Every method below except getXHost()/getXStyle() is genuinely dead code
	// from STYX's own perspective (resolveXHost() never calls them on an intermediate relay -
	// only a concrete X8/X16's own widget calls them, on its own module) but must still compile.
	XHostInterface* getXHost() override { return styxHostModule ? dynamic_cast<XHostInterface*>(styxHostModule) : nullptr; }
	float getXStyle() override { return OL_state[STYLE_JSON]; }
	int getXKnobCount() override { return 0; }
	float getXKnobValue(int channel) override { return 0.f; }
	int getXBrowseIndex() override { return 0; }
	bool consumeEngagePress() override { return false; }
	XParamType getXBrowsedParamType() override { return X_PARAM_CONTINUOUS; }
	NVGcolor getXBrowsedParamColor() override { return ORANGE; }
	XAlign getXBrowsedParamAlign() override { return X_ALIGN_LEFT; }
	std::string formatXValue(float raw) override { return ""; }
	void requestXValueClick(int channel) override {}
	void getXEngagedSummary(int &hostCount, int &slotCount) override { hostCount = 0; slotCount = 0; }

	// XOExpanderInterface - same pure-relay reasoning as above, for "STYX | XO8"-shaped chains.
	XOHostInterface* getXOHost() override { return styxHostModule ? dynamic_cast<XOHostInterface*>(styxHostModule) : nullptr; }
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
};

/**
	Resize handle - own implementation inspired by VCV core's own Blank.cpp/ModuleResizeHandle
	(read directly from /c/msys64/home/Dieter/RackDevelopment/Rack/src/core/Blank.cpp) and
	SubmarineFree's SizeableModuleWidget/ResizeHandle (github.com/david-c14/SubmarineFree, GPL -
	pattern reused, not copied verbatim). Right-edge only for v1 (simpler case both references
	support identically).
*/
struct StyxResizeHandle : OpaqueWidget
{
	Styx *module = nullptr;
	Vec dragPos;
	Rect originalBox;

	StyxResizeHandle()
	{
		box.size = Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
	}

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
		float minWidth = STYX_MIN_WIDTH_HP * RACK_GRID_WIDTH;
		newBox.size.x += deltaX;
		newBox.size.x = std::fmax(newBox.size.x, minWidth);
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
struct StyxRowCellsWidget : TransparentWidget
{
	Module *module = nullptr;
	int row = 0;
	int dragStep = -1;
	float dragStartValue = 0.f;

	Styx* styx() { return module ? dynamic_cast<Styx*>(module) : nullptr; }

	int stepAtLocalX(float x, int visibleCols)
	{
		float cellWidth = box.size.x / (float) visibleCols;
		return clamp((int) (x / cellWidth), 0, visibleCols - 1);
	}

	void draw(const DrawArgs &args) override
	{
		Styx *m = styx();
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGB(0x10, 0x10, 0x10));
		nvgFill(args.vg);

		if (!m || !m->styxHost)
			return;

		int channel = clamp((int) m->OL_state[ROW_CHANNEL_JSON + row], 0, POLY_CHANNELS - 1);
		bool mem = m->OL_state[ROW_MEMTAPE_JSON + row] > 0.5f;
		bool gate = m->OL_state[ROW_CELLTYPE_JSON + row] < 0.5f;
		int visibleCols = m->getVisibleColumns();
		int page = (int) m->OL_state[ROW_PAGE_JSON + row];
		int loopLen = m->styxHost->getLoopLen(channel);
		float cellWidth = box.size.x / (float) visibleCols;
		int colorPacked = (int) m->OL_state[CHANNEL_COLOR_JSON + channel];
		NVGcolor color = nvgRGB((colorPacked >> 16) & 0xff, (colorPacked >> 8) & 0xff, colorPacked & 0xff);

		for (int i = 0; i < visibleCols; i++)
		{
			int step = page * visibleCols + i;
			if (step >= loopLen)
				break;
			float value = mem ? m->styxHost->getMemStep(channel, step) : m->styxHost->getTapeStep(channel, step);
			float x = i * cellWidth;

			if (gate)
			{
				bool on = value > 5.f; // plain 5V threshold on a real Host voltage - see
				                       // CLAUDE.md's pitfall on X8-style dual-convention issues,
				                       // doesn't apply here since this always reads a real Host
				                       // value directly, never a raw 0..1 knob
				nvgBeginPath(args.vg);
				nvgRect(args.vg, x + 1.f, 1.f, cellWidth - 2.f, box.size.y - 2.f);
				nvgFillColor(args.vg, on ? color : nvgRGB(0x30, 0x30, 0x30));
				nvgFill(args.vg);
			}
			else
			{
				float t = clamp((value + 10.f) / 20.f, 0.f, 1.f); // -10..+10V -> 0..1
				float barHeight = t * box.size.y;
				nvgBeginPath(args.vg);
				nvgRect(args.vg, x + 1.f, box.size.y - barHeight, cellWidth - 2.f, barHeight);
				nvgFillColor(args.vg, color);
				nvgFill(args.vg);
			}
		}
	}

	void onButton(const event::Button &e) override
	{
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS)
		{
			Styx *m = styx();
			if (m && m->styxHost)
			{
				int visibleCols = m->getVisibleColumns();
				dragStep = stepAtLocalX(e.pos.x, visibleCols);
				int channel = clamp((int) m->OL_state[ROW_CHANNEL_JSON + row], 0, POLY_CHANNELS - 1);
				bool mem = m->OL_state[ROW_MEMTAPE_JSON + row] > 0.5f;
				int page = (int) m->OL_state[ROW_PAGE_JSON + row];
				int step = page * visibleCols + dragStep;
				dragStartValue = mem ? m->styxHost->getMemStep(channel, step) : m->styxHost->getTapeStep(channel, step);
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
		Styx *m = styx();
		if (!m || !m->styxHost || dragStep < 0)
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
			m->styxHost->setMemStep(channel, step, newValue);
		else
			m->styxHost->setTapeStep(channel, step, newValue);
	}

	void onDragEnd(const DragEndEvent &e) override
	{
		dragStep = -1;
		TransparentWidget::onDragEnd(e);
	}
};

/**
	Plain row-number label (v1) - full channel renaming via right-click is deferred (see
	StyxChannelNameField's own comment below), so this just shows which channel the row currently
	displays. Simple direct nvgText() draw, mirroring the rest of the codebase's own small label
	widgets rather than OrangeLine.hpp's own TextWidget (a much more specialized scrolling-display
	widget tied to module-specific animation state, not a fit for a plain static/row-number label).
*/
struct StyxRowNameWidget : TransparentWidget
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
		Styx *m = module ? dynamic_cast<Styx*>(module) : nullptr;
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
	Flat, code-drawn panel background - an SVG panel (the usual `setPanel()`/`SvgPanel` convention
	every other OrangeLine module uses) has a fixed baked width and can't stretch to match a
	resizable module, so STYX draws its own background directly instead, sized to `box.size`
	every frame (mirrors VCV core's own `BlankPanel`, `Blank.cpp`). Deliberately simple graphics
	per Dieter's own "function first, polish later" instruction - a themed flat fill + border, no
	hand-authored art at all yet.
*/
struct StyxPanelWidget : Widget
{
	Module *module = nullptr;

	void draw(const DrawArgs &args) override
	{
		Styx *m = module ? dynamic_cast<Styx*>(module) : nullptr;
		float style = m ? m->OL_state[STYLE_JSON] : STYLE_ORANGE;
		NVGcolor bg = (style == STYLE_DARK) ? X_STRIP_BG_DARK : (style == STYLE_BRIGHT) ? X_STRIP_BG_BRIGHT : X_STRIP_BG_ORANGE;
		NVGcolor frame = (style == STYLE_DARK) ? X_FRAME_DARK : (style == STYLE_BRIGHT) ? X_FRAME_BRIGHT : X_FRAME_ORANGE;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(args.vg, bg);
		nvgFill(args.vg);
		nvgStrokeWidth(args.vg, mm2px(0.5f));
		nvgStrokeColor(args.vg, frame);
		nvgStroke(args.vg);
		Widget::draw(args);
	}
};

/**
	Main Module Widget - resizable panel, 16 fixed rows (8/16-row mode toggle deferred). Function
	first, styling later per Dieter's own instruction - simple graphics throughout.
*/
struct StyxWidget : ModuleWidget
{
	StyxResizeHandle *resizeHandle = nullptr;
	StyxPanelWidget *panelWidget = nullptr;
	StyxRowCellsWidget *rowCells[STYX_NUM_ROWS] = {};
	StyxRowNameWidget *rowNames[STYX_NUM_ROWS] = {};

	StyxWidget(Styx *module)
	{
		setModule(module);

		float widthHp = module ? module->OL_state[PANEL_WIDTH_HP_JSON] : STYX_DEFAULT_WIDTH_HP;
		if (widthHp < STYX_MIN_WIDTH_HP)
			widthHp = STYX_DEFAULT_WIDTH_HP;
		box.size = Vec(widthHp * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		panelWidget = new StyxPanelWidget();
		panelWidget->module = module;
		panelWidget->box.size = box.size;
		addChild(panelWidget);

		addChild(createLightCentered<AutoHideLight<TinyLight<GreenRedLight>>>(calculateCoordinates(3.5f, 4.f, 0.f), module, CONN_LIGHT));

		for (int r = 0; r < STYX_NUM_ROWS; r++)
		{
			float y = STYX_FIRST_ROW_Y_MM + r * STYX_ROW_HEIGHT_MM;

			StyxRowNameWidget *name = new StyxRowNameWidget();
			name->module = module;
			name->row = r;
			name->box.pos = calculateCoordinates(1.f, y, 0.f);
			name->box.size = mm2px(Vec(16.f, STYX_ROW_HEIGHT_MM - 0.5f));
			addChild(name);
			rowNames[r] = name;

			struct StyxRowButton : ParamWidget
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

			StyxRowButton *memTapeBtn = createParamCentered<StyxRowButton>(calculateCoordinates(19.f, y + STYX_ROW_HEIGHT_MM / 2.f - 0.5f, 0.f), module, ROW_MEMTAPE_PARAM + r);
			memTapeBtn->box.size = mm2px(Vec(6.f, 4.f));
			memTapeBtn->onColor = nvgRGB(0x00, 0x99, 0xff);
			addParam(memTapeBtn);

			StyxRowButton *followBtn = createParamCentered<StyxRowButton>(calculateCoordinates(26.f, y + STYX_ROW_HEIGHT_MM / 2.f - 0.5f, 0.f), module, ROW_FOLLOW_PARAM + r);
			followBtn->box.size = mm2px(Vec(6.f, 4.f));
			followBtn->onColor = nvgRGB(0x00, 0xdd, 0x44);
			addParam(followBtn);

			StyxRowButton *leftBtn = createParamCentered<StyxRowButton>(calculateCoordinates(33.f, y + STYX_ROW_HEIGHT_MM / 2.f - 0.5f, 0.f), module, ROW_LEFT_PARAM + r);
			leftBtn->box.size = mm2px(Vec(4.f, 4.f));
			addParam(leftBtn);

			StyxRowButton *rightBtn = createParamCentered<StyxRowButton>(calculateCoordinates(38.f, y + STYX_ROW_HEIGHT_MM / 2.f - 0.5f, 0.f), module, ROW_RIGHT_PARAM + r);
			rightBtn->box.size = mm2px(Vec(4.f, 4.f));
			addParam(rightBtn);

			StyxRowCellsWidget *cells = new StyxRowCellsWidget();
			cells->module = module;
			cells->row = r;
			cells->box.pos = calculateCoordinates(STYX_CONTROLS_WIDTH_MM, y, 0.f);
			cells->box.size = mm2px(Vec(1.f, STYX_ROW_HEIGHT_MM - 0.5f)); // width fixed up in step()
			addChild(cells);
			rowCells[r] = cells;
		}

		resizeHandle = new StyxResizeHandle();
		resizeHandle->module = module;
		addChild(resizeHandle);
	}

	void step() override
	{
		Styx *styxModule = dynamic_cast<Styx *>(module);
		if (styxModule)
		{
			float widthMm = styxModule->OL_state[PANEL_WIDTH_HP_JSON] * 5.08f;
			box.size.x = mm2px(widthMm);
			float cellsWidthMm = std::max(1.f, widthMm - STYX_CONTROLS_WIDTH_MM);
			for (int r = 0; r < STYX_NUM_ROWS; r++)
				rowCells[r]->box.size.x = mm2px(cellsWidthMm);
		}
		if (panelWidget)
			panelWidget->box.size = box.size;
		if (resizeHandle)
			resizeHandle->box.pos.x = box.size.x - resizeHandle->box.size.x;
		ModuleWidget::step();
	}

	struct XOStyleItem : MenuItem
	{
		Styx *module;
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

	// Per-row "which channel" submenu - two-level, "Rows" -> "Row N" -> the 16 channel choices,
	// same setSize()-required pattern as CLAUDE.md's documented "Channels" submenu convention.
	struct StyxRowsItem : MenuItem
	{
		Styx *module;

		struct StyxRowItem : MenuItem
		{
			Styx *module;
			int row;

			struct StyxRowChannelItem : MenuItem
			{
				Styx *module;
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

			struct StyxRowCellTypeItem : MenuItem
			{
				Styx *module;
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
					StyxRowChannelItem *item = new StyxRowChannelItem();
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
					StyxRowCellTypeItem *item = new StyxRowCellTypeItem();
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
			for (int r = 0; r < STYX_NUM_ROWS; r++)
			{
				StyxRowItem *item = new StyxRowItem();
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
	struct StyxChannelsItem : MenuItem
	{
		Styx *module;

		struct StyxChannelNameField : ui::TextField
		{
			Styx *module;
			int channel;
			void onChange(const ChangeEvent &e) override
			{
				// Names aren't part of this v1 pass's OL_state (only color is, packed as a
				// float) - left as a visual-only field for now; full persistence needs the
				// moduleExtraDataToJson/FromJson hook (see CLAUDE.md), deferred alongside the
				// rest of the styling/polish pass.
				TextField::onChange(e);
			}
		};

		struct StyxChannelColorItem : MenuItem
		{
			Styx *module;
			int channel;
			int color;
			void onAction(const event::Action &e) override
			{
				module->OL_setOutState(CHANNEL_COLOR_JSON + channel, float(color));
			}
			void step() override
			{
				if (module)
					rightText = ((int) module->OL_state[CHANNEL_COLOR_JSON + channel] == color) ? "✔" : "";
			}
		};

		struct StyxChannelItem : MenuItem
		{
			Styx *module;
			int channel;

			Menu *createChildMenu() override
			{
				Menu *menu = new Menu;
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
					StyxChannelColorItem *item = new StyxChannelColorItem();
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
				StyxChannelItem *item = new StyxChannelItem();
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
	struct StyxDisconnectItem : MenuItem
	{
		Styx *module;
		void onAction(const event::Action &e) override
		{
			module->OL_setOutState(CONNECTED_HOST_ID_JSON, -1.f);
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		Styx *module = dynamic_cast<Styx *>(this->module);
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

		StyxRowsItem *rowsItem = new StyxRowsItem();
		rowsItem->module = module;
		rowsItem->text = "Rows";
		rowsItem->rightText = RIGHT_ARROW;
		menu->addChild(rowsItem);

		StyxChannelsItem *channelsItem = new StyxChannelsItem();
		channelsItem->module = module;
		channelsItem->text = "Channels";
		channelsItem->rightText = RIGHT_ARROW;
		menu->addChild(channelsItem);

		if ((int64_t) module->OL_state[CONNECTED_HOST_ID_JSON] >= 0)
		{
			spacerLabel = new MenuLabel();
			menu->addChild(spacerLabel);

			StyxDisconnectItem *disconnectItem = new StyxDisconnectItem();
			disconnectItem->module = module;
			disconnectItem->text = "Disconnect";
			menu->addChild(disconnectItem);
		}
	}
};

Model *modelStyx = createModel<Styx, StyxWidget>("Styx");
