[简体中文](pocket-notes-page-development.zh_CN.md) · **English**

# Pocket Notes: source map and custom-page development

Read [AGENTS.md](../../AGENTS.md), the [application guide](pocket-notes.md),
the [wire contracts](pocket-notes-protocol.md), and the
[development log](pocket-notes-development-log.md) first. This document describes
the existing five-page application. Pages are compiled into firmware; there is
no runtime plugin loader, automatic provider registry or arbitrary HTML page API.

## Source map

| Layer | Maintained source | Responsibility |
| --- | --- | --- |
| Entry | `main/main.c`, `main/Kconfig.projbuild` | `CONFIG_POCKET_NOTES_APP` selects the application; the upstream demo remains available when disabled |
| Coordination | `main/pocket_app.c` | Keys, command/result queues, network worker, visible-page refresh, login and control state |
| Pure model | `main/pocket_model.[ch]` | Page IDs, six-row snapshots, rate limiting, retained values/time on failure |
| PC display selection | `main/pocket_pc_display.[ch]` | Pure metric IDs, translated labels, distinct multi-page metric parsing and formatting |
| UI | `main/pocket_ui.[ch]`, `main/pocket_setup_view.h` | The application's own LVGL cards, dots, QR overlays and setup view |
| Providers | `main/pocket_genshin.c`, `pocket_codex.c`, `pocket_nas.c`, `pocket_pc.c`, `pocket_home.[ch]` | Protocol requests and conversion into application data |
| HTTP | `main/pocket_http.[ch]` | Certificate-verified HTTPS/HTTP, bounded incremental response storage and cleanup |
| Setup | `main/pocket_setup.[ch]`, `pocket_setup_guard.[ch]`, `assets/web/pocket_setup.html` | Temporary WPA2 hotspot, physically opened web session, per-card settings, PC pairing |
| Persistence | `main/pocket_config.[ch]`, `pocket_json.h` | Versioned NVS configuration/cache, transactional partial update, flat JSON check |
| Protocol/state helpers | `main/pocket_snmp_codec.[ch]`, `pocket_confirm.[ch]`, `pocket_verification.[ch]`, `pocket_genshin_diagnostic.h` | Bounded BER, action confirmation, temporary human verification, safe error labels |
| Desktop entry | `Codex-Assistant.cmd`, `tools/codex_assistant.py` | Windows GUI for the shared allowance/resource service |
| Desktop service | `tools/codex_companion.py`, `tools/pc_metrics.py` | Read-only Codex collection, Windows counters, authenticated local API |
| USB fallback | `WiFi-Setup.cmd`, `tools/wifi_setup.py`, `tools/configure_pocket.py` | Optional local configuration; not required for routine phone setup |
| Board | `components/bsp/` | Shared display, buttons, battery and other board APIs; no application page logic |
| Assets | `assets/fonts/`, `assets/web/` | Licensed font source, generated font, exact character inventory and setup page |
| Verification | `tests/test_pocket_*`, `tests/test_*companion.py`, `tests/test_pc_metrics.py`, `tests/render_pocket_ui.c`, `tools/validate.sh` | Pure logic, fault injection, server checks and actual LVGL host rendering |

Do not move generated IDF dependencies into `main/` or commit `build/`, private
configuration, credentials, serial dumps, ELF or firmware. Source archive tooling
is [package_pocket_source.py](../../tools/package_pocket_source.py); it preserves
the complete baseline project and does not turn the application into a partial SDK.

## Execution and ownership contract

The button callback only queues a key event. The UI task owns the page/model and
handles results. The network worker owns synchronous requests and provider state;
it never accesses LVGL. UI object access holds `bsp_lvgl_lock()`, including QR
creation/deletion. Do not hold that lock during DNS, TLS, HTTP, SNMP, NVS or waits.
Settings use their own HTTP-server task and configuration mutex. The network
worker pauses business requests while setup is active. A desktop thread/server
is not a task on Passport.

Providers are synchronous worker functions. For read-only card pages, follow
`pocket_pc_refresh()` in [pocket_pc.c](../../main/pocket_pc.c):

```c
esp_err_t pocket_custom_refresh(const pocket_config_t *config,
                              pocket_snapshot_t *snapshot);
```

- Return `ESP_OK` only when the response structure is usable. Missing optional
  metrics may display `--`; malformed required structure is an error.
- `snapshot->value[0..5]` contains NUL-terminated UTF-8, at most 63 bytes each.
  Keep dates, units and text short enough for the 97-pixel value column.
- `updated_at` is the original source success time in Unix seconds. Never replace
  an old source timestamp with the device's current time to make stale data look new.
- The return value is an ESP error, not an HTTP status or remote service code.
  Preserve those codes separately for diagnostics, without logging URLs containing
  tokens, cookies, account data or QR tickets.
- The normal model-success path copies the snapshot and marks it ready. A stale
  provider result is subsequently marked stale by the UI result handler. That
  handler currently uses a computer-specific stale message: adapt it for a new
  non-computer source instead of treating this as a generic plugin callback.
- Failure retains the last successful values and time. Authentication/setup
  failures are distinct from transient networking failures.

Home Assistant controls and Genshin QR login have separate multi-step state.
Do not copy those flows into a simple metric provider. Any new control must bind
preview and confirmation to an exact target/action/value, expire it, block duplicate
submission, reject stale targets and avoid automatically retrying an accepted action.

## Add a sixth read-only page

1. Append `POCKET_CUSTOM` after `POCKET_PC` in [pocket_model.h](../../main/pocket_model.h)
   and increase `POCKET_PAGE_COUNT`. Keep IDs 0–4 stable. Existing NVS caches use
   page IDs; insertion or reordering is not a harmless UI change.
2. Add the title, six row labels and accent in [pocket_ui.c](../../main/pocket_ui.c).
   The shared six cards and page dots are reused. Titles/accents must each have
   exactly one entry per page; no implicit provider registration exists.
3. Implement `main/pocket_custom.c`, declare its entry in
   [pocket_services.h](../../main/pocket_services.h), and add the source to
   [main/CMakeLists.txt](../../main/CMakeLists.txt). Use the `pocket_` prefix so the
   font checker scans fixed strings in its `.c` and `.h` files.
4. Add an explicit dispatch branch in `network_worker()` before its Home Assistant
   fallback. Otherwise an unknown page can accidentally request the HA list.
   Do not use a numeric page index as an array bound without checking it.
5. Extend the `intervals[POCKET_PAGE_COUNT]` initializer in `ui_worker()`. Manual
   requests are currently throttled to 30 seconds per page. Changing only the
   automatic interval does not bypass this model throttle.
6. For a source with its own timestamp, extend the worker's timestamp-preservation
   condition alongside Codex/PC. Add source-specific error/stale messages if needed.
7. Decide persistence explicitly. Only Genshin, Codex and NAS are cached to Flash
   today; the `last_cached[3]`, `< POCKET_MIHOME` save/load boundaries intentionally
   exclude live controls and PC metrics. Merely increasing page count does not
   enable caching. Do not broaden those boundaries to include control state.
8. Add the new synthetic fixture in [render_pocket_ui.c](../../tests/render_pocket_ui.c),
   update wraparound expectations in [test_pocket_model.c](../../tests/test_pocket_model.c),
   and test the new protocol's malformed, missing, stale and oversized cases.
9. Add an authenticated desktop route if using the shared companion. A new
   `/v1/custom` route is a design example, not an endpoint supplied by this release.
   Keep account login credentials on the desktop; export only display fields.

Prefer the existing companion base URL/token when an additional desktop-backed
page can share the same service. If adding configuration fields, update the C
struct, `FIELDS`, validation, Python limits and relevant setup whitelists/forms.
The current configuration blob is version 1 with a fixed C layout: implement an
explicit migration before changing its size/version. Existing initialization
does not automatically migrate an old blob and never erases NVS to fix a mismatch.
Changing snapshot layout likewise requires a deliberate cache migration/version.

PC row preferences demonstrate an additive alternative: a separate NVS string key
`pc_display` keeps the old blob and snapshot byte layouts intact. Its dedicated
setup route, validation, status response and UI labels all use the same metric
IDs. Extending available metrics requires the pure ID/label table, provider
formatting, desktop optional fields, phone checkboxes and tests together. The
actual configuration code is tested with a pre-existing version-1 blob, reboot
and commit failure; do not replace this with a factory erase.

## Hardware and memory acceptance

ESP32-C3 has no PSRAM. Keep networking sequential, leave the BSP-owned buses and
ADC alone, and reuse the six UI objects rather than allocating full screens per
page. The 40 KiB LVGL pool is separate from general heap; its free space is not
proof that TLS allocations can succeed. The E101 incident in the development log
demonstrates this difference.

`pocket_http()` accepts at most 12 KiB of response data, with TX sized between
1 and 6 KiB and bounded response growth. Unknown-length data is not unbounded.
Do not shrink TLS record limits or disable certificate checks to fit a new page.
Move expensive data gathering to the desktop and return small normalized JSON.
Windows CPU readings are interval averages, not a per-process profiler.

The font covers 21,352 mapped characters, not all Unicode. Follow the
[Chinese-font guide](engineering/lvgl-chinese-fonts.md), check every new fixed
string and verify dynamic text, long labels and real LCD readability. Unsupported
characters must not be hidden as a way to pass visual acceptance.

## Verification and delivery

Run focused tests while editing and the existing complete gate before firmware
delivery, in an activated ESP-IDF 5.5.3 environment:

```text
bash tools/validate.sh
python tools/check_pocket_font.py
node tests/test_pocket_setup_web.cjs
python tools/render_pocket_ui.py --lvgl managed_components/lvgl__lvgl --zig <zig-path>
```

The Node/renderer commands are additional checks, not replacements for the gate.
The renderer needs the pinned Managed Components and Zig; pure gate checks use
the host C compiler and Python. Do not count synthetic fixtures as live data.
Report Build, Host tests, Device tests and Unverified separately. Deliver the
verified merged image for `0x0` and its matching content-addressed debug archive.
Read the [layout policy](engineering/firmware-layout.md) before any approved flash;
preserving settings normally means a compatible application-only update, not
writing the merged image across NVS. Obtain consent before flashing or publishing.

## Configurable navigation extension

`pocket_page_layout_t` and its pure parser live in `pocket_model.[ch]`. Page IDs
remain fixed provider/cache indices; the ordered/enabled view is separate. When
adding a page, extend `PAGE_IDS`, the page settings form and its contract/tests
alongside `POCKET_PAGE_COUNT`; explicitly migrate older stored lists instead of
silently discarding their order. Only the in-RAM model grows, not the serialized
snapshot or version-1 credential struct. Save/commit failure keeps the current
view; applying a validated layout selects its first enabled page. NVS, model
wraparound and actual webpage-script tests cover independent preference storage,
all-disabled rejection and single-page navigation.


## Screen controller and numeric PC pages

`pocket_screen.[ch]` is a host-testable backlight policy. BSP PRESS/RELEASE events
are queued along with semantic keys, allowing an entire waking gesture to be
consumed before application actions. Only the UI task changes the backlight;
the HTTP task queues an atomic request. Idle inhibition covers setup/QR/control
flows. Do not reuse deep-sleep preparation APIs for backlight-only blanking.
The optional timeout uses its own NVS key, independent of credentials.

PC providers now return compact `pocket_pc_data_t` alongside their six-row
snapshot. Numeric data and valid bits stay in RAM; UI-task formatting selects
the current six metrics. `pocket_pc_navigate()` walks subpages and stable primary
page IDs. `tools/pc_performance.py` supplies optional native Windows counters
and must accompany the desktop helper. Close its PDH query during server cleanup.
The boot configuration temporary copy is heap-allocated, cleared and freed to
leave room in the default 3584-byte main-task stack. Validate new units, partial
last pages, disabled-PC navigation, wake suppression and old NVS preferences.
