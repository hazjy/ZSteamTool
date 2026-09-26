# REF-内核-Denuvo与会话身份（ZSteamTool 侧）

> 只收 **DenuvoAuth / 授权与票据 / 会话身份 / `-onlinefix` / 环境变量与进程身份**；分工与文档地图见工作区根 `README.md`。
> 来源：从 doc/内核-事实考证.md 拆出（2026-09-26）。

---

## Denuvo 身份一致性（2026-09-13）

- **现象与根因**：部分 D 加密游戏（实证：红色沙漠 3321460）按 `账号ID(SteamID)` 建**存档目录**（游戏日志 `saveRootPath` 可逐次观测身份），因此"游戏侧看到的 SteamID"直接决定存档/云存档/设置绑在哪个账号名下。
- BST `e55c90b` 删掉了 `GetSteamID` 的**授权窗口门**（上游 OpenSteamTool 保留该门），改为**整场**伪装成票据所属账号 → 一切按 SteamID 派生的用户数据被静默改绑出票账号 → 表现为"读不到自己账号下的存档"。这是 BST 相对上游的**行为差异**，不是上游的缺陷。
- **两模式语义（`<Steam>/opensteamtool.toml` 的 `[denuvo] mode`，默认 `normal`；文件变更热重载，无需重启 Steam）**：

  | 模式 | `GetSteamID` 伪装范围 | 窗口外所有权票源 | 授权窗口结束时写注册表 SteamID | 后果 |
  |---|---|---|---|---|
  | `normal`（默认，= 上游 OST 语义） | 仅授权窗口内 | `ForgeOnly`（身份=当前登录账号） | 不写 | 游戏用户数据绑定**玩家自己账号** |
  | `compat`（= BST 现行行为） | 整场 | `CredentialStoreThenForge`（身份=出票账号） | 写（当时登录账号） | 用户数据绑定**出票账号**；供窗口外仍复验 SteamID 的严格标题使用 |

- **实现要点（三处触点绑成一个开关，保证模式内部自洽）**：`Hooks_IPC_ISteamUser.cpp` 的 `GetSteamID` 窗口门与所有权票源选择；`AppTicket::GetSpoofSteamID`（`normal` 下**只认票据内身份**，忽略注册表那个旧 `compat` 运行写入的"当时登录账号"，否则窗口内身份与票据不符 → 012/54）；`DenuvoAuth::WriteSteamIdOnEndAuthorization`（`normal` 下不写，消除身份随登录账号漂移的第二来源）。
- **协议层边界（不可绕过）**：eticket 内的 SteamID 由 Valve 加密签名，**改不了**；若某标题确实需要整场以出票账号运行，其用户数据绑定出票账号即为必然——工具能做的是"默认安全 + 显式破例 + 提示"，不能消除该约束。
- **诊断方法（可复用）**：游戏侧 `saveRootPath` 行（逐次启动可见身份切换）> 内核 `ipc.log` 的 `GetSteamID ... -> Spoofed` 行。
- GUI 侧读写入口与配置文件位置见工作区根 `doc/` 下的事实考证（GUI 侧）。

## 授权窗口与 eticket 时效模型（2026-09-08 考证，含用户实测）

- **勘误**：①「30 分钟授权窗口」系误读——DenuvoAuth 的 authorization window 是**进程级一次性握手状态机**（首/二次握手开/关窗，无定时器）；README 的 30 分钟实为 **eticket 票据本身的 Steam 侧时效**（Steamworks `SteamEncryptedAppTicket_GetTicketIssueTime` 校验签发时间），**仅当"需要重新验证 eticket"时生效**。
- **正确模型（用户考证 + 证据收敛）**：**设备 token（本机激活许可）在 → Denuvo 不再验 eticket → 长期可玩**（游戏不更新即稳定，用户实测）；token 缺失/失效（游戏或系统更新、换凭据）→ 重新激活 → 需**新鲜 eticket**（30 分钟时效，超期 Steam 验证不通过）→ 须现取（正版号在线 extract / EticketClient 在线铸造后端）。
- ③88500005 线上实锤为**反篡改/滥用处罚族**（官方 support.codefusion.technology 页：88500005≈2 周 / 88500006≈24h），与票据时效分属两类错误。

## 联机会话身份 `-onlinefix`（2026-09-10 落地，commit `3dce9c9`）

- **内核侧事实**：`SpawnProcess` int3 回调解析命令行会话身份——`-onlinefix`（默认 Spacewar 480）｜`-onlinefix=<appid>`（显式，推荐）｜`-onlinefix <appid>`（跟随数字）；**非数字/0/超 uint32 一律回落 480**（`Hooks_Misc.cpp:41-60`）。同命令行出现 `-realappid` 时抑制本次 P2P appid 翻转。
- **全链路 8 处身份一致化**（`Hooks_Misc::SessionAppId()`）：① OptedInMask ② BuildSpawnEnvBlock（**只记日志、不改值** → 覆盖层保持 480，本地刻意与 BST 相反）③ LobbyInvite 改写 ④ 所有权票 appid 归一 ⑤ RichPresence topmost 排除 ⑥ Persona 好友 480→真实 ⑦ GamesPlayed 名字改写 ⑧ 调试日志。
- **设计意图**：免费联机游戏不止 480 一个，可换任意免费 AppID（含带 Steam Input 手柄配置者，解决 480 无手柄配置的问题）；**对端需同配**。
- 480 邀请失效的真凶考证与 v1/v2/v3 实证轮（属 GUI 侧调研档案）见工作区根 `doc/` 下的事实考证（GUI 侧）。
