[English](pocket-notes-page-development.md) · **简体中文**

# 多页便笺：源码导览与专属页开发规范

先读 [AGENTS.md](../../AGENTS.md)、[应用使用说明](pocket-notes.zh_CN.md)、
[接口协议](pocket-notes-protocol.zh_CN.md)和[开发日志](pocket-notes-development-log.zh_CN.md)。
本文对应现有五页应用。页面需要编译到固件中，目前没有运行时插件加载、自动 provider
注册或任意 HTML 页面接口。

## 源码导览

| 层次 | 维护的源码 | 职责 |
| --- | --- | --- |
| 入口 | `main/main.c`、`main/Kconfig.projbuild` | `CONFIG_POCKET_NOTES_APP` 选择应用；关闭时仍保留上游 demo |
| 调度 | `main/pocket_app.c` | 按键、命令/结果队列、网络 worker、当前页刷新、登录与控制状态 |
| 纯模型 | `main/pocket_model.[ch]` | 页 ID、六行快照、限流、失败保留旧值和时间 |
| 电脑显示选择 | `main/pocket_pc_display.[ch]` | 纯逻辑指标 ID、中文标签、多页指标去重校验与序列化 |
| 界面 | `main/pocket_ui.[ch]`、`main/pocket_setup_view.h` | 应用独立设计的 LVGL 卡片、分页点、二维码和设置视图 |
| 数据源 | `main/pocket_genshin.c`、`pocket_codex.c`、`pocket_nas.c`、`pocket_pc.c`、`pocket_home.[ch]` | 请求协议并转换为应用数据 |
| HTTP | `main/pocket_http.[ch]` | 校验证书的 HTTPS/HTTP、有界按需响应缓冲、资源释放 |
| 配网 | `main/pocket_setup.[ch]`、`pocket_setup_guard.[ch]`、`assets/web/pocket_setup.html` | 临时 WPA2 热点、按键打开的网页会话、分卡保存及电脑配对 |
| 存储 | `main/pocket_config.[ch]`、`pocket_json.h` | 带版本 NVS 配置/缓存、事务性部分更新、扁平 JSON 检查 |
| 协议/状态辅助 | `main/pocket_snmp_codec.[ch]`、`pocket_confirm.[ch]`、`pocket_verification.[ch]`、`pocket_genshin_diagnostic.h` | 有界 BER、动作确认、临时人工验证、安全错误标记 |
| 电脑入口 | `Codex-Assistant.cmd`、`tools/codex_assistant.py` | 共用额度/电脑资源服务的 Windows 窗口 |
| 电脑服务 | `tools/codex_companion.py`、`tools/pc_metrics.py` | 只读额度采集、Windows 计数器、有鉴权的局域网 API |
| USB 备用 | `WiFi-Setup.cmd`、`tools/wifi_setup.py`、`tools/configure_pocket.py` | 可选本地设置；日常手机设置不需要它 |
| 板级支持 | `components/bsp/` | 共用显示、按键、电池等接口，不放应用页面逻辑 |
| 资源 | `assets/fonts/`、`assets/web/` | 有许可证的字体源、生成字体、准确字形清单和设置网页 |
| 验证 | `tests/test_pocket_*`、`tests/test_*companion.py`、`tests/test_pc_metrics.py`、`tests/render_pocket_ui.c`、`tools/validate.sh` | 纯逻辑、故障注入、服务测试及真实 LVGL 主机渲染 |

不要把 IDF 自动下载依赖搬进 `main/`，也不要提交 `build/`、私密配置、凭据、串口原始
日志、ELF 或固件。[源码打包工具](../../tools/package_pocket_source.py)保留完整基线项目，
不会把本应用裁剪成缺文件的 SDK。

## 任务与所有权约定

按键回调只入队。UI 任务拥有页面/模型并处理结果；网络 worker 拥有同步请求和数据源
状态，不能访问 LVGL。所有界面访问都持有 `bsp_lvgl_lock()`，包括创建/删除二维码。
DNS、TLS、HTTP、SNMP、NVS 和等待期间不能持有界面锁。设置服务使用独立 HTTP
server 任务和配置互斥锁；配网期间网络 worker 暂停业务请求。电脑上的线程不占
Passport 的任务或栈。

数据源是网络 worker 调用的同步函数。只读卡片页参照
[pocket_pc.c](../../main/pocket_pc.c) 的 `pocket_pc_refresh()`：

```c
esp_err_t pocket_custom_refresh(const pocket_config_t *config,
                              pocket_snapshot_t *snapshot);
```

- 只有响应结构可用才返回 `ESP_OK`。可选指标缺失可显示 `--`；必要结构损坏应返回错误。
- `snapshot->value[0..5]` 必须是以 NUL 结束的 UTF-8，每项最多 63 字节。日期、单位和
  文案需适合 97 像素宽的值区域。
- `updated_at` 是数据源上一次成功时间，单位 Unix 秒。不能把旧数据时间改成设备当前
  时间，让过期数据看起来刚更新。
- 返回值是 ESP 错误，不是 HTTP 状态码或服务返回码。后两者单独保存用于诊断，不记录
  含令牌的 URL、Cookie、账号资料或二维码票据。
- 正常成功路径会复制快照并设为 ready；数据源返回 stale 时，UI 结果处理会随后标记
  过期。当前该处提示偏向电脑数据，新接非电脑来源时应改为相应文案，不要把它误当作
  已有的通用插件回调。
- 失败保留最后成功值与原时间；缺配置/认证失败应与临时网络失败区分。

HA 控制和原神扫码登录各有独立的多步状态。普通指标页不要照搬这些流程。新控制动作
必须把预览和确认绑定到准确目标、动作、参数，设置超时，阻止重复提交，拒绝过期目标，
不自动重试已经被接受的操作。

## 新增第六个只读页面

1. 在 [pocket_model.h](../../main/pocket_model.h) 的 `POCKET_PC` 后追加 `POCKET_CUSTOM`，
   增加 `POCKET_PAGE_COUNT`。保持 0–4 的现有 ID 稳定；NVS 缓存使用页 ID，插入或重排
   不只是界面顺序变化。
2. 在 [pocket_ui.c](../../main/pocket_ui.c) 增加标题、六行名称与颜色。复用现有六张卡片
   和分页点。标题/颜色必须各有与页数相等的项，不存在隐式 provider 注册。
3. 实现 `main/pocket_custom.c`，在 [pocket_services.h](../../main/pocket_services.h) 声明，
   并加入 [main/CMakeLists.txt](../../main/CMakeLists.txt)。文件使用 `pocket_` 前缀，
   字体工具才会扫描对应 `.c`、`.h` 的固定文案。
4. 在 `network_worker()` 的 HA 兜底分支前加入明确的数据源分发。否则未知页可能错误
   请求 HA 列表。用数字页号索引数组前需核对边界。
5. 补齐 `ui_worker()` 的 `intervals[POCKET_PAGE_COUNT]`。模型当前每页最短请求间隔为
   30 秒；只改自动刷新间隔不会绕过模型限流。
6. 数据源自带时间时，扩展 worker 中 Codex/PC 所用的时间保留条件；按需补充专属错误
   和过期文案。
7. 明确决定是否持久化。目前只有原神、Codex、NAS 写 Flash 缓存；`last_cached[3]` 和
   `< POCKET_MIHOME` 的存取边界刻意排除了控制状态和电脑指标。增加页数不会自动打开
   缓存，也不要直接放宽边界把控制状态缓存进去。
8. 在 [render_pocket_ui.c](../../tests/render_pocket_ui.c) 添加模拟布局样本，更新
   [test_pocket_model.c](../../tests/test_pocket_model.c) 的翻页环绕预期，并测试新协议的
   损坏、缺字段、过期与超长输入。
9. 若共用电脑服务，在电脑助手添加带鉴权的路由。`/v1/custom` 只是设计示例，不是本版
   已提供的接口。账号登录凭据留在电脑，只向板端导出显示数据。

新增电脑数据页时，优先复用同一服务的地址和配对令牌。若确需新增配置字段，必须同步
修改 C 结构、`FIELDS`、校验、Python 长度限制以及相关设置白名单/表单。当前配置是
版本 1 的固定 C 布局，改变大小/版本之前必须实现显式迁移；初始化不会自动迁移旧
blob，也不会靠擦除 NVS 修复不匹配。改变快照布局也须考虑缓存版本和迁移。

电脑行偏好展示了另一种增量方式：独立 NVS 字符串键 `pc_display` 保持原配置 blob 与
快照字节布局不变。专用设置路由、校验、状态返回和界面标签共用指标 ID。扩展指标时，
要同步纯逻辑 ID/标签表、数据源格式化、电脑端可选字段、手机复选框和测试。
回归测试使用真实配置代码，覆盖已有版本 1 blob、重启与提交失败，不能用恢复出厂替代。

## 硬件与内存验收

ESP32-C3 没有 PSRAM。网络请求保持串行，板级总线和 ADC 继续由 BSP 管理，复用六张
卡片，不要为每页分配完整屏幕对象。40 KiB LVGL 池与普通堆分开，图形池还有余量不等于
TLS 能成功分配；开发日志里的 E101 就说明了这一点。

`pocket_http()` 的响应最多 12 KiB，TX 按需为 1–6 KiB，响应有界增长；未知长度不等于
无上限。不能靠缩小 TLS 记录限制或关闭证书校验塞入新页面。复杂采集应移到电脑，
只返回小型规范化 JSON。Windows CPU 是相邻采样的平均值，不是进程分析器。

字体准确覆盖 21,352 个映射字符，不是全部 Unicode。按[中文字体说明](engineering/lvgl-chinese-fonts.zh_CN.md)
检查固定文案、动态文本、长名称和实屏可读性，不能隐藏缺字来通过验收。

## 验证与交付

开发期间运行相关测试，交付固件前在已激活的 ESP-IDF 5.5.3 环境运行现有完整门禁：

```text
bash tools/validate.sh
python tools/check_pocket_font.py
node tests/test_pocket_setup_web.cjs
python tools/render_pocket_ui.py --lvgl managed_components/lvgl__lvgl --zig <zig-path>
```

Node/渲染命令是额外检查，不代替完整门禁。渲染需要锁定的 Managed Components 和
Zig；纯主机门禁使用 C 编译器与 Python。模拟样本不能当作实时数据。分别报告 Build、
Host tests、Device tests、Unverified。交付经过校验的 `0x0` 合并镜像及其匹配的按哈希
归档的调试包。获准刷机前读[布局策略](engineering/firmware-layout.zh_CN.md)；保留设置
通常采用布局兼容的仅应用更新，而不是把合并镜像覆盖到 NVS。烧录和发布均须先获授权。

## 可配置导航扩展

`pocket_page_layout_t` 与纯逻辑解析器位于 `pocket_model.[ch]`。页面 ID 仍为固定数据源/
缓存索引，排序与启用视图独立。新增页面时，随 `POCKET_PAGE_COUNT` 同步扩展 `PAGE_IDS`、
设置表单及协议/测试，并显式迁移旧列表，不能悄悄丢弃原顺序。仅 RAM 模型增加成员，
序列化快照与版本 1 凭据结构不变。提交失败保留当前视图；应用合法列表后显示首个启用页。
NVS、翻页环绕和实际网页脚本测试覆盖独立存储、拒绝全停用、仅一页时的导航。


## 息屏控制与数值型电脑子页

`pocket_screen.[ch]` 是可主机测试的背光策略。BSP 的 PRESS/RELEASE 与语义按键一同
入队，应用动作前抑制整个唤醒手势。仅 UI 任务改背光，HTTP 任务只发原子请求。
设置/二维码/遥控流程抑制自动息屏，不可用深度休眠准备 API 代替关闭背光。
超时独立保存在 NVS，不混入凭据。

PC 数据源现同时返回紧凑 `pocket_pc_data_t` 与六行快照。数值和有效位只留 RAM，
由 UI 任务按当前子页格式化六项。`pocket_pc_navigate()` 遍历子页及稳定主页面 ID。
`tools/pc_performance.py` 提供可选 Windows 原生计数器，助手打包必须包含它；服务器
关闭时清理 PDH 查询。启动配置临时副本改为堆分配，读完清零释放，给默认 3584 字节
主任务栈留余量。新增单位、尾页空行、停用电脑页导航、唤醒抑制和旧 NVS 偏好均须验证。
