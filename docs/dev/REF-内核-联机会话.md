# REF-内核-联机会话（ZSteamTool 侧）

> 只收 **联机会话身份 `-onlinefix` / 全链路身份一致化 / 免费联机 AppID 选择**；分工与文档地图见工作区根 `README.md`。
> 来源：从 doc/内核-事实考证.md 拆出（2026-09-26，2026-09-26 按领域重划）。

---

## 联机会话身份 `-onlinefix`（2026-09-10 落地，commit `3dce9c9`）

- **内核侧事实**：`SpawnProcess` int3 回调解析命令行会话身份——`-onlinefix`（默认 Spacewar 480）｜
  `-onlinefix=<appid>`（显式，推荐）｜`-onlinefix <appid>`（跟随数字）；
  **非数字/0/超 uint32 一律回落 480**（`Hooks_Misc.cpp:41-60`）。
  同命令行出现 `-realappid` 时抑制本次 P2P appid 翻转。
- **全链路 8 处身份一致化**（`Hooks_Misc::SessionAppId()`）：
  ① OptedInMask ② BuildSpawnEnvBlock（**只记日志、不改值** → 覆盖层保持 480，本地刻意与 BST 相反）
  ③ LobbyInvite 改写 ④ 所有权票 appid 归一 ⑤ RichPresence topmost 排除 ⑥ Persona 好友 480→真实
  ⑦ GamesPlayed 名字改写 ⑧ 调试日志。
- **设计意图**：免费联机游戏不止 480 一个，可换任意免费 AppID（含带 Steam Input 手柄配置者，解决 480 无手柄配置的问题）；**对端需同配**。
- 480 邀请失效的真凶考证与 v1/v2/v3 实证轮（属 GUI 侧调研档案）见工作区根 `doc/` 下的事实考证（GUI 侧）。
