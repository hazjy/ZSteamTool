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
| **导入/导出授权**（`.ost` → `setAppTicket` / `setETicket` 写注册表；**导出前先禁用该 app 的 lua**） | **能用**，且能配自己的主账号玩 | 票是"能授权的那次会话"里的真票，内核窗口内把身份伪装成票里的 SteamID，Denuvo 交叉校验对得上；窗口结束后身份回当前账号，存档按当前账号算 |
| 切号授权（正版号启动一次 → 换回自己账号） | **能用**（2026-11-20 修复后实测） | 内核在 `858` / IPC 成功分支把真票落进 HKCU；换号后授权窗口内直接出这张票、身份伪装成票内 SteamID。前提是该 app 的真许可会话至少启动过一次，且凭据库没被清空 |
| 假入库（lua + 伪造所有权票） | `88500000`（泛化验票失败） | 伪造票只能骗客户端；`88500005` 属反篡改处罚族，假入库是否必然触发未证，不能写成定论 |

- **票据缓存会自动写入**（2026-11-20 起）：内核在 `858` 成功与 IPC `GetAppOwnershipTicketExtendedData` 的有效许可分支，把服务端真票写进 HKCU 的 `AppTicket` + `SteamID`（`HasDepot && HasValidLicense`）。lua 的 `setAppTicket` / `setETicket` 与 Tokeer 后端仍可用，作为离线、换机时的兜底。
- 实现位置：`Hooks_NetPacket.cpp:526` 成功分支、`Hooks_IPC_ISteamUser.cpp:86` 回落分支；`AppTicket::CaptureServerAppTicket` 做字节与 SteamID 校验、同票去重。
- 存档按 SteamID 分目录（红色沙漠：`%LOCALAPPDATA%\Pearl Abyss\CD\save\<accountid>`），所以"用谁的票"和"用谁的存档"是两件事，导入授权这套正好能拆开。

## 什么时候不要让内核伪造所有权（2026-11-20，客户端 11.23.65.88）

内核的伪造是「把当前账号当成真拥有」：`CheckAppOwnership` 改写 + 858 回包换成伪造票。**导出 / 提取**这类需要向服务端出示原生票的场合，伪造一定帮倒忙，会造出第二条所有权来源。**运行时启动**是否也如此，现有证据不足，见本节末尾「未定」。

| 情形 | 结果 | 依据 |
|---|---|---|
| 家庭共享来的游戏（有真共享许可）挂着 lua 启动 | **未定**：早先实测 88500005；自动落盘修复后，gao819（家庭共享）+ lua 启动成功并落 184 字节真票 | 两次观察的前置状态不同，缺报文级证据，见下 |
| 提取 / 导出授权时该 app 的 lua 在场 | 票提得出来（GUI 日志 `extract mode done：success=True`，AppTicket 178~184 字节），但不是那次会话的原生票；导入后 88500005 或时好时坏 | 2026-11-20 实测；提取走内核 hook 的 `RequestEncryptedAppTicket` |
| 纯假入库（没有真许可） | `88500000`（泛化失败）；是否升级为 `88500005` 未证 | 见上表第三行 |

- **做法**：涉及**导出 / 提取**该 app 时，先停用它的解锁（`<AppID>.lua` 改成 `.lua.disabled`，入库页「更多 → 禁用解锁」），做完再恢复。`LuaFileWatcher` 只解析 `.lua` 结尾，改名即热重载，**不用重启 Steam**。禁用只去掉伪造，不产生许可，所以对纯假入库无效。家庭共享的运行时启动不再要求禁用，票由 `858` 成功分支自动落盘。
- **判据**：内核 `netpacket.log` 里该 app 的 `OwnershipTicketResponse[858]: appid=<id> forged ownership ticket`；禁用后这条不再出现。
- **代码级链路（三条介入都以 `HasDepot` 为前提）**：
  - 总闸 `Hooks_IPC.cpp:84`：`HasDepot(ResolveAppId())` 不成立就直接放行原始请求，不进任何 IPC 后处理。提取子进程设了 `SteamAppId`，所以这里判的就是目标 appid。
  - 伪造 `Hooks_IPC_ISteamUser.cpp:83`（normal、窗口外 `ForgeOnly`）→ `AppTicket.cpp:49-67`：取 donor 票（228980 → 480 → 7）签名前的段，换上目标 appid，再拼回 donor 的签名。签名仍是 donor 的，Valve 没为这个 appid 签过。
  - 回包 `Hooks_NetPacket.cpp:461-573`：5527 / 858 仅在 `HasDepot` 且 `eresult != OK` 时用凭据库票覆盖并置 OK。
  - 所以 `.lua.disabled` 一改，三条链路同时失效，导出的才是 Valve 给当前账号的原生票。
- **未定**：家庭共享的真许可与内核伪造的“真拥有”是否会在服务端造成不自洽，目前**没有结论**。旧观察（共享 + lua → 88500005，卸内核纯原生共享可进）支持“不自洽”；新观察（自动落盘后 gao819 共享 + lua 启动成功，HKCU 落 184 字节真票、票内 SteamID 与登录账号一致，858 成功时内核不再替换票）支持“可共存”。两者可能对应不同前置状态（旧票 / 凭据库残留 vs 干净库），缺报文级证据，不能互相否定。
- **未取证（导出侧）**：用户报告的「服务端拒绝」环节没有定位到报文级证据：今天两次提取在 GUI 侧都是 `success=True`，失败表现为之后的 88500005 —— 即拒绝点在**导入后的 Denuvo 激活**，不在提取本身。要定论需要抓到服务端拒绝的那一步报文（858 / 5527 / 859 的字段差异），目前没有。

## 授权窗口与 eticket 时效模型（2026-09-08 考证，含用户实测）

- **勘误**：①「30 分钟授权窗口」系误读：DenuvoAuth 的 authorization window 是**进程级一次性握手状态机**（首/二次握手开/关窗，无定时器）；README 的 30 分钟实为 **eticket 票据本身的 Steam 侧时效**（Steamworks `SteamEncryptedAppTicket_GetTicketIssueTime` 校验签发时间），**仅当"需要重新验证 eticket"时生效**。
- ② **正确模型（用户考证 + 证据收敛）**：
  - **设备 token（本机激活许可）在 → Denuvo 不再验 eticket → 长期可玩**（游戏不更新即稳定，用户实测）；
  - token 缺失/失效（游戏或系统更新、换凭据）→ 重新激活 → 需**新鲜 eticket**（30 分钟时效，超期 Steam 验证不通过）→ 须现取（正版号在线 extract / EticketClient 在线铸造后端）。
- ③ 88500005 线上实锤为**反篡改/滥用处罚族**（官方 support.codefusion.technology 页：88500005≈2 周 / 88500006≈24h），与票据时效分属两类错误。
