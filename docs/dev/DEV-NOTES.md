# ZSteamTool 内核开发笔记（DEV-NOTES）

> 本仓库 = OpenSteamTool 内核二次开发（**2026-09-06 由 ZTool 更名**）。代码基线：上游 OpenSteamTool `2a08b0b` + 本地修复。**对齐基线：BetterSteamTools（2026-09-08 已照搬并入并实机验证，见 §8）**——参考"本尊"：`D:/Projects/OSTGUI/RefProjects/1-在用/BetterSteamTools`（OpenSteamTool 活跃 fork，含 `setlegacycdkey` 第三方产品密钥 / multi-inject / `-realappid` 等新特性，自带 env-less 追踪类实现）；原版上游 `OpenSteamTool` 降存 `RefProjects/2-挂起/` 仅作参考。

## 1. 定位与仓库

- 独立 git 仓库：单个 init 提交 = 上游 2a08b0b 源码 + onlinefix 480 三件套（LobbyInvite 改写 / 叠加层保持 480 / Persona 好友改写）；
- 职责：注入 Steam 的解锁内核（3 个 DLL），供 OSTGUI（GUI 侧，`SteamDllService` 负责注入/卸载）配合使用；
- 上游对照：对齐用源码见 `1-在用/BetterSteamTools`（暂定基线，fork 关系：OpenSteamTool 下游）；原版上游 `OPENSTEAMTOOL` 见 `2-挂起/OpenSteamTool`（仅参考不修改）；旧 git 历史备份：`D:/Projects/OSTGUI/ost-backups/ZTool-旧上游git历史备份`（含 fix/onlinefix-lobby-invite 分支 v1:94a80b8 / v3:36708b9）；
- 内核任务**不记入** OSTGUI 的 agents-log（约定见 OSTGUI `docs/dev/agents-log/README.md`）；本仓库变更记入 `docs/dev/agents-log/`（本地留存，不纳入 git）。

## 2. 架构速览

- **入口** `src/dllmain.cpp`：`DLL_PROCESS_ATTACH` 只做 `DisableThreadLibraryCalls` + 起分离线程 `InitThread`（所有文件 IO / LoadLibrary / Detour 事务移出 loader lock）；
  InitThread 顺序：Log → 定位 steamclient/steamui → toml 配置 → 远程 pattern/IPC 元数据（GitHub→jsDelivr→本地缓存）→ Lua 解析 + 双 watcher → SteamUI/SteamClient.CoreHook → CloudRedirectHost；
- **三 DLL 分工**：`dwmapi.dll` / `xinput1_4.dll` 为代理劫持（硬编码导出真实序号转发系统 DLL；进程为 steam.exe 时 `LoadLibrary("OpenSteamTool.dll")` 完成注入）；`OpenSteamTool.dll` 为核心；
- **Hook 双轨**：Detours 事务式跳板（`HOOK_FUNC`/`INSTALL_HOOK_C/U`，可链式调原函数）+ VEH/int3 软断点（捕获 this 指针 / 只改参数场景，如 `SpawnProcess` 改 pGameID=480）；
- **分层钩子**：
  - `Hooks_IPC`（接口层）：`IPCProcessMessage` 拦截 Handshake + InterfaceCall，处理器：GetSteamID 冒充 / GetAppOwnershipTicket / RequestEncryptedAppTicket / GetAPICallResult；
  - `Hooks_NetPacket`（网络层）：出站 `BBuildAndAsyncSendFrame` / 入站 `RecvPkt`，按 eMsg 分发：PICS token / UserStats / GamesPlayed(480→真实) / PersonaState / Cloud.* / manifest code / 家庭共享；
  - `Hooks_CallBack`：`SendCallbackToPipe` 分发（onlinefix LobbyInvite 480→真实 AppId）；
  - `Hooks_Decryption`：`ConfigStoreGetBinary` 拦 depot 解密密钥；备 apptickets\7；
  - `Hooks_Manifest`/`KeyValues`：`BuildDepotDependency` 锁 manifest GID；
- **支撑模块**：`PipeManager`（IPC 握手 + 进程快照 + DenuvoAuth）、`LuaConfig`（Lua 热重载）、`SteamMetadata`（Pattern/IPC 远程 TOML）、`OSTPlatform`（平台抽象）、`Tickets/AppTicket + SteamCredentialStore`（票据 / 注册表）。

## 3. 构建与部署

- 构建：VS18 自带 CMake + MSVC；Debug 配置产出 `build/Debug/{OpenSteamTool,dwmapi,xinput1_4}.dll；
  ⚠️ **本机 VS 实为 Visual Studio Community 2026（版本号 18，工具集代号 `v145`）**——CMake 生成器用 `-G "Visual Studio 17 2022" -A x64 -T v145`（工具集必须 `v145`；默认探测的 `v143`/`v180` 均 MSB8020 报未装且会挂死 TryCompile）；
  实际命令（2026-09-06 验证）：`vcvars64.bat && cmake -S src -B build -G "Visual Studio 17 2022" -A x64 -T v145 && cmake --build build --config Debug`；依赖 `.deps/` 已预填（离线可用）；
- **依赖**：FetchContent 缓存 `.deps/`（lua/spdlog/protobuf/tomlplusplus/detours 手动预填——本机 TLS 被加速器干扰，schannel 全线不可用；git 需 `http.sslBackend=openssl` + 合并根证书 CA bundle）；
- 部署：替换 `d:/steam/` 下三个 DLL（原内核备份在 `ost-backups/`，回滚=拷回）；Debug 内核默认日志可用（toml 未设 `[log]` 时走 Debug 级）；
- 验证清单：① 重启 Steam 正常加载、入库/游玩无回归；② 功能点按修复记录逐项。

## 4. 已知问题（env-less 启动器游戏）

- 现象：Steam 启动带第三方启动器（Ubisoft Connect / Epic / Rockstar）的游戏 → 真实游戏 exe 由启动器拉起（**无 SteamAppId 环境变量**）→ `ProcessInspector` 判 `likelyGameProcess=false` → `DenuvoAuth::Apply`（DenuvoAuth.cpp:169）直接 return → 授权/身份伪造不生效 → Denuvo 游戏**静默退出**（88500012 / error 54）；
- 上游修复：**PR #148**（`GetAppIDForCurrentPipe` 重试兜底 + Lua `addprocess/appid, "Exe.exe"` 映射 + `forcedenuvo()` + `gameProcess = likelyGameProcess || trackedApp`）；**已移植（2026-09-06，commit 135a110）**——最小集（PipeManager 兜底 + LuaConfig addprocess + DenuvoAuth 门放宽），排除 eticket/结构性扫描附加特性；
- 移植后的构建验证：**通过（2026-09-06）**——Debug 编译成功（`build/Debug/OpenSteamTool.dll` 等三件）。曾阻塞于 VS2026 工具集代号（见 §3），用 `-T v145` 解决；部署/实机验证待后续。

## 5. DenuvoAuth 模块说明（守门员）

- 定位：D 加密游戏"授权身份欺骗"的准入状态机——决定哪些游戏进程/管道可用伪装身份；
- 流程：`Apply(ctx)`（:169 门：gameProcess && trackedApp）→ `EnsureScanned`/`ScanProtection`（PE 特征判定 Denuvo，缓存）→ `OnHandshake` 状态机（None→Authorizing→EndAuthorization；首次握手选授权管道，第二次握手结束授权并持久化 SteamID）；
- 对外：`IsAuthorizedPipe`（Hooks_IPC 处理 GetSteamID/GetAppOwnershipTicket/GetAPICallResult 时的伪装门槛）；
- 窗口语义：Authorizing 期（首/二次握手之间）提供伪装身份；EndAuthorization 后若游戏已入库则把当前账号 SteamID 写入注册表（`Apps\<appid>\SteamID`）。

## 6. 环境坑（本机）

- **schannel 全局故障**（Steam++ 干扰/凭据层）：curl/IWR/.NET 全挂；git 走 openssl 后端；NuGet/构建须在完全权限沙箱或真实终端；
- `.deps` 需手动预填的原因同上（TLS）；
- 上游同步：用 `git fetch origin <refs/pull/N/head>`（openssl）评估上游新修复，再决定移植。

## 7. 变更台账

见 `docs/dev/agents-log/`（按日期文件，与 OSTGUI 台账同规范；本地留存，不随 git 分发）。

## 8. BST 对齐（2026-09-08 完成）

- **决策与范围**：照搬 BST `c7b435f` 主体（其 12 个专有 commit 全量内容，含上游 PR#146 / PR#148 完整版）；**关卡4（eticket 在线铸造）不启用**——无自建后端，`EticketClient` 默认禁用零网络；**AppUpdater / Tokeer 剔除**（dllmain 不接入，文件保留，见施工单 `docs/dev/bst-diff/20-migration-plan.md`）；
- **手工合并点（本地特有保留）**：
  1. `Hooks_CallBack.cpp` LobbyInvite 改写（恢复本地版，`OnlineFixRealAppId()` → `ResolveAppId()` 适配 BST API）；
  2. `Hooks_Misc.cpp` BuildSpawnEnvBlock 覆盖层**保持 480**（本地 PEAK 实测方向；与 BST 恢复真实方向冲突，取本地）；
  3. `Hooks_NetPacket.cpp` Persona 好友 480→真实改写（BST 版删除段加回）；
  4. `Steam/Callback.h` 补回 `LobbyInvite_t` + `k_iSteamMatchmakingCallbacks=300`（BST 版删除，本地依赖）。
- **构建/部署**：Debug v145 构建通过（2026-09-08）；三 DLL 部署 `d:\steam\`（旧版备份 `ost-backups/20260908-pre-bst/`：src + docs + deployed-d-steam）；
- **实机验证（2026-09-08，用户实测）**：
  - **UNO**：Steam 运行可正常安装 Ubisoft Connect → **判定正常**（730「Updating product key」本地应答生效连带走通）；
  - **PEAK（联机）**：**邀请好友界面首次可正常弹出** —— 突破。⚠️ **记忆纠正**：本地此前"480 原生 + 修复合（LobbyInvite/覆盖层/Persona）"对 PEAK **从未成功过**，本次为首次成功；⚡ 待好友侧验证真正可联（P2P flip 与三件套并存影响仍观察中）；
  - 普通 addappid 游戏回归：待补（初判无碍）。
- **后续**：BST 主体已并入；上游/BST 新 commit 用 FETCH + diff 增量评估（§6 环境坑：git openssl 后端）；三件套与 P2P flip 共存影响继续用联机样本观察。