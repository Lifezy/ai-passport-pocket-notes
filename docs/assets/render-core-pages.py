#!/usr/bin/env python3
"""Render README examples from the real LVGL UI using non-private fixture data."""
import argparse
from pathlib import Path
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]


def png_from_ppm(source, destination):
    data = source.read_bytes()
    header = b"P6\n240 320\n255\n"
    assert data.startswith(header) and len(data) == len(header) + 240 * 320 * 3
    pixels = data[len(header):]
    rows = b"".join(b"\0" + pixels[y * 720:(y + 1) * 720] for y in range(320))
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)
    destination.write_bytes(b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", 240, 320, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lvgl", type=Path, required=True)
    parser.add_argument("--zig", type=Path, required=True)
    args = parser.parse_args()
    output = ROOT / "build/readme-core-pages"
    output.mkdir(parents=True, exist_ok=True)
    fixture = (ROOT / "tests/render_pocket_ui.c").read_text(encoding="utf-8")
    old = '{"75%", "11/07 19:00", "42%", "11/10 08:00", "12.50", "2 / 11/10"}'
    assert old in fixture
    # Optional balance/card data must not imply unsupported account capabilities.
    fixture = fixture.replace(old, '{"75%", "11/09 19:00", "42%", "11/12 08:00", "--", "--"}', 1)
    fixture = fixture.replace("布局测试，非实时数据", "示例数据")
    source = output / "readme_fixture.c"
    source.write_bytes(fixture.encode("utf-8"))
    lvgl = args.lvgl.resolve()
    executable = output / "render.exe"
    arguments = ["-std=c11", "-O1", "-UNDEBUG", "-DLV_CONF_INCLUDE_SIMPLE",
        "-I" + str(ROOT / "tests/lvgl_host"), "-I" + str(ROOT / "main"),
        "-I" + str(lvgl), "-I" + str(lvgl / "src")]
    arguments += [str(path) for path in sorted((lvgl / "src").rglob("*.c"))]
    arguments += [str(source)] + [str(ROOT / name) for name in [
        "main/pocket_model.c", "main/pocket_ui.c", "main/pocket_pc_display.c", "assets/fonts/pocket_font_16.c"]]
    arguments += ["-o", str(executable)]
    response = output / "compile.rsp"
    response.write_bytes(("\n".join('"' + item.replace("\\", "/") + '"' for item in arguments)).encode("utf-8"))
    subprocess.run([str(args.zig.resolve()), "cc", "@" + str(response.resolve())], check=True)
    subprocess.run([str(executable), str(output), str(ROOT / "assets/fonts/pocket_font_16.codepoints")], check=True)
    for page, name in [(0, "genshin"), (1, "codex"), (2, "nas"), (4, "pc")]:
        png_from_ppm(output / f"page-{page}.ppm", ROOT / f"docs/assets/pocket-notes-{name}.png")
    print("Four core README screens rendered from the real UI; synthetic data only.")


if __name__ == "__main__":
    main()
