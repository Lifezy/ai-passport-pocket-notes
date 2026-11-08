[English](pocket-notes-protocol.md) · **简体中文**

# 多页便笺：已实现的接口协议

本文描述当前源码实际实现的协议，不表示已有运行时插件或任意设备兼容能力。
先读[专属页开发规范](pocket-notes-page-development.zh_CN.md)。电脑 JSON schema、
NVS blob 版本与源码修订是三个独立概念。

## 电脑服务

源码：[codex_companion.py](../../tools/codex_companion.py)、
[pc_metrics.py](../../tools/pc_metrics.py)、[codex_assistant.py](../../tools/codex_assistant.py)。
GUI 监听电脑所选局域网 IPv4 的 8765 端口。CLI 默认只监听回环，支持 `--bind`、
`--port`、`--proxy`、`--interval`。改用其他端口时，手动配置的基础地址也必须同步；
手机六位码配对目前固定连接 8765。

| 方法/路径 | 鉴权 | 结果 |
| --- | --- | --- |
| `GET /v1/codex` | `Authorization: Bearer <PAIRING_TOKEN>` | 规范化额度快照 |
| `GET /v1/pc` | 同一 bearer 令牌 | Windows 汇总资源快照 |
| `POST /v1/pair` | 明确生成的一次性六位码 | 只读显示服务令牌，不是账号登录凭据 |

GET 路径要求准确匹配，查询字符串不是发现接口。缺少或错误的 bearer 返回 401，
已通过鉴权但路径未知返回 404；成功 JSON 带 `Cache-Control: no-store`。没有允许
任意网站访问的 CORS，也没有远程 shell、兑换重置卡、文件浏览或进程控制 API。

请求示例只使用占位令牌：

```http
GET /v1/pc HTTP/1.1
Host: 192.168.1.20:8765
Authorization: Bearer <PAIRING_TOKEN>
```

### 六位码配对

GUI 必须先通过启动/生成新码按钮调用 `begin_pairing()`。配对码按单调时钟计算，
有效 120 秒，最多五次尝试，成功后消费；新码立即替代旧码。尚未生成码、码已消费，
或其他 POST 路径返回 501；无效请求、过期码及次数耗尽返回 403。`Content-Type` 必须准确为
`application/json`，请求体 1–128 字节。

```json
{"code":"123456"}
```

`123456` 是文档样本，不是实际配对码。成功响应：

```json
{"token":"<PAIRING_TOKEN>"}
```

持久令牌在本地生成，保存在 `build/private/companion.token`，不是 Codex 的账号令牌。
当前文件格式是 32–128 个 ASCII 字母、数字、下划线或短横线，请保密。配对和指标使用
局域网 HTTP，不是端到端加密；不要把服务发布到公网。暴露后应有意识地更换配对凭据，
替换令牌文件后也须更新设备。

### Codex 快照：schema 1

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

以上数值全部是模拟数据。周期时长必须采用数据源返回值；`primary`/`secondary` 是
同时生效的额度窗口，不是本期和下一期。现有屏幕命名不够准确，尚未显示
`window_minutes`。百分比为 0–100，时间为 Unix 秒，固件按中国标准时间显示。
余额为字符串，本应用不擅自解释成货币或 token 数；无限额标记与余额未知分别表达。

首次成功采集前 `data` 是 `null`；可选的 `primary`、`secondary`、`credits`、`resets`
及其子字段也可能为 `null`。未知不等于零，不得编造缺失值。重置卡数量为权威总数，
到期明细最多 32 条，不保证完整；只有 `details_complete:true` 时板端才标记最早到期
日期。不会自动兑换重置卡。`error` 只包含脱敏采集错误类别。

服务首次成功前为 `unavailable`，采集失败或超龄后为 `stale`，其余为 `ok`。
默认过期阈值 180 秒；CLI 取 180 秒与三倍采集周期中的较大值。失败保留旧数据及其
原始成功时间。采集器调用已安装 Codex App Server 的只读 `account/rateLimits/read`，
源账号登录凭据留在 Codex。

### 电脑资源快照：schema 1

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

CPU 是相邻计数器采样的平均占用。Windows kernel time 已包含 idle time，
`cpu_percent()` 的算法有回归测试。超过 64 个逻辑处理器时，原生 API 报告当前主
处理器组，不是跨组进程分析器。内存为物理内存使用量；磁盘是 Windows 系统盘，
字节显示为 GiB 时使用二进制单位。开机时长来自 `GetTickCount64`，包含睡眠时间。
不导出主机名、进程名或文件列表。

可选字段 `gpu_percent`、`gpu_temperature_c`、`gpu_memory_used_percent` 来自 NVIDIA
驱动索引 0 的显卡，不是多显卡平均值。百分比为 0–100，温度为摄氏度（接受 0–150）；
显存占用按驱动返回的已用/总 MiB 计算。不支持或读取失败时为 `null`，旧助手可能缺少字段。
GPU 查询失败时 CPU/内存正常仍为 `ok`，GPU 值清空，不冒充为刚采集的旧温度；整体
`stale` 仍表示 Windows 汇总计数器采样失败。受信任驱动工具采用隐藏子进程、两秒超时，
不用 shell，不发送控制参数或查询账号、进程、设备身份。尚未支持 AMD/Intel GPU
传感器及多显卡选择，参考 [NVIDIA SMI](https://docs.nvidia.com/deploy/nvidia-smi/index.html)。

一秒内的请求复用上次样本，API 不增加采样线程；Passport 停留在电脑页时每 30 秒
请求一次。首次没有有效 CPU 差值时为 `null`。接口与额度共用鉴权，但不受 Codex
登录/代理采集失败影响。非 Windows 或计数器不可用时返回 unavailable，后续失败保留
stale 快照。与额度不同，电脑 JSON 不含 `sample_age_seconds`、`error` 字段。电脑
资源不写入设备 Flash 缓存。

## 设备网页设置会话

源码：[pocket_setup.c](../../main/pocket_setup.c)、
[pocket_setup_guard.c](../../main/pocket_setup_guard.c)、
[pocket_setup.html](../../assets/web/pocket_setup.html)。
首次未保存 Wi-Fi 时打开有密码的临时热点。长按下键打开设置，长按确定关闭；设备
在线时使用其局域网 IP。会话十分钟后到期；开机三十秒仍离线时自动提供一次恢复设置。
不依赖自动弹出配网页，按确定在热点码与网页码之间切换。

所有路由要求二维码中随机生成的 32 位十六进制 `k` 查询参数和准确的本机 IP Host；
写入还要求与该 Host 匹配的 Origin。会话密钥与热点密码独立随机生成。不能为了自动
发现而广播管理员二维码 URL 或已保存凭据。

| 方法/路径 | 请求/结果 |
| --- | --- |
| `GET /?k=<SESSION_KEY>` | 本地设置 HTML |
| `GET /status?k=<SESSION_KEY>` | 非秘密的 SSID、NAS/HA/助手地址和 `codex_ready`/`nas_ready`/`home_ready` 布尔值 |
| `POST /save?k=<SESSION_KEY>` | 扁平部分 JSON，白名单见下；成功为 `{"ok":true,"restart":true}` |
| `POST /codex-pair?k=<SESSION_KEY>` | `{"address":"192.168.1.20","code":"123456"}`，内部取得并保存助手令牌 |
| `POST /pc-display?k=<SESSION_KEY>` | `{"pc_display":"cpu,gpu,gpu_temp,memory,disk,status"}`，保存电脑显示项目 |

写入须准确使用 `Content-Type: application/json`。`/save` 接受 2–2304 字节，字段仅
限 `wifi_ssid`、`wifi_password`、`companion_url`、`companion_token`、`nas_host`、
`nas_community`、`home_url`、`home_token`，值均为字符串。`/codex-pair` 接受 2–256
字节，电脑地址为 RFC1918 IPv4，配对码为六位数字，连接固定 8765 端口。成功响应
与保存相同，不把取得的令牌返回浏览器。会话失败为 403；非法/未保存的配置或配对
失败为 400。保存后在响应结束后安排重启。

网页省略空白秘密字段以保留原值，这是 UI 行为，不是“空字符串总是保留”的协议。
调用者明确发送空字符串可以清空该值；开放 Wi-Fi 明确发送 `wifi_password:""`。
`/status` 从不返回已保存秘密。设备网页没有导出原神 Cookie、触发米家设备按键或
执行任意代码的路由。

## 配置与 USB 备用协议

`/status` 还返回非私密字段 `pc_display`。`/pc-display` 沿用会话、Host、Origin、JSON
内容类型检查，请求体 2–512 字节，必须恰好有一个字符串字段 `pc_display`。其值为一至
二十四个不重复、逗号分隔的 ID（完整目录见下文；原有 ID 保留）：`cpu`、`gpu`、`gpu_temp`、`memory`、`gpu_memory`、`disk`、
`disk_free`、`uptime`、`status`。拒绝空格、空项、未知 ID。接口保留顺序，手机复选框
使用上述固定顺序。成功同样返回 `{"ok":true,"restart":true}`，非法或未保存返回 400，
未授权返回 403；默认值为 `cpu,gpu,gpu_temp,memory,disk,status`。

选择独立保存在字符串键 `pocket_notes/pc_display`，不扩大版本 1 配置或缓存快照结构。
缺少/非法的可选偏好使用默认值。保存选择不写入原凭据 blob；该字段刻意不加入通用
`/save` 和 USB 字段白名单。

可选 USB 备用使用 [configure_pocket.py](../../tools/configure_pocket.py)，以短分块发送
扁平 UTF-8 JSON 加换行，最多 4096 字节。只有完整的 `POCKET_CONFIG_OK` 或
`POCKET_CONFIG_REJECTED` 行是确认；其他串口行不可回显，因为可能包含私密输入。
保存是事务性的，成功后重启，不代表获准写固件或擦除 NVS。

| 字段 | UTF-8 最大字节数 | 说明 |
| --- | ---: | --- |
| `wifi_ssid` / `wifi_password` | 32 / 64 | 提交的 SSID 非空；开放网络密码为空，否则为 8–63 字节或恰好 64 位十六进制 |
| `companion_url` / `companion_token` | 192 / 128 | 基础地址和显示服务令牌 |
| `nas_host` / `nas_community` | 63 / 64 | 当前客户端需要 IPv4 和只读 SNMPv2c 团体名，UDP 161 |
| `genshin_uid` / `genshin_region` | 15 / 31 | 进阶 USB 备用；UID 为数字，region 为小写字母/数字/下划线 |
| `genshin_cookie` / `genshin_widget_cookie` | 511 / 511 | 私密进阶字段，通常由原生扫码登录取得 |
| `genshin_device_id` / `genshin_device_fp` | 64 / 64 | 私密进阶字段 |
| `home_url` / `home_token` | 192 / 512 | HA 基础地址及长期令牌 |

只有 USB 支持进阶原神字段。未知字段、嵌套结构不被接受。URL 使用 HTTP/HTTPS，不
包含用户信息、查询或 fragment。Python 额外拒绝占位符和 C0 控制字符；固件检查
字符串结束、长度、CR/LF、Wi-Fi、URL 和原神标识。不得把配置 URL 作为 shell 命令
执行。本应用 NVS 未加密。

## 其他接入及版本兼容

威联通使用有界 SNMPv2c GET/GETNEXT，检查请求 ID/团体名，读取 HOST-RESOURCES-MIB
的 CPU、存储、运行时长；选择最大固定卷，不累加存储池，不发送 SET。HA 使用带
鉴权 REST 模板，每批四个功能项，只有明确确认后才调用白名单服务动作。当前没有
直接小米云登录或已验证的八键设备模拟实现。原神签名/端点来自用户提供的参考，属于
内部接入细节，不是承诺给第三方专属页的稳定公共 API，也不能经助手导出账号资料。

schema 1 应保持字段名/类型、单位、未知/null 含义和成功时间戳。兼容增加可选字段，
消费端忽略不认识的字段；破坏性变更须使用明确的新 schema 及对应固件读取器，不可
悄悄改变旧键含义。响应遵守 12 KiB 上限，并测试 401、未知路由、缺字段、过期、
坏值与分配失败。自定义示例路由必须在两端注册并测试后，才能标记为已实现。

## 页面导航偏好

`GET /status` 新增非私密 `page_layout`。`POST /page-layout` 沿用会话密钥、Host/Origin
及准确 JSON 内容类型检查。请求体 `{"page_layout":"pc,-genshin,codex,-nas,home"}`，
2–160 字节，恰好一个字符串字段。五个稳定 ID 必须各出现一次，前缀 `-` 停用但保留位置。
空格、未知/重复/缺少 ID、空项或全停用返回 400；未授权返回 403；NVS 提交成功返回
`{"ok":true,"restart":true}`。默认 `genshin,codex,nas,home,pc`。
独立字符串键 `pocket_notes/page_layout` 缺少或非法时使用默认值。通用 `/save` 与 USB
不接受此字段。不删除凭据；重启后当前页刷新策略不再请求停用页。


## 扩展指标与显示设置

新增 `pc_display` ID 与 JSON 数据键一致：`memory_used_bytes`、`memory_free_bytes`、
`memory_total_bytes`、`gpu_memory_used_bytes`、`gpu_memory_free_bytes`、
`gpu_memory_total_bytes`、`gpu_power_w`、`gpu_fan_percent`、`gpu_clock_mhz`、
`cpu_clock_mhz`、`disk_read_bps`、`disk_write_bps`、`net_rx_bps`、`net_tx_bps`。
都是可选的非负有限数字或 null，旧助手可能缺字段。功耗 W、频率 MHz、容量字节、
速率字节/秒，未知不是零。原 ID 保留。选择字符串容量含 NUL 为 384 字节，允许最多
24 个不重复的已知 ID（当前 23 项）；`/pc-display` 接受 2–512 字节，六行快照不变。

`GET /status` 新增数字 `screen_timeout`。`POST /display-settings` 只接受
`{"screen_timeout":"60"}`，沿用会话/Host/Origin/JSON 检查，体长 2–160 字节；
字符串值只允许 0、30、60、120、300。提交成功返回通常的重启响应。独立可选 NVS
字符串键 `screen_timeout` 默认 60，不加入 USB 或主配置结构。
`POST /screen-off` 采用相同鉴权，要求恰好两字节 JSON `{}`，返回
`{"ok":true,"restart":false}`。原子请求交给 UI 任务，不在 HTTP 任务访问 LVGL；
设置会话继续。非法输入 400，会话失败 403。不会进入休眠或关闭网络。
唤醒事件抑制至释放并附加短暂保护期，覆盖长按与双击回归。

## 背光亮度偏好

`GET /status` 增加数字 `brightness`，默认 75。`POST /brightness` 接受恰好一个
字符串字段，例如 `{"brightness":"35"}`，体长 2–96 字节，沿用私密会话密钥、
Host/Origin、准确 JSON 内容类型和扁平对象检查；十进制数字字符串值须为 10..100。
非法输入或保存失败返回 400，未授权返回 403。NVS 提交成功返回
`{"ok":true,"restart":false}`，通过原子值交给 UI 任务调用已有 BSP 背光接口。
手机会话继续；HTTP 任务不访问 LVGL/显示。独立可选 NVS 字符串键为
`pocket_notes/brightness`，不扩大 2,464 字节版本 1 凭据或六行快照，缺少/非法时
回退为 75。仅显式提交时写 Flash；亮度修改不改变息屏状态，唤醒恢复所选亮度。
USB 与通用 `/save` 白名单不变，九个路由未超出现有十个处理器上限。

设备端菜单通过专用 `DISPLAY_SAVE` 命令，在既有工作任务调用相同的单字段配置保存
接口；按键回调仅入队。亮度预览由 UI 任务完成，息屏超时仅在保存成功后应用。
纯状态机回归覆盖忙碌、失败、取消和未修改状态。菜单不增加持久化业务页 ID、快照
布局、任务、对外网络路由或依赖。
