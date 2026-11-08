[English](README.md) · [简体中文](README.zh_CN.md)

# Home Assistant for Pocket Notes

This definition is for a suitable Linux Docker host. It has not been deployed
to the user's NAS/computer. It does not install Docker/WSL, change the router,
or sign into Xiaomi automatically. TS-212P3 has 1 GB RAM; its container and
runtime suitability has not been verified.

On Windows, follow the [official VM installation guide](https://www.home-assistant.io/installation/windows/).
On Linux, use the [official installation guide](https://www.home-assistant.io/installation/linux/).
For Container installation on a suitable Linux host, run in this directory:

```text
docker compose up -d
```

The file pins HA 2026.11.0, uses host networking and retains configuration in
ignored `config/`. No privileged mode or USB attachment is requested; cloud
Xiaomi integration does not require a board-side Bluetooth adapter. Container
runtime availability/image pull are deployment checks, not firmware validation.

In the HA UI at the host LAN address, normally port 8123:

1. Create the HA account and configure timezone/network.
2. Install [Xiaomi's official integration](https://github.com/XiaoMi/ha_xiaomi_home) via its documented HACS/manual route and restart HA.
3. Add Xiaomi Home in Devices & Services, sign in yourself, select the region and import the desired home/devices. Retain the integration's license.
4. Verify each device in HA. Create a board-specific long-lived access token in the HA profile and place it in private `home_token`; place the LAN base address in `home_url`.
5. Apply private board settings using the [application guide](../../docs/development/pocket-notes.md).

The firmware queries functionality exposed by `xiaomi_home` and `xiaomi_miio`
through HA's authenticated template API, then submits only selected and confirmed
supported service actions. It does not contain Xiaomi's restricted integration
code or copy the Xiaomi account password onto the board.

Compatibility follows the actual integration/device capabilities. Xiaomi's FAQ
excludes some Bluetooth, infrared and virtual categories. Sensors and unsupported
domains have no writable actions; proprietary app features are not universally
reproduced. A successful API response is not proof that a physical appliance
changed state. Verify each appliance locally.
