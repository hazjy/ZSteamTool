<div align="center">
  <img src="docs/logo/logo-animated.svg" width="180" alt="ZSteamTool logo">

  <h1>ZSteamTool</h1>

  <p>
    <strong>Steam 解锁工具内核（OpenSteamTool 定制发行版）</strong>
  </p>

  <p>
    <img src="https://img.shields.io/badge/C%2B%2B-20%2B-2ea44f?logo=cplusplus&logoColor=white" alt="C++ 20+">
    <img src="https://img.shields.io/badge/CMake-3.20%2B-2ea44f?logo=cmake&logoColor=white" alt="CMake 3.20+">
    <img src="https://img.shields.io/badge/Windows-only-d73a49?logo=windows&logoColor=white" alt="Windows only">
    <img src="https://img.shields.io/badge/License-GPL--3.0-blue" alt="GPL-3.0">
  </p>

  <p>
    配套图形界面：<a href="https://github.com/hazjy/OSTGUI"><b>OSTGUI</b></a>
  </p>
</div>

## 这是什么

ZSteamTool 是一个注入 Steam 客户端的内核（三个 DLL），用来解锁并下载未入库的游戏 / DLC、处理联机会话身份、以及让 D 加密游戏按你**自己的账号**保存进度。

| 组件 | 作用 |
|---|---|
| `OpenSteamTool.dll` | 主逻辑（Hook、配置、Lua 解析、票据与身份处理） |
| `dwmapi.dll` / `xinput1_4.dll` | 加载器（Steam 启动时自动加载 `OpenSteamTool.dll`，无需手动注入） |

配套的图形界面 **[OSTGUI](https://github.com/hazjy/OSTGUI)**（清单源管理、入库、联机启动、D 加密模式开关等）；本内核是它的运行后端，两者配合使用。

## 与上游的关系（必读）

本仓库是以下项目的**衍生作品**，全部按 **GPL-3.0** 分发：

| 项目 | 说明 | 基线提交 |
|---|---|---|
| [OpenSteamTool](https://github.com/OpenSteam001/OpenSteamTool) | 原始上游，本仓库的代码起点 | `2a08b0b` |
| [BetterSteamTools](https://github.com/madoiscool/BetterSteamTools) | 活跃 fork，主体改动已对齐并入 | `c7b435f` |

- 许可证与版权归属见 [`LICENSE`](LICENSE) 与 [`NOTICE`](NOTICE)；
- 改了哪些地方、后续如何跟上上游：见 [`docs/dev/UPSTREAM-SYNC.md`](docs/dev/UPSTREAM-SYNC.md)；
- 完整的技术文档（构建、Lua API、Steam 版本兼容、逐模块日志说明）沿用上游英文文档：[`docs/upstream/README-en.md`](docs/upstream/README-en.md)。

## 本发行版相对上游的差异

| 领域 | 差异 |
|---|---|
| 下载 / 请求码 | 服务器收口环境下以**清单投喂 depotcache**为正路（`<depot>_<gid>.manifest` 放入 `steam\depotcache\`，客户端只读根目录）；858 `OwnershipTicket` 增加 **Forge 兜底**，manifest 请求码等待窗口 12s → 30s |
| D 加密身份 | 新增 `[denuvo] mode`（**normal 默认** / compat），决定游戏在授权窗口外看到哪个 SteamID —— 关系到存档 / 云存档绑定到哪个账号，详见下文 |
| 联机会话身份 | `-onlinefix` 默认 Spacewar(480)；`-onlinefix=<appid>` 可自定义会话身份（挑带 Steam Input 手柄配置的免费 AppID） |
| 联机一致性 | 保留本地三件套：LobbyInvite 回调改写、覆盖层保持 480、Persona 好友数据改写 |
| 分发层 | 剔除 Tokeer（`bst://` 兑换码桥）与内核自更新，改为手动部署三 DLL；未启用 eticket 在线铸造后端 |

## 安装

1. 备份 Steam 根目录下现有的 `OpenSteamTool.dll`、`dwmapi.dll`、`xinput1_4.dll`；
2. 从发布包的 `Release\`（日常）或 `Debug\`（排障，日志更全）取三个 DLL，复制到 Steam 根目录覆盖；
3. 配置模板 `opensteamtool.toml` 放到 Steam 根目录（**升级用户不必替换**：缺少 `[denuvo]` 段时默认即 `normal`，覆盖反而会丢掉你已改过的配置）；
4. Lua 配置放到 `<Steam>\config\lua\`（不是 `config\stplug-in`）；
5. 重启 Steam。

## 配置

`<Steam>\opensteamtool.toml`，**改动后热重载，无需重启 Steam**（`ConfigFileWatcher` 监听整文件变更）。完整模板与注释见 [`opensteamtool.example.toml`](opensteamtool.example.toml)。

| 小节 | 作用 |
|---|---|
| `[denuvo] mode` | `normal`（默认）= 仅 D 加密授权握手期间使用票据身份，其余时间按**你当前登录的账号**运行 → 游戏存档 / 云存档绑定你自己的账号；`compat` = 整场使用票据账号（严格标题用，**代价是该游戏用户数据绑定出票账号**） |
| `[manifest]` | 请求码源与超时；若 `config/lua/manifest.lua` 定义了 `fetch_manifest_code(_ex)`，Lua 优先 |
| `[log] level` | 日志级别（Debug 版写 `<Steam>\opensteamtool\*.log`） |
| `[lua] paths` | 附加 Lua 配置目录（默认 `<Steam>\config\lua` 最后加载，用户配置优先） |
| `[inject]` | 可选的游戏进程注入（默认关） |
| `[cloud]` | 可选云存档重定向（需自备 `cloud_redirect.dll`，默认关） |
| `[remote]` | 自定义元数据镜像（默认走 GitHub + jsDelivr 回退） |

## 构建

**要求**：Windows 10/11、CMake 3.20+、Visual Studio（MSVC x64）。部分环境需要显式指定工具集，例如 VS 18 用 `v145`。

```powershell
build.bat                          # 自动探测生成器，构建 Release + Debug
set TOOLSET=v145 && build.bat      # 指定 MSVC 工具集（生成器为 Visual Studio 时生效）
```

产物：`build\Release\` 与 `build\Debug\` 下各三个 DLL（`OpenSteamTool.dll`、`dwmapi.dll`、`xinput1_4.dll`）。

**运行时**：首次在 Steam 大更新后启动需要访问 `raw.githubusercontent.com` 拉取函数签名与 IPC 元数据（之后走本地缓存）；若该域名不可达，需要自建镜像并配置 `[remote] url_template`。

## 已知限制

- **请求码**：服务器已对非拥有者收口，能否下载取决于清单是否投喂成功（`depotcache` 与 Lua 取码两条路），不是所有游戏都能下；
- **`compat` 模式**下游戏用户数据绑定出票账号是**协议层必然**（eticket 内的 SteamID 由 Valve 签名，无法伪造），已有存档需自行迁移；
- **eticket 在线铸造**后端默认关闭（不随本仓库分发）；
- **Steam 客户端大更新**期间 Hook 签名可能失效，需要等适配（见 `docs/dev/UPSTREAM-SYNC.md`）；
- Debug 版日志量大、性能略低，日常请用 Release。

## 免责声明

本项目仅供**学习与研究**使用。请自行确认你的使用方式符合所在地法律、平台服务条款与软件许可协议；因使用本工具产生的任何后果由使用者自行承担。

本项目按 GPL-3.0 分发，**不提供任何担保**。上游作者与本发行版维护者均不对使用后果负责。
