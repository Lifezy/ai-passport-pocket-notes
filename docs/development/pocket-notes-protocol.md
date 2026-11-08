[简体中文](pocket-notes-protocol.zh_CN.md) · **English**

# Pocket Notes: implemented interface contracts

This is the contract implemented by the current source, not a promise of runtime
plugins or arbitrary device support. Start with the [page-development guide](pocket-notes-page-development.md).
The desktop JSON schema, NVS blob version and source revision are separate identities.

## Desktop service

Source: [codex_companion.py](../../tools/codex_companion.py),
[pc_metrics.py](../../tools/pc_metrics.py), [codex_assistant.py](../../tools/codex_assistant.py).
The GUI binds the computer's selected LAN IPv4 address on port 8765. The CLI
defaults to loopback and accepts explicit `--bind`, `--port`, `--proxy` and
`--interval` options. A non-default port must also be reflected in manual base-URL
configuration: phone six-digit pairing currently targets port 8765.

| Method/path | Authorization | Result |
| --- | --- | --- |
| `GET /v1/codex` | `Authorization: Bearer <PAIRING_TOKEN>` | Normalized allowance snapshot |
| `GET /v1/pc` | Same bearer token | Windows aggregate resource snapshot |
| `POST /v1/pair` | Explicitly generated one-time six-digit code | Display-only service token; no account login credentials |

GET paths must match exactly; query strings are not a discovery API. Missing or
incorrect bearer authorization returns 401; an authorized unknown path returns
404. Successful JSON responses carry `Cache-Control: no-store`. There is no CORS
permission for arbitrary websites, remote shell, reset-credit redemption route,
file browser or process-control API.

Example request (placeholder token only):

```http
GET /v1/pc HTTP/1.1
Host: 192.168.1.20:8765
Authorization: Bearer <PAIRING_TOKEN>
```

### Six-digit pairing

The GUI must call `begin_pairing()` through its start/new-code button first.
The code lasts 120 monotonic seconds, allows at most five redemption attempts,
and is consumed on success. Generating a new code invalidates the old one.
Before a code is generated or after it has been consumed, and for another POST
route, the server returns 501. Invalid pairing requests, expired codes and exhausted
attempts return 403. `Content-Type` must be `application/json`;
the body length must be 1–128 bytes.

```json
{"code":"123456"}
```

`123456` is a documentation fixture, not an actual pairing code. Success is:

```json
{"token":"<PAIRING_TOKEN>"}
```

The persistent token is locally generated and stored under
`build/private/companion.token`; it is not the Codex account token. The current
file format is 32–128 ASCII letters/digits/underscore/hyphen. Keep it private.
Pairing exchanges and metrics use LAN HTTP; they are not end-to-end encrypted.
Do not publish this service to the Internet. Regenerate local pairing credentials
deliberately if exposed; replacing a token file also requires updating the device.

### Codex snapshot, schema 1

```json
{
  "schema_version":1,
  "status":"ok",
  "updated_at":1794000000,
  "sample_age_seconds":10,
  "error":null,
  "data":{
    "primary":{"used_percent":25,"remaining_percent":75,"window_minutes":300,"resets_at":1794010000},
    "secondary":{"used_percent":60,"remaining_percent":40,"window_minutes":10080,"resets_at":1794500000},
    "credits":{"balance":"12.50","has_credits":true,"unlimited":false},
    "resets":{"available_count":2,"expires_at":[1794600000,1794700000],"details_complete":true}
  }
}
```

All numbers above are synthetic. Window lengths must come from the source;
`primary`/`secondary` are simultaneous windows, not the current and next period.
The existing display labels are imperfect and do not expose `window_minutes`.
Percent values are 0–100. Times are Unix seconds, displayed in China Standard
Time by this firmware. Credit balance is a string; this application does not
interpret it as a currency or token count. An unlimited flag is separate from
an unknown balance.

`data` is `null` before the first successful collection. Optional `primary`,
`secondary`, `credits`, `resets` or subfields may also be `null`; unknown is not
zero. Missing source fields must not be fabricated. Reset count is authoritative;
expiry rows are capped at 32 and are not guaranteed complete. Only
`details_complete:true` permits the device to label the earliest expiry. No reset
is automatically redeemed. `error` contains only a sanitized collection category.

The service reports `unavailable` before success, `stale` after a collection
failure or excessive age, and `ok` otherwise. Default stale age is 180 seconds;
the CLI uses the greater of 180 seconds and three collection intervals. A failure
retains the last values and original success time. The collector uses the installed
Codex App Server's read-only `account/rateLimits/read` operation; source login
credentials remain in Codex.

### PC snapshot, schema 1

```json
{
  "schema_version":1,
  "status":"ok",
  "updated_at":1794000000,
  "data":{
    "cpu_percent":27.5,
    "gpu_percent":50.0,
    "gpu_temperature_c":62.0,
    "gpu_memory_used_percent":68.5,
    "memory_used_percent":41.0,
    "system_disk_total_bytes":536870912000,
    "system_disk_free_bytes":199286482944,
    "uptime_seconds":183600
  }
}
```

CPU is averaged between counter reads. Windows kernel time already includes idle
time; the calculation is in `cpu_percent()` and has regression tests. On machines
with more than 64 logical processors, the native API reports the current primary
processor group; this is not a cross-group profiler. RAM is physical memory use;
the disk is the Windows system drive, and bytes use binary units when displayed
as GiB. Uptime follows `GetTickCount64`, including sleep. No hostname, process
names or file lists are exported.

Optional `gpu_percent`, `gpu_temperature_c` and `gpu_memory_used_percent` are
NVIDIA driver-index-0 readings, not an average across adapters. Percentages are
0–100; temperature is Celsius (accepted range 0–150). VRAM percentage uses used/
total MiB from the driver. Unsupported or failed fields are `null`; old assistants
may omit them. A GPU query failure leaves usable CPU/RAM status `ok` and clears
GPU values rather than publishing old temperatures. Overall `stale` still means
the aggregate Windows-counter sample failed. The trusted driver tool is queried
without a shell, with a hidden window and two-second timeout; no control flags
or account/process/device-identity queries are used. AMD/Intel GPU sensors and
multi-GPU selection are not implemented. Reference:
[NVIDIA SMI](https://docs.nvidia.com/deploy/nvidia-smi/index.html).

Requests within one second reuse the last sample. The API does not add a sampling
thread; Passport polls the visible PC page every 30 seconds. A first CPU sample
with no usable delta is `null`. The endpoint uses the same bearer authorization
as allowances but works when Codex login/proxy collection fails. Non-Windows hosts
or unavailable counters return an unavailable snapshot; subsequent counter
failures retain a stale snapshot. Unlike allowances, PC JSON has no
`sample_age_seconds` or `error` field. PC values are not cached to device Flash.

## Device setup web session

Source: [pocket_setup.c](../../main/pocket_setup.c),
[pocket_setup_guard.c](../../main/pocket_setup_guard.c),
[pocket_setup.html](../../assets/web/pocket_setup.html).
First boot without saved Wi-Fi opens a password-protected temporary hotspot.
Holding DOWN opens settings; holding OK closes it. Online devices expose the
session on their LAN IP. Sessions expire after ten minutes. A boot that remains
offline for 30 seconds offers recovery once. There is no assumed captive-portal
auto-popup; OK switches between hotspot and webpage QR codes.

All routes require the QR's random 32-hex-character `k` query parameter and an
exact local-IP Host header. Writes additionally require an Origin matching that
host. Keys and hotspot credentials are generated independently. Do not implement
discovery by broadcasting the administrative QR URL or saved credentials.

| Method/path | Body/result |
| --- | --- |
| `GET /?k=<SESSION_KEY>` | Local setup HTML |
| `GET /status?k=<SESSION_KEY>` | Non-secret SSID, NAS/HA/companion addresses and `codex_ready`/`nas_ready`/`home_ready` booleans |
| `POST /save?k=<SESSION_KEY>` | Flat partial JSON; allowed settings below; success `{"ok":true,"restart":true}` |
| `POST /codex-pair?k=<SESSION_KEY>` | `{"address":"192.168.1.20","code":"123456"}`; obtains/saves the companion token internally |
| `POST /pc-display?k=<SESSION_KEY>` | `{"pc_display":"cpu,gpu,gpu_temp,memory,disk,status"}`; saves selected PC rows |

Write requests require exact `Content-Type: application/json`. `/save` accepts
2–2304 bytes and only `wifi_ssid`, `wifi_password`, `companion_url`,
`companion_token`, `nas_host`, `nas_community`, `home_url`, `home_token`, all strings.
`/codex-pair` accepts 2–256 bytes, an RFC1918 IPv4 address and six numeric digits;
it calls the companion's fixed port 8765. It returns the same success envelope,
not the acquired token. Session failures return 403; invalid/unsaved settings or
failed pairing return 400. Saving schedules a restart after the response.

The UI omits blank secret fields to preserve them; this is UI behavior, not a
universal empty-string rule. A caller that explicitly sends an empty secret can
clear it. Open Wi-Fi explicitly sends `wifi_password:""`. Saved secrets are never
returned by `/status`. There are no device-web routes to export Genshin cookies,
press a Xiaomi device button or execute arbitrary code.

## Configuration and USB fallback

`/status` also returns non-secret `pc_display`. `/pc-display` requires the same
session, Host, Origin and JSON content type checks; body length is 2–512 bytes.
Its body must contain exactly one string field, `pc_display`: one to 24 distinct
comma-separated IDs from the catalog below (including the original `cpu`, `gpu`, `gpu_temp`, `memory`, `gpu_memory`, `disk`, `disk_free`, `uptime`, `status`). No spaces, empty tokens or unknown IDs are accepted.
Order is preserved; the phone checkboxes use the listed order. Success is the same
`{"ok":true,"restart":true}` envelope, invalid/unsaved selection is 400, unauthorized
session is 403. The default is `cpu,gpu,gpu_temp,memory,disk,status`.

This preference is a separate string key `pocket_notes/pc_display`; it does not
enlarge the version-1 configuration or cached snapshot. Missing/invalid optional
preferences use defaults. Saving it does not write the existing credential blob.
It is deliberately not added to the generic `/save` or USB field whitelist.

Use [configure_pocket.py](../../tools/configure_pocket.py) for the optional USB
fallback. It sends a flat UTF-8 JSON object plus newline, at most 4096 bytes,
in short chunks. Only exact `POCKET_CONFIG_OK` or `POCKET_CONFIG_REJECTED` lines
are acknowledgements. Arbitrary console lines must never be echoed as a response
because they can contain private input. Saving is transactional and restarts
the device; this protocol never authorizes a firmware write or NVS erase.

| Key | Maximum UTF-8 bytes | Notes |
| --- | ---: | --- |
| `wifi_ssid` / `wifi_password` | 32 / 64 | Nonempty submitted SSID; password empty for open Wi-Fi, otherwise 8–63 bytes or exactly 64 hex digits |
| `companion_url` / `companion_token` | 192 / 128 | Base URL and display-service token |
| `nas_host` / `nas_community` | 63 / 64 | Current client needs IPv4 and read-only SNMPv2c community, UDP 161 |
| `genshin_uid` / `genshin_region` | 15 / 31 | Advanced USB fallback; UID digits, region lowercase letters/digits/underscore |
| `genshin_cookie` / `genshin_widget_cookie` | 511 / 511 | Advanced private fields; normally obtained by native QR login |
| `genshin_device_id` / `genshin_device_fp` | 64 / 64 | Advanced private fields |
| `home_url` / `home_token` | 192 / 512 | HA base URL and long-lived token |

Only USB exposes the advanced Genshin fields. Unknown keys/nested structures
are not accepted. URLs use HTTP/HTTPS without embedded userinfo, query or fragment.
Python additionally rejects placeholders and C0 control characters; firmware
validates termination, sizes, CR/LF, Wi-Fi, URLs and Genshin identifiers. Never
interpret a configured URL as a shell command. NVS is not encrypted in this app.

## Other integrations and versioning

QNAP uses bounded SNMPv2c GET/GETNEXT, request ID/community validation and
HOST-RESOURCES-MIB CPU/storage/uptime. It selects the largest fixed volume rather
than summing pools. No SNMP SET is sent. HA uses authenticated REST templates
for four entities at a time and whitelisted service actions after explicit
confirmation. There is no direct Xiaomi cloud login or validated eight-key
device-emulation implementation. Genshin endpoints/signatures are an integration
detail adapted from the supplied reference, not a stable public API promised to
third-party pages. Do not expose their account material through the companion.

For schema 1, preserve field names/types, units, unknown/null meaning and success
timestamps. Add optional fields compatibly and make consumers ignore unfamiliar
fields. Add an explicit schema version and corresponding firmware reader for
breaking changes; do not silently repurpose an old key. Keep response size within
the 12 KiB transport bound and test 401, unknown routes, missing fields, stale
data, malformed values and allocation failures. Never describe an example custom
route as implemented until it is registered and tested at both ends.

## Page navigation preference

`GET /status` also returns non-secret `page_layout`. `POST /page-layout` requires
the existing session key, Host/Origin and exact JSON Content-Type checks.
Body: `{"page_layout":"pc,-genshin,codex,-nas,home"}`, 2–160 bytes, exactly one
string field. List all five stable IDs exactly once; a leading `-` disables that
page while retaining its order. Whitespace, unknown/duplicate/missing IDs,
empty tokens and all-disabled lists are rejected with 400. Unauthorized requests
return 403; successful NVS commit returns `{"ok":true,"restart":true}`.
Default: `genshin,codex,nas,home,pc`. The independent `pocket_notes/page_layout`
string key is optional; missing or invalid stored preferences fall back to the
default. Generic `/save` and USB do not accept this field. No credentials are
deleted. The visible-only worker refresh policy skips disabled pages after reboot.


## Expanded metrics and display settings

Additional `pc_display` IDs (identical JSON data keys):
`memory_used_bytes`, `memory_free_bytes`, `memory_total_bytes`,
`gpu_memory_used_bytes`, `gpu_memory_free_bytes`, `gpu_memory_total_bytes`,
`gpu_power_w`, `gpu_fan_percent`, `gpu_clock_mhz`, `cpu_clock_mhz`,
`disk_read_bps`, `disk_write_bps`, `net_rx_bps`, `net_tx_bps`.
They are optional nonnegative finite numbers or null. Old assistants may omit
them. Power uses W, clocks MHz, capacities bytes, rates bytes/second. Unknown is
not zero. Original IDs are unchanged. Storage capacity is now 384 bytes including
NUL; the selection allows up to 24 distinct known IDs (23 currently exist).
`/pc-display` accepts 2–512 bytes, and six-row snapshots remain unchanged.

`GET /status` additionally returns numeric `screen_timeout`.
`POST /display-settings` accepts exactly `{"screen_timeout":"60"}` with the
existing session/Host/Origin/JSON checks and 2–160 byte body. Only string values
0, 30, 60, 120, 300 are accepted. Successful commit returns the normal restart
envelope. The independent optional NVS string `screen_timeout` defaults to 60.
It is not added to USB or the main configuration blob.
`POST /screen-off` requires exactly the two-byte JSON body `{}` and the same
checks. Success `{"ok":true,"restart":false}` queues an atomic UI request and
keeps the settings session active; it never touches LVGL from the HTTP task.
Invalid input returns 400; session failures 403. These routes do not enter sleep
or disable networking. Wake events are consumed through release and a short
post-release guard, with tests for long/double gestures.

## Backlight brightness preference

`GET /status` includes numeric `brightness` (default 75). `POST /brightness`
accepts exactly one string field, for example `{"brightness":"35"}`, in a
2–96-byte flat JSON body with the existing session-key, Host/Origin and exact
JSON content-type checks. Decimal digit strings must resolve to 10..100.
Invalid input/save failure returns 400; unauthorized requests return 403.
Successful NVS commit returns `{"ok":true,"restart":false}` and queues an
atomic value for the UI task, which alone invokes the existing BSP backlight
API. The phone session stays usable; no HTTP task accesses LVGL or display.
The optional `pocket_notes/brightness` string key is independent of the
2,464-byte version-1 credential blob and six-row snapshot. Missing/invalid keys
fall back to 75. Flash writes happen only on explicit submission. Brightness
does not change screen-off state and is restored on wake. USB `/save` field
allowlists are unchanged. Nine routes fit the existing ten-handler limit.

The on-device menu writes these same single-field APIs from the existing worker
task via a dedicated `DISPLAY_SAVE` command; button callbacks only queue input.
Local edits preview brightness in the UI task, while timeout applies after a
successful save. Busy/error/cancel/no-change states are covered by the pure menu
state-machine tests. No new persisted page ID, snapshot layout, task, public
network endpoint or dependency is introduced by the menu.
