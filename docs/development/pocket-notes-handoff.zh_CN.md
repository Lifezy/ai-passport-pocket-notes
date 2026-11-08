**简体中文** · [English](pocket-notes-handoff.md)

# 多页便笺：给开发者的交接说明

这是基于 FoloToy AI Passport 的完整当前工作区，包含尚未提交的应用源码、上游 BSP、
字体及授权、必需 Skill、测试和文档。它不是运行时页面插件，也不附带已安装的工具链。
解压后先看根目录 `START-HERE.zh_CN.txt`。

## 在新电脑准备环境

1. 完整解压到简短、可写的目录，例如 `C:\Projects\AI-Passport`，不要在压缩包里开发。
2. 压缩包不含 Git 历史，需要建立索引，供仓库检查和源码打包工具枚举文件：

   ```bash
   git init
   git add .
   git switch -c codex/my-pages
   ```

   开始开发不需要先提交、推送或发布。`SOURCE-INFO.json` 记录原基线、分支及未提交
   状态；本包是源码快照，不是上游发布标签。
3. 先读 [AGENTS.md](../../AGENTS.md) 和[文档索引](../README.zh_CN.md)。按
   [环境引导](engineering/environment-setup.zh_CN.md)安装 ESP-IDF **5.5.3** 和
   ESP32-C3 工具。依赖按包内声明和锁文件联网恢复；不包含 `managed_components`、
   构建缓存及原电脑的 SDK。
4. 按 [skills/README.md](../../skills/README.zh_CN.md)核对并安装 `skills/` 中五个
   必需 Skill。交给 AI 开发时，应由 AI 自行检查和准备环境，不要复制别人的路径或凭据。

目标硬件为 ESP32-C3、8 MB Flash、**没有 PSRAM**。使用包内 `sdkconfig.defaults`
和 `partitions.csv`，不要用普通 ESP32 开发板模板替换 BSP。

## 开发自己的页面

先读[源码导览与专属页清单](pocket-notes-page-development.zh_CN.md)，再读
[接口规范](pocket-notes-protocol.zh_CN.md)及[开发与验收日志](pocket-notes-development-log.zh_CN.md)。

- `main/pocket_model.[ch]`：稳定页面 ID、六行数据快照及导航。
- `main/pocket_ui.[ch]`：应用的 LVGL 卡片界面与屏幕设置。
- `main/pocket_app.c`：按键分发、当前页刷新、工作队列。
- `main/pocket_services.h`、`main/pocket_*.c`：数据源实现。
- `assets/web/pocket_setup.html`、`main/pocket_setup.c`：手机设置。
- `components/bsp/`：共用板级驱动；`assets/fonts/`：字体及授权。

页面必须编译进固件，没有动态 HTML/插件加载或自动 provider 注册。新增 ID 追加在
末尾，不重排旧 ID；同步扩展分发、刷新间隔、标签/颜色、页面顺序设置及测试，明确
旧导航偏好的迁移方案。网络和存储操作不放在按键回调或 LVGL 锁内。检查中文覆盖与
真实渲染。完整清单还包括缓存/配置布局、失败保留旧值等约束。

第一步建议先做使用固定模拟数据的只读页面，检查布局，再连接真实接口。扩展本应用
可复用现有卡片组件；另做独立派生应用时仍须遵守仓库的界面重新设计要求。文档中的
`/v1/custom` 是说明示例，不是已提供的接口。

## 验证与烧录

在已激活的 ESP-IDF 5.5.3 终端中，按[构建指南](engineering/build-and-test.zh_CN.md)
准备主机依赖后运行：

```bash
bash tools/validate.sh --static
node tests/test_pocket_setup_web.cjs
bash tools/validate.sh
```

完整门禁按默认配置构建，检查分区和合并镜像，并保留匹配的 ELF/MAP。
`build/FoloToy-AI-Passport-full.bin` 是从 **0x0** 写入的合并固件。已有配置的设备应
先核对分区兼容，再按实际地址更新匹配应用以保留 NVS；完整合并刷写可能覆盖设置。
遵循[存储数据政策](engineering/firmware-layout.zh_CN.md)，烧录前获得用户同意，
不要把整片擦除当作普通环境准备步骤。

本源码包不含固件二进制或私密构建日志。先前交付的 `58be2aca` 固件构建及主机门禁
通过，用户确认本机菜单、亮度预览/保存、取消和不自动息屏正常。新电脑构建、新页面、
断电保留、耐久性和耗电仍需独立验证。左侧电源键短按控制尚未实现，缺少已确认的
可读硬件接口；HA 部署和原生米家八键模拟也不是已交付能力。

## 配置自己的数据及校验压缩包

不包含账号、Wi-Fi 密码、配对令牌、NVS、Python/IDF 安装目录或 Git 历史。接收方
需配置自己的设备及账号。NAS 默认值 `192.168.1.250` 只是局域网默认地址，应按新
环境修改；不能凭本包访问原用户的服务。

可选 Windows 助手：环境准备后运行 `Codex-Assistant.cmd`，或使用带 tkinter 的
Windows Python 3.12 执行 `python tools/codex_assistant.py`。Codex 采集需要接收方
自己的已安装并登录的 CLI/App Server 及网络。电脑资源使用 Windows 计数器和可选
NVIDIA 索引 0 查询。通过手机设置重新配对，包里没有旧配对状态；详见
[应用使用说明](pocket-notes.zh_CN.md)。

`SOURCE-MANIFEST.sha256` 覆盖包内各文件，`SOURCE-INFO.json` 记录快照来源。
解压后可核对原压缩包：

```bash
python tools/package_pocket_source.py --verify /path/to/pocket-notes-source.zip
```

包外另附整个 ZIP 的 SHA-256。转发时保留 `LICENSE` 和字体授权。
