/*
	XOCommon.hpp

	Shared widget/control classes for the whole XO family (XO8/XD8/XOD8/XO16/XD16/XOD16) -
	#include'd at file scope by each of their own .cpp files, before their own module struct
	definitions. Mirrors X8Common.hpp's own role for the X family, but kept as a fully separate
	file rather than extending X8Common.hpp's widgets with a second dynamic_cast branch: the
	semantics genuinely differ (no Bind button, no mine/taken/available tri-color since watching
	a Host's output is never exclusive, a brand-new non-interactive gate-square widget, a
	channel-count overflow light with no X-family equivalent) - same "LANES vs X" full-separation
	precedent CLAUDE.md already documents for cases like this.

	Everything here operates on a plain `Module *` + `dynamic_cast<XOExpanderInterface*>`, never
	on a concrete XO8/XD8/etc. type - XOExpanderInterface (XOShared.hpp) exposes everything a
	control widget needs (browsed type/color/align/format, capacity, live channel count), so this
	code never needs to know which concrete Expander it's attached to.
*/

// Position/size of the two OrangeLine wordmark pieces - identical values to X8Common.hpp's own
// X8_LOGO_COVER*_MM (same logo, same fixed size regardless of family or panel width), duplicated
// here rather than shared per this file's own "fully separate from X8Common.hpp" decision above.
#define XO_LOGO_COVER1_Y_MM      121.41325f
#define XO_LOGO_COVER1_WIDTH_MM  12.7f
#define XO_LOGO_COVER1_HEIGHT_MM 2.9735825f
#define XO_LOGO_COVER2_Y_MM      125.03953f
#define XO_LOGO_COVER2_WIDTH_MM  7.6199994f
#define XO_LOGO_COVER2_HEIGHT_MM 2.9777272f

/**
	Static background-colored cover for one piece of the OrangeLine wordmark - purely cosmetic,
	shown only while this Expander is actually connected to a Host (see updateXOLogoCovers()),
	mirrors X8LogoCover exactly.
*/
struct XOLogoCover : TransparentWidget
{
	Module *module = nullptr;

	void draw(const DrawArgs &args) override
	{
		XOExpanderInterface *expander = module ? dynamic_cast<XOExpanderInterface*>(module) : nullptr;
		float style = expander ? expander->getXOStyle() : STYLE_ORANGE;
		NVGcolor fill = (style == STYLE_DARK) ? X_STRIP_BG_DARK
		              : (style == STYLE_BRIGHT) ? X_STRIP_BG_BRIGHT
		              : X_STRIP_BG_ORANGE;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(args.vg, fill);
		nvgFill(args.vg);
	}
};

inline void addXOLogoCovers(ModuleWidget *w, float panelWidthMm, XOLogoCover **cover1Out, XOLogoCover **cover2Out)
{
	XOLogoCover *cover1 = new XOLogoCover();
	cover1->module = w->module;
	cover1->box.pos = calculateCoordinates(panelWidthMm / 2.f - XO_LOGO_COVER1_WIDTH_MM / 2.f, XO_LOGO_COVER1_Y_MM, 0.f);
	cover1->box.size = mm2px(Vec(XO_LOGO_COVER1_WIDTH_MM, XO_LOGO_COVER1_HEIGHT_MM));
	cover1->visible = false;
	w->addChild(cover1);
	*cover1Out = cover1;

	XOLogoCover *cover2 = new XOLogoCover();
	cover2->module = w->module;
	cover2->box.pos = calculateCoordinates(panelWidthMm / 2.f - XO_LOGO_COVER2_WIDTH_MM / 2.f, XO_LOGO_COVER2_Y_MM, 0.f);
	cover2->box.size = mm2px(Vec(XO_LOGO_COVER2_WIDTH_MM, XO_LOGO_COVER2_HEIGHT_MM));
	cover2->visible = false;
	w->addChild(cover2);
	*cover2Out = cover2;
}

// Hidden exactly when, and only when, EITHER seam-closing ext-strip (extStrip/extStripLeft) is
// currently showing on that same edge. Uses the exact same condition
// updateXOExtStrip()/updateXOExtStripLeft() themselves use - bridgeConnected()
// (ExpanderBridge.hpp: same resolved bridge host id on both sides, neither -1) - so the two can
// never disagree, and the logo correctly reappears the instant this Expander no longer shares a
// connection with that neighbor. See X8Common.hpp's own updateXLogoCovers() for the X-family twin.
inline void updateXOLogoCovers(XOLogoCover *cover1, XOLogoCover *cover2, Module *module)
{
	bool seamVisible = module && (bridgeConnected(module, module->rightExpander.module)
	                           || bridgeConnected(module, module->leftExpander.module));
	cover1->visible = seamVisible;
	cover2->visible = seamVisible;
}

// Same hand-tuned centering-offset table as X8Common.hpp's own X8_CENTER_OFFSET_MM (identical
// font/size, same use case) - duplicated rather than shared per this file's own separation
// decision above.
static const float XO_CENTER_OFFSET_MM[6] = { 0.f, 3.725f + 1.242f, 3.725f, 2.483f, 1.242f, 0.f }; // index = strlen

// Same left/right margin as X8Common.hpp's own X8_NAME_DISPLAY_MARGIN_MM - every XONameDisplay
// instance's own box.pos.x, and (for the double-width XO16/XD16/XOD16/XR16 siblings) its box.size.x
// too, is measured against this same margin from each module's own panel edge.
#define XO_NAME_DISPLAY_MARGIN_MM 1.41287f

// Whether XONameDisplay ever shows the full descriptive name at all, on the wider displays that
// have room for it - disabled 2026-07-21, same instruction and same reasoning as X8Common.hpp's
// own X8_NAME_DISPLAY_SHOW_LONG_NAME ("showing the long name was a nice idea but it confuses
// myself... always show the short name... on all modules"). Kept as an easy-to-flip switch.
#define XO_NAME_DISPLAY_SHOW_LONG_NAME false

// Is `channel` within the browsed output's actual LIVE channel count right now? Below this, a
// value display should read as inert/dimmed (there's real no signal on that channel at all) even
// though the module itself has physical capacity for it (8 or 16 jacks/cells) - mirrors X8Knob's
// own channel-limit dimming, just against the Host's live count instead of a fixed sender limit.
static bool xoChannelLive(Module *module, int channel)
{
	XOExpanderInterface *expander = module ? dynamic_cast<XOExpanderInterface*>(module) : nullptr;
	if (!expander || !expander->getXOHost())
		return false;
	return channel < expander->getXOBrowsedChannelCount();
}

/**
	LEFT/RIGHT browse button - squarish rounded-rect matching the X family's own button artwork,
	but with no Bind-equivalent at all (no engagement on this side): plain active/inactive
	(orange/grey), active exactly while a Host is resolved. Deliberately much simpler than
	X8ButtonBase (no "taken" concept exists - reading is never exclusive).
*/
struct XOButtonBase : ParamWidget
{
	std::string label;

	bool isActive()
	{
		engine::ParamQuantity *pq = getParamQuantity();
		XOExpanderInterface *expander = pq && pq->module ? dynamic_cast<XOExpanderInterface*>(pq->module) : nullptr;
		return !pq || !pq->module || (expander && expander->getXOHost() != nullptr);
	}

	void onButton(const event::Button &e) override
	{
		engine::ParamQuantity *pq = getParamQuantity();
		if (isActive() && e.button == GLFW_MOUSE_BUTTON_LEFT && pq)
		{
			if (e.action == GLFW_PRESS)
			{
				pq->setValue(1.f);
				e.consume(this);
			}
			else if (e.action == GLFW_RELEASE)
			{
				pq->setValue(0.f);
			}
		}
		ParamWidget::onButton(e);
	}

	void draw(const DrawArgs &args) override
	{
		float style = STYLE_ORANGE;
		engine::ParamQuantity *pq = getParamQuantity();
		XOExpanderInterface *expander = pq && pq->module ? dynamic_cast<XOExpanderInterface*>(pq->module) : nullptr;
		if (expander)
			style = expander->getXOStyle();
		NVGcolor fill = (style == STYLE_DARK) ? X_BUTTON_FILL_DARK
		              : (style == STYLE_BRIGHT) ? X_BUTTON_FILL_BRIGHT
		              : X_BUTTON_FILL_ORANGE;
		NVGcolor accent = isActive() ? xThemedTextColor(style) : X_COLOR_INACTIVE_GREY;

		float r = mm2px(Vec(0.529f, 0.f)).x;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, r);
		nvgFillColor(args.vg, fill);
		nvgFill(args.vg);
		nvgStrokeWidth(args.vg, mm2px(Vec(0.3f, 0.f)).x);
		nvgStrokeColor(args.vg, accent);
		nvgStroke(args.vg);

		if (!label.empty())
		{
			std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, mm2px(Vec(2.82222f, 0.f)).x);
			nvgFillColor(args.vg, accent);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, label.c_str(), nullptr);
		}
	}
};

/**
	Currently-browsed output slot's name, LCD-style like the X family's own X8NameDisplay - but
	just one connected-color state (always the slot's own full-brightness accent color, never
	mine/taken/available) since watching an output is never exclusive. Same three-way "does it
	fit?" width tiering as X8NameDisplay for both the connected name and the disconnected
	greeting - see its own comment in X8Common.hpp for the full reasoning, identical here.
*/
struct XONameDisplay : TransparentWidget
{
	Module *module = nullptr;

	bool fits(NVGcontext *vg, const std::string &text)
	{
		float bounds[4];
		nvgTextBounds(vg, 0.f, 0.f, text.c_str(), nullptr, bounds);
		return (bounds[2] - bounds[0]) <= box.size.x;
	}

	// Always measures the actual rendered text and centers it within this widget's own CURRENT
	// box.size.x, rather than the hand-tuned XO_CENTER_OFFSET_MM table - this widget is reused
	// as-is across XO8/XO16/XOD8/XOD16/XD8/XD16/XR8/XR16, each with its own different display
	// width, so a single fixed-offset table could only ever be right for one of them (Dieter's
	// own catch, 2026-07-21 - see X8NameDisplay's own identical fix, X8Common.hpp, for the full
	// reasoning - XO_CENTER_OFFSET_MM itself is untouched, still correct for its OTHER use below
	// against a genuinely fixed-width display).
	float centerX(NVGcontext *vg, const std::string &text)
	{
		float bounds[4];
		nvgTextBounds(vg, 0.f, 0.f, text.c_str(), nullptr, bounds);
		float x = (box.size.x - (bounds[2] - bounds[0])) / 2.f;
		return (x > 0.f) ? x : 0.f;
	}

	void drawLayer(const DrawArgs &args, int layer) override
	{
		if (layer != 1)
		{
			Widget::drawLayer(args, layer);
			return;
		}

		XOExpanderInterface *expander = module ? dynamic_cast<XOExpanderInterface*>(module) : nullptr;

		float fontSizePx = mm2px(Vec(4.49792f, 0.f)).x;
		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, fontSizePx);

		// No Host resolved - same three-tier greeting as X8NameDisplay (widest-fitting wins).
		std::string text = OL_GREETING_WORD1;
		NVGcolor color = ORANGE;
		bool leftAlign = false;

		XOHostInterface *xoHost = expander ? expander->getXOHost() : nullptr;
		if (!xoHost)
		{
			if (fits(args.vg, OL_GREETING_LONG))
				text = OL_GREETING_LONG;
			else
			{
				std::string fullGreeting = std::string(OL_GREETING_WORD1) + " " + OL_GREETING_WORD2;
				if (fits(args.vg, fullGreeting))
					text = fullGreeting;
			}
		}
		else
		{
			int count = xoHost->getXOCount();
			if (count > 0)
			{
				int idx = clamp(expander->getXOBrowseIndex(), 0, count - 1);
				color = xoHost->getXOColor(idx); // always full brightness - watching is never exclusive

				std::string longName = xoHost->getXOName(idx);
				if (XO_NAME_DISPLAY_SHOW_LONG_NAME && fits(args.vg, longName))
				{
					text = longName;
					leftAlign = true;
				}
				else
				{
					char buffer[6];
					snprintf(buffer, sizeof(buffer), "%s", xoHost->getXOShortName(idx));
					text = buffer;
				}
			}
		}

		nvgFillColor(args.vg, color);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		float x = leftAlign ? 0.f : centerX(args.vg, text);

		nvgSave(args.vg);
		nvgScissor(args.vg, 0.f, -fontSizePx * 1.2f, box.size.x, fontSizePx * 1.6f);
		nvgText(args.vg, x, 0.f, text.c_str(), nullptr);
		nvgRestore(args.vg);
		Widget::drawLayer(args, 1);
	}
};

// XOD8/XOD16/XD8/XD16's own numeric readout (XOValueDisplay below) has no separate "digital
// display" cell baked into their panel art at all - unlike the X-family's X8D/X16D, the value text
// is drawn directly over the plain panel background. So XOButtonCover/XOGateIndicator's own
// background (2026-07-18, corrected from an earlier wrong assumption that copied the X-family's
// X_DISPLAY_BG_* convention) must match that plain background - X_STRIP_BG_* (XShared.hpp), the
// general panel/strip background used elsewhere (e.g. behind a knob-ring cover) - not a darker
// "LCD" color nothing in this family's own art actually uses.

/**
	Covers one display column's own panel decoration - mirrors X8DButtonCover/X16DButtonCover
	exactly (a single plain rect spanning the whole column's row range at once, not per-channel):
	shown whenever the browsed output is a gate/button type, hiding whatever's printed on the
	panel behind the display column since a lit/unlit square occupies that space instead of a
	number. Position/size come directly from Dieter's own MASK guide rect per module/column - see
	each Widget's own construction code.
*/
struct XOButtonCover : TransparentWidget
{
	Module *module = nullptr;

	void draw(const DrawArgs &args) override
	{
		XOExpanderInterface *expander = module ? dynamic_cast<XOExpanderInterface*>(module) : nullptr;
		float style = expander ? expander->getXOStyle() : STYLE_ORANGE;
		NVGcolor fill = (style == STYLE_DARK) ? X_STRIP_BG_DARK
		              : (style == STYLE_BRIGHT) ? X_STRIP_BG_BRIGHT
		              : X_STRIP_BG_ORANGE;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(args.vg, fill);
		nvgFill(args.vg);
	}
};

/**
	Per-channel numeric readout for a continuous-type output slot (XD8/XOD8/XD16/XOD16 only) -
	mirrors X8Common.hpp's own per-channel value display shape (X8DValueDisplay in X8D.cpp),
	generalized here since it's identical across all four display-bearing modules. Dims (via
	xoChannelLive()) when this channel is beyond the browsed output's actual live channel count.
*/
struct XOValueDisplay : TransparentWidget
{
	Module *module = nullptr;
	int channel = 0;

	void drawLayer(const DrawArgs &args, int layer) override
	{
		if (layer != 1)
		{
			Widget::drawLayer(args, layer);
			return;
		}
		XOExpanderInterface *expander = module ? dynamic_cast<XOExpanderInterface*>(module) : nullptr;
		if (!expander || !expander->getXOHost())
			return;

		bool live = xoChannelLive(module, channel);
		float value = live ? expander->getXOBrowsedChannelValue(channel) : 0.f;
		char raw[16];
		snprintf(raw, sizeof(raw), "%s", expander->formatXOBrowsedValue(value).c_str());
		if (raw[0] == '\0')
			return;

		XAlign align = expander->getXOBrowsedAlign();
		char buffer[16];
		float x = 0.f;
		if (align == X_ALIGN_RIGHT)
			snprintf(buffer, sizeof(buffer), "%5s", raw);
		else
		{
			snprintf(buffer, sizeof(buffer), "%s", raw);
			if (align == X_ALIGN_CENTER)
				x = mm2px(XO_CENTER_OFFSET_MM[clamp((int) strlen(buffer), 0, 5)]);
		}

		float fontSizePx = mm2px(Vec(4.49792f, 0.f)).x;
		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/repetition-scrolling.regular.ttf"));
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, fontSizePx);
		nvgSave(args.vg);
		if (!live)
			nvgGlobalAlpha(args.vg, 0.3f);
		nvgFillColor(args.vg, expander->getXOBrowsedColor());
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		nvgScissor(args.vg, 0.f, -fontSizePx * 1.2f, box.size.x, fontSizePx * 1.6f);
		nvgText(args.vg, x, 0.f, buffer, nullptr);
		if (!live)
			nvgGlobalAlpha(args.vg, 1.f);
		nvgRestore(args.vg);
		Widget::drawLayer(args, 1);
	}
};

/**
	Non-interactive lit/unlit square indicator for a gate/trigger-type output slot (XD8/XOD8/
	XD16/XOD16 only) - replaces XOValueDisplay in the same cell whenever the browsed slot's own
	getXOBrowsedType() is XO_TYPE_GATE (see each Widget's own step(), mirrors X8Widget's knob/
	button morph). Plain TransparentWidget, zero interactivity - this is a monitor, not a control.
	Threshold is a plain 5.f on the real Host voltage (no X8-style dual-convention concern here,
	since this always reads a real port's real voltage directly, never an Expander's own raw
	0..1 knob - see CLAUDE.md's pitfall about that other case).
*/
struct XOGateIndicator : TransparentWidget
{
	Module *module = nullptr;
	int channel = 0;
	widget::SvgWidget *cap = nullptr;
	// Same square-light-inside-a-cap look as X8ValueButton (X8Common.hpp), same fixed size - no
	// scaling of any kind, per Dieter's explicit instruction that this cap must render exactly
	// like the input modules' own cap.
	static constexpr float LIGHT_SIZE_MM = 4.572f;

	XOGateIndicator()
	{
		cap = new widget::SvgWidget();
		cap->setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SquareButton.svg")));
		addChild(cap);
		box.size = cap->box.size; // matches the loaded SVG's own physical size exactly - same
			                       // convention as X8ValueButton, unscaled. Each Widget's own
			                       // construction code must position this box CENTERED on the
			                       // row's own center point (not the panel rect's top-left),
			                       // since box.size no longer matches that rect's own size.
	}

	void drawThemeFrame(const DrawArgs &args, float style)
	{
		// Background is the plain panel/strip color (X_STRIP_BG_*, XShared.hpp) - same as
		// XOButtonCover's own cell (see its comment for why: this family's panel art has no
		// separate "digital display" cell at all, the numeric readout just draws over the plain
		// background) - needs to match the always-visible cover surrounding it with no visible
		// seam.
		NVGcolor background, frame;
		switch ((int) style)
		{
			case STYLE_DARK:   background = X_STRIP_BG_DARK;   frame = X_FRAME_DARK;   break;
			case STYLE_BRIGHT: background = X_STRIP_BG_BRIGHT; frame = X_FRAME_BRIGHT; break;
			default:           background = X_STRIP_BG_ORANGE; frame = X_FRAME_ORANGE; break; // STYLE_ORANGE
		}
		// Exactly X8ValueButton's own fixed frame size/radius (X8Common.hpp) - same cap, same
		// frame, full parity with the input modules, independent of whatever real panel cell this
		// indicator happens to be centered over.
		float w = mm2px(9.144f), h = mm2px(9.144f), r = mm2px(1.852f);
		float x = box.size.x / 2.f - w / 2.f, y = box.size.y / 2.f - h / 2.f;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, x, y, w, h, r);
		nvgFillColor(args.vg, background);
		nvgFill(args.vg);
		nvgStrokeWidth(args.vg, mm2px(0.3f));
		nvgStrokeColor(args.vg, frame);
		nvgStroke(args.vg);
	}

	void draw(const DrawArgs &args) override
	{
		XOExpanderInterface *expander = module ? dynamic_cast<XOExpanderInterface*>(module) : nullptr;
		drawThemeFrame(args, expander ? expander->getXOStyle() : STYLE_ORANGE);
		Widget::draw(args); // draws the cap child, simply centred - no offset/scale needed
	}

	void drawLayer(const DrawArgs &args, int layer) override
	{
		if (layer != 1)
		{
			Widget::drawLayer(args, layer);
			return;
		}
		XOExpanderInterface *expander = module ? dynamic_cast<XOExpanderInterface*>(module) : nullptr;
		bool live = expander && expander->getXOHost() && xoChannelLive(module, channel);
		// Stretched, not raw-instantaneous - see getXOBrowsedChannelGateLit()'s own comment in
		// XOShared.hpp for why a plain live threshold compare misses most real trigger pulses.
		bool lit = live && expander->getXOBrowsedChannelGateLit(channel);
		NVGcolor onColor = expander ? expander->getXOBrowsedColor() : ORANGE;

		nvgSave(args.vg);
		if (!live)
			nvgGlobalAlpha(args.vg, 0.3f);
		float lightSize = mm2px(LIGHT_SIZE_MM);
		nvgBeginPath(args.vg);
		nvgRect(args.vg, box.size.x / 2.f - lightSize / 2.f, box.size.y / 2.f - lightSize / 2.f, lightSize, lightSize);
		nvgStrokeColor(args.vg, lit ? onColor : X_VALUE_LIGHT_UNLIT);
		nvgStrokeWidth(args.vg, mm2px(0.5f));
		nvgStroke(args.vg);
		if (!live)
			nvgGlobalAlpha(args.vg, 1.f);
		nvgRestore(args.vg);
		Widget::drawLayer(args, 1);
	}
};

// XOOutputPort (a PJ301MPort with an accent ring in the browsed slot's own color) previously
// lived here - removed 2026-07-18. The ring implied these real output jacks were themselves
// browsable XO-family candidate slots, but they're plain poly outputs a further Expander cannot
// connect to at all, so the ring was misleading (same reasoning that removed it from XR8/XR16's
// outputs). XO8/XOD8/XO16/XOD16 now use plain PJ301MPort for their real output jacks.

// Clears the remembered non-adjacent connection target (getXOConnectedHostId(), XOShared.hpp)
// only - never touches anything else, since the XO family has no engagement/binding/exclusivity
// concept at all to unwind (unlike the X-family's own equivalent, X8Common.hpp).
struct XODisconnectItem : MenuItem
{
	XOExpanderInterface *expander;
	void onAction(const event::Action &e) override { expander->disconnectXOHost(); }
};

// Adds a single "Disconnect: <Host>" item if a non-adjacent connection is currently remembered -
// omitted entirely otherwise (no dead/disabled menu clutter). Mirrors the X-family's own
// equivalent (X8Common.hpp's addXBindMenuItems(), the Disconnect half of it) - call from the
// exact same spot in each XO-family/XR widget's own appendContextMenu(), alongside the base
// Style items.
inline void addXODisconnectMenuItem(Menu *menu, Module *module)
{
	XOExpanderInterface *expander = dynamic_cast<XOExpanderInterface*>(module);
	if (!expander)
		return;
	int64_t connectedId = expander->getXOConnectedHostId();
	if (connectedId < 0)
		return;
	Module *m = APP->engine->getModule(connectedId);
	if (!m)
		return;
	// Editable Host name now generic (ExpanderBridgeInterface::getBridgeHostName(),
	// ExpanderBridge.hpp) - every Host implements it, so this is no longer XO-family-specific
	// always-slug+id text; falls back to slug+id only when no custom name is actually set.
	ExpanderBridgeInterface *bridge = dynamic_cast<ExpanderBridgeInterface*>(m);
	std::string hostName = bridge ? bridge->getBridgeHostName() : "";
	XODisconnectItem *item = new XODisconnectItem();
	item->expander = expander;
	item->text = hostName.empty()
		? string::f("Disconnect: %s #%lld", m->model->slug.c_str(), (long long) connectedId)
		: string::f("Disconnect: %s", hostName.c_str());
	menu->addChild(item);
}
