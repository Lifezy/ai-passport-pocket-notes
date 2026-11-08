[简体中文](mihome-eight-key-research.zh_CN.md) · **English**

# Eight-key Mi Home switch: implementation research

Checked on 2026-11-08. The requested outcome is a Passport that can be added to
Mi Home as an eight-key wireless switch, with each key assigned an automation
in that app. Receiving events from an existing switch or controlling HA scenes
is a different capability. No verified, ready-to-flash ESP32-C3 implementation
meeting that exact registration/binding goal was found in this review. This is
a search result, not a proof that such an implementation cannot exist.

| Route/project | What it provides | Fit for the requested outcome |
| --- | --- | --- |
| [GY-BT8 eight-key open hardware](https://oshwhub.com/moth/mi-mijia-8-key-wireless-switch) | The author describes a GY-BT8 module needing no user firmware flashing, eight Mi Home keys with single/double/hold automations, and a compatible Bluetooth gateway | An existing dedicated-module route; requires extra hardware and is neither a Passport software-only solution nor verified on this board |
| [Official standard BLE integration](https://github.com/MiEcosystem/miio_open/blob/master/ble/02-%E6%A0%87%E5%87%86BLE%E6%8E%A5%E5%85%A5%E5%BC%80%E5%8F%91.md) | Product-development SDK workflow, authentication and registration | Its published procedure requires requesting the authentication library and product/platform access; not a ready-made hobbyist eight-key firmware |
| [XiaomiGateway3](https://github.com/AlexxIT/XiaomiGateway3) | HA integration for supported Xiaomi gateways and their devices | Existing gateway/device control, not Passport registration as a new Mi Home switch |
| [ESPHome Bluetooth proxy](https://esphome.io/components/bluetooth_proxy/) and [BTHome](https://www.home-assistant.io/integrations/bthome/) | Established HA Bluetooth transport and DIY-device ecosystem | Suitable building blocks for HA-based buttons; neither promises Mi Home native-device registration |
| [ATC_MiThermometer](https://github.com/pvvx/ATC_MiThermometer) | Replacement thermometer firmware; a documented Gateway 3 path reuses original registration/binding | Different hardware/product; not evidence of generic eight-key switch emulation |
| [mijia-api](https://github.com/Do1e/mijia-api) | Existing-device control from a host application | A possible separate control service, not an advertised eight-key Mi Home device; not installed or tested here |
| [ha-esp32 releases](https://gitee.com/panzuji/ha-esp32/releases/) | Existing-device/cloud/voice gateway control; some older releases list a C3 image | Not evidence of native eight-key registration; Passport compatibility and external service/account requirements are unverified |

The [official BLE document](https://raw.githubusercontent.com/MiEcosystem/miio_open/master/ble/02-%E6%A0%87%E5%87%86BLE%E6%8E%A5%E5%85%A5%E5%BC%80%E5%8F%91.md)
describes product setup and authenticated binding; copying an advertising name
or product ID does not demonstrate that flow. The
[thermometer project's own limitations](https://github.com/pvvx/ATC_MiThermometer)
also distinguish its custom firmware from native Mi Home support. Protocol
research such as [ble-in-xiaomi](https://github.com/freenetwork/ble-in-xiaomi)
is useful background, but is not a maintained eight-key product implementation.

The closest existing DIY route to native registration is the GY-BT8 project
above: a dedicated module provides authentication and radio behavior, with PCB,
bill of materials and module-document attachments supplied by its author.
Direct access returned HTTP 403; the author's project description, GY-BT8 parts
list and no-flashing statement were checked through indexed search content.
Electrical attachments were not downloaded or verified, and no module was
purchased or tested. This is distinct from ESP32-C3 firmware emulation.
Before evaluating Passport-triggered module buttons, obtain the module's formal
interface/electrical documentation and verify voltage, power, trigger method,
channel requirements and available board pins/expansion. Direct wiring or a
serial-control protocol is not established. Indexed prices are not a purchasing
quotation.

For a working remote page today, the prepared HA REST integration remains the
available application route once HA is deployed and actual devices are tested.
An eight-shortcut HA page would require separate implementation/acceptance and
would not create a new switch in Mi Home. HA is still not installed in the user's
environment; no migration or extra service was performed during this research.

For native Mi Home registration, first confirm the exact Jiuhao model/hardware,
gateway and binding method, then locate a tested implementation or obtain the
appropriate product integration materials. The earlier `cddz.remote.cdwx8` name
remains an unconfirmed candidate for the user's physical switch. No switch was
reset, no binding keys were extracted, and no advertising spoofing was enabled.
The ESP32-C3 Wi-Fi/BLE coexistence and RAM budget would need validation before
adding any radio-based implementation to the current application.
