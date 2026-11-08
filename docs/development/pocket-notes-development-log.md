[简体中文](pocket-notes-development-log.zh_CN.md) · **English**

# Pocket Notes development log

This log records the implementation milestones and acceptance boundaries of this
derivative application. It is separate from the upstream release changelog.
For ongoing work, start with the [source/page guide](pocket-notes-page-development.md)
and [implemented interfaces](pocket-notes-protocol.md). Development continues on
2026-11-08, on `feature/custom-application`; the application changes remain
uncommitted. Preserve the worktree when continuing.

## 2026-11-08: configurable PC metrics

Added GPU load, Celsius temperature and VRAM use through the installed NVIDIA
driver's read-only index-0 query. CPU/RAM remain usable when GPU fields are
unsupported, missing or time out. Unsupported fields are unavailable rather than
zero; an old temperature is not silently reused. No hardware-monitor service,
extra Python package or privileged sensor driver is installed. The local RTX
3060 Ti returned all three GPU metrics; this is a computer-side check, not board
acceptance. Other GPU vendors and multiple-adapter selection remain unimplemented.

The phone setup page now selects one to six of nine metrics. It defaults to CPU,
GPU load, GPU temperature, RAM, system-drive use and connection state. The new
`/pc-display` route uses the existing private session checks. A separate NVS key
preserves the old credential blob and cached snapshot layouts. Unselected rows
are hidden and restored when switching to other pages. No Passport task or
concurrent network request is added. Default and alternate-layout rendering,
selection bounds, GPU unknown/timeout behavior and old-settings compatibility
have targeted regressions. Build evidence and the candidate identity are recorded
in the generated local `build/pc-custom-validation.txt`; limited user acceptance
is recorded below. The older accepted firmware identity later in this log is the
earlier Genshin memory-fix checkpoint, not this candidate.

Candidate validation: Build PASS; Host tests PASS; Device tests PASS, limited to
the user-confirmed three-item configuration and display.
Merged SHA-256: `98b2b35372d2b3647e2e9faddac876a054def1c8e6d751e50c4e6adc5a63af49`.
Matching ELF SHA-256: `0bb08f1d28619638109f3367a0ad0afb3fa78b92714f7ed5583429ed219cd317`.
The complete gate and actual LVGL rendering passed. The optimized host renderer
explicitly enables assertions. Extra headless Edge attempts timed out and are
not browser acceptance; the shipped web-script harness passed. Long-term and
power-cycle persistence remain separate checks.

After explicit approval on 2026-11-08, COM8's live partition table matched this
bundle. The 3,170,752-byte application was written at `0x10000` and write-time
verification passed, followed by a reset command. Only sectors `0x10000`–`0x316fff`
were erased; NVS, PHY, bootloader and partition table were not written. No
rebuild or full-chip erase was performed. Programming is PASS. The user then
confirmed that selecting three metrics resulted in exactly three displayed rows
and reported no other issues so far. This is a limited device PASS, not individual
verification of GPU values, all older integrations or endurance.
The local device record is `build/device-test-pc-custom.json`.

### Follow-up request: more metrics and PC subpages

Implemented by the expanded-monitoring increment below: a 23-item catalog,
selected metrics grouped into six-row PC views (items 1–6, 7–12, and so on),
visible page indicators and persistent selection. Existing selections and main
page IDs stay compatible, with UI objects and the network worker reused.
CPU/RAM use native Windows APIs, additional frequency/rate readings use PDH,
and GPU readings use the NVIDIA driver. Missing sensors remain unavailable.
The earlier six-item limit is historical.

### Follow-up request: screen-off and wake

Implemented by the backlight-only idle/wake increment below: the default idle
timeout is 60 seconds, with phone controls for disabled/30/60/120/300 seconds
and manual entry. Any key wakes the screen; the first complete gesture is
consumed to avoid accidental actions. Holding UP manually turns it off outside
Mi Home, whose existing refresh action is retained. Current-page refresh
continues, with no deep sleep. Host state-machine regression passed; reliable
waking, retained settings and actual power consumption require device checks.

### Follow-up request: page order and visibility

Implemented with the limited device acceptance recorded below. Order and
visibility are stored independently, at least one page stays enabled, navigation
and polling skip disabled pages, credentials remain intact and settings stay
reachable. Saving restarts at the first enabled page. PC subpages stay contiguous
at the PC business-page position without adding persisted business-page IDs.
Power-cycle persistence and endurance require separate checks.

## Accepted checkpoint: 2026-11-07

| Order | Delivered change | Decision or boundary |
| --- | --- | --- |
| 1 | Complete upstream project reused; required skills and ESP-IDF 5.5.3 prepared | BSP retained; an independent card UI replaces the demo shell |
| 2 | Genshin, Codex, QNAP and Home Assistant pages | Genshin adapts the supplied reference implementation; the NAS uses read-only SNMPv2c; Codex uses the computer's existing login/proxy |
| 3 | Phone-based private setup | Password-protected hotspot, two QR views, per-card settings, six-digit computer pairing; USB is optional |
| 4 | Fifth page: Windows PC resources | Shares the existing companion address/token; CPU, physical RAM, system drive and uptime; no added Passport task |
| 5 | QR-login diagnostics | Failure stage separated from HTTP, upstream and ESP errors; the observed fingerprint failure was E101 (`ESP_ERR_NO_MEM`) |
| 6 | Bounded HTTP memory correction | TX allocated for actual request size, response allocated/grown to actual content; no TLS-security weakening |
| 7 | User-approved application update and acceptance | User confirmed that the Genshin QR appeared and configuration succeeded |
| 8 | Developer handoff | Bilingual source map, custom-page checklist, wire contracts, this log, and clean source/helper packaging |

The GUI entry remains `Codex-Assistant.cmd`; its current heading describes both
allowance and computer-resource synchronization. Existing users should close the
old process and reopen it from the same project directory to retain pairing.
Starting an extracted copy creates separate local pairing state.

Home Assistant was selected as the broad-coverage Xiaomi integration, but has
not been installed or accepted on this user's hardware. The supplied deployment
files are preparation only. Emulating a Jiuhao eight-key switch as a device that
can be added to Mi Home is not implemented. The actual switch model remains
unconfirmed; a catalog candidate is not proof of compatible authentication.

## E101 investigation and correction

The user observed failure within 1–2 seconds of starting QR login while NAS and
Codex refreshed. Stage diagnostics identified device-fingerprint allocation
failure. Bounded serial observation captured no matching lines, so it did not
provide heap peaks or establish that the network worker was healthy. Anonymous
computer-side requests succeeded, but did not prove the board had sufficient RAM.

The HTTP path previously reserved a 6 KiB TX buffer and roughly 12 KiB of response
space even for a short reply. It now sizes TX between 1 and 6 KiB, uses known
content length where available, and grows unknown-length responses from 1 KiB
up to the unchanged 12 KiB limit. Reads account for already-prefetched bytes;
allocation failures and truncated/oversized replies remain errors. Certificate
verification and TLS record limits were retained. Fault-injection host tests
exercise the actual HTTP source, including tiny replies, streaming, exact limits,
truncation and allocation failures.

An earlier, separate LVGL rendering check had increased its pool from 32 to
40 KiB for QR creation/deletion. LVGL-pool free space is not general/TLS heap
headroom. The HTTP fix addresses a different allocation budget. Do not infer
board heap peaks from host-renderer results.

## Accepted firmware identity

| Artifact | SHA-256 |
| --- | --- |
| Merged image for `0x0` | `0e57c534f35868350888550db78b56bc3fd0a3af4e540fa7c0e1ad519d83dcad` |
| Matching ELF | `4402e4ba26824875fd5a6dc5c77c22ef4bb028d18c41ff884503a84fdc1482a9` |
| Application image | `a65b1ca10f05433968aa859fd53a599437225d5e4eba2b7a5141a9d43f03ab94` |

Local outputs are under ignored `build/`: `FoloToy-AI-Passport-full.bin`,
`pocket-notes-firmware.zip`, and the content-addressed debug archive
`firmware/<merged-sha256>/`. Do not substitute an ELF from another build.
The app image is 3,166,096 bytes at `0x10000` in the default 8 MB layout.

For the accepted update, the live partition table matched the verified bundle.
Only the approved application sectors `0x10000`–`0x314FFF` were written and
write-time verification passed. No full-chip erase, NVS/PHY/bootloader/table
write or private settings dump occurred. A merged image spanning settings is
not the routine update method for retaining NVS. Read the
[layout and stored-data policy](engineering/firmware-layout.md) before flashing.

## Validation checkpoint

```text
Build: PASS
Host tests: PASS
Device tests: PASS (limited to user-confirmed Genshin QR login and configuration)
Unverified: long-run stability, peak board heap/stack, automatic Wi-Fi reconnect,
all displayed values and individual PC/NAS metrics, and Xiaomi control.
```

The complete gate passed for the identified firmware. The Windows baseline had
21 platform-dependent symlink-test skips; these are not passing tests. Additional
checks covered setup-page behavior and actual LVGL rendering of all five pages,
the full font inventory and repeated QR transitions. Synthetic host fixtures are
not live integration acceptance. The user also reported NAS/Codex refresh working;
that does not establish every metric's correctness.

Local evidence: `build/validation-report.txt` is the build-time report;
`build/device-test-memoryfix.txt` contains the later user acceptance. The source
inventory at build time is retained with the historical firmware package. The
documentation/package-only handoff preserved runtime source. The subsequent GPU
extension has separate build/device evidence; earlier board acceptance does not
transfer to it. The newest candidate's inventory is `build/source-sha256.txt`.

## Source handoff and next work

Run `python tools/package_pocket_source.py` to create complete source and separate
desktop-helper ZIPs under `build/delivery/`. The package records the baseline
commit, dirty branch and a per-file SHA-256 inventory; it includes uncommitted
application sources, fonts/licenses, tests and required skills. It excludes Git
metadata, generated dependencies, local configuration and build/private state.
It neither builds firmware nor claims that the new documentation was in the
previous firmware build.

After extracting the complete source into a new directory, initialize a local
Git repository (`git init`, then `git add .`) before using repository checks that
enumerate Git files. Follow [environment setup](engineering/environment-setup.md)
and [build/test](engineering/build-and-test.md); dependencies are restored by IDF,
not copied from a development machine. Verify any downloaded package using its
SHA-256 inventory. This source bundle does not include credentials or pairing.

Priorities for future work are real-board endurance/memory measurement, metric
accuracy against source systems, then separately authorized HA integration. New
pages follow the explicit model/UI/worker/refresh/cache/config/test checklist.
Do not reorder persisted page IDs or enlarge NVS blobs without migration. No
commit, remote push, community publication or new flash is part of this handoff.

## 2026-11-08: page order/visibility implementation

Implemented the settings card with up/down moves and per-page enable checkboxes,
a private `/page-layout` route, independent NVS preference and visible-page model
navigation/dots. At least one page is required; a single enabled page wraps to
itself. Saved credentials, PC metrics selection and cached snapshot formats are
unchanged. Startup selects the first enabled page, disabled pages are skipped
by visible-page polling, and settings remain reachable. Host tests cover parsing,
navigation, invalid input, old settings, reboot restoration and commit failure;
actual web-script tests cover ordering, restored checkboxes and all-disabled
rejection. Real LVGL rendering covers filtered dots. Device tests: PASS, limited
to the user's report of no observed abnormalities in the requested sorting/
visibility acceptance scenario. At that checkpoint, screen-off and more PC metrics/subpages remained
future work.

Build/Host tests: PASS for the complete gate. Candidate merged SHA-256:
`cb1b67f15b0fd7b959240260e80658786b7a615c3fce622539ad6912ae4d4f85`.
Matching ELF: `afd7bbd110e18f9882960367e153079182d43113b34aebde46f2c4176dcc5394`.
See local `build/page-layout-validation.txt` and the matching debug bundle.
After explicit user approval, COM8's partition table matched the bundle and the
3,176,528-byte application was written at `0x10000` with successful write-time
verification and a reset command. Only application sectors `0x10000`–`0x317fff`
were erased; NVS, PHY, bootloader and partition table were not written. No rebuild
or chip erase occurred. Programming: PASS. In response to the sorting, disabling,
restart and settings-retention test request, the user reported no abnormalities
so far. This is user-reported acceptance, not instrumented validation of each
step, power-cycle persistence, all integrations or endurance. Evidence:
`build/device-test-page-layout.json`. Previous acceptance was not used as evidence
for this increment.


## 2026-11-08: backlight idle/wake and expanded PC subpages

Implemented all three explicitly requested remaining features: configurable idle
blanking/manual blanking/consumed wake gesture, a 23-metric catalog, and automatic
six-row PC subpages. Existing page ordering/enabling, credential layout, six-row
cache layout and pairing survive. No new task, PSRAM assumption or deep-sleep
path is introduced. Tests include wake press/long/release/double suppression,
NVS timeout preservation/failure, seven-item navigation and four-page rendering.
Real Windows/NVIDIA extended metrics were read successfully on the host, including
PDH rates; this does not establish board behavior or metric accuracy against
instruments. The research result is in [eight-key research](mihome-eight-key-research.md).
No native Mi Home eight-key emulator was implemented. Build and device results
for this increment are recorded separately in `build/expanded-validation.txt`.

This candidate: Build PASS; Host tests PASS; Device tests NOT RUN. The complete
gate passed after correcting the display-settings save-function mismatch.
Python suites ran 129 tests, with 21 baseline Windows symlink cases skipped;
those skips are not counted as passing tests. Additional web-script and actual
LVGL render checks passed; all 21,352 font characters are covered, with 17,240
bytes left in the host-rendered LVGL pool. Real Windows/NVIDIA/PDH readings
passed separately, without claiming board acceptance or instrument accuracy.
Merged SHA-256: `66960f245b2b3eb2915eccfa023f0b1934854f1ea7193e837fb8850686b4265d`.
Matching ELF SHA-256: `75b4b4202df19cc530cceeaca73a386f496eaa538fe641382ffd754b6b1af253`.
Application size: 3,183,040 bytes at `0x10000`.
The merged image is for `0x0`; stored-data policy still applies. No new flash
was performed. Board backlight/wake gestures, retained settings, subpage
navigation, original integrations and long-term RAM/stack margins are unverified.
The earlier page-layout device PASS belongs to its own `cb1b67f1` checkpoint.
GY-BT8 open hardware is an additional native-Mi-Home research lead requiring
extra hardware; electrical documentation and Passport integration remain
unverified. No native eight-key emulation was added. Existing source changes,
branch and pairing state are retained; no commit or push was performed.

## 2026-11-08: authorized expanded-monitoring/screen-off application update

After the user connected the device and explicitly approved this candidate,
COM8 was identified as the sole native-USB ESP32-C3 target. Its live partition
table matched the verified `66960f245b2b3eb2915eccfa023f0b1934854f1ea7193e837fb8850686b4265d` bundle. The existing
3,183,040-byte application was written at `0x10000` without rebuilding; write-time
hash verification passed and a hard-reset command was sent. Only application
sectors `0x10000–0x319fff` were erased. NVS, PHY,
bootloader and partition table were not written; no full-chip erase or private
settings dump occurred. Initial sandbox access was denied before connecting;
authorized native USB access then succeeded without system-permission changes.

The bounded 55-second read-only serial observation received
0 lines and recorded sanitized event categories
`[]`. The port is closed and no extra reset was performed.
Missing ready events cannot establish successful startup, and no captured error
is not proof of an error-free run. Flash programming: PASS. Device tests: NOT RUN
(feature acceptance awaiting user observation). Board screen-off/wake, phone
timeout settings, PC subpages, original integrations/settings, power-cycle
persistence and endurance remain unverified. Records:
`build/device-test-expanded.json` and `build/device-test-expanded.txt`.

## 2026-11-08: brightness setting and left power-key investigation

The user requested short-press screen-off/on through the left power key and a
phone brightness control. Implemented the verified BSP capability: persistent
10%–100% PWM brightness, immediate UI-task application, no restart, and saved
brightness restored on wake. No new task, dependency, BSP driver or partition
change. Host regression covers decimal/range validation, optional-key fallback,
restart restoration, commit failure and original credential preservation. The
actual phone-script harness covers slider labels, no writes during dragging,
restoration, repeat saves without ending the session, and invalid/save failures.

Left-power-key handling remains unimplemented. Local specifications, hardware
guide, pin map and button BSP were checked, then the official upstream hardware
guide and pin map were checked. They expose three ADC function keys and an
independent hardware power key, with no readable side-key signal/controller
defined. Do not invent a pin or reuse an occupied bus. Gitee was inaccessible
through the web tool; the official FoloToy GitHub mirror was accessible. This
does not establish an electrical impossibility; confirmed schematics or a
manufacturer-specified readable interface are still needed. The user has asked
for project-based investigation rather than relying on their recollection.

The user then requested on-device brightness and screen-off timeout controls in
the same increment. Added a double-OK entry on normal page overviews, a pure
select/edit/save/cancel state machine, reusable existing UI rows and worker-side
single-field NVS saves. Brightness previews without writing each step; cancel
and save failure restore the stored value. The menu keeps the screen awake and
does not hijack QR login, phone setup or Mi Home action flows. Host fixtures
include menu boundaries, repeat/double key events, unchanged values, busy/save
failure/cancel, real LVGL menu screens and return to ordinary pages.

Candidate validation: Build PASS; Host tests PASS; Device tests NOT RUN.
The complete gate passed. Windows baseline symlink tests include 21 skips,
which are not counted as passing tests. The actual web script passed and
LVGL menu/old-page/QR-cycle rendering passed, including all 21,352 font
characters; the final host LVGL pool had 17,280 bytes free. These are not
physical-device or current-consumption measurements.
Merged SHA-256: `58be2aca0c4a0756f3a6488d0535324305aec5cffb868d4b22c61aea2e97b937`.
Matching ELF SHA-256: `5f6bf0231ba6f4f4ad9e069de6a74bee19169a517eee8a2f064ef693d44aff3d`.
Application: 3,187,664 bytes at `0x10000`;
merged image starts at `0x0`. Local report: `build/display-settings-validation.txt`.
This candidate has not been flashed. Pending checks include physical backlight
changes, menu/wake gestures, phone settings, persistence, original integrations
and endurance. Left-power-key screen toggling is not implemented because its
readable hardware interface remains undefined. No commit or push was made.

## 2026-11-09: authorized screen-settings application update

The user connected the device and explicitly approved the screen-settings
candidate. COM8 was the sole ESP32-C3 native-USB target; the live partition table
matched the verified `58be2aca0c4a0756f3a6488d0535324305aec5cffb868d4b22c61aea2e97b937` bundle. The delivered
3,187,664-byte application was written at `0x10000` without rebuilding.
Write-time hash verification passed and a hard-reset command was sent. Only
application sectors `0x10000–0x31afff` were erased;
NVS, PHY, bootloader and partition table were not written. No full-chip erase,
private-settings dump or system-permission change was performed.

The bounded 55-second read-only observation received 0
lines, with sanitized event categories `[]`. The serial port
is closed and no extra reset was performed. Missing ready events do not prove
successful startup; absence of captured errors is not proof of a clean run.
Flash programming: PASS. Device tests: NOT RUN (awaiting user observations).
Board brightness, local menu controls/preview/cancel/save, disabled automatic
screen-off, phone updates, persistence and older integrations remain unverified.
The left power-key short-press request remains unimplemented, as documented
above. Records: `build/device-test-display-settings.json` and
`build/device-test-display-settings.txt`. No commit or push was performed.

### User-reported screen-settings acceptance, 2026-11-09

The user reported that all requested checks were normal: double-OK menu entry,
brightness preview/save, long-OK cancel and disabled automatic screen-off after
leaving the menu and waiting more than one minute. Device tests: PASS only for
these user-observed scenarios on the `58be2aca` candidate. Phone live updates,
restart/power-cycle persistence, older integrations, endurance and measured
current remain unverified. The device record and delivery package were updated
without changing or reflashing firmware.
