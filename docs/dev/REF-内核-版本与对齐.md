# REF-内核-版本与对齐（ZSteamTool 侧）

> 只收 **版本 / 上游基线 / 对齐与移植 / 客户端大更新映射**；分工与文档地图见工作区根 `README.md`。
> 来源：从 doc/内核-事实考证.md 拆出（2026-09-26）。

---

## 对齐基线（照搬 BST 主体）

> 决策落地凭证：code commit `991d55c`，docs `0c67c3c`（事件登记原在 EVENTS，2026-09-13 并入本文件）。
> 纯事件经过与时间线见 `doc/EVENTS/`。

- **对齐正文已收敛**（一个事实只在一处）：决策与范围、关卡4/AppUpdater 取舍、四处本地手工合并点、后续纪律 → [`REF-内核-BST对齐档案.md`](REF-内核-BST对齐档案.md)。
- **构建/部署**：Debug v145 构建通过（2026-09-08）；三 DLL 部署 `d:\steam\`（旧版备份 `内核备份/20260908-pre-bst/`：src + docs + deployed-d-steam）。
- **实机验证（2026-09-08，用户实测）**：
  - **UNO**：Steam 运行可正常安装 Ubisoft Connect → **判定正常**（730「Updating product key」本地应答生效连带走通）；
  - **PEAK（联机）**：**邀请好友界面首次可正常弹出**——突破。⚠️ **记忆纠正**：本地此前"480 原生 + 修复合（LobbyInvite/覆盖层/Persona）"对 PEAK **从未成功过**，本次为首次成功；⚡ 待好友侧验证真正可联（P2P flip 与三件套并存影响仍观察中）；
  - 普通 addappid 游戏回归：待补（初判无碍）。

## Steam 客户端大更新的技术映射与应对纪律

> 事件经过、Beta 情报与时间线见 `doc/EVENTS/`；本节只保留**跨事件稳定**的映射与纪律。

- **符号 → 本体系功能映射**（群友工具"未识别规则"暴露的客户端内部重构点）：

  | 符号 | 语义 | 对应本体系功能（风险点） |
  |---|---|---|
  | `steamclient/BuildDepotDependency` | depot 依赖关系构建 | 依赖/共享 depot 处理（228980 redistributables 挂载、SharedDepots、依赖下载链） |
  | `steamclient/GetAppIDForCurrentPipe` | 由 pipe 推断当前 AppID | **env-less 启动器链路**（上游 PR#148 PipeManager；本地手合三件套之一；NBA 2K26/Suicide Squad 类） |
  | `steamclient/ProcessPendingLicenseUpdates` | 待处理 license 更新流程 | **许可证注入核心**（858 票据体系、credential store、addappid/lua 解锁）——最可能直接打脸入库 |
  | `steamclient/RecvPkt` | 网络包接收底层 | Hooks_NetPacket 依赖的包/消息层（job/UM 层若移位则 151/147/857/858 拦截点全变） |
  | `steamui/GetTopManager` | steamui 顶层管理器 | steamui 层注入（overlay 保 480、UI 相关 hook） |

- **hook 面的固有脆弱度**：hook 目标 **100% 走 RemoteToml 签名体系**（RVA 优先、字节签名次之，键为函数名 FNV-1a 哈希），**零 `GetProcAddress`**（仅 xinput 代理与 vstdlib/Nt 符号解析例外）⇒ 客户端更新后的适配主路径是**更新远程 TOML 元数据**，未必需要重编译；`Hooks_NetPacket`（消息层按 job 名）较稳，`Hooks_Misc`/`Hooks_CallBack`（导出函数 Detours）最脆。
- **应对纪律（不变）**：① 大更新落地前为 `d:\steam\` 部署件 + src 打备份；② 上游 OpenSteamTool/BST 出适配 commit 再增量评估（FETCH + diff）；③ **不碰 Beta 通道**；④ 上游适配前**暂停对游戏库做入库/更新操作**（避免半失效态写坏 appmanifest/lua）；⑤ 更新后小样本回归顺序：任意 addappid 入库 → 下载 → PEAK/UNO 启动与联机 → env-less 启动器类 → workshop 订阅下载。

## 已知问题

- **下载链路补丁（2026-09-09 起部署，架构行为）**：① 858 `OwnershipTicket` handler **Forge 兜底**（无凭据库票/无后端时 off-by-four 伪造票应答，修复"更新所有权票 AccessDenied"）；② `kMaxWaitSeconds` 12→30（内置源慢响应不再超时）；③ `<Steam>/config/lua/manifest.lua` 采用**短路版** `fetch_manifest_code(gid) return "0"`——L1（lua 层）恒失败 → L2（内置 provider）永不执行 → CM 原始应答透传，第三方码注入彻底清零（原实现存档 `manifest.lua.bak`）。
- **请求码机制的当前结论（含 9/9 修复的实质 = CDN 补"清单归属"校验）见 `doc/EVENTS/` 02 主题档案 §一，流程与"9/9 后为何失效"见 §二/§三；机制时间线 §四、侦查经过 §五、被推翻的说法 §六**。

| 缺陷 | 位置 | 说明 |
|---|---|---|
| `RecvPkt` 进程级全局缓冲无锁 | `Hooks_NetPacket.cpp:34-51` | 若该回调可被多个网络线程并发进入即为数据竞态（取决于 Steam 线程模型，未验证） |
| 取码等待阻塞网络接收线程 | `Hooks_NetPacket.cpp:584,646` | 入站 147 在回调内 `wait_for(kMaxWaitSeconds=30s)`，且 `g_CodeFutures` 无淘汰上限，慢源下条目累积 |
| `BuildCompleteAppOverviewChange` 移除列表只增不减 | `Hooks_SteamUI.cpp:36,40-54` | 加入后被永久重复追加进 `removed_appid` |
| `lua_addappid` 只校验密钥长度不校验 hex | `LuaConfig.cpp:219-221` | 坏密钥静默变成含 `0x00` 的密钥交给 Steam |
| 858 处理段注释与实现矛盾 | `Hooks_NetPacket.cpp:474-487` | 注释称"legacy NON-protobuf 无 schema"，实现却 `ParseFromArray` |
