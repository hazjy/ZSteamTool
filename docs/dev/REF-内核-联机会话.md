# REF · 内核联机会话（`-onlinefix`）

> **状态：2026-10-17 起联机工作暂停，本文件冻结**。完整实测数据在 `doc/EVENTS/报告-机械狂欢联机可行性-2026-10-16.md` §七。
> 何时读：重开联机、排查大厅建不起来 / 邀请弹不出、或改会话身份相关代码时。
> 管什么：会话身份机制与全链路一致化、大厅建不起来的服务端判定、已跑通的做法及其边界。
> 不写什么：试错流水见 `doc/EVENTS/`；分支做了什么、提交如何分类见该分支的 git 提交信息。

---

## 一、会话身份 `-onlinefix`（2026-09-10 落地，commit `3dce9c9`）

- 解析：`SpawnProcess` 的 int3 回调读命令行：`-onlinefix`（默认 Spacewar 480）｜`-onlinefix=<appid>`（显式，推荐）｜`-onlinefix <appid>`；**非数字 / 0 / 超 uint32 一律回落 480**（`Hooks_Misc.cpp:41-60`）。同命令行出现 `-realappid` 时抑制本次 P2P appid 翻转。
- 全链路 8 处身份一致化走 `Hooks_Misc::SessionAppId()`：① OptedInMask ② BuildSpawnEnvBlock（**只记日志不改值**，覆盖层保持 480，本地刻意与 BST 相反）③ LobbyInvite 改写 ④ 所有权票 appid 归一 ⑤ RichPresence topmost 排除 ⑥ Persona 好友 480→真实 ⑦ GamesPlayed 名字改写 ⑧ 调试日志。
- 意图：免费联机游戏不止 480 一个，可换任意免费 AppID（含带 Steam Input 手柄配置者，解决 480 无手柄配置的问题）；**对端需同配**。
- **会自检 appid 的游戏换身份就起不来**：个别游戏在初始化阶段校验"客户端报告的 appid 是否等于自己的 appid"，不符就静默退出，表现为无窗口、无报错、存档目录一条不写、系统日志无崩溃记录。实测 2026-11-17：Terraria（105600）走 `-onlinefix=480` 起来约 9 秒自退，改 `-onlinefix=105600` 正常进游戏。这类游戏直接启动即可（假许可已让客户端认成本 appid），换身份没有收益。

## 二、大厅为什么建不起来（2026-10-16 实证，机械狂欢 4108000）

- MMS 建厅 `k_EMsgClientMMSCreateLobby`(6601) 的 `#1` **不是游戏填的**，由 steamclient 按"该游戏以哪个 appid 启动"填入；纯 480 伪装（`-onlinefix=480`）下它依旧是 4108000。我们的伪装只改了游戏侧（env / `GetAppID`）。
- 同一会话内三次实测（`netpacket.log` 的 `MMS dump`，其余字段完全相同）：

| 请求 `#1` | 响应 | 含义 |
|---|---|---|
| 4108000（原样） | `#3=15` = `AccessDenied` | 未拥有该 appid |
| 480（改写为会话 appid） | `#3=2` = `Fail` | 拥有权过了，卡在别处 |
| **1046930（该账号真正拥有的免费游戏）** | `#3=2` = `Fail` | 仍失败 → **不是"拥有权"一个条件** |

- 结论：报文里的 appid 必须**同时**满足"已拥有"和"与服务端记录的本次会话一致"。会话 appid 由启动方式决定，游戏侧伪装改不掉它。因此"入库 + 内核伪装 + `steam.exe -applaunch`"这条路上建不了厅。
- **环境变量也改不掉这份关联**：把游戏复制到库外、用 `OnlineHost` 设 `SteamAppId/SteamGameId/SteamOverlayGameId=480` 拉起，游戏侧 `MasterServerQuery` 确实是 480，但 steamclient 仍把该进程认成 4108000（`CreateLobby #1` 与 `GamesPlayed` 的 `game_id` 都是它）。
- 连锁后果：大厅 ID 恒为 0 → 「复制房间 ID」显示 0 → `SetLobbyData` 返回 false → 「邀请」在 IPC 层 `result=OK` 但方法返回 false，界面什么都不弹。
- **边界**：以上只适用于"仍用真 Steam 客户端"的路线。OLFIX 式补丁能让同类游戏联机，是因为它在**游戏进程内冒充 Steam 客户端那一层**（`OnlineFix64.dll` 的导出面就是 `steamclient64.dll` 的，配 `SteamOverlay64.dll` 覆盖层桩，保留原版 `steam_api64.dll`，`RealAppId=4108000 / FakeAppId=480`）。它不在真客户端这条路上，走的是进程内替换 Steam 层。

## 三、已跑通的做法（2026-10-16 11:30 实测）

- 做法 = **"环境变量会话 + 客户端侧 appid 洗白"，零报文改写**：
  - 游戏不经 Steam 启动（GUI 的 `OnlineHost`），给进程设 `SteamAppId / SteamGameId / SteamOverlayGameId = 会话身份`；内核在 pipe 握手时用 `ProcessInspector` 从环境变量登记会话（`Hooks_Misc::EngageEnvSession`）。
  - `GetAppIDForCurrentPipe` 从一次性捕获改为真 hook：当"该 appid 在 lua 里配过（`HasDepot`）"且与会话身份不同时，把客户端内部算出的真 appid 换成会话身份；**只洗客户端内部调用**，内核自身逻辑仍取原始值。
  - 结果：`CreateLobby #1=480` → `#3=1` + 大厅 SteamID；`SetLobbyData` / `LobbyData` / `LeaveLobby` 全 `#3=1`；**能建房、能弹出邀请弹窗**。提交 `8755f4e`（钩子）/ `e5c3ce2`（总开关），2026-10-17 那批无效报文改写已删除。
- **仍未验证**：对端同配能否加入（闭环最后一环）、其它入库游戏的普适性。
- 判据（日志在 `<Steam>\opensteamtool\`）：建房成功看 `MMS CreateLobby(6601) #1=480` → 响应带大厅 SteamID → 后续 lobby 调用全 `#3=1`；零报文改写看 `MMS appid rewrite` 行数为 0；会话生命周期看 `armed(宿主) → re-armed(游戏) → cleared(退出)` 三行齐。
- 边界：垫片按文件名认 `steam.exe`（改名即不注入）；同一时间只支持一个会话游戏；宿主与游戏在 Steam 侧是两条独立记录。
