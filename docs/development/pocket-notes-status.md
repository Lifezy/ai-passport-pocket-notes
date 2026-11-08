[简体中文](pocket-notes-status.zh_CN.md) · **English**

# Pocket Notes v0.1.0: status and known issues

Updated 2026-11-09. `VERSION` identifies this initial public **source edition**.
It does not change the ESP-IDF version or retroactively rename the previously
flashed development firmware. No tag or GitHub Release is implied by this file.

## Included in v0.1.0

- Five business pages: Genshin, Codex, NAS, Home Assistant and PC monitoring.
- Phone Wi-Fi/service configuration, short-lived QR setup sessions and desktop pairing.
- Twenty-three PC metrics with automatic six-row subpages; page order and visibility.
- Phone and local brightness/idle-timeout settings; optional disabled auto screen-off.
- Complete BSP/application source, fonts/licenses, skills, host tests, API contracts
  and a page-development guide.

## Unresolved or unsupported

| Item | Current behavior or boundary |
| --- | --- |
| Left-side power-key short press | Not implemented; the public board definitions provide no confirmed readable signal/controller. Do not invent a GPIO |
| Fast input just after waking | The first wake gesture is consumed and a 500 ms post-release guard suppresses following events. Quick repeated presses can extend suppression and feel unresponsive |
| Some screens never idle off | Phone setup, login QR and Mi Home device/action flows keep the screen awake. In particular a Mi Home list remains awake even without further input |
| HA/Mi Home coverage | HA must be deployed separately and devices tested individually. Native eight-key registration in Mi Home is not implemented |
| GPU/temperature coverage | NVIDIA driver index 0 only; AMD/Intel GPU sensors, GPU selection and CPU temperature are not implemented. Missing values remain unavailable |
| Aggregate monitoring | CPU/rates average the sampling interval. Disk throughput aggregates physical disks while capacity refers to the system drive. Virtual/VPN network interfaces can double-count traffic |
| Codex fields/connectivity | Uses the recipient's own signed-in CLI/App Server and desktop network. Unsupported balance/card fields remain unavailable; no card redemption or quota-changing operation is implemented |
| Genshin compatibility | Mainland China QR flow only. Internal upstream interfaces can change or require manual security verification; international login and automatic captcha solving are not implemented |
| Storage/security | Application NVS is not encrypted. A complete merged flash can overwrite stored settings; settings preservation requires compatible partitions and the intended component update |

These are product limits or known interaction issues, not features claimed as
complete. Issues and contributions can use the symptom, reproduction steps,
firmware/ELF identity and sanitized error code; never include cookies, pairing
tokens or private QR contents.

## Problems encountered and addressed

| Problem | Change and evidence |
| --- | --- |
| Genshin QR failed in 1–2 seconds with device-check E101 | Diagnostics identified `ESP_ERR_NO_MEM` during fingerprint preparation. Bounded HTTP allocation was changed to actual request/response sizes while preserving certificate checks; the user confirmed QR login/configuration succeeded |
| PC rows were fixed and limited | Added saved metric selection, optional NVIDIA/PDH fields and six-row subpages without expanding the persisted six-row snapshot |
| Page order/visibility was fixed | Added an independent NVS preference; the user reported normal ordering/disable/restart behavior in its limited acceptance scenario |
| No screen-off or brightness controls | Added backlight idle/manual control, saved PWM brightness and a local menu with worker-side saves. The user accepted local menu entry, brightness preview/save, cancellation and disabled auto screen-off |
| Boot configuration consumed stack space | Moved the temporary 2,464-byte configuration copy to short-lived heap allocation; board peak stack/heap margins still need measurement |
| Windows build subprocess failed to start | The development PC had `cmd.exe` administrator compatibility settings. A process-only `RunAsInvoker` override enabled the build without changing global registry settings; this is an environment-specific workaround, not a normal installation requirement |

## Validation evidence and remaining checks

The last flashed development image has merged SHA-256
`58be2aca0c4a0756f3a6488d0535324305aec5cffb868d4b22c61aea2e97b937`
and matching ELF SHA-256
`5f6bf0231ba6f4f4ad9e069de6a74bee19169a517eee8a2f064ef693d44aff3d`.

| Check | Result and scope |
| --- | --- |
| Build | PASS for that development image using ESP-IDF 5.5.3 and merged-image verification |
| Host tests | PASS; 21 Windows baseline symlink cases skipped rather than counted as passes |
| Font/UI | 21,352 glyphs checked; actual LVGL host rendering and QR lifecycle regression passed with synthetic data |
| Programming | Partition match and application write-time verification passed; NVS/bootloader/partition table were not written |
| Device tests | Limited user-reported PASS for local menu, brightness preview/save, cancellation and disabled auto screen-off after leaving the menu for more than a minute |
| Serial observation | A bounded 55-second window received no lines; it did not establish a clean startup independently |

Still unverified: phone live brightness changes, restart/power-cycle persistence
for the new display preferences, endurance, current consumption, board peak
heap/stack usage, every older integration and HA device coverage. A new build
from this source has its own identity and needs separate board validation.
Publication preparation passed repository/host checks and secret scanning, but
does not create new firmware or hardware evidence.

## Contributor starting points

Use the [page checklist](pocket-notes-page-development.md),
[protocol](pocket-notes-protocol.md) and [handoff guide](pocket-notes-handoff.md).
Candidate follow-ups are a less restrictive wake-input guard, idle behavior for
Mi Home lists, additional supported sensors and measured low-power behavior.
Power-key work requires confirmed electrical information first. Preserve stable
page IDs/NVS compatibility and include reproduction/validation for a change.
