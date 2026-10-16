# REF · 内核联机测试分支（`exp/onlinefix-appid-rewrite`）

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

## 3. 验证状态（截至 2026-10-16 20:45）

**已实证**

- 建房成功：`MMS CreateLobby(6601) #1=480` → `CreateLobbyResponse(6602) #1=480 … #3=1` + 大厅 SteamID（11:30 `109775241059253359`、20:38 `109775241059920251`）；随后 `SetLobbyData` / `LobbyData` / `LeaveLobby` 全 `#3=1`。
- 界面：**建房成功且 Steam 邀请好友弹窗正常出现**；服务器列表是 480 世界。
- **零报文改写**（诊断开关关闭后，`MMS appid rewrite` 行数 0）。
- 会话生命周期：`armed(宿主) → re-armed(游戏) → cleared(退出)` 三行齐 ✓（新代码在真实场景工作）。
- 20:38 那轮 `misc/netpacket/ipc/pipe/onlinefix` 的 warning+error 合计 **0**。

**未实证 / 存疑（下一步就是它们）**

- ⚠️ **20:38 那轮洗白日志 0 条** → 报文上分不清"客户端自己就认为这条 pipe 是 480"还是"靠我们洗成 480"；已加探针 `env session probe: real=… session=… locked=…`（会话期内前 12 次），**等一次单人重跑**（机械狂欢 + 宿主会话，无需好友）即可定论。
- ⚠️ 因此 **"限定到会话那条 pipe"（作用域）与"首次判定时锁定"两条逻辑本身还没被验证**。
- ⚠️ **对端能否加入**（真·双机）：等好友 → 你 `Start → Host`，好友（同版本内核 + 从宿主会话启动）用房间 ID / 接受邀请进入。
- ⚠️ **PEAK（宿主会话，3527290）回归**：确认这条路没被本次改动弄坏。

**读日志时的判据行**（都落在 `<Steam>\opensteamtool\`）

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
- **备份**：每次部署前存 `内核备份\20261016-expN-<主题>\`。当前部署件 = **`6BDE60E6…`**（`exp12-probe`，20:42）；`20261016-卸载前` 是 1.1.4 原样基线，`exp8-diagonly` 是"零改写"诊断版。
- **回滚**：把某个备份目录里的三个 DLL 拷回 `<Steam>\` 即可（回滚前先退出 Steam，否则文件被占用）。

## 5. 已知边界（别当 bug 查）

- 垫片**按文件名**认 Steam：宿主 exe 不叫 `steam.exe` 就永不注入（Steam 照常跑、游戏全不解锁）——想支持改名要改成按目录/签名识别。
- 同一 Steam 进程内可重挂会话，但**同一时间只支持一个会话游戏**（沿用内核原有"一次一个"的假设）。
- 宿主进程与游戏进程在 Steam 侧是**两条独立记录**（480 与真 appid）：UI 显示真名、大厅走 480，属预期。
- 签名表取表时 `RemoteToml … mirror failed` 是**多镜像回退的正常噪声**（本机实测：前 4 个镜像不通、第 5 个 `git.lua.tools` 通，表照常加载并写缓存）；真正"取不到"才会弹 `Unsupported Steam Version` / `Missing Signatures` 窗。
- 出站报文改写路线**已被实测否掉**（MMS 建厅改 480/1046930 都得 `Fail`；服务器查询改真 appid 反而列表变空），相关代码仅留观测，行为默认关闭。
