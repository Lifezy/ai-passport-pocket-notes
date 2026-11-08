#!/usr/bin/env python3
"""Compile and run the actual LVGL UI on a host, using synthetic fixtures."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lvgl", type=Path, required=True)
    parser.add_argument("--zig", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build/ui-preview")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    source = args.lvgl.resolve()
    executable = args.output / "render_pocket_ui.exe"
    arguments = ["-std=c11", "-O1", "-UNDEBUG", "-DLV_CONF_INCLUDE_SIMPLE", "-I" + str(ROOT / "tests/lvgl_host"),
                 "-I" + str(ROOT / "main"), "-I" + str(source), "-I" + str(source / "src")]
    arguments += [str(path) for path in sorted((source / "src").rglob("*.c"))]
    arguments += [str(ROOT / path) for path in ["tests/render_pocket_ui.c", "main/pocket_model.c",
                                               "main/pocket_ui.c", "main/pocket_pc_display.c", "assets/fonts/pocket_font_16.c"]]
    arguments += ["-o", str(executable)]
    response = args.output / "compile.rsp"
    response.write_text("\n".join('"' + value.replace('\\', '/') + '"' for value in arguments), encoding="utf-8")
    subprocess.run([str(args.zig.resolve()), "cc", "@" + str(response.resolve())], check=True)
    subprocess.run([str(executable.resolve()), str(args.output.resolve()),
                    str(ROOT / "assets/fonts/pocket_font_16.codepoints")], check=True)


if __name__ == "__main__":
    main()
