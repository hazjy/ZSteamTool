# ZSteamTool v1.0.0 更新说明

> 适用版本：v1.0.0（首个正式版）
> 基线：OpenSteamTool 上游 `2a08b0b` + BetterSteamTools 主体对齐（含上游 PR#146 multi-inject/P2P、PR#148 env-less/结构检测）+ 本地特有合入项

## 新功能

- **自定义联机会话身份**：`-onlinefix=<appid>`（或 `-onlinefix <appid>`）可把联机会话身份从默认的 Spacewar(480) 换成任意 AppID——免费联机游戏不止 480 一个，也可挑选带 Steam Input 手柄配置的 AppID 以解决 480 无手柄配置的问题；不带参时行为与旧版完全一致（回落 480）。对端联机需使用相同会话身份；
- 覆盖范围：P2P 身份翻转、LobbyInvite 校验、Persona 好友同玩、GamesPlayed、控制器 opt-in、票据身份还原、覆盖层身份、RichPresence topmost 全部随会话身份一致化。

## 修复与增强

- **858 所有权票据 Forge 兜底**：无凭据库票据、无铸造后端时以本地伪造票据应答，修复下载前"更新所有权票 AccessDenied"导致的流程中断；
- **manifest 请求码等待窗口 12→30 秒**：内置码源慢响应时不再因等待窗口过短而丢码（表现为下载报"无法获取清单请求码"）；
- **请求码注入短路**：`<Steam>/config/lua/manifest.lua` 采用返回 `"0"` 的短路实现——阻止一切第三方注入码覆盖 CM 原始应答（避免无效码导致的 CDN 401 干扰）。

## 构建与发布

- **构建修复**：显式指定 `/utf-8`，Release 配置可正常编译（此前仅 Debug 携带该选项，Release 因源码中文注释按 CP936 解码而编译失败）；
- **双版本发布包**：`ZSteamTool-v1.0.0.zip` 内含 `Debug\`（带日志，排障用）与 `Release\`（日常使用）两个版本，各含三个 DLL。

## 配置说明

- 配置文件 `opensteamtool.toml` 置于 Steam 根目录；
- 关键小节：`[log]` 日志级别、`[manifest]` 码源与超时、`[lua] paths` 附加配置目录、`[cloud]` 云存档重定向（默认关）、`[inject]` 游戏进程注入（默认关）。

## 已知限制

- **Steam 客户端大更新预警（Beta #1788989629）**：客户端内部重构，Beta 通道强制更新；请勿手动切换 Beta 通道，正式版当前链路可用（详见 `../../doc/内核-事实考证.md`）；
- **下载依赖清单投喂**：服务器对非拥有账号的清单请求码签发受限，新游戏下载需将清单文件放入 `steam\depotcache\`（详见 `../../doc/EVENTS/02-下载与请求码.md`）；
- **Denuvo**：设备 token 优先；eticket 在线铸造后端未启用（默认关闭，无网络请求）。

---

## 升级方式

替换 Steam 根目录下的 `OpenSteamTool.dll` / `dwmapi.dll` / `xinput1_4.dll` 三个文件（升级前建议备份旧文件），`opensteamtool.toml` 按需更新，重启 Steam 生效。

> 相关文档：发布说明 `docs/RELEASE-v1.0.0.md`；开发笔记 `docs/dev/DEV-NOTES.md`；事件与调研 `EVENTS/`（工作区根）。