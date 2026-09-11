# 重要事件记录（EVENTS）

> 按时间正序，新事件追加到文末。字段：发生时间 / 具体情况 / 是否解决 / 处置（方法与现状）。
> 关联：架构与技术决策见 `DEV-NOTES.md`；按日操作台账见 `agents-log/`。

---

## 2026-09-08 内核基线切换至 BetterSteamTools

- **发生时间**：2026-09-08（决策日晚；code commit `991d55c`，docs `0c67c3c`）
- **具体情况**：本地 ZTool 内核从"上游 OpenSteamTool 2a08b0b + 自研三件套"切换为**照搬 BST `c7b435f` 主体**（其 12 个专有 commit 全量，含上游 PR#146 multi-inject/P2P / PR#148 env-less+结构检测+按需 eticket）；**关卡4（eticket 在线铸造后端）不做**；AppUpdater/Tokeer 剔除；本地三件套手工合并保留（LobbyInvite 重写 / 覆盖层保 480 / Persona 480→真实）。
- **是否解决**：是（主体完成；P2P flip 影响观察中）
- **处置**：v145 构建通过，三 DLL 部署 `d:\steam\`；备份 `ost-backups/20260908-pre-bst/`（src+docs+deployed）；实机验证：UNO 正常（Ubisoft Connect 可装）、**PEAK 邀请界面首次正常弹出**（本地此前 480 修复从未成功，属突破）；待好友侧验证真正联机。详见 DEV-NOTES §8。

## 2026-09-09 上午-下午 Steam 下载封锁侦查（服务器收口）

- **发生时间**：2026-09-09（10:50 仍放行 → 11:16 起收口，时间线见 `docs/dev/bst-diff/30-server-block-2026-09-09.md`）
- **具体情况**：非拥有账号的入库游戏**下载全部失败**（manifest 拿不到）。全链侦查结论：① CM 的 `GetManifestRequestCode` 对无 license 账户**拒绝发码**（11:16 起）；② 请求码**会话绑定**——跨会话/第三方静态码 CDN 一律 401（带 `/5/` 真实 URL 格式、多域名复测）；③ 开源工具链（opensteamtool.com/wudrm/steamrun 码服务、20770407 lua 端点、SteamManifestCache 等）全部走不通；④ **唯一正路 = 清单文件投喂 depotcache**（MHub 共享源 = 群友"代理下载+共享"同形态）。
- **是否解决**：是（链路打通；游戏本身仍受服务器拥有权限制，非本方可解）
- **处置**：内核两补丁部署：858 `OwnershipTicket` **Forge 兜底** + `kMaxWaitSeconds` 12→30；GUI 两修复（SteamCMD API 重试、清单拷贝逐份容错）；SAI 实例 Sifu 投喂清单后**下载成功**（28767 chunks）。备份 `ost-backups/20260909-pre-forge858/`、`20260909-pre-wait30/`。提交：ZSteamTool `688037b`(code)/`6f65bfb`(docs)、OSTGUI `08bfa41`(fix)。

## 2026-09-09 晚 Sifu 下载失败「No connection」排查与修复

- **发生时间**：2026-09-09 21:08-21:21
- **具体情况**：Sifu 再次下载报 "No connection"。排查出三件事：① 客户端要的清单 `2138711_5545504035024010382` / `2253841_3893356194676043314` **只在 config\depotcache（13:44 写入），根 depotcache 无**——Steam 客户端只读根目录；② 根目录清单此前被 Steam **卸载/回滚时清理**（13:33 失败自动卸载一次、21:21 用户手动卸载一次，两次都清根）——"清单乱动"真相；③ 15:27 起生效的 `manifest.lua`（打 20770407.xyz）在 CM 拒绝后**注入第三方码** → 绑定第三方会话 → CDN 必 401 → 拦截功能（上游原生）把 CM 原始应答覆盖，放大干扰。
- **是否解决**：是
- **处置**：清单复制回根 depotcache → 下载真实启动（22.7GB 开拉，21:20 实测 19.7Mbps）→ 用户手动停止+卸载（验证完成）；**`manifest.lua` 改为短路版 `fetch_manifest_code(gid) return "0"`**——L1（lua 层）恒成功返回 0 → L2（内置 provider）永不执行 → CM 原应答透传，第三方码注入彻底清零（原版存档 `manifest.lua.bak`）。**供未来参考：config\depotcache 为持久层，根 depotcache 为易失工作层（卸载必清）**。

## 2026-09-10 创意工坊系统性考证（item 独享清单 / 源实测 / 匿名路线）

- **发生时间**：2026-09-10
- **具体情况**：① **Workshop 下载流程** = 普通 depot 同一套 CDN 系统（`depot_id=consumer_appid`、`gid=hcontent_file`，同走 `GetManifestRequestCode`）；② **每个 item 独享清单、每次版本更新换新 gid**（实证：431960 item 3756621387 已装 `3320321238485096518` vs 服务器最新 `7330815643251988493`，ACF `NeedsUpdate=1`；旧 gid 文件永久残留）；③ **清单源实测**：MHub 有游戏清单（1990040 与本地逐字节一致）但**无任何工坊清单**（500=查无此文件语义钉死）；20770407.xyz 实锤是**请求码生成器**（返回 19 位数字）不是清单源；无码直拉工坊 manifest 实测 401；④ 群友"创意工坊下载器"日志 = **匿名登录 + 自动启用令牌**（不需要正版账号）→ 指向"匿名会话内拿有效码"路线；本地 `token_cache.json`（Sudama 源，8382 条**包令牌**，键=packageid）即同类钥匙库；**发现 GUI bug：LuaBuilder 按 appid 查 token 库而键是 packageid → addtoken 基本永不生成**（待修）。
- **是否解决**：部分（机制全查清；匿名会话实证脚本就绪**未跑通**：steam 库 connect 卡在 api.steampowered.com，本机系统 CA 被 Steam++ 污染 → 已装 certifi 未验证）
- **处置**：待续跑 `python -u %TEMP%\anon_step1.py` → `anontest.py`（workshop vs 游戏要码对照）；若 workshop 匿名放行实锤 → 复刻"匿名+令牌"下载路线可立项。MHub 实测结论：`500 = 未收录`、游戏清单健康、工坊清单为零。

## 2026-09-10 Steam 客户端大更新预警（Beta #1788989629）

- **发生时间**：2026-09-10（群友工具出现"未识别规则"；Beta 通道 #1788989629 情报细化；非官方 changelog，社区分析）
- **具体情况**：五条未识别规则（`BuildDepotDependency` / `GetAppIDForCurrentPipe` / `ProcessPendingLicenseUpdates` / `RecvPkt` / `steamui/GetTopManager`）→ 客户端内部重构信号。Beta 三失效点：① **manifest 清单重定向失效**（固定 gid 不更新 = 9/9 服务器码验证的客户端侧正式化，"必须用最新清单"）；② **D 加密授权依赖的当前应用身份识别失效**（env-less 链路重构）；③ **刷新库机制失效**（假 CDKey 激活不即时入库，需重开客户端）。函数布局/堆栈大规模变更，适配难度高；Beta 通道**强制更新**（steam.cfg 失效，"除非不重启"）。
- **是否解决**：未解决（预防性预警；正式版 9/10 晨实测链路仍可用）
- **处置**：DEV-NOTES §9 全档（符号→功能映射 / 预案五条 / Beta 三失效点 / 结论）；**纪律：不碰 Beta；上游 OpenSteamTool/BST 适配 commit 再增量评估；大更新落地前打备份；上游适配前不操作游戏库**。提交：ZSteamTool `f490b5a`(预警)、`43400c2`(情报细化)。

---

## 2026-09-10 群友提供新版 steamclient hook 签名表（Beta 适配弹药）

- **发生时间**：2026-09-10（紧随 Beta #1788989629 预警；群友经 LocalSend 提供 toml）
- **具体情况**：25 条 `[0xHASH] name / rva / sig` 签名表，覆盖 `RecvPkt`/`GetAppIDForCurrentPipe`/`BuildSpawnEnvBlock`/`SendCallbackToPipe`/`GetPipeClient`/`CheckAppOwnership`/`GetPackageInfo`/`MarkLicenseAsChanged`/`ProcessPendingLicenseUpdates`/`LoadDepotDecryptionKey`/`BuildDepotDependency` 等——**正是"未识别规则"的展开**。验证：25/25 hash 与本地 Fnv1a32 算法完全一致（同生态），绝大多数字节模式与我们现有 hook 点一一对应。
- **是否解决**：否（适配储备；当前正式版 GetProcAddress 定位仍有效，不动）
- **处置**：原样存档 `bst-diff/30-client-sigs-beta-2026-09-10.toml` + 分析 `...-10.md`（映射表/适配方向：HookMacros 定位扩展"hash→sig scan"回退路径，优先照搬上游适配）；提交见 git log。**适配窗口到来时的第一动作：用本机 beta dll 实测 sig 命中率**。

## 2026-09-10 CDN `/patch/` 端点侦查（群友线索）

- **发生时间**：2026-09-10（群友发送示例 `steampipe.akamaized.net/depot/3624141/patch/<old>/<new>`，称"CDN 不鉴权可拿，旧清单升级新清单"）
- **具体情况**（实测）：
  - ✅ 端点真实存在且无鉴权：3624141 → **200 / 22MB**；1206561 方向性：`patch/7056392980458818047/275499322938599766` → 200 / 69MB，反向 404；workshop 虚拟 depot（431960/1206560 consumer_appid）→ 404。即**仅真实游戏 depot 有效**。
  - ✅ 与 DepotDownloader issue #50（2013）的 URL 格式一致（`/depot/<id>/patch/<old>/<new>`，无码）。
  - ❌ **响应是加密/专有格式**：高熵 1.1 万+ 片段无任何可读文件名；用 Sudama 三把 depot key（1206561/1206560/431960）AES-ECB 解密后 zlib/LZMA 全失败；本地 depotcache 的 manifest 反而是**明文 protobuf**（文件名可见，如 discord_game_sdk.dll）。**目前无任何公开工具能解析该响应**（SteamKit2/DepotDownloader 无 delta 实现，issue #50 从未落地）。
  - 🎯 附带校正：1206560=**WorldBox**、431960=**Wallpaper Engine**（api.steamcmd.net 实名；此前"Core Keeper 地图"系误猜）；demo 说明 workshop manifest 文件名明文可见、item 独享清单结论不变。
  - ❌ Lizerium/LizeriumSteam + LizeriumFindChanges：与 Steam CDN **无关**（Lizerium 生态自研 launcher + 目录 diff 工具），仅"delta patch 思路"参考价值。
- **是否解决**：否（端点确认可用；结构解析未破，需要专业逆向或等上游实现）
- **处置**：结论暂记此条；**不立项**（解析成本高、收益未明；更实际路线仍是"最新 gid 清单投喂"与"匿名会话+令牌"）。若后续 Steam 客户端更新走 delta（beta 客户端可能内置 /patch/ 调用），届时以客户端实际请求抓包为准再跟进。

### patch 响应定性（后续补充，同日定稿）

- **确定格式**：`/patch/<old>/<new>` 响应 = 用 depot key 加密的 **delta patches（差分更新数据包）**。解密模型（对齐 tek-steamclient `sp_decode.c` 实现并实测验证）：首 16 字节 **AES-ECB 解出 = IV**，余下 **AES-CBC(IV)** 解密（PKCS7 padding）；解出后为数百个 **delta chunk 容器**：`zsv`（zstd + 源 chunk 字典，实测 411 个，随机期望仅 4）为主、`vzd`（LZMA + preset_dict）少量。大小自洽：1206561 全量新增 264MB vs 差分 66MB。
- **用途与局限**：增量更新通道（旧端点，客户端现行流程不用它）；**重建目标数据必须持有源 chunk（旧版文件）作字典**——只能服务"已有旧版本"场景；**不能当清单用、不能无中生有**。当前主线（清单投喂 / 匿名+令牌）不受影响。
- **参照实现**：teknology-hub/tek-steamclient（2025，C 库，`sp_decode.c`/`depot_patch.c`/`am_job_patch.c`；支持 manifests/patches/chunks、匿名 CM、tek-s3 拉码/钥匙——与群友下载器架构同源参考）。

### delta patch 复用线索（后续补充，供以后研究）

**核心诉求**：像 Steam 客户端一样"用本地旧安装 + 差分数据包 → 新版本"（无拥有权、无码增量更新）。

1. **tek-steamclient**（https://github.com/teknology-hub/tek-steamclient ，另有 SteamStuff fork）：C/C++ 库 + tek-sc-cli。`am_job_patch.c` = 应用差分的完整实现（patch job 管线：读旧安装 → 拉 patch → 字典解压 → 产出新文件）；`depot_delta_compute.c` 还能自产差分；`sp_decode.c` 已实测对齐（见上）。我们已验证其解密模型（ECB-IV+CBC）与真实响应吻合——**最可信的实现参照**。
2. **steamroom / steamroom-client**（https://github.com/landaire/steamroom ，crates.io 0.3.0 / 2026-07-15 活跃）：Rust 高层下载编排 + **delta patching**——备选（Rust 生态，可嵌入 CLI/库）。
3. **SteamKit PR #616**（2018 WIP，未合）：`ContentDeltaChunks` proto 原型 + `BInitializeDeltaChunks`——接口定义参考。
4. **RedAlt-SteamUp-Creator**（Reddiepoint）：SteamDB changelist + DepotDownloader 拉变更文件制作/安装更新包（xDelta 替代思路，周边参考）。

**研究入口建议**：以 tek-steamclient 为主线（C 可直接对照我们 C++ 内核），验证路径 = "本地旧 manifest + 旧文件 → `/patch/<old>/<new>` 拉差分 → 重建目标 chunk → 校验（尾部 zsv 容器 CRC32）→ 产出新文件树"。**前提约束**：必须有旧文件（差分无中生有），且需知道旧/新 gid（appinfo/ACF）。

---

## 2026-09-11 群友清单站请求码实测有效 —— 9/9「第三方码必 401」结论修正

- **发生时间**：2026-09-11（群友分享其建的清单站 + 配套 manifest.lua：`https://procurement-viii-sara-lang.trycloudflare.com/manifest/{gid}`）
- **具体情况**（实测）：群友站请求码 → CDN 下载 manifest **200**（Sifu 2138711：1.06MB zip；**workshop 431960：1394B zip 也 200**，两个 host 均验证）；同批对照：20770407（SSL-EOF）、wudrm（HTTP 530，Cloudflare origin error）、opensteamtool 官方（403）——**老三家全挂**。主页为"Steam Manifest 下载器"交互站（trycloudflare 隧道 = 个人临时服务，域名可能漂移）。
- **机制正解（SteamDB 《manifest request codes》2021/2022 官方观测 + 今日交叉实验确认）**：
  - `GetManifestRequestCode` 鉴权在**拿码环节**（CM）：校验拥有权 + appid/depotid/manifestid 匹配 + manifest 须在当前 appinfo 分支；不通过 → AccessDenied。
  - **码 = 全局一致**（同一 depot+gid 所有会话拿相同码）、**每 5 分钟轮换、CDN 侧 10 分钟有效**；码**不绑定请求者**，谁拿到真码都能用。
  - 交叉实验确认：SIFU 码请求 workshop gid → 401、反向 → 401（码与 depot+gid 严格绑定）；同 gid 重复要码值相同（5 分钟内全局一致）——**真码特征全部对上**。
- **结论修正**：9/9「跨会话/第三方码一律 401 / 码绑会话」的**机制推断错了**——码本身全局可用；9/9 观察到的 401 源于**拿到的不是真码**（假码/过期码）。**"鉴权在拿码环节"始终成立**（群友判断正确）。
- **"集体拉闸"变故分析**：9/9 11:16 起 3 家老码服务（opensteamtool 官方/wudrm/20770407）同时失效 = **公共"持拥有权会话"代签源集体断供**（官方 403、wudrm 530=CF origin error、20770407 死），时间点与 9/9 客户端正式更新（Beta #1788989629 同期）吻合——疑似 Valve 9/9 打击共享会话（封禁/强制会话失效）或各家停摆；**Valve 的码机制本身未变**（2021 年即如此）。
- **意义**：请求码路线（有效会话代签）**复活**——最新清单可直接拉（含工坊），配合 chunk（无码）+ depot key（Sudama/内核库），**完整下载链无拥有权可行**；依赖群友服务持有的拥有会话存活。
- **处置/下一步**：① 换 `d:\steam\config\lua\manifest.lua` 为群友实现（备份短路版）实测全链；② 隧道域名漂移需更新；③ GUI 把"码服务"纳入清单源体系（可选）。**短路策略**：仅对有效源启用注入。

## 未结事项速查（2026-09-10）

1. 匿名会话实证（workshop 码放行与否）——脚本就绪待跑
2. GUI addtoken 键错配 bug（appid vs packageid）——待修
3. PEAK 好友侧真实联机验证——待测
4. 普通 addappid 游戏回归——待测
5. origin/main 推送（本地领先众多 commit）——长期挂账
6. Steam 大更新上游适配跟踪——预警状态