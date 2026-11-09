#!/usr/bin/env python3
"""Generate the application's CJK font subsets and verify glyph coverage.

The application's Chinese copy lives in ``main/app/app_strings.h``. This script
turns that copy into an exact character inventory, renders one LVGL bitmap font
per size, and then reads the generated files back to prove that every requested
code point really is in the font.

Usage:
    python3 tools/gen_app_fonts.py            # regenerate + verify
    python3 tools/gen_app_fonts.py --check    # verify only, write nothing

Requires ``lv_font_conv`` on PATH (pinned in assets/fonts/README.md) and a
licensed CJK TTF containing the requested glyphs.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STRINGS_HEADER = ROOT / "main" / "app" / "app_strings.h"
OUTPUT_DIR = ROOT / "assets" / "fonts"

# Sizes must match the ones the UI selects; adding a size here means adding a
# widget that uses it, not just a bigger file.
SIZES = (16, 24)

# ASCII is deliberately NOT part of the subset: LVGL's built-in Montserrat is the
# fallback for Latin text, so the generated font only carries CJK glyphs.
ASCII_END = 0x7E

DEFAULT_FONT = "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"
DEFAULT_FONT_SHA256 = "acb6440a713d880a13a21b468ba7cd43f5a2b2934972e51be791c880730777b8"

STRING_LITERAL_RE = re.compile(r'#define\s+\w+\s+"((?:[^"\\]|\\.)*)"')
UNICODE_LIST_RE = re.compile(r"unicode_list_(\d+)\[\]\s*=\s*\{(.*?)\};", re.S)
CMAP_RE = re.compile(
    r"\{\s*\.range_start\s*=\s*(\d+)\s*,\s*\.range_length\s*=\s*(\d+)\s*,"
    r"\s*\.glyph_id_start\s*=\s*\d+\s*,\s*\.unicode_list\s*=\s*(\w+)\s*,"
    r"\s*\.glyph_id_ofs_list\s*=\s*\w+\s*,\s*\.list_length\s*=\s*(\d+)\s*,"
    r"\s*\.type\s*=\s*(\w+)\s*\}",
    re.S,
)
HEX_RE = re.compile(r"0x([0-9A-Fa-f]{1,6})")


def header_characters() -> set[str]:
    """Every character used by a UI string literal.

    The literals are plain UTF-8 text (the header is UTF-8 by contract), so they
    are used as-is; C escape sequences would have to be resolved before this
    point if the copy ever gains one.
    """
    text = STRINGS_HEADER.read_text(encoding="utf-8")
    chars: set[str] = set()
    for literal in STRING_LITERAL_RE.findall(text):
        if "\\" in literal:
            raise SystemExit(f"escape sequences are not supported in UI copy: {literal!r}")
        chars.update(literal)
    return chars


def inventory() -> list[int]:
    chars = header_characters()
    # Only non-ASCII needs a generated glyph; Latin falls back to Montserrat.
    return sorted(ord(c) for c in chars if ord(c) > ASCII_END)


def font_output(size: int) -> Path:
    return OUTPUT_DIR / f"app_cjk_{size}.c"


def generated_codepoints(path: Path) -> set[int]:
    """Code points actually mapped by the generated font.

    lv_font_conv emits either a contiguous FORMAT0 cmap or a SPARSE cmap whose
    ``unicode_list`` holds offsets from ``range_start``. Both are decoded here so
    the check reads the font's real mapping instead of trusting the converter.
    """
    text = path.read_text(encoding="utf-8")
    lists = {
        name: [int(value, 16) for value in HEX_RE.findall(body)]
        for name, body in UNICODE_LIST_RE.findall(text)
    }

    found: set[int] = set()
    for start, length, list_name, list_length, kind in CMAP_RE.findall(text):
        start_i, length_i = int(start), int(length)
        if "SPARSE" in kind:
            key = list_name.replace("unicode_list_", "")
            for offset in lists.get(key, []):
                found.add(start_i + offset)
        else:
            found.update(range(start_i, start_i + length_i))
        key = list_name.replace("unicode_list_", "")
        if "SPARSE" in kind and len(lists.get(key, [])) != int(list_length):
            raise SystemExit(f"{path.name}: unicode_list length mismatch")
    return found


def verify(size: int, wanted: list[int]) -> list[int]:
    path = font_output(size)
    if not path.is_file():
        return wanted
    have = generated_codepoints(path)
    return [cp for cp in wanted if cp not in have]


def render(size: int, wanted: list[int], font: Path) -> None:
    converter = shutil.which("lv_font_conv")
    if converter is None:
        raise SystemExit("lv_font_conv is not on PATH; see assets/fonts/README.md")

    symbols = "".join(chr(cp) for cp in wanted)
    cmd = [
        converter,
        "--font", str(font),
        "--size", str(size),
        "--bpp", "4",
        "--format", "lvgl",
        "--no-compress",
        "--lv-include", "lvgl.h",
        "--output", str(font_output(size)),
    ]
    if symbols:
        cmd.extend(["--symbols", symbols])
    subprocess.run(cmd, check=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify only, write nothing")
    parser.add_argument("--font", default=os.environ.get("APP_FONT_PATH", DEFAULT_FONT))
    args = parser.parse_args(argv)

    wanted = inventory()
    print(f"Inventory: {len(wanted)} code points from {STRINGS_HEADER.relative_to(ROOT)}")

    if not args.check:
        font = Path(args.font)
        if not font.is_file():
            raise SystemExit(f"source font not found: {font}")
        OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
        for size in SIZES:
            render(size, wanted, font)
            print(f"Generated assets/fonts/app_cjk_{size}.c")

    failed = False
    for size in SIZES:
        missing = verify(size, wanted)
        if missing:
            failed = True
            print(f"app_cjk_{size}: MISSING {len(missing)} code points:", file=sys.stderr)
            for cp in missing[:40]:
                print(f"  U+{cp:04X} {chr(cp)!r}", file=sys.stderr)
        else:
            print(f"app_cjk_{size}: coverage OK ({len(wanted)} code points)")

    if failed:
        print("Font coverage check failed.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
