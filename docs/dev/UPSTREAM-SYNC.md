# 上游基线与同步记录（UPSTREAM-SYNC）

> 本文件是**对外**的上游关系说明：基线是什么、移植了什么、本地改了什么、以后怎么跟上。
> 逐版本的用户可读变更见 `docs/changelog/UPDATE-NOTES-*.md`；架构与模块说明见 `docs/dev/DEV-NOTES.md`。
> 随仓库分发的只有这份说明与更新日志；更细的内部调研笔记不随仓库分发。

## 1. 基线

| 角色 | 项目 | 提交 | 说明 |
|---|---|---|---|
| 起点（B） | [OpenSteamTool](https://github.com/OpenSteam001/OpenSteamTool) | `2a08b0b` | 本仓库首个提交 `9a7729a` 即以此初始化（含 `-onlinefix` 480 邀请修复） |
| 对齐基线（A） | [BetterSteamTools](https://github.com/madoiscool/BetterSteamTools) | `c7b435f` | 2026-09-08 决策"照搬主体"，其经实机验证的改动整体并入 |

同步过程中参考的 BST 关键提交：

| 提交 | 内容 | 本项目处置 |
|---|---|---|
| `e55c90b` | Denuvo 结构检测、env-less 追踪、按需 eticket 铸造 | 结构检测与 env-less 追踪**已并入**；eticket 在线铸造**未启用**（无后端）；该提交同时移除了上游 `GetSteamID` 的授权窗口门 —— 这一处**在 v1.1.0 做了纠正**（见第 3 节） |
| `7936adc`（上游 PR#146） | multi-inject（`[[inject]]` 数组 + 条件匹配）、P2P AppID 翻转 | **未移植**（当前为标量 `[inject]` 配置，无 P2P 翻转） |
| PR#148 | env-less 启动器游戏的追踪 | 最小集已移植（`135a110`） |
| `f7b7caf` | Denuvo activation redeem uri | 未采用 |
| `c7b435f` | `-realappid` | 未移植（本项目当前无 P2P 翻转，行为上等价于恒 `-realappid`） |

## 2. 已移植并生效的改动

- **Denuvo 结构检测**（`ProtectionScan`：PE 特征 + RWX/熵第三级判定）与 **env-less 进程追踪**（`PipeManager` 归属链），使无 `SteamAppId` 环境变量的启动器型游戏也能被正确识别；
- **票据与身份链**：`AppTicket` / `Hooks_IPC_ISteamUser` / `DenuvoAuth` 的授权窗口与票源逻辑（v1.1.0 在其上做了一致性纠正，见第 3 节）；
- **配置体系**：`[remote] url_template`（自定义元数据镜像）、`[[inject]]` 之外的其余配置项、`ConfigFileWatcher` 整文件热重载。

## 3. 本项目的本地改动（相对上游）

| 领域 | 改动 | 版本 |
|---|---|---|
| 下载 / 请求码 | 858 `OwnershipTicket` **Forge 兜底**；manifest 请求码等待窗口 12s → 30s；清单投喂 depotcache 作为收口环境下的正路 | v1.0.0 |
| D 加密身份一致性 | 新增 `[denuvo] mode`（`normal` 默认 / `compat`）。上游语义（`normal`）只在授权握手窗口内使用票据身份，窗口外返回**当前登录账号**；`compat` 保留"整场伪装"行为供严格标题使用，其代价是用户数据绑定出票账号。同时把三处触点（授权窗口门、窗口外票源、窗口结束时的注册表写入）绑成同一个开关 | v1.1.0 |
| 联机会话身份 | `-onlinefix=<appid>` / `-onlinefix <appid>` 自定义会话身份（默认 480） | v1.0.0 |
| 联机一致性（本地三件套，上游无等价物） | ① `LobbyInvite` 回调 480 → 真实 AppId 改写；② 覆盖层 `OverlayCGameID` **保持 480**（改回会破坏邀请对话框，已有实测依据，与 BST 方向相反）；③ 入站 `PersonaState` 好友条目 480 → 真实 AppId 改写 | v1.0.0 |
| 分发层裁剪 | 不接入 Tokeer（`bst://` 兑换码桥）与内核自更新，改为手动部署三 DLL；eticket 在线铸造后端默认关闭 | v1.0.0 |

## 4. 明确不采用

- **`bst://` 兑换码桥 / 自更新**：依赖外部服务与自动下载，本发行版为手动部署模式；
- **目录迁移 `config\lua` → `config\stplug-in`**：本项目沿用 `config\lua` 作为默认目录（内核默认读 `config\lua`，即 GUI 写入的位置；同时仍读取 `config\stplug-in`，兼容 BST 时代的旧配置）；
- **`eticket` 在线铸造**：需要后端，默认关闭（编译期 `OST_ETICKET_URL` 为空即禁用）。

## 5. 以后怎么跟上上游

1. 取上游：`git remote add upstream-ost https://github.com/OpenSteam001/OpenSteamTool`（BST 同理另加一个 remote），`git fetch upstream-ost`；
2. 看差异：`git log --oneline HEAD..upstream-ost/main`、`git range-diff` 或直接 `git diff`；**不要**用分支长期承载上游快照，`main` 只保留本项目产品线；
3. 需要移植时开一次性主题分支（如 `sync/bst-<主题>`），移植 + 实机验证后合回 `main`，并在本文件登记；
4. **已知冲突点**（移植时逐条确认）：
   - `Hooks_Misc` 的覆盖层 AppId 恢复路线 ↔ 本地"保持 480"；
   - `Hooks_CallBack` / `Steam/Callback.h` 的 `LobbyInvite_t` ↔ 本地 LobbyInvite 改写；
   - `GetSteamID` 的授权窗口语义 ↔ 本地 `[denuvo] mode`（上游与 BST 在此方向相反，本项目取上游语义为默认）。

## 6. Steam 客户端大更新

Hook 通过函数签名 / RVA 定位（见上游文档的 "Steam version compatibility"）。客户端大更新后签名可能失效，此时需要等上游或本项目更新映射表；映射表由运行时从 GitHub 拉取并本地缓存，可用 `[remote] url_template` 指向自建镜像。
