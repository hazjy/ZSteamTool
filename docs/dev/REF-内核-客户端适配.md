# REF-内核-客户端适配（ZSteamTool 侧）

> 只收 **Steam 客户端大更新的技术映射与应对纪律**；分工与文档地图见工作区根 `README.md`；事件经过与时间线见 `doc/EVENTS/`。

---

## Steam 客户端大更新的技术映射与应对纪律

> 本节只保留**跨事件稳定**的映射与纪律。

- **符号 → 本体系功能映射**（群友工具"未识别规则"暴露的客户端内部重构点）：
  - `steamclient/BuildDepotDependency`：depot 依赖关系构建
    - 依赖/共享 depot 处理（228980 redistributables 挂载、SharedDepots、依赖下载链）。
  - `steamclient/GetAppIDForCurrentPipe`：由 pipe 推断当前 AppID
    - **env-less 启动器链路**（上游 PR#148 PipeManager；本地手合三件套之一；NBA 2K26/Suicide Squad 类）。
  - `steamclient/ProcessPendingLicenseUpdates`：待处理 license 更新流程
    - **许可证注入核心**（858 票据体系、credential store、addappid/lua 解锁）——最可能直接打脸入库。
  - `steamclient/RecvPkt`：网络包接收底层
    - Hooks_NetPacket 依赖的包/消息层（job/UM 层若移位则 151/147/857/858 拦截点全变）。
  - `steamui/GetTopManager`：steamui 顶层管理器
    - steamui 层注入（overlay 保 480、UI 相关 hook）。
- **hook 面的固有脆弱度**：hook 目标 **100% 走 RemoteToml 签名体系**（RVA 优先、字节签名次之，键为函数名 FNV-1a 哈希），**零 `GetProcAddress`**（仅 xinput 代理与 vstdlib/Nt 符号解析例外）；
  ⇒ 客户端更新后的适配主路径是**更新远程 TOML 元数据**，未必需要重编译；`Hooks_NetPacket`（消息层按 job 名）较稳，`Hooks_Misc`/`Hooks_CallBack`（导出函数 Detours）最脆。
- **包层有两个不可信来源，别只判非空**（2026-10-14 自崩事故的教训）：
  - **数据指针**：发送侧来自 `BBuildAndAsyncSendFrame` 的调用参数，接收侧来自 `CNetPacket.m_pubData`，都必须判**「可读」**而不只是判空（`PlausiblePtr`：`0x10000 ≤ p < 0x800000000000`）；
    客户端会给出**非空但不可读**的哨兵值（实测 `0xFFFFFFFF` = 版本戳初值 `-1`），解引用即把 Steam 带走；门槛只该收紧到"不解析这帧"，不要改变"解析不了就原样透传"的既有语义。
  - **`CNetPacket` 的布局随客户端版本漂移**：新客户端在 `m_hConnection` 之后插入了两个 4 字节版本戳（`m_pubData` +0x08→**+0x10**、`m_cubData` +0x10→**+0x18**）；
    `src/Steam/Structs.h` 用 `static_assert` 锁死这两个偏移——**布局再变要让编译先失败**，别留到运行时才发现；根治方向是 BST `1d15f39e` 的不透明结构 + 运行时探测（`NetPkt::Data()/Size()`），本项目尚未移植。
- **应对纪律（不变）**：① 大更新落地前为 `d:\steam\` 部署件 + src 打备份；② 上游 OpenSteamTool/BST 出适配 commit 再增量评估（FETCH + diff）；③ **不碰 Beta 通道**；
  ④ 上游适配前**暂停对游戏库做入库/更新操作**（避免半失效态写坏 appmanifest/lua）；⑤ 更新后小样本回归顺序：任意 addappid 入库 → 下载 → PEAK/UNO 启动与联机 → env-less 启动器类 → workshop 订阅下载。

---

## 库可见性：注入许可 ≠ 进库（2026-10-22 实证）

- **症状**：`InitFakeLicense` 报 `adding 528 apps` 成功、`CheckAppOwnership` 也返回拥有，但库里只出现**已安装**的游戏（有 `appmanifest_*.acf`），未安装的整批缺席。
- **机制**：库的可见性由 steamui 侧 `CSteamApp.OwnershipFlags` 决定（移除路径就是靠把它清成 `None` 把 app 摘掉的）；注入只改客户端的**许可/包**，Steam 不会替未安装的 app 主动重建 overview，标志一直是 `None`。
- **修法（已落地）**：`Hooks_SteamUI` 补对称的"新增"通道——`QueueAdditions()` 排队 → `CSteamUIAppControllerRunFrame` 每帧 ≤64 个 → 对 `GetAppByID(id,false)` **已存在**的 app：清 `LicenseLocked`、补 `OwnsLicense|LicensePermanent`、还原 `PurchasedTime`、`MarkAppChange(AppInfoOrConfig)`；`FillInAppOverview` 内也做同一套补写（原来只设 `PurchasedTime`）。实现收在 `ApplyFakeOwnership()`。
- **硬约束：绝不 `GetAppByID(id, true)`**：会造出没有 appinfo 的空壳 `CSteamApp`，正是 1.2.0 修过的未初始化断言 dump 来源；查不到就跳过并计数（`DepotKeySet` 里多数是 depot id，本就查不到，属正常）。
- **验证判据**：`steamui.log` 有 `QueueAdditions: N id(s) queued` 且 pending 归零；`pics.log` 出现原先缺席的 appid（已进入客户端"拥有"集合）；`package.log` 出现成批 `Marking app … as owned`。
