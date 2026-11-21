# ZSteamTool v1.2.2 —— 切号授权修复

> 适用版本：v1.2.2（对比 v1.2.1）

## 注意事项

- **D 加密游戏切号授权（新版 Steam）**：该游戏带 lua 时，可能需要先禁用解锁（GUI 入库页「更多 → 禁用解锁」，或把 `<AppID>.lua` 改名为 `.lua.disabled`），再用有正版或家庭共享许可的账号成功启动一次游戏，让内核把真票落盘；之后启用解锁、切回自己的账号即可。票据缓存（HKCU 的 `AppTicket` / `SteamID` 或账号 `localconfig.vdf` 的 `apptickets`）被清空后，需要重新做一次。
- **家庭共享 + lua 改走假入库分支**：购买状态、成就、时长挂在假许可上；好友侧是否显示在玩什么取决于「向好友显示正在玩的游戏」开关（对应 `[presence] display`，关闭后看不到）。共享的真许可与内核伪造的「真拥有」是否不自洽仍无定论，遇 `88500005` 先禁用该游戏的解锁再试。

## 修复

- **切号后 D 加密游戏 `88500000`**：新版 Steam 把所有权票缓存在各账号的 `localconfig.vdf`，内核只读 HKCU，切号后票库为空只能伪造。现在 `858` 成功分支与 IPC `GetAppOwnershipTicketExtendedData` 的有效许可分支会把服务端真票写入 HKCU 的 `AppTicket` + `SteamID`（校验长度与 SteamID、同票去重）；正版或家庭共享账号启动一次即落票，切到无许可账号后授权窗口内直接出这张票。假入库拿不到 OK 票，不会写入。
- **家庭共享游戏被提示「账号在别处登录」并掉线（eresult 6）**：服务端改为按自己记录的许可判定后，内核继续丢弃停玩 / 锁库通知会造成账目对不上。现按上游方案改走假入库分支，并清干净共享标记，不再触发强行掉线。

## 更改

- 掉线（757）时解析并记录 `eresult`，日志里能区分「别处登录 / 会话被替换 / 远端断开」。

## 升级方式

- 完全退出 Steam（托盘图标也要退出）。
- 把 `Release\` 里的 `OpenSteamTool.dll`、`dwmapi.dll`、`xinput1_4.dll` 覆盖到 Steam 根目录（`steam.exe` 所在文件夹），覆盖前先备份原文件。
- 重新启动 Steam。配置、lua 与已入库游戏都不受影响，无需重新入库；`opensteamtool.toml` 可保留原文件，缺失的段按默认值工作。
- 想让好友看到正在玩的游戏，在 GUI 设置里勾选「向好友显示正在玩的游戏」；内核侧对应 `[presence] display = "spacewar"`。

---

[![](https://img.shields.io/badge/powered_by-dsh-4D6BFE?style=flat-square&logo=deepseek&logoColor=white)](https://github.com/deepseek-ai/deepseek-harness)
