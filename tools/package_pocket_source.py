#!/usr/bin/env python3
"""Package the complete worktree source and desktop helper without local secrets.

No firmware build, Git mutation, flashing, or publication is performed.
Run from any directory; outputs always stay in this repository's build/delivery.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DESTINATION = ROOT / "build" / "delivery"
EXCLUDED_PARTS = {".git", ".agents", ".codex", ".aws", "build", "managed_components",
                  "__pycache__", ".venv", ".vscode", ".idea"}
EXCLUDED_NAMES = {"sdkconfig", "sdkconfig.old"}
HELPER_FILES = ("Codex-Assistant.cmd", "tools/codex_assistant.py",
                "tools/codex_companion.py", "tools/pc_metrics.py", "tools/pc_performance.py", "LICENSE")
MANIFEST = "SOURCE-MANIFEST.sha256"
INFO = "SOURCE-INFO.json"


def git(*arguments: str) -> bytes:
    return subprocess.check_output(["git", *arguments], cwd=ROOT)


def safe_name(name: str) -> bool:
    path = PurePosixPath(name)
    return bool(name) and not path.is_absolute() and ":" not in name and "\\" not in name and all(
        part not in ("", ".", "..") for part in name.split("/")) and not any(
        char in name for char in "\r\n\x00")


def excluded(name: str) -> bool:
    path = PurePosixPath(name)
    return (bool(set(path.parts) & EXCLUDED_PARTS) or path.name in EXCLUDED_NAMES
            or path.suffix.lower() in {".pyc", ".log", ".elf", ".map"})


def source_files() -> dict[str, bytes]:
    names = set(git("ls-files", "--cached", "--others", "--exclude-standard", "-z").decode("utf-8").split("\0"))
    files: dict[str, bytes] = {}
    for name in sorted(names - {""}):
        if not safe_name(name):
            raise ValueError("Unsafe source path")
        if excluded(name) or name in {MANIFEST, INFO}:
            continue
        path = ROOT / name
        if not path.exists():  # Respect worktree deletions.
            continue
        resolved = path.resolve(strict=True)
        if resolved != path or not resolved.is_relative_to(ROOT) or not path.is_file():
            raise ValueError(f"Source must be a regular in-repository file: {name}")
        if (path.name.lower().startswith(".env") or path.suffix.lower() in {".key", ".pem", ".p12"}
                or path.name.lower() in {"companion.token", "credentials.json", "auth.json"}):
            raise ValueError(f"Private configuration must not enter a source package: {name}")
        files[name] = path.read_bytes()
    for required in ("CMakeLists.txt", "sdkconfig.defaults", "partitions.csv", "main/pocket_app.c", *HELPER_FILES):
        if required not in files:
            raise ValueError(f"Missing required source: {required}")
    return files


def verify(path: Path) -> int:
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(set(names)) != len(names) or any(not safe_name(name) or excluded(name) for name in names):
            raise ValueError("Duplicate, unsafe, or excluded archive path")
        if archive.testzip() is not None:
            raise ValueError("Archive CRC failure")
        listed: dict[str, str] = {}
        for line in archive.read(MANIFEST).decode("utf-8").splitlines():
            digest, name = line.split("  ", 1)
            if len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest) or name in listed:
                raise ValueError("Invalid source manifest")
            listed[name] = digest
        if set(listed) != set(names) - {MANIFEST}:
            raise ValueError("Source manifest does not cover every payload file")
        for name, digest in listed.items():
            if hashlib.sha256(archive.read(name)).hexdigest() != digest:
                raise ValueError(f"Source hash mismatch: {name}")
        if json.loads(archive.read(INFO))["format"] != 1:
            raise ValueError("Unsupported package metadata")
        return len(listed)


def package(name: str, files: dict[str, bytes], metadata: dict) -> Path:
    payload = dict(files)
    if MANIFEST in payload or INFO in payload:
        raise ValueError("Reserved package metadata filename")
    payload[INFO] = (json.dumps(metadata, indent=2, ensure_ascii=True) + "\n").encode()
    payload[MANIFEST] = "".join(
        f"{hashlib.sha256(data).hexdigest()}  {filename}\n" for filename, data in sorted(payload.items())
    ).encode("utf-8")
    path = DESTINATION / name
    temporary = path.with_suffix(".zip.tmp")
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for filename, data in sorted(payload.items()):
            entry = zipfile.ZipInfo(filename, date_time=(1980, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = (0o100755 if filename.endswith(".sh") else 0o100644) << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data)
    count = verify(temporary)
    temporary.replace(path)
    print(f"PASS: {path.name}: {count} hashed payload files")
    return path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", type=Path, help="Verify an existing package's CRC, paths and SHA-256 inventory")
    args = parser.parse_args()
    if args.verify:
        print(f"PASS: {verify(args.verify)} hashed payload files")
        return
    if DESTINATION.resolve() != DESTINATION or not DESTINATION.resolve().is_relative_to(ROOT):
        raise ValueError("Delivery destination must stay inside the worktree")
    DESTINATION.mkdir(parents=True, exist_ok=True)
    files = source_files()
    files.setdefault("START-HERE.txt", (
        "AI Passport Pocket Notes - complete developer source snapshot\n\n"
        "Read docs/development/pocket-notes-handoff.md first.\n"
        "Chinese instructions: START-HERE.zh_CN.txt and docs/development/pocket-notes-handoff.zh_CN.md\n\n"
        "Extract the entire ZIP into a short writable path. Git history is excluded.\n"
        "Create a local index: git init; git add .; git switch -c codex/my-pages\n"
        "Read AGENTS.md and docs/README.md before editing. Required skills are included under skills/.\n"
        "Prepare ESP-IDF 5.5.3 / ESP32-C3 / 8 MB Flash / no PSRAM; restore declared dependencies online.\n"
        "Custom-page checklist: docs/development/pocket-notes-page-development.md\n"
        "Implemented API contracts: docs/development/pocket-notes-protocol.md\n"
        "Accepted scope and pending work: docs/development/pocket-notes-development-log.md\n"
        "Preserve page IDs and stored-data compatibility. Pages are compiled into firmware, not dynamically loaded.\n"
        "Use your own Wi-Fi, accounts and pairing. Private state, SDKs, build outputs and firmware binaries are excluded.\n"
        "Verify this ZIP: python tools/package_pocket_source.py --verify <path-to-this-zip>\n"
        "Preserve LICENSE and font licenses when forwarding.\n"
    ).encode("utf-8"))
    files.setdefault("START-HERE.zh_CN.txt", (
        "AI Passport 多页便笺——完整项目源码，开发者请先阅读\n\n"
        "详细交接说明：docs/development/pocket-notes-handoff.zh_CN.md\n\n"
        "1. 完整解压到简短可写路径，例如 C:\\Projects\\AI-Passport。不要在ZIP中开发。\n"
        "2. 本包不含Git历史。先在目录执行 git init、git add .、git switch -c codex/my-pages。\n"
        "3. 先读 AGENTS.md 和 docs/README.zh_CN.md，按说明准备ESP-IDF 5.5.3及五个必需Skill。\n"
        "4. 页面源码导览：docs/development/pocket-notes-page-development.zh_CN.md\n"
        "5. 接口规范：docs/development/pocket-notes-protocol.zh_CN.md\n"
        "6. 已完成、有限真机验收和未完成项：docs/development/pocket-notes-development-log.zh_CN.md\n\n"
        "目标是ESP32-C3、8MB Flash、没有PSRAM。页面编译进固件，不支持拖入HTML或运行时插件。\n"
        "包内包含BSP、应用、字体与授权、测试、Skill和锁文件；依赖及工具链需在新电脑恢复/安装。\n"
        "不包含真实账号凭据、Wi-Fi密码、配对令牌、NVS、构建日志、SDK或固件二进制。请配置自己的设备。\n"
        "修改后运行完整验证，保留页面ID及存储兼容；未经确认不要烧录、整片擦除、提交或发布。\n\n"
        "可复制给AI的开发任务：\n"
        "请基于此完整项目开发我的专属页面。先读AGENTS.md、docs/README.zh_CN.md及交接说明；\n"
        "保留已有修改，核对并安装必需Skill，复用BSP，按页面开发指南扩展应用，检查中文字体覆盖。\n"
        "先用模拟数据验证布局，再接入真实数据；保持原页面ID和NVS兼容，关键需求不明确再问。\n"
        "自行准备环境，运行完整验证，交付0x0合并固件和对应ELF/MAP，分别报告构建/主机/设备测试；\n"
        "完成后主动询问刷机测试，未经确认不烧录、不擦除、不提交、不推送。我的页面需求：<填写功能和数据来源>。\n\n"
        "校验原ZIP：python tools/package_pocket_source.py --verify <ZIP完整路径>\n"
        "转发请保留LICENSE和字体授权。\n"
    ).encode("utf-8"))
    metadata = {"format": 1, "baseline_commit": git("rev-parse", "HEAD").decode().strip(),
                "branch": git("branch", "--show-current").decode().strip(),
                "worktree_dirty": bool(git("status", "--porcelain")),
                "contents": "Current worktree, including uncommitted application sources; no Git history or private build state.",
                "firmware": "Not built by this packager. See docs/development/pocket-notes-development-log.md for the accepted firmware identity."}
    source = package("pocket-notes-source.zip", files, metadata)
    helper = {name: files[name] for name in HELPER_FILES}
    helper["START-HERE.txt"] = (
        "AI Passport desktop assistant: Codex allowances + Windows resource monitoring.\n"
        "Extract the entire archive into a writable directory. Close the old assistant window.\n"
        "On the existing development PC, double-click Codex-Assistant.cmd; click Start.\n"
        "This is Python source, not a standalone EXE. The launcher needs the installed ESP-IDF 5.5 Python environment.\n"
        "Alternatively: python tools/codex_assistant.py (Windows Python 3.12 with tkinter).\n"
        "Codex collection needs an installed, signed-in Codex CLI/App Server and suitable network access.\n"
        "PC metrics do not need Codex account access. Keep this window running.\n"
        "NVIDIA index-0 GPU load/temperature/VRAM use the installed driver tool; unsupported fields stay unavailable.\n"
        "After installing the matching firmware, hold DOWN and scan settings to select PC metrics across automatic six-row subpages.\n"
        "Private pairing state is generated in build/private on first start. No tokens are in this ZIP.\n"
        "Pair once using the displayed LAN address and six-digit code (120 seconds).\n"
        "To preserve the old pairing, launch from the original project directory instead of a fresh extraction.\n"
        "Never run two assistants on the same address/port 8765. Do not expose the service to the Internet.\n"
    ).encode()
    helper_info = dict(metadata, contents="Desktop helper source only; no Python runtime or existing pairing token.")
    assistant = package("pocket-notes-desktop-assistant.zip", helper, helper_info)
    sums = "".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in (source, assistant))
    (DESTINATION / "SHA256SUMS.txt").write_text(sums, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
