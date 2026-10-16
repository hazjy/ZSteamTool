# REF · 内核联机测试分支（`exp/onlinefix-appid-rewrite`）
> **【2026-10-17 状态：联机工作已暂停，本文件冻结】** 最终结论与全部实测数据见工作区 `doc/报告-机械狂欢联机可行性-2026-10-16.md` §七（`doc/` 不在本仓库内）。
> 冻结原因：大厅层被"宿主会话 + 客户端 appid 洗白"打通，但游戏层 P2P 始终建不起连接；独立探针显示本机到 Steam SDR 中继节点测不到任何 ping 数据（`CheckPingDataUpToDate=0`、POP 0/0），`ConnectP2P` 连自己都 10 秒 `5003` 超时。

> 读它的时机：接手这条分支、要复现/回滚某次内核部署、或排查"联机为什么这样打"的时候。
> 它只描述**分支的面貌**：里面有什么、验证到哪一步、怎么部署回滚、下一步等什么。
> **机制与证据链不在此复述** —— 见 [`REF-内核-联机会话.md`](REF-内核-联机会话.md)；待办与测试清单见 `doc/TODO/进行中/04-机械狂欢联机诊断.md`；GUI 侧配套见 `OSTGUI` 仓库同名提交。

## 1. 这是什么

2026-10-16 一整天的实验分支，主题是"**入库游戏走 Steam 官方大厅联机**"。基点是 `main`（`1a9d2d3`），分支共 21 个提交，分四类：

| 类别 | 提交（时间） | 内容 |
|---|---|---|
| 观测/诊断 | `e1f8382`…`ffdfd01`（10-16 00:15–01:22，7 个） | 出站 IPC 调用画像、好友/大厅响应记录、MMS 字段转储（含下钻一层）、服务器查询观测与 varint 手术改写 |
| 诊断总开关 + 关键修复 | `e5c3ce2`(11:08)、`8755f4e`(11:25) | 那批出站 appid 改写全部**默认关闭**（实测无效）；**客户端侧 appid 洗白**（`GetAppIDForCurrentPipe` 真 hook） |
| 生命周期与开关 | `d25f3a9`(11:46) | 会话绑 `(pid, 创建时间)`、退出即清、换进程重挂；`[onlinefix] env_session` 开关 |
| 作用域/锁定/探针 | `7cc15d7`(20:09)、`c3889d0`(20:33)、`75abe64`(20:42) | 洗白限定到会话那条 pipe；真值改"首次判定时锁定"；会话期内探针（前 12 次打印 `real/locked`） |

另有文档类提交（`dfa8559`、`4bc885a` 修 REF；`6becd9d`…`58a5fc1` 是 CloudRedirect 上游删库后的出处订正，与本主题无关）。

## 2. 实现地图

| 位置 | 职责 |
|---|---|
| `src/Hook/Hooks_Misc.cpp` | `hkGetAppIDForCurrentPipe`：洗白判定（会话已登记 + `real != 会话身份` + `real` 在本机 lua 配过 + `real == 锁定值`）+ 探针；`EngageEnvSession`：登记会话身份、真值、启动退出看门狗；`ResetEnvSession`：进程退出即清 |
| `src/Pipe/PipeManager.cpp` | 握手时调用 `EngageEnvSession(pid, 创建时间, env 身份, 尝试量到的真值)` —— 量值失败传 0（引擎指针未就绪），改由首次判定时锁定 |
| `src/Utils/Config/Config.{h,cpp}` · `opensteamtool.example.toml` | `[onlinefix] env_session`（默认 `true`）：**只作排查用的总开关**，关掉后只剩老的 `-onlinefix` 命令行路线 |
| `src/dllmain.h` | `kEnableAppIdRewrites = false`：MMS 建厅 / 服务器查询 / RemoteStorage 那批报文改写全部不启用 |
| 垫片 `src/dwmapi` · `src/xinput1_4` | 两个垫片都只做"导出转发 + 加载 `OpenSteamTool.dll`"，且**只在宿主 exe 名 = `steam.exe` 时加载内核**（改名即不注入）；任一垫片单独存在也能加载内核 |

## 3. 验证状态（截至 2026-10-17 00:05）

### 2026-10-16 夜 v2 双机实测（好友当房主，本机当加入方）

- **PEAK：正常**（v1 下实测；双向都能建房/加入，两个账号在同一 480 大厅互发消息 `LobbyChatMsg` ✓）→ 老游戏回归通过，`锁定会话 pipe 的真 appid` 与 `4108000 -> 480` 全部按设计工作。
- **机械狂欢：大厅层全通，但加入方最终连不上**，逐层证据：
  1. `CreateLobby(6601) #1=480 → #3=1`（房主建房 ✓）、`InviteToLobby(6621)` ✓、**`UserJoinedLobby(6619)`**（对端确实进厅 ✓）；
  2. 加入方 `JoinLobby(6603) #3=1` ✓（同一 480 大厅 ✓）；
  3. 加入方联网调用**全部 `result=OK`**，且：
     - `46::0xAF9F2205` 响应 129 B = **STUN 服务器 `103.28.54.178:3478`** ✓
     - `46::0x61AE97CB` 请求体 = connect string `steamid:<房主ID>` → 响应 4 B `0A 00 00 00` = **连接句柄 10（受理 ✓）**
     - `46::0x784B61FE` 每 ~0.5 s 轮询一次、响应体 0 B（客户端在等连接状态）
  4. **约 8 秒后 `LeaveLobby`，连接始终未 CONNECTED** ✗ → 游戏侧报 `Trying to call an RPC via a multiplayer peer which is not connected`（`%APPDATA%\Godot\app_userdata\Machine Party\logs\godot.log`）。
  → 结论：**不是身份/appid/大厅的问题，而是 P2P 连接建立本身**；首要嫌疑是**房主侧 `CreateListenSocketP2P` 没建起监听套接字**（插件 `create_host` 在这种情况下返回 `ERR_CANT_CREATE`，而 guest 的 `ConnectP2P` 仍会被受理、然后永远连不上）。
- **v2 的行为改动已验证**：`GetAppID: spoof response 480 -> 4108000` 在 v2 下**消失**（0 条 ✓），进入会话后游戏侧也看 480 ✓。

### 实验过程中被排除的两条（都很值钱）

1. **邀请链接是死路**：点 Steam 邀请会让 Steam 自己启动游戏、绕过宿主会话 → 会话环境变量不存在 → 直接失败；只能用「房间 ID」加入。
2. **"机械狂欢老症状"里有一半是插件状态机 + 测试流程**：GodotSteam 的 `steam-multiplayer-peer`（expressobits 版）在 `active_mode != MODE_NONE` 时拒绝 `create_client`（`steam_multiplayer_peer.cpp:267`），而 `active_mode` **只在成功时被设置、只有 `_close()` 才归零** → **同一进程里先当过房主，就再也点不了 Join**（日志：`The multiplayer instance is already active.`）。修正流程后该错误消失 ✓。加入方必须**全新启动 → 直接 Join**，中途换角色要重启游戏。

### 未实证 / 下一步

- ⚠️ **房主侧 `CreateListenSocketP2P` 的响应句柄未采到**：需要**房主**那一侧在 v3 下跑一轮（本机自己当房主即可，单人可做）→ 看 `ipc.log` 里 4 字节句柄是 0（失败 ✗）还是非 0（成功 ✓）。
- ⚠️ **PEAK 在 v2 下未回归**（v2 把游戏侧 appid 也改成 480，理论上有影响面）。
- ⚠️ 真·双机"进房后能实际玩"仍未达成。

**读日志时的判据行**（都落在 `<Steam>\opensteamtool\`）

- 建房成功：`MMS CreateLobby(6601) #1=480` → `CreateLobbyResponse(6602)` 带大厅 SteamID（11:30 `109775241059253359`、20:38 `109775241059920251`、23:46 `109775241083874096`）→ 随后 `SetLobbyData` / `LobbyData` / `LeaveLobby` 全 `#3=1`。
- 界面：**建房成功且 Steam 邀请好友弹窗正常出现**（但邀请链接本身不可用，见上）；服务器列表是 480 世界。
- **零报文改写**（`kEnableAppIdRewrites=false` 后，`MMS appid rewrite` 行数 0）。
- 会话生命周期：`armed(宿主) → re-armed(游戏) → cleared(退出)` 三行齐 ✓。
- 早期几轮 `misc/netpacket/ipc/pipe/onlinefix` 的 warning+error 合计 0。

```
misc.log      env session armed: session appid = 480 (pid=…)…            ← 登记（含真值或"将在首次判定时锁定"）
misc.log      env session: 锁定会话 pipe 的真 appid = <N>                ← 锁定发生（值应等于游戏真 appid）
misc.log      env session probe: real=… session=… locked=…             ← 会话期内探针（前 12 次）
misc.log      GetAppIDForCurrentPipe: <真> -> <会话> (env session)        ← 洗白真的发生
misc.log      env session cleared (game process exited)                  ← 退出即清
netpacket.log MMS dump … CreateLobby(6601) #1=… / CreateLobbyResponse(6602) #3=…
```

## 4. 部署与回滚

- **三件套**：`OpenSteamTool.dll` + `dwmapi.dll` + `xinput1_4.dll` → `<Steam>\`（GUI 的「内核」页可装；手工拷贝等价）。
- **部署 Debug**（带日志，联机诊断必需）；Release 只用来保证"Release 也编得过"。
- **部署件**：日常用 Release 构建（`发布包\测试版\ZSteamTool-v1.1.4+联机修复-20261017-r1.zip`，`E8605AB67011B6C5…`）；需要日志排查时改用 `build\Debug` 的同名三件套。
- **不保留部署快照**：ZST 是本地开发，任何版本都能从对应提交重建；上游/历史版本在各版发布包里（`发布包\正式版\ZSteamTool-v<版本>.zip`）。回滚 = 重新构建或从发布包取，回滚前先退出 Steam。

## 5. 已知边界（别当 bug 查）

- 垫片**按文件名**认 Steam：宿主 exe 不叫 `steam.exe` 就永不注入（Steam 照常跑、游戏全不解锁）——想支持改名要改成按目录/签名识别。
- 同一 Steam 进程内可重挂会话，但**同一时间只支持一个会话游戏**（沿用内核原有"一次一个"的假设）。
- 宿主进程与游戏进程在 Steam 侧是**两条独立记录**（480 与真 appid）：UI 显示真名、大厅走 480，属预期。
- 签名表取表时 `RemoteToml … mirror failed` 是**多镜像回退的正常噪声**（本机实测：前 4 个镜像不通、第 5 个 `git.lua.tools` 通，表照常加载并写缓存）；真正"取不到"才会弹 `Unsupported Steam Version` / `Missing Signatures` 窗。
- 出站报文改写路线**已被实测否掉**（MMS 建厅改 480/1046930 都得 `Fail`；服务器查询改真 appid 反而列表变空），相关代码仅留观测，行为默认关闭。
