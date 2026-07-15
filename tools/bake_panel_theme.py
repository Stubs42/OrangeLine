#!/usr/bin/env python3
"""
Bake a <Name>Work.svg panel's TextWork texts + PanelOrange decoration into the
TextOrange/TextDark/TextBright and PanelDark/PanelBright layers.

Process (see PanelDesignGuide.md's "Baking per-theme layers" section):
  1. Clear each target layer's CONTENT (not the <g> layer element itself -
     its id/label/style attributes must survive untouched).
  2. Read every <text> in TextWork, bake each one via fontTools into a path
     at the same x/y position, filled with that theme's Text color, and
     populate TextOrange/TextDark/TextBright with the result.
  3. Copy PanelOrange's own content into PanelDark/PanelBright, recoloring
     the Frame-color stroke and DisplayFill-color fill to each theme's
     equivalent (Colors.txt), and renaming every element id (append
     -dark/-bright) so ids stay unique across the document. Path-effect
     references (#path-effectN) are left untouched since those live in the
     shared <defs> and don't change per theme.

Usage: python3 tools/bake_panel_theme.py res/<Name>Work.svg
"""
import re
import sys
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

FONT_PATH = os.path.join(os.path.dirname(__file__), "..", "res", "RobotoCondensed-Bold.ttf")

THEME_TEXT_COLOR = {
    'Orange': '#ff6600',
    'Dark': '#c4bac4',
    'Bright': '#15152b',
}
THEME_FRAME_COLOR = {
    'Orange': ('#803300', '1'),
    'Dark': ('#606060', '0.98'),
    'Bright': ('#606080', '0.98'),
}
THEME_DISPLAYFILL_COLOR = {
    'Orange': '#100600',
    'Dark': '#171717',
    'Bright': '#15152b',
}


class Baker:
    def __init__(self, font_path=FONT_PATH):
        self.font = TTFont(font_path)
        self.upm = self.font['head'].unitsPerEm
        self.glyph_set = self.font.getGlyphSet()
        self.cmap = self.font.getBestCmap()

    def _measure_advance(self, text, scale):
        total = 0.0
        for ch in text:
            if ord(ch) not in self.cmap:
                continue
            glyph = self.glyph_set[self.cmap[ord(ch)]]
            total += glyph.width * scale
        return total

    def bake_text(self, text, font_size_mm, x, baseline_y, anchor):
        scale = font_size_mm / self.upm
        total_width = self._measure_advance(text, scale)
        cur_x = x - total_width / 2.0 if anchor == 'middle' else x
        d_parts = []
        for ch in text:
            if ord(ch) not in self.cmap:
                continue
            glyph = self.glyph_set[self.cmap[ord(ch)]]
            pen = SVGPathPen(self.glyph_set)
            tpen = TransformPen(pen, (scale, 0, 0, -scale, cur_x, baseline_y))
            glyph.draw(tpen)
            d = pen.getCommands()
            if d:
                d_parts.append(d)
            cur_x += glyph.width * scale
        return " ".join(d_parts)


def extract_texts(svg_content):
    """Extract every <text> element (id, x, y, effective font-size, anchor, content) from TextWork."""
    m = re.search(r'<g\s+inkscape:groupmode="layer"\s+id="layer-text-work"[^>]*>', svg_content)
    if not m:
        raise RuntimeError("TextWork layer not found")
    inner, _end = find_balanced_g_block(svg_content, m.end())
    texts = []
    for tm in re.finditer(r'<text\b([^>]*)>(.*?)</text>', inner, re.S):
        attrs_str, text_inner = tm.groups()
        def get_attr(name, s=attrs_str):
            am = re.search(name + r'="([^"]*)"', s)
            return am.group(1) if am else None
        text_id = get_attr('id')
        outer_x, outer_y, outer_style = get_attr('x'), get_attr('y'), get_attr('style')
        tspan_m = re.search(r'<tspan\b([^>]*)>([^<]*)</tspan>', text_inner, re.S)
        if not tspan_m:
            continue
        tspan_attrs, content = tspan_m.groups()
        def get_tattr(name, s=tspan_attrs):
            am = re.search(name + r'="([^"]*)"', s)
            return am.group(1) if am else None
        tx = get_tattr('x') or outer_x
        ty = get_tattr('y') or outer_y
        tspan_style = get_tattr('style') or outer_style
        fs_m = re.search(r'font-size:([0-9.]+)px', tspan_style or '')
        font_size = float(fs_m.group(1)) if fs_m else 3.175
        anchor = 'middle' if 'text-anchor:middle' in (outer_style or '') else 'start'
        content = content.replace('&lt;', '<').replace('&gt;', '>').replace('&amp;', '&')
        texts.append({
            'id': text_id, 'x': float(tx), 'y': float(ty),
            'font_size': font_size, 'anchor': anchor, 'content': content,
        })
    return texts


def build_text_layer(baker, texts, theme, layer_id, layer_label):
    color = THEME_TEXT_COLOR[theme]
    paths = []
    for t in texts:
        d = baker.bake_text(t['content'], t['font_size'], t['x'], t['y'], t['anchor'])
        if not d.strip():
            continue
        paths.append(
            f'    <path\n       id="{t["id"]}-{theme.lower()}"\n'
            f'       style="fill:{color};fill-rule:nonzero;stroke:none"\n'
            f'       d="{d}" />'
        )
    body = "\n".join(paths)
    return (
        f'  <g\n     inkscape:groupmode="layer"\n     id="{layer_id}"\n'
        f'     inkscape:label="{layer_label}"\n     style="display:none">\n{body}\n  </g>\n'
    )


def find_balanced_g_block(content, start_search_from):
    """Given the index right after an opening (non-self-closing) <g ...> tag,
    scan forward counting nested <g> opens/closes until balance returns to zero.
    Returns (inner_text, index_right_after_the_matching_closing_tag)."""
    depth = 1
    pos = start_search_from
    tag_re = re.compile(r'<g\b[^>]*?(/?)>|</g>')
    while depth > 0:
        m = tag_re.search(content, pos)
        if not m:
            raise RuntimeError("Unbalanced <g> tags - no matching close found")
        if m.group(0) == '</g>':
            depth -= 1
        elif m.group(1) != '/':
            depth += 1
        pos = m.end()
        if depth == 0:
            return content[start_search_from:m.start()], m.end()
    raise RuntimeError("unreachable")


def get_panel_orange_block(content):
    m = re.search(r'<g\s+inkscape:groupmode="layer"\s+id="layer-panel-orange"[^>]*>', content)
    if not m:
        raise RuntimeError("PanelOrange not found")
    inner, _end = find_balanced_g_block(content, m.end())
    return inner


def recolor_for_theme(inner, theme):
    frame_color, frame_opacity = THEME_FRAME_COLOR[theme]
    fill_color = THEME_DISPLAYFILL_COLOR[theme]
    suffix = '-' + theme.lower()
    block = inner.replace('#803300', frame_color).replace('#100600', fill_color)
    if theme != 'Orange':
        block = re.sub(
            r'(stroke:' + re.escape(frame_color) + r';stroke-width:0\.3)(?!;stroke-opacity)',
            r'\1;stroke-opacity:' + frame_opacity,
            block)
    block = re.sub(r'\bid="([^"]+)"', lambda m: f'id="{m.group(1)}{suffix}"', block)
    return block


def replace_layer_block(content, layer_id, new_inner_block):
    """Replace an existing (self-closing or populated) layer <g> by id, keeping only its
    own opening/closing tags out of the replacement (new_inner_block supplies both)."""
    open_re = re.compile(r'<g\s+inkscape:groupmode="layer"\s+id="' + re.escape(layer_id) + r'"[^>]*?(/?)>', re.S)
    m = open_re.search(content)
    if not m:
        raise RuntimeError(f"layer {layer_id} not found")
    if m.group(1) == '/':
        return content[:m.start()] + new_inner_block + content[m.end():]
    _inner, end_idx = find_balanced_g_block(content, m.end())
    return content[:m.start()] + new_inner_block + content[end_idx:]


def process_file(path):
    baker = Baker()
    with open(path, encoding="utf-8") as f:
        content = f.read()

    texts = extract_texts(content)
    for theme, layer_id, layer_label in [
        ('Orange', 'layer-text-orange', 'TextOrange'),
        ('Dark', 'layer-text-dark', 'TextDark'),
        ('Bright', 'layer-text-bright', 'TextBright'),
    ]:
        block = build_text_layer(baker, texts, theme, layer_id, layer_label)
        content = replace_layer_block(content, layer_id, block)

    panel_orange_inner = get_panel_orange_block(content)
    for theme, layer_id, layer_label in [
        ('Dark', 'layer-panel-dark', 'PanelDark'),
        ('Bright', 'layer-panel-bright', 'PanelBright'),
    ]:
        recolored = recolor_for_theme(panel_orange_inner, theme)
        block = (
            f'  <g\n     inkscape:groupmode="layer"\n     id="{layer_id}"\n'
            f'     inkscape:label="{layer_label}"\n     style="display:none"\n'
            f'     sodipodi:insensitive="true">{recolored}</g>\n'
        )
        content = replace_layer_block(content, layer_id, block)

    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"Baked {path}: {len(texts)} texts, PanelDark/PanelBright recolored from PanelOrange.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 tools/bake_panel_theme.py res/<Name>Work.svg")
        sys.exit(1)
    process_file(sys.argv[1])
