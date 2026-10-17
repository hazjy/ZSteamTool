# REF-内核-联机会话（ZSteamTool 侧）
> **【2026-10-17 状态：联机工作已暂停，本文件冻结】** 最终结论与全部实测数据见工作区 `doc/报告-机械狂欢联机可行性-2026-10-16.md` §七（`doc/` 不在本仓库内）。
> 冻结原因：大厅层被"宿主会话 + 客户端 appid 洗白"打通，但游戏层 P2P 始终建不起连接；独立探针显示本机到 Steam SDR 中继节点测不到任何 ping 数据（`CheckPingDataUpToDate=0`、POP 0/0），`ConnectP2P` 连自己都 10 秒 `5003` 超时。

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
- 因此**对这类游戏，"入库 + 内核伪装"**——在**经 `steam.exe -applaunch` 启动**的那条路上——无法建厅、也就无法邀请（换任意已拥有的免费 AppID 当会话身份同样受这条限制）。**该缺口已于 2026-10-16 11:30 补上，见本节末"已跑通的做法"。**
- **边界（2026-10-17 补，实样核对过）**：以上只适用于**"仍在使用真 Steam 客户端"**的路线。OLFIX 式补丁能让同类游戏联机，是因为它**在游戏进程内冒充 Steam 客户端那一层**：实样（online-fix 的 Machine Party 补丁）中 `OnlineFix64.dll`（版本信息 "Online-Fix Steamclient"）的导出面就是 `steamclient64.dll` 的（`CreateInterface` / `Steam_*` / `Breakpad_Steam*`），由 `winmm.dll` 早期加载、另配 `SteamOverlay64.dll` 覆盖层桩，**保留原版 `steam_api64.dll` 不动**；配置里同样是 `RealAppId=4108000 / FakeAppId=480`。
- **重要修正二（2026-10-16 11:17 实测，推翻修正一里的"官方通道"推断）**：把游戏**复制到 Steam 库之外**、用 `OnlineHost`（`SteamAppId/SteamGameId/SteamOverlayGameId=480`）拉起后实测：
  - **游戏侧**确实按环境变量走 480 —— `GameServers.MasterServerQuery` 的 body 仍是 `08 E0 03`（480）；
  - 但 **steamclient 仍把该进程认成 4108000** —— `k_EMsgClientMMSCreateLobby` 的 `#1=4108000`、响应 `#3=15 AccessDenied`；同一会话里 `k_EMsgClientGamesPlayedWithDataBlob` 的 `game_id` 也是 4108000（就是 Steam 界面显示"在玩机械狂欢"的来源）。
  - 结论：MMS 建厅的 appid **由客户端对进程的应用关联决定**，而这份关联**既不是启动 appid 一种来源，也不按库内/库外路径区分**（库外仍是 4108000）。**环境变量只能改变游戏侧视角，改变不了这份关联。**
- **由此反推补丁的机制（推断，其主体加壳不可读）**：补丁在同一台机器上能邀请成功，而 Steam 客户端对它的进程同样会认成 4108000 —— 若它依赖 Valve 的大厅，就该撞同一道 `AccessDenied`。**所以它的大厅/P2P 只能在游戏进程内自答**（`OnlineFix64.dll` 冒充客户端层 + `SteamOverlay64.dll` 供覆盖层），`FakeAppId=480` 是给游戏侧/自有命名空间用的。也就是说：**它不在"真 Steam 客户端"这条路上**，走的是"进程内替换 Steam 层"（= GUI 侧 GSE/Goldberg 的同类，只是接入点不同）。
- **对内核的含义**：内核唯一还可能翻盘的一处是**让客户端对这条 pipe 的应用关联也变成 480**（它已经捕获了 `GetAppIDForCurrentPipe`），使 MMS 的 appid 与会话一致 —— 但这属于改客户端内部状态，风险与收益都待实测。
- **已跑通的做法（2026-10-16 11:30，实测）**：上述那一处**已经做了并且成了**。做法 = **"环境变量会话 + 客户端侧 appid 洗白"，零报文改写**：
  - 游戏**不经 Steam 启动**（GUI 的 `OnlineHost` 宿主启动器），给游戏进程设 `SteamAppId / SteamGameId / SteamOverlayGameId = 会话身份`（480）→ 内核在 pipe 握手时用 `ProcessInspector` 读到的环境变量登记会话身份（`Hooks_Misc::EngageEnvSession`）；
  - 内核把 `GetAppIDForCurrentPipe` **从一次性捕获改为真 hook**：当"该 appid 在 lua 里配过（`HasDepot`）"且与会话身份不同时，把客户端内部算出的真 appid 换成会话身份（4108000 → 480）。**只洗客户端内部调用**，内核自身逻辑仍取原始值；
  - 结果：`MMSCreateLobby #1=480` → 响应 `#3=1（OK）` + 大厅 SteamID `109775241023573114`；`SetLobbyData`/`LobbyData`/`LeaveLobby` 全 `#3=1`；**界面能建房、能弹出 Steam 邀请好友弹窗**（此前恒为"无弹窗、房间 ID=0"）；
  - 提交：`8755f4e`（钩子）、`e5c3ce2`（出站改写总开关，把那批无效改写全部关掉；2026-10-17 那批改写代码已删除，只留 `IClientRemoteStorage` 首参数一处未验证的改写，默认关）；
  - **仍未验证**：对端同配能否加入（闭环最后一环）、以及其它入库游戏上的普适性。适用范围"在别的游戏上验证"这条仍挂着。
