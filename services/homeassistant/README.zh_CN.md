[English](README.md) · [简体中文](README.zh_CN.md)

# 多页便笺的 Home Assistant 服务

此文件适用于合适的 Linux Docker 主机，尚未部署到用户 NAS/电脑。它不会安装
Docker/WSL、修改路由器或自动登录小米。TS-212P3 只有 1 GB 内存，尚未验证
容器和运行环境是否适合。

Windows 请参考[官方虚拟机安装指南](https://www.home-assistant.io/installation/windows/)，
Linux 参考[官方安装指南](https://www.home-assistant.io/installation/linux/)。
在合适的 Linux 主机选择 Container 安装时，在本目录运行：

```text
docker compose up -d
```

文件固定 HA 2026.11.0，使用主机网络，配置保存在已忽略的 `config/`。
未申请特权模式或挂载 USB；米家云端集成不需要板端蓝牙适配器。容器环境和
镜像拉取仍属于部署检查，不能当成固件验证。

在主机局域网地址的 HA 界面完成以下设置，通常使用 8123 端口：

1. 创建 HA 账号，配置时区和网络。
2. 按[小米官方集成](https://github.com/XiaoMi/ha_xiaomi_home)的 HACS/手动安装说明安装并重启 HA。
3. 在“设备与服务”添加 Xiaomi Home，本人登录、选择区域并导入家庭和设备，保留集成许可。
4. 先在 HA 验证每台设备；在个人资料创建给板端使用的长期令牌，填入私密 `home_token`，局域网基础地址填入 `home_url`。
5. 按[应用说明](../../docs/development/pocket-notes.zh_CN.md)应用板端私密设置。

固件通过 HA 带鉴权的模板 API 读取 `xiaomi_home`/`xiaomi_miio` 功能项，只有
选中并确认后才提交支持的服务动作。固件不包含小米受限集成源码，也不向板端
复制小米账号密码。

兼容性取决于集成和设备实际能力。小米 FAQ 排除了部分蓝牙、红外和虚拟类别。
传感器和未支持域不提供写操作，也不完整复刻专有 App 功能。API 成功不证明
物理设备已改变状态，应逐台现场核对。
