#!/usr/bin/env python3
"""Validate generated font descriptors, bitmap bounds and fixed UI coverage."""
from pathlib import Path
import argparse
import re

ROOT = Path(__file__).resolve().parents[1]


def coverage(source):
    maps = source.split("static const lv_font_fmt_txt_cmap_t cmaps[] =", 1)[1].split("};", 1)[0]
    points = set()
    for item in re.findall(r"\{([^{}]+)\}", maps):
        start = int(re.search(r"range_start = (\d+)", item)[1])
        length = int(re.search(r"range_length = (\d+)", item)[1])
        kind = re.search(r"\.type = (\w+)", item)[1]
        if kind == "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY":
            points.update(range(start, start + length))
        elif kind == "LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL":
            array = re.search(r"glyph_id_ofs_list = (\w+)", item)[1]
            raw = re.search(rf"{array}\[\] = \{{(.*?)\}}", source, re.S)[1]
            offsets = [int(number, 0) for number in re.findall(r"0x[0-9a-f]+|\d+", raw)]
            points.update(start + i for i, offset in enumerate(offsets) if offset or i == 0)
        elif kind == "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY":
            array = re.search(r"unicode_list = (\w+)", item)[1]
            raw = re.search(rf"{array}\[\] = \{{(.*?)\}}", source, re.S)[1]
            offsets = [int(number, 0) for number in re.findall(r"0x[0-9a-f]+|\d+", raw)]
            expected = int(re.search(r"list_length = (\d+)", item)[1])
            assert len(offsets) == expected and all(0 <= value < length for value in offsets)
            points.update(start + value for value in offsets)
        else:
            raise AssertionError(f"Unsupported font cmap: {kind}")
    return points


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update-inventory", action="store_true")
    args = parser.parse_args()
    font = (ROOT / "assets/fonts/pocket_font_16.c").read_text(encoding="utf-8")
    points = coverage(font)
    inventory = ROOT / "assets/fonts/pocket_font_16.codepoints"
    if args.update_inventory:
        inventory.write_text("".join(f"{point:04X}\n" for point in sorted(points)), encoding="ascii")
    assert points == {int(line, 16) for line in inventory.read_text(encoding="ascii").splitlines()}
    assert ord("中") in points and ord("笺") in points
    assert 0x1F600 not in points, "Known-missing negative control unexpectedly passed"
    missing = set()
    for path in (ROOT / "main").glob("pocket_*.[ch]"):
        for text in re.findall(r'"((?:[^"\\]|\\.)*)"', path.read_text(encoding="utf-8")):
            missing.update(ord(char) for char in text if ord(char) > 127 and ord(char) not in points)
    assert not missing, "Missing UI characters: " + ", ".join(f"U+{point:04X}" for point in sorted(missing))
    bitmap = font.split("glyph_bitmap[] = {", 1)[1].split("};", 1)[0]
    bitmap = re.sub(r"/\*.*?\*/", "", bitmap, flags=re.S)
    byte_count = len(re.findall(r"0x[0-9a-f]+", bitmap))
    descriptors = re.findall(r"\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)", font)
    previous = 0
    for values in descriptors:
        offset, advance, width, height, x, y = map(int, values)
        assert offset >= previous
        assert offset + (width * height * 2 + 7) // 8 <= byte_count
        previous = offset
    assert ".bpp = 2" in font and ".bitmap_format = 0" in font
    assert re.search(r"\.line_height = (\d+)", font)[1] == "21"
    print(f"Pocket font: PASS ({len(points)} mapped characters, {byte_count} bitmap bytes)")


if __name__ == "__main__":
    main()
