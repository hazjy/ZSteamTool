# REF-内核-客户端适配（ZSteamTool 侧）

> 只收 **Steam 客户端大更新的技术映射与应对纪律**；分工与文档地图见工作区根 `README.md`。
> 来源：从 doc/内核-事实考证.md 拆出（2026-09-26，2026-09-26 按领域重划）。
> 事件经过与时间线见 doc/EVENTS/。

---

## Steam 客户端大更新的技术映射与应对纪律

> 事件经过、Beta 情报与时间线见 `doc/EVENTS/`；本节只保留**跨事件稳定**的映射与纪律。

- **符号 → 本体系功能映射**（群友工具"未识别规则"暴露的客户端内部重构点）：
  - `steamclient/BuildDepotDependency`：depot 依赖关系构建
    - 依赖/共享 depot 处理（228980 redistributables 挂载、SharedDepots、依赖下载链）
  - `steamclient/GetAppIDForCurrentPipe`：由 pipe 推断当前 AppID
    - **env-less 启动器链路**（上游 PR#148 PipeManager；本地手合三件套之一；NBA 2K26/Suicide Squad 类）
  - `steamclient/ProcessPendingLicenseUpdates`：待处理 license 更新流程
    - **许可证注入核心**（858 票据体系、credential store、addappid/lua 解锁）——最可能直接打脸入库
  - `steamclient/RecvPkt`：网络包接收底层
    - Hooks_NetPacket 依赖的包/消息层（job/UM 层若移位则 151/147/857/858 拦截点全变）
  - `steamui/GetTopManager`：steamui 顶层管理器
    - steamui 层注入（overlay 保 480、UI 相关 hook）
- **hook 面的固有脆弱度**：hook 目标 **100% 走 RemoteToml 签名体系**（RVA 优先、字节签名次之，
  键为函数名 FNV-1a 哈希），**零 `GetProcAddress`**（仅 xinput 代理与 vstdlib/Nt 符号解析例外）
  ⇒ 客户端更新后的适配主路径是**更新远程 TOML 元数据**，未必需要重编译；`Hooks_NetPacket`
  （消息层按 job 名）较稳，`Hooks_Misc`/`Hooks_CallBack`（导出函数 Detours）最脆。
- **应对纪律（不变）**：
  - ① 大更新落地前为 `d:\steam\` 部署件 + src 打备份；
  - ② 上游 OpenSteamTool/BST 出适配 commit 再增量评估（FETCH + diff）；
  - ③ **不碰 Beta 通道**；
  - ④ 上游适配前**暂停对游戏库做入库/更新操作**（避免半失效态写坏 appmanifest/lua）；
  - ⑤ 更新后小样本回归顺序：任意 addappid 入库 → 下载 → PEAK/UNO 启动与联机 → env-less 启动器类
    → workshop 订阅下载。
