# REF-内核-版本与对齐（ZSteamTool 侧）

> 只收 **版本 / 上游基线 / 对齐与移植 / 已知问题**；分工与文档地图见工作区根 `README.md`。
> 来源：从 doc/内核-事实考证.md 拆出（2026-09-26，2026-09-26 按领域重划）。

---

## 对齐基线（照搬 BST 主体）

> 决策落地凭证：code commit `991d55c`，docs `0c67c3c`（事件登记原在 EVENTS，2026-09-13 并入本文件）。
> 纯事件经过与时间线见 `doc/EVENTS/`。

- **对齐正文已收敛**（一个事实只在一处）：决策与范围、关卡4/AppUpdater 取舍、四处本地手工合并点、
  后续纪律 → [`REF-内核-BST对齐档案.md`](REF-内核-BST对齐档案.md)。
- **构建/部署**：Debug v145 构建通过（2026-09-08）；三 DLL 部署 `d:\steam\`
  （旧版备份 `内核备份/20260908-pre-bst/`：src + docs + deployed-d-steam）。
- **实机验证（2026-09-08，用户实测）**：
  - **UNO**：Steam 运行可正常安装 Ubisoft Connect → **判定正常**（730「Updating product key」本地应答生效连带走通）；
  - **PEAK（联机）**：**邀请好友界面首次可正常弹出**——突破。⚠️ **记忆纠正**：本地此前"480 原生 + 修复合
    （LobbyInvite/覆盖层/Persona）"对 PEAK **从未成功过**，本次为首次成功；⚡ 待好友侧验证真正可联
    （P2P flip 与三件套并存影响仍观察中）；
  - 普通 addappid 游戏回归：待补（初判无碍）。

## 已知问题

- **下载链路补丁（2026-09-09 起部署，架构行为）**：
  - ① 858 `OwnershipTicket` handler **Forge 兜底**（无凭据库票/无后端时 off-by-four 伪造票应答，
    修复"更新所有权票 AccessDenied"）；
  - ② `kMaxWaitSeconds` 12→30（内置源慢响应不再超时）；
  - ③ `<Steam>/config/lua/manifest.lua`：**2026-10-06 起由 OSTGUI 设置页「请求码源」生成**
    （`ManifestLuaService`）——至少启用一个源时是**级联版**（`fetch_manifest_code_ex` /
    `fetch_manifest_code` 双钩子，按勾选顺序逐源取码，20 秒预算；需要 `depot_id` 的源只在 `_ex`
    那一路生效）；**全部关闭时仍是短路版** `fetch_manifest_code(gid) return "0"`——
    L1（lua 层）恒"成功" → L2（内置 provider）永不执行 → CM 原始应答透传，第三方码注入清零。
    写入由设置页直接覆盖（勾选一变就写，无二次确认、也不留备份；界面上只用黄字提醒用户自己备份）。
- **请求码机制的当前结论（含 9/9 修复的实质 = CDN 补"清单归属"校验）见 `doc/EVENTS/` 02 主题档案 §一，
  流程与"9/9 后为何失效"见 §二/§三；机制时间线 §四、侦查经过 §五、被推翻的说法 §六**。

|缺陷|位置|说明|
|---|---|---|
|`RecvPkt` 进程级全局缓冲无锁|`Hooks_NetPacket.cpp:34-51`|若该回调可被多个网络线程并发进入即为数据竞态（取决于 Steam 线程模型，未验证）|
|取码等待阻塞网络接收线程|`Hooks_NetPacket.cpp:584,646`|入站 147 在回调内 `wait_for(kMaxWaitSeconds=30s)`，且 `g_CodeFutures` 无淘汰上限，慢源下条目累积|
|`BuildCompleteAppOverviewChange` 移除列表只增不减|`Hooks_SteamUI.cpp:36,40-54`|加入后被永久重复追加进 `removed_appid`|
|`lua_addappid` 只校验密钥长度不校验 hex|`LuaConfig.cpp:219-221`|坏密钥静默变成含 `0x00` 的密钥交给 Steam|
|858 处理段注释与实现矛盾|`Hooks_NetPacket.cpp:474-487`|注释称"legacy NON-protobuf 无 schema"，实现却 `ParseFromArray`|
