[English](pocket-notes.md) · [简体中文](pocket-notes.zh_CN.md)

# Pocket Notes

Five Chinese pages for AI Passport: Genshin daily notes, Codex allowance,
QNAP status, Xiaomi Home controls through Home Assistant, and PC resources. The default
startup is the original application UI, not the upstream hardware-test menu.
Drivers and hardware constants remain in the BSP.

Developer handoff: [source map and custom-page checklist](pocket-notes-page-development.md),
[implemented interfaces](pocket-notes-protocol.md), and
[development/acceptance log](pocket-notes-development-log.md).

## Pages and keys

| Page | Information and actions |
| --- | --- |
| Genshin | Resin/recovery time, commissions, reward collection, expeditions and realm currency; domestic-server QR login and phone-assisted manual security verification |
| Codex | Primary/secondary remaining allowance, reset times, credit balance, earned-reset count and earliest expiry when complete details are available; no reset redemption |
| QNAP | Average reported CPU load, memory use, largest reported fixed-volume use/free capacity, host uptime and connection state |
| Xiaomi Home | Paginated functionality exposed by `xiaomi_home`/`xiaomi_miio` in HA; state, operation preview and explicit confirmation |
| PC resources | Choose from 23 metrics from CPU, GPU load/temperature, RAM, VRAM, system-drive use/free capacity, uptime and connection state; shares the Codex companion pairing |

Normally UP/DOWN changes page and OK refreshes. Recognized double UP/DOWN
advances two pages. Manual refresh is limited to once per page per 30 seconds.
While visible, Genshin refreshes every five minutes, Codex every minute, and
NAS/HA/PC every 30 seconds. Waiting for Wi-Fi/time synchronization retries sooner;
missing configuration and authentication failures pause automatic requests.

On Genshin, hold OK to generate a login QR code and confirm it yourself in the
official domestic MiYouShe app. When security verification is required, hold OK
to open that session instead: scan with a phone on the same LAN and complete
the challenge manually. It expires after five minutes; changing page cancels
it. Credentials remain on the board, and the web page receives only public
captcha configuration. Sync is restored only after a successful real query.
The selected domestic role is the API's chosen role or its first domestic role.
The widget response does not identify its role: verify the official widget's
associated role if you have multiple roles. Overseas HoYoLAB login and
automatic captcha solving are not implemented.

On Xiaomi Home, OK enters functionality selection, UP/DOWN selects an item,
OK enters operation selection, and UP/DOWN selects an action. OK previews the
target/parameter; a further OK submits it. UP/DOWN cancels a preview; holding
OK exits to normal page navigation. Holding UP on the Xiaomi page refreshes its
functionality list. Previews expire after 30 seconds; stale or changed targets
and duplicate pending submissions are blocked. An accepted
command whose readback fails is shown as submitted but unconfirmed; it is not
automatically retried.

Common controls include power, brightness, fan speed, climate temperature,
covers, vacuum start/pause/dock, select/number entities, buttons, scenes/scripts
and basic media controls when exposed and supported. Other types remain
read-only. This does not reproduce every proprietary Xiaomi app function or
guarantee every device category. One physical device may expose multiple HA
functionality items; actual compatibility requires testing each device.

## Private setup

The current desktop entry is [Codex-Assistant.cmd](../../Codex-Assistant.cmd).
Its main heading describes allowance and PC-resource synchronization; the window
title still says Codex allowance assistant. It is the same shared service, not a
second PC-monitor application. Reopen it from the original project directory to
retain pairing. A fresh extracted copy has separate private pairing state.

PC monitoring uses the existing desktop assistant and its pairing. If already
paired, close and reopen the updated assistant once; no new settings or pairing
are needed. It also works when Codex authentication/networking is unavailable.
Hold DOWN, scan the settings QR and use the PC-monitor card to select metrics across automatic six-row subpages. Saving restarts the board; unselected rows are hidden. The default is
CPU, GPU load, GPU temperature, RAM, system-drive use and connection state.
The selection has its own NVS key and preserves the existing version-1 settings,
Genshin login and companion pairing. It is not a new login or USB setup step.

The PC endpoint is authenticated `GET /v1/pc` and gathers only aggregate
counters, with no process names or file lists. CPU is averaged between samples;
the system drive is the Windows drive, not necessarily all installed drives.
GPU load, Celsius temperature and VRAM use are read from the first NVIDIA GPU
(driver index 0), using the installed driver's read-only `nvidia-smi` query with a
two-second timeout and hidden window. No extra Python package, privileged sensor
driver or hardware-monitor service is installed. Unsupported/failed GPU fields
are unavailable, never invented as zero or retained as a fresh old temperature;
CPU/RAM remain usable. AMD/Intel GPU sensors and process lists are not implemented. NVIDIA utilization can differ from Task Manager's busiest
engine percentage. See [NVIDIA's query documentation](https://docs.nvidia.com/deploy/nvidia-smi/index.html).
PC monitoring is Windows-only; its last values remain in RAM on connection
failure, with their original time and an expired-data status, not a persistent
Flash cache. No additional Passport UI task or parallel network request is added.

### Phone setup (recommended)

1. After installing this firmware, first boot without saved Wi-Fi automatically
   shows a QR code for a password-protected `Passport-XXXX` hotspot. Scan it with
   the phone camera, connect and keep the connection even though it has no Internet.
2. Press OK on the device to switch to the webpage QR code. Scan it, enter the
   home 2.4 GHz Wi-Fi name/password and save. Switch the phone back to home Wi-Fi
   after the device restarts; check the device screen for actual connection.
3. Hold DOWN on any page to reopen unified settings. When the device is online,
   scan the webpage QR code directly from the same home network. No USB setup is
   required. Hold OK to exit; each settings session closes after ten minutes.
4. Genshin uses the device's native QR login. For Codex, open
   [`Codex-Assistant.cmd`](../../Codex-Assistant.cmd), start the desktop assistant,
   and enter its displayed computer address and six-digit code into the phone's
   Codex card. The code expires in two minutes, accepts at most five attempts,
   and is single-use. The assistant detects the existing computer proxy.
5. The NAS card prefills `192.168.1.250`; enable read-only SNMP on QNAP once and
   enter its community. The Xiaomi card accepts the HA address/token after HA
   and Xiaomi Home are installed and logged in. Both may be left for later.

Each card saves its own fields and restarts the device; other configuration is
preserved. Blank password/token fields retain their existing values; the Wi-Fi
card has an explicit open-network option. The webpage never returns saved
secrets. Settings sessions require the QR's random access key, same-origin
writes and exact local host checks. Only initial setup or explicit recovery uses
the temporary WPA2 hotspot; normal browsing runs in STA mode. If no network is
obtained for thirty seconds after boot, a recovery session is offered once.
During settings, business requests pause to limit memory use. Captive-portal
auto-popup is not assumed; scan the webpage code as described above.

### Optional USB fallback

On Windows, configure Wi-Fi by double-clicking [`WiFi-Setup.cmd`](../../WiFi-Setup.cmd).
Connect and power on the device, select its port, enter the 2.4 GHz Wi-Fi name
and password, then click the save button. The GUI applies only Wi-Fi settings,
keeps other device settings, and stores no password file on the computer. It
requires Pocket Notes firmware to be installed first. A saved-settings acknowledgement
does not prove a successful Wi-Fi connection; check the device screen after restart.
The launcher uses the installed ESP-IDF 5.5 Python environment. No shell activation
or JSON editing is needed for this route.

For the remaining services or command-line setup:

Use 2.4 GHz Wi-Fi with LAN access to the computer, NAS and HA. To use the advanced
USB fallback, create the private template without overwriting a file:

```text
python tools/configure_pocket.py --create-template
```

Edit `build/private/device.json` locally. Fill in Wi-Fi credentials, the PC LAN
base URL/port in `companion_url`, the NAS read-only SNMPv2c community in
`nas_community`, and HA's base URL/access token in `home_url`/`home_token`.
The selected NAS address is `192.168.1.250`. Empty optional fields leave a page
unconfigured; never send credentials through chat or commit the private file.

Start the Codex companion using the installed Codex CLI's existing login:

```text
python tools/codex_companion.py --bind <PC_LAN_IP> --proxy http://127.0.0.1:7890
```

Replace the LAN address; 7890 is the existing proxy observed on this computer.
Use the actual proxy on another computer. Without `--bind` it listens only on
loopback. Only authenticated `GET /v1/codex` is exposed. Its separate read-only
pairing token is stored in `build/private/companion.token`; the USB tool reads
it automatically when `companion_url` is present and no token was supplied.
Allow private-LAN firewall access if needed; the tools change no firewall or
router settings. Account credentials are not copied onto the board.

Validate, then apply using the actual board port:

```text
python tools/configure_pocket.py --dry-run
python tools/configure_pocket.py --port <DEVICE_PORT>
```

Applying needs `pyserial`, available in the activated ESP-IDF environment.
It saves settings, waits for acknowledgement and restarts the app; it does not
flash firmware. Private values and serial input are not echoed. Board settings
use NVS and are not encrypted by this app. Private files are under ignored
`build/`; do not distribute them or a configured device's flash contents.

Successful values are cached at most once per page per five minutes. After
restart they are marked stale with their original timestamp. Network failures
preserve the last successful values; missing fields show `--`. Synthetic
account/device data exists only in host-renderer tests, not the application.

## NAS, HA and independent operation

The NAS is TS-212P3 at `192.168.1.250`; its installed QTS version remains
unverified. Enable a dedicated read-only SNMPv2c community. The client uses
HOST-RESOURCES-MIB CPU/storage tables and host uptime, with bounded walks.
Storage is the largest reported fixed volume, not a sum of all pools.
Unsupported metrics remain unavailable; compare results with the real NAS.

HA was accepted as the Xiaomi bridge. Xiaomi's official integration license
limits its code/cloud interfaces to HA use, so it was not transplanted into
this firmware. Configure Xiaomi login within HA, then use HA's public REST API.
See [HA deployment notes](../../services/homeassistant/README.md). TS-212P3 has
1 GB RAM; its suitability for additional services has not been established.
Use the existing computer or a suitable Linux host instead of assuming that
this older NAS can run the full stack.

The PC collector must remain running for fresh Codex values. A side router
solves routing only; collection still needs a host. No AC9 firmware was selected
or flashed, and its hardware revision is unknown. The collector can be moved
later after verifying the new host's Codex/runtime compatibility.

## Verification and delivery

For Genshin failures, diagnostic firmware displays a fixed request-stage label
and `H` (HTTP status), `A` (service return code), or `E` (ESP error in hex).
USB warning logs record the numeric stage, error and current heap sizes without
request URLs, cookies, account data or QR tickets. This helps isolate the fault;
it does not itself fix a failed login. The reported one-to-two-second failure
was subsequently reported as fingerprint-stage `E101` (`ESP_ERR_NO_MEM`). HTTP
now sizes TX storage for each request and allocates response storage for the
announced length, growing bounded chunked responses only when needed. The
12 KiB response limit, certificate verification and TLS record limits remain.
Host fault-injection checks cover short responses, large challenge headers,
chunked/oversized/truncated bodies and realloc failure. This allocation reduction
was verified by the user on 2026-11-07 with image SHA-256
`0e57c534f35868350888550db78b56bc3fd0a3af4e540fa7c0e1ad519d83dcad`:
QR login and configuration succeeded after the application-only update. This
confirms recovery from the reported E101; long-run stability and note-value
accuracy were not independently measured.

The original UI uses the licensed Source Han font in the [assets guide](../../assets/README.md).
Its inventory contains 21,352 characters. Static checks cover fixed UI text and
bitmap bounds; the host renderer queries every inventory glyph through real
LVGL and checks a known missing emoji. Unsupported dynamic text is explicitly
reported. Host previews are labeled synthetic test data. Actual LCD colors,
clipping and readability still need board verification.

Run `tools/validate.sh` in ESP-IDF 5.5.3. The full gate retains the merged
`build/FoloToy-AI-Passport-full.bin` for offset `0x0` and matching ELF/MAP under
the image-hash archive. Windows may require an ASCII build checkout and explicit
compatible-shell tools. Development workarounds do not change global proxy,
Git, compatibility or cache settings.

Board acceptance covers boot, keys, Chinese display, login/manual verification,
Wi-Fi reconnect, PC sleep/restart, NAS comparison and each Xiaomi control.
Compilation is not hardware acceptance. A merged flash can reset stored
settings; explicit approval is required before writing it. Neither full-chip
erase nor original-firmware readback is a prerequisite.

Primary sources: [Codex App Server](https://learn.chatgpt.com/docs/app-server),
[HOST-RESOURCES-MIB](https://www.rfc-editor.org/rfc/rfc2790),
[QNAP SNMP](https://www.qnap.com/de-de/how-to/faq/article/how-to-get-the-list-of-messages-send-by-the-snmp-server),
[Xiaomi integration](https://github.com/XiaoMi/ha_xiaomi_home),
[HA REST API](https://developers.home-assistant.io/docs/api/rest/).

## Page order and visibility

Hold DOWN and scan settings, then use the page-order card. Move pages up/down
and uncheck pages to disable them. At least one page must stay enabled. Save and
restart to use the first enabled page in that order; UP/DOWN (including double
press) skips disabled pages. Dots follow the visible order/count. Disabled pages
are not polled automatically and keep their saved account/configuration data.
Hold DOWN on any remaining page to re-enable them. Existing devices default to
the original five pages until changed. Preferences use a separate NVS string;
the version-1 credential blob and cached snapshots retain their layouts.


## Screen-off and expanded monitoring

Idle backlight-off defaults to 60 seconds; choose Off/30/60/120/300 seconds in
phone settings. The first key gesture only wakes the screen, including its long/
double events; wait briefly before the next operation. Settings, login QR and
active control flows inhibit automatic blanking. Wi-Fi and visible-page refresh
continue; this is not deep sleep. Long UP blanks ordinary non-Xiaomi pages;
Xiaomi's existing long-UP refresh is retained. The phone also has an immediate
screen-off button that does not restart or close its settings session.

The PC catalog now contains 23 metrics: the original nine plus RAM used/free/
total, VRAM used/free/total, GPU power/fan/graphics clock, CPU frequency, disk
read/write rates and network receive/send rates. Restart the desktop helper from
the same directory to keep pairing. No Python package is added. Windows PDH
supplies optional performance counters; denied/unavailable counters show unknown.
Disk rates cover all physical disks, while capacity remains the system drive.
Network rates sum reported interfaces; virtual/VPN adapters can duplicate traffic.
CPU frequency is the counter's reported MHz; GPU fan is a driver percentage,
not RPM. CPU temperature is not provided. Memory capacity uses GiB and rates
KiB/s or MiB/s; rates/CPU usage average the request interval, not an instantaneous
profiler. GPU extras remain driver-index-0 only.

Choose more than six metrics in the PC card to create subpages. UP/DOWN crosses
PC subpages before moving to the next enabled application page, and reverse
navigation enters the last PC subpage. The title shows the PC page number; main
dots still represent enabled application pages. Old six-item selections survive.
Only one compact numeric PC sample is held in RAM, not extra screens/tasks or
Flash metric caches. See the [eight-key research](mihome-eight-key-research.md)
for the separate Mi Home investigation.

## Screen brightness

Hold DOWN and scan the phone settings page. In Screen Brightness, choose
10%–100% and click Apply and Save. The default is the previous 75%. Saving
applies within the UI loop without a restart and persists across restarts.
Dragging only updates the phone's numeric label; it does not write Flash.
Changing brightness while the screen is off keeps it off; waking restores the
saved value. Zero is reserved for screen-off. Actual current and battery-life
improvements need device measurements; brightness percentage is PWM duty.

Short-press toggling through the left power key is requested but not implemented.
The repository's specifications identify an independent hardware power key;
the pin map/BSP expose only UP, DOWN and OK, with no power-key GPIO or readable
power-controller interface. This is insufficient evidence to implement a side
key or assert that all hardware revisions lack a readable signal. It requires
confirmed electrical information. Existing function-key wake remains available.

## On-device screen settings

Double-click OK on a normal business-page overview to open Screen Settings.
UP/DOWN selects Brightness, Auto Screen-off or Return. Click OK to edit;
UP increases and DOWN decreases (brightness steps by 5 points, bounded 10..100;
timeout choices are disabled/30/60/120/300 seconds). Click OK again to save.
Brightness previews immediately, without Flash writes on each adjustment.
Holding OK while editing cancels and restores the saved value; holding it in
the list exits. Return plus OK also exits. Unchanged values do not write Flash.
Saving displays progress and temporarily blocks further menu commands; a failed
save restores the saved value and offers retry. The menu stays awake while open.
Both phone and on-device settings use the same persisted keys. This entry does
not replace Genshin long-OK login or DOWN-hold phone setup. It is unavailable
during QR login, phone setup and Mi Home device/action flows. The existing
first-wake gesture guard still applies before any double-click.
