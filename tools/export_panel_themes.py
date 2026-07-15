#!/usr/bin/env python3
"""
Export <Name>Orange.svg / <Name>Dark.svg / <Name>Bright.svg from a finished
<Name>Work.svg, per the visibility matrix in PanelDesignGuide.md's "SVG layer
structure" section: each export shows only that theme's Background/Panel/Text
layers, everything else (including TextWork/Controls) set to display:none.

Usage: python3 tools/export_panel_themes.py res/<Name>Work.svg
"""
import re
import sys
import os

LAYER_IDS = [
    'layer-bg-bright', 'layer-panel-bright', 'layer-text-bright',
    'layer-bg-dark', 'layer-panel-dark', 'layer-text-dark',
    'layer-bg-orange', 'layer-panel-orange', 'layer-text-orange',
    'layer-text-work', 'layer-controls',
]

THEME_VISIBLE_LAYERS = {
    'Orange': {'layer-bg-orange', 'layer-panel-orange', 'layer-text-orange'},
    'Dark': {'layer-bg-dark', 'layer-panel-dark', 'layer-text-dark'},
    'Bright': {'layer-bg-bright', 'layer-panel-bright', 'layer-text-bright'},
}


def set_layer_visibility(content, visible_ids):
    out = content
    for lid in LAYER_IDS:
        vis = 'inline' if lid in visible_ids else 'none'
        out = re.sub(
            r'(<g\s+inkscape:groupmode="layer"\s+id="' + re.escape(lid) + r'"[^>]*?style=")display:[a-z]+',
            r'\1display:' + vis,
            out, count=1)
    return out


def export_theme(work_path, theme):
    with open(work_path, encoding="utf-8") as f:
        content = f.read()
    base_dir = os.path.dirname(work_path)
    base_name = os.path.basename(work_path)
    assert base_name.endswith("Work.svg"), f"expected a *Work.svg file, got {base_name}"
    stem = base_name[: -len("Work.svg")]
    out_name = f"{stem}{theme}.svg"
    out_path = os.path.join(base_dir, out_name)

    out = set_layer_visibility(content, THEME_VISIBLE_LAYERS[theme])
    out = re.sub(r'sodipodi:docname="[^"]*"', f'sodipodi:docname="{out_name}"', out, count=1)

    with open(out_path, "w", encoding="utf-8") as f:
        f.write(out)
    return out_path


def process_file(work_path):
    for theme in ('Orange', 'Dark', 'Bright'):
        out_path = export_theme(work_path, theme)
        print(f"wrote {out_path}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 tools/export_panel_themes.py res/<Name>Work.svg")
        sys.exit(1)
    process_file(sys.argv[1])
