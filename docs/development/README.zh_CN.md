<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 工程规范（Development）

本目录存放 AI Passport 的工程开发规范和可复用工作流，按用途分组：AI 开发工作流（`ai-guide.md`）、工程约定（`engineering/`）、CI 文档（`ci/`）、发布/完成流程（`release/`）。

## 收录标准

- 收录构建验证、代码风格、注释、测试、资源约束和 AI 开发流程。
- `ai-guide.md` 面向 AI 编程助手，允许包含本项目结构和硬件边界。
- 每条规则应写清触发条件、必须做什么、禁止做什么、验证方法和例外条件。
- 涉及本板具体硬件事实的结论引用 `docs/hardware-design/`，不重复。
- 可以由 lint、测试或脚本强制的要求，应同时落实到自动化检查，不能只靠 agent 阅读文字。
- 新增规约时在本文件更新索引。

## AI 工作流

- [ai-guide.zh_CN.md](ai-guide.zh_CN.md)：AI 开发工作流（面向 AI 编程助手：上下文建立、需求拆解、BSP 边界、验收交付格式）。
- [核心 AI 技能](../../skills/README.zh_CN.md)：开发、环境准备、构建、真机测试及诊断，包含安装和使用示例。

## 多页便笺应用

- [开发者交接说明](pocket-notes-handoff.zh_CN.md)：完整源码包、新电脑准备及自定义页面入门。
- [使用指南](pocket-notes.zh_CN.md)：五页功能、手机设置与电脑助手。
- [源码导航与专属页开发](pocket-notes-page-development.zh_CN.md)：模块职责与完整的扩展清单。
- [已实现接口规范](pocket-notes-protocol.zh_CN.md)：电脑 JSON 鉴权、配对、设备设置及配置限制。
- [开发记录](pocket-notes-development-log.zh_CN.md)：决策、E101 修复、已验收固件身份及未验证事项。
- [米家八键方案调研](mihome-eight-key-research.zh_CN.md)：现有项目、原生绑定限制及 HA 替代路线。

## 工程约定（engineering）

- [game-demo-to-device-acceptance.zh_CN.md](engineering/game-demo-to-device-acceptance.zh_CN.md)：共享 C/Wasm 游戏逻辑，先验收网页玩法，再验收真机表现。
- [environment-setup.zh_CN.md](engineering/environment-setup.zh_CN.md)：AI 在全新机器上的环境引导，包含国际与中国大陆下载线路。
- [build-and-test.zh_CN.md](engineering/build-and-test.zh_CN.md)：构建与验证（ESP-IDF 命令、逻辑测试、改动验证要求）。
- [firmware-layout.zh_CN.md](engineering/firmware-layout.zh_CN.md)：默认/用户自定义分区布局与合并产物验证。
- [coding-conventions.zh_CN.md](engineering/coding-conventions.zh_CN.md)：代码约定（语言风格、复用、注释、测试同步、资源约束等），包含中文字体接入、空白/方框排查与显示验收。
- [lvgl-chinese-fonts.zh_CN.md](engineering/lvgl-chinese-fonts.zh_CN.md)：CJK 配置、字体生成/链接、fallback 示例、缺字检查和故障排查的分步指南。
- [wifi-provisioning.zh_CN.md](engineering/wifi-provisioning.zh_CN.md)：参考 BLUFI 分支实现蓝牙 Wi-Fi 配网，包含配套小程序名称及接入检查。

## CI（ci）

- [CI-validation.zh_CN.md](ci/CI-validation.zh_CN.md)：Pull Request 与 main 的自动仓库检查、host tests 和固件验证。
- [CI-build-and-release.zh_CN.md](ci/CI-build-and-release.zh_CN.md)：自动构建与发布说明（tag 触发自动编译固件并发布 Release）。
- [CI-sync-main.zh_CN.md](ci/CI-sync-main.zh_CN.md)：上游同步说明（定期把上游 `FoloToy/ai-passport` 的 `main` 同步到本 fork 的 `main`）。

## 发布/完成流程（release）

- [publish-to-community.zh_CN.md](release/publish-to-community.zh_CN.md)：发布到社区说明（把当前固件发布到 AI Passport 社区市场）。
- [project-completion.zh_CN.md](release/project-completion.zh_CN.md)：项目开发完成流程说明（一组可选收尾动作）。
- [file-issues.zh_CN.md](release/file-issues.zh_CN.md)：提交 issue 说明（把建议作为上游 GitHub issue 提交）。
