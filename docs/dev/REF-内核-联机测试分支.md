# REF · 内核联机测试分支（`exp/onlinefix-appid-rewrite`）

> 何时读：接手这条分支、复现 / 回滚某次内核部署，或排查「联机为什么这样打」时。
> 管什么：分支做了什么（提交分类与改动点）、三条判据、结论与边界、部署回滚。
> 不写什么：实测流水与试错过程见 `doc/EVENTS/04-内核联机测试分支实测.md`；机制与证据链见 `REF-内核-联机会话.md`；待办见 `doc/TODO/挂起/261016-机械狂欢联机诊断.md`。

## 1. 分支做了什么

- 2026-10-16 一天：基点 `main`（`1a9d2d3`），21 个提交，主题是「入库游戏走 Steam 官方大厅联机」；2026-10-17 随联机工作暂停而冻结。
- 提交四类：观测 / 诊断（`e1f8382`…`ffdfd01`，7 个）· 诊断总开关 + 客户端侧 appid 洗白（`e5c3ce2` / `8755f4e`）· 会话生命周期与开关（`d25f3a9`）· 作用域 / 锁定 / 探针（`7cc15d7` / `c3889d0` / `75abe64`）。
- 改动点：`Hooks_Misc` 加 `hkGetAppIDForCurrentPipe` 洗白判定 + 探针与 `EngageEnvSession` / `ResetEnvSession`；`PipeManager` 握手登记会话；`Config` / `opensteamtool.example.toml` 加 `[onlinefix] env_session`（默认 true，排查用总开关）；`dllmain.h` 关掉其余改写，只留 `IClientRemoteStorage` 首参数一处（未做行为验证）。

## 2. 三条判据（日志在 `<Steam>\opensteamtool\`）

- 建房成功：`MMS CreateLobby(6601) #1=480` → `CreateLobbyResponse(6602)` 带大厅 SteamID → 随后的 `SetLobbyData` / `LobbyData` / `LeaveLobby` 全 `#3=1`，界面邀请弹窗出现。
- 零报文改写：`MMS appid rewrite` 行数 0（2026-10-17 那批改写代码已删除，成为编译期事实）。
- 会话生命周期：`armed(宿主) → re-armed(游戏) → cleared(退出)` 三行齐。

## 3. 结论与边界

- 结论：大厅层被「宿主会话 + 客户端 appid 洗白」打通；游戏层 P2P 始终未建立，联机工作暂停、分支冻结；完整数据见 `doc/EVENTS/报告-机械狂欢联机可行性-2026-10-16.md` §七。
- 边界：垫片按文件名认 `steam.exe`（改名即不注入）、同一时间只支持一个会话游戏、宿主与游戏在 Steam 侧是两条独立记录；签名表取表规则见 `doc/PREFERENCES.md`。
- 部署：三件套 `OpenSteamTool.dll` + `dwmapi.dll` + `xinput1_4.dll` → `<Steam>\`；日常 Release、联机诊断必须 Debug；回滚 = 重编或取对应版本发布包（先退 Steam）。
