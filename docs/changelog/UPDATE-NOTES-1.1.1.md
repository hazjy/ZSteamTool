# ZSteamTool v1.1.1 更新说明

> 适用版本：v1.1.1（对比 v1.1.0；补丁修正，无功能变更）

## 修复

- **新机器"装了但没用"：默认 Lua 目录不一致**。内核此前默认只监视 `<Steam>\config\stplug-in`（对齐 BetterSteamTools 时引入），而图形界面 **OSTGUI 把入库配置写到 `<Steam>\config\lua`** —— 两者只有在内核配置里显式写 `[lua] paths` 时才打通。因此**全新机器（没有 `opensteamtool.toml`）上入库 / DLC / 解锁全部不生效**。
  现在：默认读取 `<Steam>\config\lua`，并**继续读取** `<Steam>\config\stplug-in` 以兼容旧配置；配置文件热重载后同样保持这两个默认目录的监视。
- **DLL 内嵌版本号错误**。`OPENSTEAMTOOL_VERSION` 原为 CMake **缓存**变量，构建目录沿用了旧版本的缓存值，导致 v1.1.0 的 DLL 内部仍报告 `1.0.0`（仅影响诊断信息中的版本号显示，不影响功能）。现改为唯一来源 `project(VERSION ...)`。

## 配置说明

- **`opensteamtool.toml` 不是必需的**：文件不存在时内核按全套默认值运行 —— D 加密模式 `normal`、Lua 目录 `config\lua`、stats API 开启、云存档重定向关闭、不注入任何库。
- 需要 **`compat`（D 加密兼容模式）** 时：在 toml 里写 `[denuvo] mode = "compat"`，或直接用 OSTGUI 设置页切换（**会自动创建该文件**）。新建 / 修改都会触发**热重载，不必重启 Steam**。
- `[lua] paths` 只在需要**额外**目录时才写；默认的 `config\lua`（以及兼容读取的 `config\stplug-in`）无需任何配置。

## 升级方式

替换 Steam 根目录下的 `OpenSteamTool.dll` / `dwmapi.dll` / `xinput1_4.dll`（`Release\` 日常用、`Debug\` 排障用，日志在 `<Steam>\opensteamtool\`），重启 Steam 生效。已有的 `opensteamtool.toml` **无需改动**。

> 相关文档：上游基线与本地改动见 `docs/dev/UPSTREAM-SYNC.md`；开发笔记 `docs/dev/DEV-NOTES.md`；构建 / Lua API / 兼容性等技术细节见 `docs/upstream/README-en.md`。
