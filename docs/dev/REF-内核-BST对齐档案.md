# REF-内核-BST对齐档案（ZSteamTool 侧）

> 只收 **BetterSteamTools（BST）对齐的档案性稳定结论**（基线/签名/Beta 适配结论）；分工与文档地图见工作区根 `README.md`。
> 来源：从 doc/内核-事实考证.md 拆出（2026-09-26）。

---

## BST 对齐（原 DEV-NOTES §8，2026-09-08 完成）

> 决策落地凭证：code commit `991d55c`，docs `0c67c3c`（事件登记原在 EVENTS，2026-09-13 并入本文件）。
> 纯事件经过与时间线见 `doc/EVENTS/`。

- **决策与范围**：照搬 BST `c7b435f` 主体（其 12 个专有 commit 全量内容，含上游 PR#146 / PR#148 完整版）；
  **关卡4（eticket 在线铸造）不启用**——无自建后端，`EticketClient` 默认禁用零网络；
  **AppUpdater / Tokeer 剔除**（dllmain 不接入，文件保留，见上游同步台账
  `docs/dev/upstream-log/0001-2026-09-08-bst-c7b435f.md` §2/§4）；
- **手工合并点（本地特有保留）**：
  1. `Hooks_CallBack.cpp` LobbyInvite 改写（恢复本地版，`OnlineFixRealAppId()` → `ResolveAppId()` 适配 BST API）；
  2. `Hooks_Misc.cpp` BuildSpawnEnvBlock 覆盖层**保持 480**（本地 PEAK 实测方向；与 BST 恢复真实方向冲突，取本地）；
  3. `Hooks_NetPacket.cpp` Persona 好友 480→真实改写（BST 版删除段加回）；
  4. `Steam/Callback.h` 补回 `LobbyInvite_t` + `k_iSteamMatchmakingCallbacks=300`（BST 版删除，本地依赖）。
- **构建/部署**：Debug v145 构建通过（2026-09-08）；三 DLL 部署 `d:\steam\`
  （旧版备份 `内核备份/20260908-pre-bst/`：src + docs + deployed-d-steam）；
- **实机验证（2026-09-08，用户实测）**：
  - **UNO**：Steam 运行可正常安装 Ubisoft Connect → **判定正常**（730「Updating product key」本地应答生效连带走通）；
  - **PEAK（联机）**：**邀请好友界面首次可正常弹出** —— 突破。⚠️ **记忆纠正**：本地此前"480 原生 + 修复合
    （LobbyInvite/覆盖层/Persona）"对 PEAK **从未成功过**，本次为首次成功；⚡ 待好友侧验证真正可联
    （P2P flip 与三件套并存影响仍观察中）；
  - 普通 addappid 游戏回归：待补（初判无碍）。
- **后续**：BST 主体已并入；上游/BST 新 commit 用 FETCH + diff 增量评估（git 需 openssl 后端）；三件套与 P2P flip 共存影响继续用联机样本观察。

## 稳定结论（跨事件不变）

- **签名/Beta 适配**：hook 目标走 RemoteToml 签名体系（RVA 优先、字节签名次之），适配主路径是更新远程 TOML 元数据，未必需要重编译——详见 `REF-内核-版本与对齐.md`。
- **Beta 纪律**：不碰 Beta 通道；上游/BST 出适配 commit 后再增量评估（FETCH + diff）。
