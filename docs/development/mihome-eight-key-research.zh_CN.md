**简体中文** · [English](mihome-eight-key-research.md)

# 米家八键无线开关：方案调研

核对日期：2026-11-08。目标是让 Passport 在米家 App 中作为八键无线开关添加，
并给各键配置自动化。接收已有开关事件、控制 HA 场景属于另一种能力。
本次未找到经过验证、可直接刷入 ESP32-C3 并实现上述注册/绑定目标的现成固件。
这是本次检索结果，不代表证明所有实现都不可能。

| 路线/项目 | 已提供能力 | 对目标的适用性 |
| --- | --- | --- |
| [GY-BT8 八键开关开源硬件](https://oshwhub.com/moth/mi-mijia-8-key-wireless-switch) | 作者说明使用 GY-BT8 模块，无需自行刷固件，支持米家八键及单击/双击/长按自动化，需要兼容蓝牙网关 | 找到了专用模块的实现路线；需额外硬件，不能当作 Passport 纯软件方案或本机已验证兼容 |
| [官方标准 BLE 接入](https://github.com/MiEcosystem/miio_open/blob/master/ble/02-%E6%A0%87%E5%87%86BLE%E6%8E%A5%E5%85%A5%E5%BC%80%E5%8F%91.md) | 产品 SDK 开发流程、认证和注册 | 公开流程要求申请认证库及产品/平台接入，不是可直接刷写的个人八键固件 |
| [XiaomiGateway3](https://github.com/AlexxIT/XiaomiGateway3) | HA 接入受支持的小米网关及其设备 | 操作已有网关/设备，不会把 Passport 注册成新的米家开关 |
| [ESPHome 蓝牙代理](https://esphome.io/components/bluetooth_proxy/)与 [BTHome](https://www.home-assistant.io/integrations/bthome/) | 已有 HA 蓝牙传输及 DIY 设备生态 | 可作为 HA 按钮方案基础，不承诺米家原生设备注册 |
| [ATC_MiThermometer](https://github.com/pvvx/ATC_MiThermometer) | 温湿度计替代固件，文档中的 Gateway 3 路线沿用原厂注册/绑定 | 硬件、产品不同，不能据此认定通用八键模拟可用 |
| [mijia-api](https://github.com/Do1e/mijia-api) | 常开主机控制已有米家设备 | 可另行评估控制服务，不是可添加的八键开关；本次未安装或测试 |
| [ha-esp32 发布页](https://gitee.com/panzuji/ha-esp32/releases/) | 已有设备/云平台/语音网关控制，部分旧版本列出 C3 镜像 | 不是原生八键注册的证据；Passport 兼容性及外部服务/账号要求未验证 |

[官方 BLE 文档](https://raw.githubusercontent.com/MiEcosystem/miio_open/master/ble/02-%E6%A0%87%E5%87%86BLE%E6%8E%A5%E5%85%A5%E5%BC%80%E5%8F%91.md)
描述了产品创建和认证绑定，复制广播名称或产品 ID 不能证明完成这些流程。
[温湿度计项目自身的限制](https://github.com/pvvx/ATC_MiThermometer)也区分了替代固件
与米家原生支持。[ble-in-xiaomi](https://github.com/freenetwork/ble-in-xiaomi)等协议研究
可供理解背景，但不是维护中的八键产品固件。

目前最接近原生目标的已有 DIY 路线是上述 GY-BT8 项目：认证与无线通信由专用模块
承担，作者提供 PCB、材料清单与模块文档附件。本次直接打开页面返回 403，已核对
搜索索引中的作者项目介绍、GY-BT8 材料清单及“无需刷写固件”说明；未下载并核对
附件电气规范、未购买或实测。它与 ESP32-C3 固件模拟有明确区别。
若后续评估由 Passport 触发模块按键，应先取得模块正式接口/电气文档，核实电平、
供电、触发方式、所需通道及 Passport 可用引脚/扩展方式，再设计连接；目前不承诺
可直接接线，也没有依据猜测一个串口控制协议。没有把检索到的价格当作采购报价。

当前若希望先落地遥控页，已准备的 HA REST 接入仍是应用路线，需要先部署 HA 并逐设备
验证。做 HA 八个快捷键还需另行实现和验收，而且不会在米家新增一个开关。
用户环境仍未安装 HA；本次没有迁移或启动额外服务。

若坚持米家原生添加，应先核实实际 Jiuhao 型号/硬件、网关及绑定方式，再寻找对应的
已验证实现或取得产品接入材料。此前 `cddz.remote.cdwx8` 仍只是用户实物的未确认候选。
本次未重置开关、未提取绑定密钥，也未开启广播伪装。后续如加蓝牙方案，还须验证
ESP32-C3 的 Wi-Fi/BLE 共存和 RAM 余量。
