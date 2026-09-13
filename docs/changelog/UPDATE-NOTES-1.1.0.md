# ZSteamTool v1.1.0 更新说明

> 适用版本：v1.1.0（对比上一版本 1.0.0）
> 基线：OpenSteamTool 上游 `2a08b0b` + BetterSteamTools 主体对齐 + 本地补丁（同 1.0.0）

## 新功能

- **D 加密模式开关（`[denuvo] mode`）**：配置文件新增 `[denuvo]` 段，二选一，**改完热重载，无需重启 Steam**：
  - `mode = "normal"`（**默认**，= 上游 OpenSteamTool 语义）：`GetSteamID` 只在 D 加密授权握手窗口内使用票据所属账号，**窗口外返回你当前登录的账号** → 游戏按 SteamID 派生的用户数据（存档目录、云存档、设置）始终绑定**你自己的账号**；
  - `mode = "compat"`（= BST 行为，1.0.0 的既有行为）：整场使用票据所属账号，供少数在窗口外仍会复验 SteamID 的严格标题使用；**代价是该游戏的用户数据会绑定出票账号**（保存档请自行迁移）。
  - 模式生效范围覆盖三处触点（`GetSteamID` 窗口门、窗口外所有权票源、授权窗口结束时的 SteamID 持久化），保证每种模式内部自洽，不会出现"半场伪装"。

## 修复与增强

- **修复 D 加密游戏"读不到自己的存档"（身份一致性）**：根因是 1.0.0（BST 语义）把 SteamID 整场伪装成票据账号，导致游戏按出票账号的 `账号ID` 建立/读取存档目录。本次改动：
  1. `GetSteamID` 恢复**授权窗口门**（窗口外不伪装）；
  2. 窗口外所有权票源改回 **ForgeOnly**（forge 票取自 app 7 缓存票，身份 = 当前登录账号），与窗口外 `GetSteamID` 一致；
  3. **单写者**：授权窗口结束时**不再**把"当时登录的账号"写进注册表 `HKCU\...\Apps\<appid>\SteamID`——该值是身份随登录账号漂移、进而串档的第二来源；
  4. `GetSpoofSteamID` 在 `normal` 下只认**票据内身份**，忽略注册表里旧 `compat` 运行遗留的值（避免窗口内身份与票据不符 → 012 / 54）。
- 承接 1.0.0 的三条本地补丁不变：858 `OwnershipTicket` Forge 兜底、manifest 请求码等待窗口 30s、`config/lua/manifest.lua` 短路防第三方码注入。

## 构建与发布

- `OpenSteamTool.dll` 内嵌版本号同步为 1.1.0（`VERSION` 文件即当前版本）；
- 发布包 `ZSteamTool-v1.1.0.zip` 内含 `Debug\`（带日志，排障用）与 `Release\`（日常用）两个版本，各含三个 DLL，附 `opensteamtool.toml` 与 `README-v1.1.0.txt`。

## 配置说明

- 配置文件 `opensteamtool.toml` 置于 Steam 根目录；**旧版 toml 不带 `[denuvo]` 段也能直接用**（缺省即 `normal`）。
- 关键小节：`[denuvo] mode`（本次新增）、`[log]` 日志级别、`[manifest]` 码源与超时、`[lua] paths` 附加配置目录、`[cloud]` 云存档重定向（默认关）、`[inject]` 游戏进程注入（默认关）。

## 已知限制

- **`compat` 模式下"用户数据绑定出票账号"是协议层必然**：eticket 内的 SteamID 由 Valve 加密签名，无法伪造；若某标题确实需要整场以出票账号运行，其存档/云存档绑定该账号无法通过工具消除，只能手动迁移。
- 若某标题在 `normal` 下启动报 **012 / 88500012**，说明它会在授权窗口外复验 SteamID → 将该游戏切到 `compat`（或在 lua 中 `forcedenuvo(appid)` 辅助识别）。
- **Steam 客户端大更新预警（Beta #1788989629）** 仍在：请勿手动切换 Beta 通道，正式版当前链路可用（见工作区根 `doc/` 与 `doc/EVENTS/`）。
- **下载**：请求码收口环境下正路仍是**清单投喂 depotcache**；第三方码源可恢复部分游戏下载，属第三方依赖，稳定性自担。
- **Denuvo**：设备 token 优先；eticket 在线铸造后端未启用（默认关闭，无网络请求）。

---

## 升级方式

替换 Steam 根目录下的 `OpenSteamTool.dll` / `dwmapi.dll` / `xinput1_4.dll` 三个文件（升级前建议备份旧文件），`opensteamtool.toml` 按需更新（补上 `[denuvo]` 段即可显式选择模式），重启 Steam 生效。

> 相关文档：上游基线与本地改动见 `docs/dev/UPSTREAM-SYNC.md`；开发笔记 `docs/dev/DEV-NOTES.md`；构建 / Lua API / 兼容性等技术细节见 `docs/upstream/README-en.md`。
