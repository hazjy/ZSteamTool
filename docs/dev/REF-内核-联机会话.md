# REF-内核-联机会话（ZSteamTool 侧）

> 只收 **联机会话身份 `-onlinefix` / 全链路身份一致化 / 免费联机 AppID 选择**；分工与文档地图见工作区根 `README.md`。

---

## 联机会话身份 `-onlinefix`（2026-09-10 落地，commit `3dce9c9`）

- **内核侧事实**：`SpawnProcess` int3 回调解析命令行会话身份——`-onlinefix`（默认 Spacewar 480）｜`-onlinefix=<appid>`（显式，推荐）｜`-onlinefix <appid>`（跟随数字）；**非数字/0/超 uint32 一律回落 480**（`Hooks_Misc.cpp:41-60`）；同命令行出现 `-realappid` 时抑制本次 P2P appid 翻转。
- **全链路 8 处身份一致化**（`Hooks_Misc::SessionAppId()`）：① OptedInMask ② BuildSpawnEnvBlock（**只记日志、不改值** → 覆盖层保持 480，本地刻意与 BST 相反）③ LobbyInvite 改写 ④ 所有权票 appid 归一 ⑤ RichPresence topmost 排除 ⑥ Persona 好友 480→真实 ⑦ GamesPlayed 名字改写 ⑧ 调试日志。
- **设计意图**：免费联机游戏不止 480 一个，可换任意免费 AppID（含带 Steam Input 手柄配置者，解决 480 无手柄配置的问题）；**对端需同配**。
- 480 邀请失效的真凶考证与 v1/v2/v3 实证轮见 GUI 侧 `OSTGUI/docs/dev/REF-联机.md`。

## 大厅面：为什么"入库"的游戏建不了大厅（2026-10-16 实证，机械狂欢 4108000）

- MMS 建厅 `k_EMsgClientMMSCreateLobby`(6601) 的 `#1` **不是游戏填的**，由 steamclient 按"该游戏以哪个 appid 启动"填入 —— 纯 480 伪装（`-onlinefix=480`）下它依旧是 4108000；我们的 480 伪装只改了游戏侧（env / `GetAppID`）。
- 同一会话内三次实测（`netpacket.log` 的 `MMS dump`，请求其余字段完全相同）：

| 请求 `#1` | 响应 | 含义 |
|---|---|---|
| 4108000（原样） | `#3=15` = `k_EResultAccessDenied` | 未拥有该 appid |
| 480（改写为会话 appid） | `#3=2` = `k_EResultFail` | 拥有权过了，卡在别处 |
| **1046930（该账号真正拥有的免费游戏）** | `#3=2` = `k_EResultFail` | 仍失败 → **不是"拥有权"一个条件** |

- 为什么改 appid 之后仍是 `Fail`？两个候选解释（**都未证实，但第二个更贴合三组数据**）：
  - ① 请求里除 appid 外还带与该 app 绑定的不透明字段（实测 `#9` 是 5 字节嵌套消息，内含 u32 `0xDB4CFE0D`，不是 appid 的任何简单变换，也不是游戏名/安装目录名的 crc32 或 FNV-1a），服务端要求它与 appid 自洽；
  - ② **报文里的 appid 必须与 steamclient 自己的"本次会话 appid"一致**。证据：MMS 的 `#1` 本来由 steamclient 按**启动 appid**（4108000）填入 —— 也就是说**我们的 480/1046930 只是游戏侧视角，steamclient 仍认为这次会话是 4108000**。于是三种情况自洽地给出三种结果：报 4108000（与会话一致但未拥有）→ `AccessDenied`；报 480 / 报 1046930（已拥有但与会话不一致）→ `Fail`。**没有任何一种既是"已拥有"又"与会话一致"**，因为会话 appid 由启动方式（`-applaunch <appid>`）决定，内核只能在 spawn/GetAppID 等游戏侧伪装，改不掉它。
- 推论（**未实测**）：要过 MMS，需要**让 Steam 侧的会话 appid 本身就是一个已拥有的 appid**（例如不经 `-applaunch 4108000` 启动、而让游戏进程的 pipe appid 直接等于已拥有 appid）。这条没试过，是本主题唯一还值得再验的方向。
- **连锁后果**（与现场完全吻合）：大厅 ID 恒为 0 → 游戏「复制房间ID」显示 **0** → `SetLobbyData` 返回 false → 「邀请」在 IPC 层 `result=OK` 但**方法返回 false**，界面什么都不弹。
- 因此**对这类游戏，"入库 + 内核伪装"无法建厅、也就无法邀请**；换任意已拥有的免费 AppID 当会话身份同样受这条限制。适用范围是否限于"steamclient 侧 appid 为真实 appid 且请求带 app 绑定令牌"的游戏，尚未在别的游戏上验证。
- **边界（2026-10-17 补，实样核对过）**：以上只适用于**"仍在使用真 Steam 客户端"**的路线。OLFIX 式补丁能让同类游戏联机，是因为它**在游戏进程内冒充 Steam 客户端那一层**：实样（online-fix 的 Machine Party 补丁）中 `OnlineFix64.dll`（版本信息 "Online-Fix Steamclient"）的导出面就是 `steamclient64.dll` 的（`CreateInterface` / `Steam_*` / `Breakpad_Steam*`），由 `winmm.dll` 早期加载、另配 `SteamOverlay64.dll` 覆盖层桩，**保留原版 `steam_api64.dll` 不动**；配置里同样是 `RealAppId=4108000 / FakeAppId=480`。
- **重要修正（2026-10-16 晚，实机验证）**：该补丁在本机**开着 Steam 直接启动 `Machine Party.exe`**（**不经 `steam.exe -applaunch`**）后，**建房成功、Steam 邀请好友界面正常弹出**、服务器列表为 480 世界 —— 即**走 Steam 官方通道的 480 联机是通的**。差别只在**会话 appid**：`-applaunch 4108000` 会让 steamclient 把本次会话认成 4108000（于是 MMS 建厅 `#1` 恒为 4108000 → 未拥有 → `AccessDenied`；改写又与会话不一致 → `Fail`），而补丁**不经过 Steam 启动**，会话与游戏侧统一为 480 → 放行。**所以内核路线的缺口不是"Valve 关死"，而是"会话 appid 必须也是 480"**（对应 TODO `待办/04-…` 的重开项）。
