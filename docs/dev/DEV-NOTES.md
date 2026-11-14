# ZSteamTool 内核开发笔记（DEV-NOTES）

> 何时读：接手内核仓库、改 hook / 模块边界、查构建部署或已知问题时。
> 管什么：定位与仓库、架构速览、构建与部署、env-less 已知问题、DenuvoAuth 模块。
> 不写什么：DenuvoAuth 的模式与协议边界见 `REF-内核-Denuvo与授权.md`；本机环境与工具链的坑见工作区根 `doc/开发经验.md` §五；上游对齐见 git 提交信息。

## 1. 定位与仓库

- 独立 git 仓库：单个 init 提交 = 上游 `2a08b0b` 源码 + onlinefix 480 三件套（LobbyInvite 改写 / 叠加层保持 480 / Persona 好友改写）；对齐基线 = BetterSteamTools `c7b435f`。职责是注入 Steam 的解锁内核（3 个 DLL），供 OSTGUI 的 `SteamDllService` 注入 / 卸载。
- 上游对照源码：`RefProjects/2-挂起/BetterSteamTools`（BST，下游）与 `2-挂起/OpenSteamTool`（原版，仅参考）；内部台账与调研笔记本地留存（`.gitignore` 排除），对外只发 `README.md`、`docs/changelog/`、`NOTICE`。

## 2. 架构速览

- **入口** `src/dllmain.cpp`：`DLL_PROCESS_ATTACH` 只做 `DisableThreadLibraryCalls` + 起分离线程 `InitThread`（IO / LoadLibrary / Detour 全部移出 loader lock）；顺序：Log → 定位 steamclient/steamui → toml → 远程 pattern/IPC 元数据（GitHub→jsDelivr→本地缓存）→ Lua 解析 + 双 watcher → CoreHook → CloudRedirectHost。
- **三 DLL 分工**：`dwmapi.dll` / `xinput1_4.dll` 为代理劫持（导出序号转发系统 DLL；进程为 steam.exe 时加载 `OpenSteamTool.dll`），后者为核心。
- **Hook 双轨与分层**：Detours 事务式跳板（`HOOK_FUNC` / `INSTALL_HOOK_C/U`，可链式调原函数）+ VEH/int3 软断点（捕获 this 指针或只改参数）；分层：`Hooks_IPC`（Handshake + InterfaceCall，GetSteamID / 票据 / GetAppID / 接口 46 P2P 探测）｜`Hooks_NetPacket`（出站 `BBuildAndAsyncSendFrame` / 入站 `RecvPkt`，按 eMsg 分发）｜`Hooks_CallBack`｜`Hooks_Decryption`（`ConfigStoreGetBinary` 拦 depot 解密密钥）｜`Hooks_Manifest` / `KeyValues`（`BuildDepotDependency` 锁 GID）；支撑模块 `PipeManager` / `LuaConfig` / `SteamMetadata` / `OSTPlatform` / `Tickets`。
- **联机会话身份（`-onlinefix`）**：默认 Spacewar(480)；`-onlinefix=<appid>` / `-onlinefix <appid>` 可指定，其余写法回落 480；身份由 `Hooks_Misc::SessionAppId()` 统一提供（外部 7 处 + 内部 2 处），一次只允许一个会话，进程退出即清。见 `REF-内核-联机会话.md`。

## 3. 构建与部署

- 构建：VS18 自带 CMake + MSVC；`vcvars64.bat && cmake -S src -B build -G "Visual Studio 17 2022" -A x64 -T v145 && cmake --build build --config Debug`；产物在 `build/Debug/` 与 `build/Release/`（三件 DLL），依赖 `.deps/` 已预填、离线可用。
- 本机工具集：VS Community 2026（版本号 18、`v145`）；默认探测的 `v143` / `v180` 均报 MSB8020 且会挂死 TryCompile；`cmake` 不在 PATH 时用 VS 自带的 `Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`。
- **字符集**：`target_compile_options(OpenSteamTool PRIVATE /utf-8)` 显式声明（Release 缺位曾致中文注释按 CP936 解码破坏语法）。
- 部署：三 DLL 复制到 Steam 根目录（覆盖前备份，回滚即拷回）；日志级别由 `opensteamtool.toml` 的 `[log] level` 控制。**日志只在 Debug 编入**，Release 只能靠行为确认；每个 logger `flush_on(trace)` 写即落盘，空文件等于那条路径没跑，但每次启动会 `truncate`、只留本次会话。验证：重启 Steam 正常加载、入库 / 游玩无回归，功能点按修复记录逐项。
- **版本号两处手工同步**：仓库根 `VERSION`（给人看、不参与构建）与 `src/CMakeLists.txt` 的 `project(OpenSteamTool VERSION x.y.z)`（唯一来源）；configure 生成 `OpenSteamToolBuildInfo.h` 与 `version.rc`（DLL 的 Windows 版本资源，唯一版本外显通道）；改完重配即可，不必清 CMake 缓存。

## 4. 已知问题（env-less 启动器游戏）

- 现象：第三方启动器（Ubisoft Connect / Epic / Rockstar）拉起的游戏 exe 无 SteamAppId 环境变量 → `ProcessInspector` 判 `likelyGameProcess=false` → `DenuvoAuth::Apply` 直接 return → Denuvo 游戏静默退出（88500012 / error 54）。上游 PR #148 最小集已移植（PipeManager 兜底 + Lua `addprocess` 映射 + DenuvoAuth 门放宽，commit `135a110`），构建验证通过。

## 5. DenuvoAuth 模块（守门员）

- 定位：D 加密游戏「授权身份欺骗」的准入状态机，决定哪些游戏进程 / 管道可用伪装身份。
- 流程：`Apply(ctx)` 门（gameProcess && trackedApp）→ `EnsureScanned` / `ScanProtection`（PE 特征判 Denuvo 并缓存）→ `OnHandshake`（None→Authorizing→EndAuthorization）。
- 对外 `IsAuthorizedPipe` 是 Hooks_IPC 的伪装门槛；Authorizing 期提供伪装身份，EndAuthorization 后若游戏已入库则把 SteamID 写入注册表 `Apps\<appid>\SteamID`。模式、协议边界与诊断方法见 `REF-内核-Denuvo与授权.md`。
