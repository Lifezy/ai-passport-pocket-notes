[简体中文](pocket-notes-handoff.zh_CN.md) · **English**

# Developer handoff: Pocket Notes

This is the complete current worktree of the FoloToy AI Passport derivative,
including uncommitted application code, upstream BSP, fonts/licenses, required
skills, tests and documentation. It is not a runtime page plugin or a preinstalled
toolchain. Start with `START-HERE.txt` at the archive root.

## Prepare a fresh computer

1. Extract the entire archive into a short writable path, for example
   `C:\Projects\AI-Passport`. Do not develop inside the ZIP.
2. The archive excludes Git history. Create an index so repository checks and
   source packaging can enumerate the files:

   ```bash
   git init
   git add .
   git switch -c codex/my-pages
   ```

   No commit, push or upstream publication is needed to begin. `SOURCE-INFO.json`
   records the original baseline/branch and dirty-worktree status; this is a
   source snapshot rather than an upstream release tag.
3. Read [AGENTS.md](../../AGENTS.md) and the [documentation index](../README.md).
   Install ESP-IDF **5.5.3** with ESP32-C3 tools using the
   [environment guide](engineering/environment-setup.md). Dependencies are
   restored from the checked-in declarations/lockfile with network access;
   `managed_components`, build caches and the original computer's SDK are absent.
4. Check/install the five required skills from `skills/` following
   [skills/README.md](../../skills/README.md). An AI agent should perform its own
   environment checks and setup; do not copy paths or credentials from another PC.

The board is ESP32-C3, 8 MB Flash, **no PSRAM**. Use the checked-in
`sdkconfig.defaults` and `partitions.csv`; do not replace the BSP with a generic
ESP32 board template.

## Develop a custom page

Start with the [source map and page checklist](pocket-notes-page-development.md),
then the [wire contracts](pocket-notes-protocol.md) and
[development/acceptance log](pocket-notes-development-log.md).

- `main/pocket_model.[ch]`: stable page IDs, six-row snapshots and navigation.
- `main/pocket_ui.[ch]`: the application's LVGL card UI and display settings.
- `main/pocket_app.c`: button dispatch, current-page refresh and worker queues.
- `main/pocket_services.h` and `main/pocket_*.c`: data-source implementations.
- `assets/web/pocket_setup.html` and `main/pocket_setup.c`: phone settings.
- `components/bsp/`: shared board drivers; `assets/fonts/`: font source/license.

Pages are compiled into firmware; there is no dynamic HTML/plugin loader or
automatic provider registration. Append new IDs without renumbering old ones.
Extend dispatch, refresh intervals, labels/colors, page-order settings and tests
together. Existing saved navigation needs an explicit migration decision.
Keep network/storage work out of button callbacks and LVGL locks. Validate
Chinese glyph coverage and actual rendering. The complete checklist includes
cache/config layout constraints and error behavior.

For a first increment, implement a read-only page with deterministic fixture
data, render it, then connect the real data source. Use the existing card
components when extending this app; an independent derivative must still follow
the repository's mandatory UI-redesign rule. API examples named `/v1/custom`
are illustrative, not implemented endpoints.

## Validate and flash

From an activated ESP-IDF 5.5.3 shell, with the host dependencies described in
the [build guide](engineering/build-and-test.md), run:

```bash
bash tools/validate.sh --static
node tests/test_pocket_setup_web.cjs
bash tools/validate.sh
```

The complete gate builds from defaults, verifies the partition/merged image and
preserves matching ELF/MAP files. `build/FoloToy-AI-Passport-full.bin` is the
merged image for offset **0x0**. On an already configured device, verify layout
compatibility and use the matching application at its actual offset to preserve
NVS. A merged flash may overwrite settings. Follow the
[stored-data policy](engineering/firmware-layout.md); ask before flashing.
Do not erase the chip as a routine setup step.

This source handoff does not include firmware binaries or private build logs.
The previously delivered `58be2aca` image passed its build/host gate; the user
accepted local menu entry, brightness preview/save, cancellation and disabled
automatic screen-off. Fresh-machine builds, new pages, power-cycle persistence,
endurance and current consumption still need their own verification. Left-side
power-key short-press control remains unimplemented pending a confirmed readable
hardware interface. HA deployment and native Mi Home eight-key emulation are
not delivered capabilities.

## Configure your own data and verify the archive

Accounts, Wi-Fi passwords, pairing tokens, NVS, Python/IDF installations and Git
history are excluded. Each recipient must configure their own device/accounts.
The NAS default `192.168.1.250` is only a local default; replace it for the new
network. Do not expect access to the original user's services.

For the optional Windows helper, use `Codex-Assistant.cmd` after environment
setup or `python tools/codex_assistant.py` with Windows Python 3.12 and tkinter.
Codex collection requires the recipient's installed/signed-in CLI/App Server
and network access. PC monitoring uses Windows counters and optional NVIDIA
index-0 queries. Pair the new device through phone settings; old pairing state
is not included. Settings and account details are in the
[user guide](pocket-notes.md).

`SOURCE-MANIFEST.sha256` covers every archive payload; `SOURCE-INFO.json` records
snapshot provenance. Verify after extraction:

```bash
python tools/package_pocket_source.py --verify /path/to/pocket-notes-source.zip
```

An archive-level SHA-256 is supplied alongside the ZIP. Preserve `LICENSE` and
the font licenses when redistributing.
