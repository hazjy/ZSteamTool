# ZSteamTool v1.1.3 更新说明

> 适用版本：v1.1.3（对比 v1.1.2；只修 `-onlinefix` 会话状态的清理，并给 DLL 补上 Windows 版本信息，无其他功能变更）

## 修复

- **`-onlinefix` 的会话状态会跨启动残留**。这份状态（真实 AppId、P2P 身份翻转、`-realappid`、会话身份）只属于**那一个游戏进程**，旧版却要等到"Steam 下次自己启动一个不带 `-onlinefix` 的游戏"才清空。于是用 `-onlinefix` 玩过之后，再改用**不经过 Steam 启动链**的方式起游戏（例如配套 GUI 的「其他 → DLL 注入」），状态会继续作用于整场 Steam 会话：好友状态被持续改写（好友处在 480 世界时，你这边会把他显示成真实游戏名而不是 Spacewar），`LobbyInvite` 改写、GamesPlayed 名字补丁、P2P 翻转门控也都停留在"已激活"状态——换成别的游戏就可能出真问题。
- **现在状态随游戏进程一起结束**：游戏管道握手时把会话绑定到该游戏进程并抓取其句柄，看门狗线程等它退出，一退出立即清空。

## 新增

- **`OpenSteamTool.dll` 带上了 Windows 版本信息**：从此可以直接读到内核版本号（资源管理器 → 属性 → 详细信息，或配套 GUI 的「信息」页）。以前版本号只写在日志与诊断弹窗里，Release 版连日志都没有，外部无从得知。

## 升级方式

替换 Steam 根目录下的 `OpenSteamTool.dll` / `dwmapi.dll` / `xinput1_4.dll`（日常用 `Release\`，排障用 `Debug\`，日志在 `<Steam>\opensteamtool\`），重启 Steam 生效。`opensteamtool.toml` 无需改动。

> 一个正常现象：480 方案下双方都以 Spacewar(480) 身份运行，好友列表里互相显示"正在玩 480"是预期结果，不是故障。
