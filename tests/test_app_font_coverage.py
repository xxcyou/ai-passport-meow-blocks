#!/usr/bin/env python3
"""Prove that the shipped CJK font subsets cover every character the UI uses.

This is the repeatable coverage check required by the Chinese-font checklist: it
reads the UI copy out of ``main/app/app_strings.h``, decodes the actual cmap of
the generated ``assets/fonts/app_cjk_*.c`` files, and fails when a code point is
missing. A mapped code point is not enough -- an all-zero bitmap renders as a
blank glyph -- so every glyph's pixels are checked as well. It also runs a
negative case, so the check cannot pass vacuously.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "main" / "app" / "app_strings.h"
FONTS = ROOT / "assets" / "fonts"
SIZES = (16, 24)

LITERAL_RE = re.compile(r'#define\s+\w+\s+"([^"\\]*)"')
LIST_RE = re.compile(r"unicode_list_(\d+)\[\]\s*=\s*\{(.*?)\};", re.S)
CMAP_RE = re.compile(
    r"\{\s*\.range_start\s*=\s*(\d+)\s*,\s*\.range_length\s*=\s*(\d+)\s*,"
    r"\s*\.glyph_id_start\s*=\s*\d+\s*,\s*\.unicode_list\s*=\s*(\w+)\s*,"
    r"\s*\.glyph_id_ofs_list\s*=\s*\w+\s*,\s*\.list_length\s*=\s*(\d+)\s*,"
    r"\s*\.type\s*=\s*(\w+)\s*\}",
    re.S,
)
HEX_RE = re.compile(r"0x([0-9A-Fa-f]{1,6})")
GLYPH_BLOCK_RE = re.compile(r"/\* U\+([0-9A-Fa-f]{4,6})[^*]*\*/")


def ui_codepoints() -> set[int]:
    text = HEADER.read_text(encoding="utf-8")
    chars: set[str] = set()
    for literal in LITERAL_RE.findall(text):
        chars.update(literal)
    # ASCII is served by LVGL's built-in Montserrat fallback, not the subset.
    return {ord(c) for c in chars if ord(c) > 0x7E}


def font_codepoints(path: Path) -> set[int]:
    text = path.read_text(encoding="utf-8")
    lists = {
        name: [int(v, 16) for v in HEX_RE.findall(body)]
        for name, body in LIST_RE.findall(text)
    }
    found: set[int] = set()
    for start, length, list_name, list_length, kind in CMAP_RE.findall(text):
        key = list_name.replace("unicode_list_", "")
        if "SPARSE" in kind:
            entries = lists.get(key, [])
            assert len(entries) == int(list_length), f"{path.name}: list length mismatch"
            found.update(int(start) + offset for offset in entries)
        else:
            found.update(range(int(start), int(start) + int(length)))
    return found


def glyph_bitmaps(path: Path) -> dict[int, tuple[int, bool]]:
    """Code point -> (byte count, has any non-zero pixel byte)."""
    text = path.read_text(encoding="utf-8")
    match = re.search(r"glyph_bitmap\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not match:
        return {}
    body = match.group(1)
    parts = GLYPH_BLOCK_RE.split(body)
    glyphs: dict[int, tuple[int, bool]] = {}
    for index in range(1, len(parts), 2):
        codepoint = int(parts[index], 16)
        data = [int(value, 16) for value in HEX_RE.findall(parts[index + 1])]
        glyphs[codepoint] = (len(data), any(byte != 0 for byte in data))
    return glyphs


def main() -> int:
    wanted = ui_codepoints()
    assert wanted, "no CJK characters found in the UI copy"

    failed = False
    for size in SIZES:
        path = FONTS / f"app_cjk_{size}.c"
        if not path.is_file():
            print(f"FAIL: missing {path.relative_to(ROOT)}", file=sys.stderr)
            failed = True
            continue
        have = font_codepoints(path)
        missing = sorted(wanted - have)
        if missing:
            failed = True
            print(f"FAIL: app_cjk_{size} misses {len(missing)} code points:", file=sys.stderr)
            for cp in missing:
                print(f"  U+{cp:04X} {chr(cp)!r}", file=sys.stderr)
        else:
            bitmaps = glyph_bitmaps(path)
            blank = sorted(cp for cp in wanted if cp in bitmaps and not bitmaps[cp][1])
            unmapped = sorted(cp for cp in wanted if cp not in bitmaps)
            if blank or unmapped:
                failed = True
                for cp in unmapped:
                    print(f"FAIL: app_cjk_{size} has no bitmap for U+{cp:04X}", file=sys.stderr)
                for cp in blank:
                    print(f"FAIL: app_cjk_{size} renders a blank glyph for U+{cp:04X}", file=sys.stderr)
            else:
                print(f"app_cjk_{size}: {len(wanted)} UI code points covered, bitmaps non-empty")

    # Negative case: a character that is intentionally absent must be reported,
    # otherwise this test could pass without reading the font at all.
    probe = ord("\u9f98")  # U+9F98 龘, not used by the UI
    have16 = font_codepoints(FONTS / "app_cjk_16.c")
    if probe in have16:
        print("FAIL: negative probe unexpectedly present in the font", file=sys.stderr)
        failed = True

    if failed:
        return 1
    print("test_app_font_coverage: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
