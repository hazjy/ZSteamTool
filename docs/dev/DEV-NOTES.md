# ZSteamTool 内核开发笔记（DEV-NOTES）

> 何时读：接手内核、找某个功能在哪段代码、构建部署、查已知缺陷。
> 管什么：项目概览、模块 → 代码位置、构建与部署、已知缺陷、文档地图。
> 不写什么：Steam 侧机制与外部项目知识见 `REF-*`；上游对齐见 git 提交信息；本机工具链与构建踩坑见工作区 `doc/开发经验.md` §四 / §五。

## 1. 项目概览

- 注入 Steam 客户端的解锁内核（三件 DLL），供 OSTGUI 的 `SteamDllService` 注入 / 卸载。内核只改 Steam 侧（许可、票据、网络包、库），不进游戏进程。
- 代码来源：上游 OpenSteamTool `2a08b0b` + BetterSteamTools `c7b435f` 主体对齐 + 本地补丁；GPL-3.0，署名见 `LICENSE` / `NOTICE`。
- 对外只发 `README.md`、`docs/changelog/`、`NOTICE`；本目录供开发与接手用。

## 2. 模块 → 代码位置

| 模块 | 位置 | 职责 |
|---|---|---|
| 启动装配 | `src/dllmain.cpp`、`dllmain.h` | DllMain 只起分离线程；InitThread：Log → 定位 steamclient/steamui → toml → 远程元数据 → Lua 解析 + 双 watcher → CoreHook → CloudRedirectHost |
| 代理劫持 | `src/dwmapi/`、`src/xinput1_4/` | 导出序号转发系统 DLL；进程是 steam.exe 时加载 `OpenSteamTool.dll` |
| Hook 基建 | `src/Hook/HookMacros.h`、`HookManager.*`、`src/Utils/HookSupport/` | Detours 事务式跳板（可链式调原函数）+ VEH/int3 软断点（抓 this 指针或只改参数） |
| IPC 接口 | `src/Hook/Hooks_IPC*.cpp` | Handshake + InterfaceCall：`GetSteamID`、票据、`GetAppID`、接口 46 P2P 探测 |
| 网络包 | `src/Hook/Hooks_NetPacket.cpp` | 出站 `BBuildAndAsyncSendFrame` / 入站 `RecvPkt`，按 eMsg 与 target_job_name 分发：所有权票、清单请求码、统计、好友播报、家庭共享上报 |
| 许可与包 | `src/Hook/Hooks_Package.cpp` | 假许可包（包 0）注入、`CheckAppOwnership` 改写、家庭共享分类与出借方归属 |
| 库可见性 | `src/Hook/Hooks_SteamUI.cpp` | `CSteamApp.OwnershipFlags` 增删队列（每帧 ≤64），决定库里显示什么 |
| 启动与身份 | `src/Hook/Hooks_Misc.cpp`、`Hooks_CallBack.cpp` | `SpawnProcess` 解析 `-onlinefix` 会话身份、`BuildSpawnEnvBlock`、`GetAppIDForCurrentPipe` 洗白、LobbyInvite 改写 |
| 解密与清单 | `src/Hook/Hooks_Decryption.cpp`、`Hooks_Manifest.cpp`、`Hooks_KeyValues.cpp` | `ConfigStoreGetBinary` 拦 depot 密钥、`BuildDepotDependency` 锁 GID、KV 解析 |
| 配置与 Lua | `src/Utils/Config/` | toml 读取 + 热重载；lua 解析（`addappid` / `addtoken` / `setManifestid`）+ 文件 watcher |
| 元数据 | `src/Utils/SteamMetadata/` | 远程 pattern / IPC 元数据（RemoteToml、PatternLoader、IPCLoader）、镜像、清单请求码、统计、诊断 |
| 票据 | `src/Utils/Tickets/` | `AppTicket`（伪造票与身份）、`EticketClient`（在线铸造，默认禁用）、`LegacyCDKey` |
| 管道与进程 | `src/Pipe/` | 内核 ↔ 游戏进程命名管道协议、进程识别（env-less 兜底）；`Features/DenuvoAuth` 授权状态机 + PE 特征扫描、`Features/Injection` 库注入 |
| 云存档 | `src/Utils/CloudRedirect/` | 与用户自备 `cloud_redirect.dll` 的宿主 shim |
| 平台层 | `src/OSTPlatform/` | 进程、内存、PE、HTTP、日志、credential store 等 Windows 封装 |
| 协议与结构 | `src/proto/steam_messages.proto`、`src/Steam/` | 消息 proto、结构体 / 枚举 / 回调定义；`Structs.h` 用 `static_assert` 锁偏移 |
| 构建 | `src/CMakeLists.txt`、`src/cmake/` | 依赖与代码生成（protobuf、ipc_codegen、lua、spdlog、detours） |

## 3. 构建与部署

- 构建：VS 自带 CMake + MSVC；`vcvars64.bat && cmake -S src -B build -G "Visual Studio 17 2022" -A x64 -T v145 && cmake --build build --config Debug`；产物 `build/Debug/` 与 `build/Release/`（三件 DLL），`.deps/` 已预填、可离线。
- 工具集：VS Community 2026（版本号 18、`v145`）；`v143` / `v180` 报 MSB8020 并挂死 TryCompile。cmake 路径、`.deps/` 预填与 `build.bat` 退码这些环境坑见 `doc/开发经验.md` §五。
- 字符集：`target_compile_options(OpenSteamTool PRIVATE /utf-8)` 显式声明。
- 部署：三 DLL 复制到 Steam 根目录，覆盖前备份、回滚即拷回；日志级别在 `opensteamtool.toml` 的 `[log] level`。
- 日志：只在 Debug 编入，Release 只能靠行为确认；各 logger `flush_on(trace)` 写即落盘，空文件等于那条路径没跑，每次启动 `truncate` 只留本次会话。
- 版本号两处手工同步：根 `VERSION`（给人看）与 `src/CMakeLists.txt` 的 `project(OpenSteamTool VERSION x.y.z)`（唯一来源）；configure 生成 `OpenSteamToolBuildInfo.h` 与 `version.rc`；改完重配即可，不必清缓存。

## 4. 已知缺陷

| 缺陷 | 位置 | 说明 |
|---|---|---|
| `RecvPkt` 进程级全局缓冲无锁 | `Hooks_NetPacket.cpp:34-51` | 若回调可被多网络线程并发进入即为数据竞态（取决于 Steam 线程模型，未验证） |
| 取码等待阻塞网络接收线程 | `Hooks_NetPacket.cpp:584,646` | 入站 147 在回调内 `wait_for(30s)`，`g_CodeFutures` 无淘汰上限 |
| `BuildCompleteAppOverviewChange` 移除列表只增不减 | `Hooks_SteamUI.cpp:36,40-54` | 加入后被永久重复追加进 `removed_appid` |
| `lua_addappid` 只校验密钥长度不校验 hex | `LuaConfig.cpp:219-221` | 坏密钥静默变成含 `0x00` 的密钥交给 Steam |
| 858 处理段注释与实现矛盾 | `Hooks_NetPacket.cpp:474-487` | 注释称"legacy 非 protobuf 无 schema"，实现却 `ParseFromArray` |
| env-less 启动器游戏静默退出 | `Pipe/` 与 `DenuvoAuth` | 第三方启动器拉起的 exe 无 `SteamAppId` → 判非游戏进程 → `DenuvoAuth::Apply` 直接 return（88500012 / error 54）；上游 PR #148 最小集已移植（`135a110`） |

## 5. REF 索引

| 文件 | 收什么 |
|---|---|
| `REF-内核-客户端适配.md` | Steam 客户端内部结构与 hook 面脆弱度、包层不可信来源、库可见性机制 |
| `REF-内核-联机会话.md` | `-onlinefix` 身份与全链路一致化、大厅建不起来的服务端判定 |
| `REF-内核-Denuvo与授权.md` | 授权窗口与 eticket 时效模型、Denuvo 身份一致性 |
