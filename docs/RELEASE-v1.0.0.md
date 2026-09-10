# ZSteamTool v1.0.0 发布说明（2026-09-10）

## 版本信息

- 版本号：**v1.0.0**（首个正式版；`VERSION` 文件即当前版本）
- 构建：Release 配置 · MSVC v145 · x64（Debug v145 亦通过）
- 基线：OpenSteamTool 上游 2a08b0b + BetterSteamTools 主体对齐（上游 PR#146 / PR#148 完整版）+ 本地三件套 + 本地补丁

## 能力清单

### 入库与解锁
- Lua 配置体系：`addappid` / `addtoken` / `setappticket` / `seteticket` / `setmanifestid` 等（`<Steam>/config/lua/` 热加载，支持 `[lua] paths` 扩展目录）
- DLC 全解锁、`games_played` / Persona / Cloud 数据一致性改写

### 下载链路（服务器收口环境）
- **清单投喂 depotcache 为正路**：请求码遭 CM 收口（非拥有者不发码、码会话绑定、第三方码必 401）的现状下，`<depot>_<gid>.manifest` 放入 `steam\depotcache\` 即可绕过请求码完成下载（chunk 下载不鉴权）
- 858 `OwnershipTicket` **Forge 兜底**：无凭据库票/无后端时本地伪造应答
- manifest 请求码等待窗口放宽至 30s（内置源慢网络不超时）
- **请求码注入短路**：`config/lua/manifest.lua` 返回 `"0"` 阻止一切第三方码注入（第三方码绑定他人会话，注入必 401；同时短路内置 provider 链）

### 联机（onlinefix）
- `-onlinefix`：会话身份默认 Spacewar(**480**)，邀请 / P2P cert / 同玩检测 / overlay 全链路一致化
- **【v1.0.0 新增】自定义联机会话身份**：`-onlinefix=<appid>` 或 `-onlinefix <appid>` 可将会话身份替换为任意 AppID（免费联机游戏不止 480；可用于携带 Steam Input 手柄配置的会话，修复 480 无手柄配置问题）；无参回落 480，行为与旧版完全一致
- `-realappid`：压制 P2P 身份翻转（Bodycam 类黑屏逃生门）；`-onlinefix=<id> -realappid` 正交共存
- P2P flip：检测 SteamNetworkingSockets 后 GetAppID 报会话身份以匹配 session cert
- LobbyInvite 改写 / Persona 好友改写 / 覆盖层保持会话身份（PEAK 实测邀请对话框必需）

### 其他
- env-less 启动器游戏支持（第三方启动器拉起、无 SteamAppId 环境变量，PR#148 移植）
- DenuvoAuth 守门员（设备 token 优先；**关卡4 eticket 在线铸造未启用**，需要自建后端）
- CloudRedirect 云存档重定向（`[cloud] enabled=true` 时加载）
- 清单源集成：MHub / Sudama（depot keys + App 访问令牌，GUI 侧使用）

## 安装

1. 备份 Steam 根目录现有 `OpenSteamTool.dll` / `dwmapi.dll` / `xinput1_4.dll`
2. 用发布包内同名文件替换
3. 将 `opensteamtool.toml` 放到 Steam 根目录（如需 lua 扩展目录按注释调整 `[lua] paths`）
4. 重启 Steam 验证

## 已知限制与注意

- Steam **Beta #1788989629** 大更新预警：正式版当前链路可用；**不要手动切 Beta 通道**（强制更新且 hook/清单机制变更，适配进行中，见 `docs/dev/DEV-NOTES.md §9`）
- 请求码收口现状下，**新游戏下载必须依赖清单文件投喂**（文档 `docs/dev/bst-diff/30-server-block-2026-09-09.md`）
- 日志：本发布为 Release 构建（`[log] level` 配置项生效范围见配置注释）；Debug 构建带完整调试日志
- 对端联机需**相同会话身份**（`-onlinefix=<appid>` 值一致）