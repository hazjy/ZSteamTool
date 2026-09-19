# ZSteamTool 内核开发笔记（DEV-NOTES）

> 本仓库 = OpenSteamTool 内核二次开发（**2026-09-06 由 ZTool 更名**）。代码基线：上游 OpenSteamTool `2a08b0b` + 本地修复。**对齐基线：BetterSteamTools `c7b435f`**（2026-09-08 已照搬并入并实机验证）。基线与移植/改动清单见 `docs/dev/UPSTREAM-SYNC.md`（对外）；上游原始文档见 `docs/upstream/`。
>
> 本文只承载**项目结构与架构**。对外文档：`README.md`（使用/安装/配置）、`docs/dev/UPSTREAM-SYNC.md`（上游基线与改动）、`docs/changelog/`（逐版本更新说明）；上游原始文档存档在 `docs/upstream/`。本机环境相关与内部调研笔记**不随仓库分发**。

## 1. 定位与仓库

- 独立 git 仓库：单个 init 提交 = 上游 2a08b0b 源码 + onlinefix 480 三件套（LobbyInvite 改写 / 叠加层保持 480 / Persona 好友改写）；
- 职责：注入 Steam 的解锁内核（3 个 DLL），供 OSTGUI（GUI 侧，`SteamDllService` 负责注入/卸载）配合使用；
- 上游对照：对齐用源码见 `1-在用/BetterSteamTools`（暂定基线，fork 关系：OpenSteamTool 下游）；原版上游 `OPENSTEAMTOOL` 见 `2-挂起/OpenSteamTool`（仅参考不修改）；
- 内核任务的内部台账与调研笔记**本地留存、不随仓库分发**（`.gitignore` 已排除）；本仓库对外只提供 `docs/changelog/`（逐版本更新说明）与 `docs/dev/UPSTREAM-SYNC.md`（上游关系）。

## 2. 架构速览

- **入口** `src/dllmain.cpp`：`DLL_PROCESS_ATTACH` 只做 `DisableThreadLibraryCalls` + 起分离线程 `InitThread`（所有文件 IO / LoadLibrary / Detour 事务移出 loader lock）；
  InitThread 顺序：Log → 定位 steamclient/steamui → toml 配置 → 远程 pattern/IPC 元数据（GitHub→jsDelivr→本地缓存）→ Lua 解析 + 双 watcher → SteamUI/SteamClient.CoreHook → CloudRedirectHost；
- **三 DLL 分工**：`dwmapi.dll` / `xinput1_4.dll` 为代理劫持（硬编码导出真实序号转发系统 DLL；进程为 steam.exe 时 `LoadLibrary("OpenSteamTool.dll")` 完成注入）；`OpenSteamTool.dll` 为核心；
- **Hook 双轨**：Detours 事务式跳板（`HOOK_FUNC`/`INSTALL_HOOK_C/U`，可链式调原函数）+ VEH/int3 软断点（捕获 this 指针 / 只改参数场景，如 `SpawnProcess` 改 pGameID=会话身份）；
- **分层钩子**：
  - `Hooks_IPC`（接口层）：`IPCProcessMessage` 拦截 Handshake + InterfaceCall，处理器：GetSteamID 冒充 / GetAppOwnershipTicket / RequestEncryptedAppTicket / GetAPICallResult；另含 P2P 身份翻转探测（SteamNetworkingSockets 接口 46）；
  - `Hooks_NetPacket`（网络层）：出站 `BBuildAndAsyncSendFrame` / 入站 `RecvPkt`，按 eMsg 分发：PICS token / UserStats / GamesPlayed(会话身份→真实) / PersonaState / Cloud.* / manifest code / 家庭共享；
  - `Hooks_CallBack`：`SendCallbackToPipe` 分发（onlinefix LobbyInvite 会话身份→真实 AppId）；
  - `Hooks_Decryption`：`ConfigStoreGetBinary` 拦 depot 解密密钥；备 apptickets\7；
  - `Hooks_Manifest`/`KeyValues`：`BuildDepotDependency` 锁 manifest GID；
- **支撑模块**：`PipeManager`（IPC 握手 + 进程快照 + DenuvoAuth）、`LuaConfig`（Lua 热重载）、`SteamMetadata`（Pattern/IPC 远程 TOML）、`OSTPlatform`（平台抽象）、`Tickets/AppTicket + SteamCredentialStore`（票据 / 注册表）。
- **联机会话身份（`-onlinefix`）**：默认 Spacewar(480)，`-onlinefix=<appid>` / `-onlinefix <appid>` 可指定其它会话身份（解析在 `Hooks_Misc.cpp: ParseSessionAppId`，由 `SpawnProcess` 的 VEH 回调 `OnSpawnProcessHit` 调用；其它 `-onlinefix*` 写法一律回落 480）。会话身份统一由 `Hooks_Misc::SessionAppId()` 提供：外部调用点 **7 处**（`Hooks_CallBack` ×1、`Hooks_IPC_ISteamUser` ×1、`Hooks_NetPacket` ×5），另有 `Hooks_Misc` 内部 2 处。一次只允许一个会话。
- **会话状态的生命周期（v1.1.3 起）**：`g_OnlineFixRealAppId` / `g_SessionAppId` / `g_NetworkingSocketsActive` / `g_SuppressAppIdFlip` 只描述**当前那一个游戏进程**。该进程在**管道握手**时被绑定（`PipeManager::OnHandshake` → `Hooks_Misc::TrackOnlineFixGameProcess`），看门狗线程 `StartOnlineFixGameWatcher` 持有句柄轮询，**它一退出立刻 `ResetOnlineFixSession("game process exited")`**（上述字段 + `g_OnlineFixGamePid` 一次清干净）。旧版要等"下一个不带 `-onlinefix` 的启动"才清，于是绕过 Steam spawn 的启动方式（配套 GUI 的 `OnlineHost` 宿主 480）会继承陈旧状态：好友 persona 持续被改写、`LobbyInvite` / GamesPlayed 名字补丁 / P2P 翻转门控都停在"已激活"。
  ⚠️ **判存活不能用"进程还在不在"**：已终止但尚未回收的进程照样答 `OpenProcess` + `GetProcessTimes`（Steam 自己还持有游戏进程句柄），第一版修复（`TickOnlineFixSession` 按创建时间巡检）就是这样从不触发的。必须用在**进程活着时**抓到的句柄 `WaitForSingleObject` 等信号，顺带免疫 pid 复用。
- **版本外显（给外部软件读）**：`OpenSteamTool.dll` 带 **Windows 版本资源**（`src/cmake/version.rc.in`，版本取 `project(VERSION)`）→ `FileVersionInfo` / 资源管理器属性 / 任何会读 PE 资源的软件都能读到**已安装的那份**。这是 Windows 原生的版本落点，也是唯一的版本外显通道——不要把版本另写到 txt/toml 之类的外部文件里（上游连这个资源都没有：版本只是编进 DLL 的字符串，只有 Debug 日志与诊断弹窗可见，Release 连日志都没有）。

## 3. 构建与部署

- 构建：VS18 自带 CMake + MSVC；Debug 配置产出 `build/Debug/{OpenSteamTool,dwmapi,xinput1_4}.dll`；Release 产出 `build/Release/` 同名三件；
  ⚠️ **本机 VS 实为 Visual Studio Community 2026（版本号 18，工具集代号 `v145`）**——CMake 生成器用 `-G "Visual Studio 17 2022" -A x64 -T v145`（工具集必须 `v145`；默认探测的 `v143`/`v180` 均 MSB8020 报未装且会挂死 TryCompile）；
  实际命令：`vcvars64.bat && cmake -S src -B build -G "Visual Studio 17 2022" -A x64 -T v145 && cmake --build build --config Debug`；依赖 `.deps/` 已预填（离线可用）；
- **字符集**：`target_compile_options(OpenSteamTool PRIVATE /utf-8)`（显式；Release 缺位曾致中文注释按 CP936 解码破坏语法，2026-09-10 修复）；
- **依赖**：FetchContent 缓存 `.deps/`（lua/spdlog/protobuf/tomlplusplus/detours 手动预填——本机 TLS 被加速器干扰，schannel 全线不可用；git 需 `http.sslBackend=openssl` + 合并根证书 CA bundle）；
- 部署：把三个 DLL 复制到 Steam 根目录（覆盖前先备份原文件，回滚即拷回）；日志级别由 `opensteamtool.toml` 的 `[log] level` 控制，未设置时走内核默认；
- **版本号两处手工同步**：仓库根 `VERSION` 文件（给人/外部工具看的标记，**不参与构建**）与 `src/CMakeLists.txt` 的 `project(OpenSteamTool VERSION x.y.z)`（**唯一来源**）。configure 时按 `PROJECT_VERSION` 生成两份：`build/generated/OpenSteamToolBuildInfo.h`（`OPENSTEAMTOOL_VERSION`，供日志与诊断弹窗）与 `build/generated/version.rc`（DLL 的 Windows 版本资源，供外部读取，见 §2）。改完直接重配即可，**不需要清 CMake 缓存**（历史坑：早期 `OPENSTEAMTOOL_VERSION` 是 CACHE 变量，1.1.0 的 DLL 里烙着 1.0.0，见 `docs/changelog/UPDATE-NOTES-1.1.1.md`）。
- 验证清单：① 重启 Steam 正常加载、入库/游玩无回归；② 功能点按修复记录逐项。

## 4. 已知问题（env-less 启动器游戏）

- 现象：Steam 启动带第三方启动器（Ubisoft Connect / Epic / Rockstar）的游戏 → 真实游戏 exe 由启动器拉起（**无 SteamAppId 环境变量**）→ `ProcessInspector` 判 `likelyGameProcess=false` → `DenuvoAuth::Apply`（DenuvoAuth.cpp:169）直接 return → 授权/身份伪造不生效 → Denuvo 游戏**静默退出**（88500012 / error 54）；
- 上游修复：**PR #148**（`GetAppIDForCurrentPipe` 重试兜底 + Lua `addprocess/appid, "Exe.exe"` 映射 + `forcedenuvo()` + `gameProcess = likelyGameProcess || trackedApp`）；**已移植（2026-09-06，commit 135a110）**——最小集（PipeManager 兜底 + LuaConfig addprocess + DenuvoAuth 门放宽），排除 eticket/结构性扫描附加特性；
- 移植后的构建验证：**通过（2026-09-06）**——Debug 编译成功。曾阻塞于 VS2026 工具集代号（见 §3），用 `-T v145` 解决。

## 5. DenuvoAuth 模块说明（守门员）

- 定位：D 加密游戏"授权身份欺骗"的准入状态机——决定哪些游戏进程/管道可用伪装身份；
- 流程：`Apply(ctx)`（:169 门：gameProcess && trackedApp）→ `EnsureScanned`/`ScanProtection`（PE 特征判定 Denuvo，缓存）→ `OnHandshake` 状态机（None→Authorizing→EndAuthorization；首次握手选授权管道，第二次握手结束授权并持久化 SteamID）；
- 对外：`IsAuthorizedPipe`（Hooks_IPC 处理 GetSteamID/GetAppOwnershipTicket/GetAPICallResult 时的伪装门槛）；
- 窗口语义：Authorizing 期（首/二次握手之间）提供伪装身份；EndAuthorization 后若游戏已入库则把当前账号 SteamID 写入注册表（`Apps\<appid>\SteamID`）。

## 6. 环境坑（本机）

- **schannel 全局故障**（Steam++ 干扰/凭据层）：curl/IWR/.NET 全挂；git 走 openssl 后端；NuGet/构建须在完全权限沙箱或真实终端；
- `.deps` 需手动预填的原因同上（TLS）；
- 上游同步：用 `git fetch origin <refs/pull/N/head>`（openssl）评估上游新修复，再决定移植。

## 7. 文档与变更记录

- **对外**（随仓库分发）：`README.md`（使用 / 安装 / 配置）、`docs/changelog/UPDATE-NOTES-*.md`（逐版本更新说明）、`docs/dev/UPSTREAM-SYNC.md`（上游基线与本地改动清单）、`docs/upstream/`（上游原始英文文档存档）、`NOTICE`（版权与第三方归属）。
- **本地留存**（`.gitignore` 排除，不随仓库分发）：内部变更台账、BST 对齐调研笔记、机器环境备注与事件考证笔记。
- 版本号：`VERSION` 与 `src/CMakeLists.txt` 的 `project(... VERSION ...)` 需同步。