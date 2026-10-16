# p2ptest —— Steam P2P 连通性探针

**用途**：把"Steam 的 P2P 到底能不能建立连接"从游戏里剥离出来单独测。`p2p_probe.exe` 只调
`CreateListenSocketP2P` / `ConnectP2P` / 中继节点 ping 查询，不牵扯任何游戏。

## 构建

```
tools\p2ptest\build.cmd        # 用 VS18 自带的 vcvars64，产物落 build\p2ptest\
```

源码含中文，必须 `/utf-8`（否则 cl 按 cp936 读会把注释里的引号吃掉，报 C2001）；批处理必须 CRLF。

## 用法

| 模式 | 命令 | 说明 |
|---|---|---|
| 中继节点 | `p2p_probe.exe pop` | 查本机到 Steam SDR 中继节点的 ping 数据，几秒出结果。两边都 `0 / 0` 说明链路根本没通 |
| 监听 | `p2p_probe.exe listen <虚拟端口> --seconds N` | 建监听并挂回调自动接受连接 |
| 连接 | `p2p_probe.exe connect <对端SteamID> <虚拟端口> --seconds N` | 连过去并每秒打印连接状态 |

`--appid`（默认 480，两边必须一致）、`--api <steam_api64.dll 路径>`（默认当前目录）。
程序自己会设 `SteamAppId/SteamGameId/SteamOverlayGameId`，并在同目录写一份 `steam_appid.txt`；
启动前会检查 `steam.exe` 是否在跑，控制台按 UTF-8 输出（避免乱码）。

## 交付包（发给别人）的预打包清单

**每次打包前逐条过一遍，任何一条不过就别发：**

1. **版本号三处一致**：包名 `-rN`、探针源码 `kProbeRev`、包内说明开头的版本号；改一次就同时加一；
2. **对方怎么用它**：默认对方是**双击**，所以——程序结束必须停在窗口等按键（`pause` 或程序自带等回车）、
   每个动作都要有一个**双击就能跑的 bat**、bat 内容纯 ASCII + CRLF；
3. **中文不能乱码**：程序里 `SetConsoleOutputCP(CP_UTF8)`，源码编译加 `/utf-8`；
4. **运行前提写进说明第一屏**：Steam 必须已启动并登录（装内核那步刚退过 Steam 是最常见的踩坑）；
5. **在包目录里实跑一遍**：双击路径、输出内容、退出行为都自己看过（用 `<nul` 喂输入避免挂住），
   跑完确认没有残留进程占着目录（`p2p_probe.exe` / `cmd.exe` 会锁住文件夹导致改名失败）；
6. **SteamID 等具体值直接写死**在说明与 bat 文件名里，不留 `<占位符>`；
7. **自检顺序：先查版本号/文件清单 → 再在包目录里实跑 → 最后清场（删掉实跑生成的 `steam_appid.txt`、
   确认没残留进程）→ 再打 zip**。顺序反了会把实跑产生的文件当成遗漏，或者把残留文件打进包；
8. **zip 内文件清单**在打包后列出来核对一次（数量、大小、有无漏 `steam_api64.dll`）；
9. **旧包删掉**：一次只留最新的一个包（目录 + zip），避免对方拿错版本。
