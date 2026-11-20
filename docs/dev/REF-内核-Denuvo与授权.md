# REF-内核-Denuvo与授权（ZSteamTool 侧）

> 何时读：改 Denuvo 授权窗口、票据身份、排查 D 加密游戏存档或启动异常时。
> 管什么：授权窗口与 eticket 时效模型、Denuvo 身份一致性、两种 `[denuvo] mode` 的语义、协议层边界。
> 不写什么：模块 → 代码位置见 `DEV-NOTES.md` §2；GUI 侧读写入口见工作区 `doc/`。

---

## Denuvo 身份一致性（2026-09-13）

- **现象与根因**：部分 D 加密游戏（实证：红色沙漠 3321460）按 `账号ID(SteamID)` 建**存档目录**（游戏日志 `saveRootPath` 可逐次观测身份），因此"游戏侧看到的 SteamID"直接决定存档/云存档/设置绑在哪个账号名下。
- BST `e55c90b` 删掉了 `GetSteamID` 的**授权窗口门**（上游 OpenSteamTool 保留该门），改为**整场**伪装成票据所属账号 → 一切按 SteamID 派生的用户数据被静默改绑出票账号 → 表现为"读不到自己账号下的存档"；这是 BST 相对上游的**行为差异**，不是上游缺陷。
- **两模式语义（`<Steam>/opensteamtool.toml` 的 `[denuvo] mode`，默认 `normal`；文件变更热重载，无需重启 Steam）**：

|模式|`GetSteamID` 伪装范围|窗口外所有权票源|授权窗口结束时写注册表 SteamID|后果|
|---|---|---|---|---|
|`normal`（默认，= 上游 OST 语义）|仅授权窗口内|`ForgeOnly`（身份=当前登录账号）|不写|游戏用户数据绑定**玩家自己账号**|
|`compat`（= BST 现行行为）|整场|`CredentialStoreThenForge`（身份=出票账号）|写（当时登录账号）|用户数据绑定**出票账号**；供窗口外仍复验 SteamID 的严格标题使用|

- **实现要点（三处触点绑成一个开关，保证模式内部自洽）**：
  - `Hooks_IPC_ISteamUser.cpp` 的 `GetSteamID` 窗口门与所有权票源选择；
  - `AppTicket::GetSpoofSteamID`（`normal` 下**只认票据内身份**，忽略注册表那个旧 `compat` 运行写入的"当时登录账号"，否则窗口内身份与票据不符 → 012/54）；
  - `DenuvoAuth::WriteSteamIdOnEndAuthorization`（`normal` 下不写，消除身份随登录账号漂移的第二来源）。
- **协议层边界（不可绕过）**：eticket 内的 SteamID 由 Valve 加密签名，**改不了**；若某标题确实需要整场以出票账号运行，其用户数据绑定出票账号即为必然；工具能做的是"默认安全 + 显式破例 + 提示"，不能消除该约束。
- **诊断方法（可复用）**：游戏侧 `saveRootPath` 行（逐次启动可见身份切换）> 内核 `ipc.log` 的 `GetSteamID ... -> Spoofed` 行。
- GUI 侧读写入口与配置文件位置见工作区根 `doc/` 下的事实考证（GUI 侧）。

## 拿到授权身份的三条路（2026-11-20 实测排名）

同一款 D 加密游戏、同一台机器，三种拿身份的方式结果不同：

| 方式 | 结果 | 原因 |
|---|---|---|
| **导入/导出授权**（`.ost` → `setAppTicket` / `setETicket` 写注册表） | **能用**，且能配自己的主账号玩 | 票是"能授权的那次会话"里的真票，内核窗口内把身份伪装成票里的 SteamID，Denuvo 交叉校验对得上；窗口结束后身份回当前账号，存档按当前账号算 |
| 切号授权（登出票账号跑一次 → 换回自己账号） | 不行 | 换号后客户端会重建该 app 的凭据，缓存与当前会话对不上 |
| 假入库（lua + 伪造所有权票） | 必然 88500005 | 伪造票只能骗客户端，Denuvo 要的是 Valve 用游戏专属密钥签的加密票，改不了 |

- **导出授权时，目标 app 不要出现在 lua 里**（2026-11-20 实测）：lua 在，内核就会介入该 app 的票据，导出来的就不是那次会话的原生票；导入后要么 88500005，要么时好时坏。先移走 lua → 导出 → 再按需放回。
- **票据缓存不会被自动写入**：内核只在 lua 的 `setAppTicket(appid, hex)` / `setETicket(appid, hex)` 与 Tokeer 后端那条路写，运行会话不会顺手覆盖。
- **有内核时用家庭共享位直接启动会被判篡改**：共享会话的票 + 内核介入，前后不一致 → 88500005；同一款游戏卸掉内核、纯原生共享反而能进。
- 存档按 SteamID 分目录（红色沙漠：`%LOCALAPPDATA%\Pearl Abyss\CD\save\<accountid>`），所以"用谁的票"和"用谁的存档"是两件事，导入授权这套正好能拆开。

## 授权窗口与 eticket 时效模型（2026-09-08 考证，含用户实测）

- **勘误**：①「30 分钟授权窗口」系误读：DenuvoAuth 的 authorization window 是**进程级一次性握手状态机**（首/二次握手开/关窗，无定时器）；README 的 30 分钟实为 **eticket 票据本身的 Steam 侧时效**（Steamworks `SteamEncryptedAppTicket_GetTicketIssueTime` 校验签发时间），**仅当"需要重新验证 eticket"时生效**。
- ② **正确模型（用户考证 + 证据收敛）**：
  - **设备 token（本机激活许可）在 → Denuvo 不再验 eticket → 长期可玩**（游戏不更新即稳定，用户实测）；
  - token 缺失/失效（游戏或系统更新、换凭据）→ 重新激活 → 需**新鲜 eticket**（30 分钟时效，超期 Steam 验证不通过）→ 须现取（正版号在线 extract / EticketClient 在线铸造后端）。
- ③ 88500005 线上实锤为**反篡改/滥用处罚族**（官方 support.codefusion.technology 页：88500005≈2 周 / 88500006≈24h），与票据时效分属两类错误。
