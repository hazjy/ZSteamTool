# ZSteamTool v1.2.1

> 适用版本：v1.2.1（对比 v1.2.0）

## 修复

- **未安装的已入库游戏整批不出现在库里**：启动 Steam 后只有已安装（下载过）的游戏能看到，其余全部缺席。本版起它们会正常出现在库中；lua 里新加的 app 也立即生效，不用重开。
- **版本号**：DLL 版本资源升到 1.2.1（GUI「信息」页可读）。

## 升级方式

- 完全退出 Steam（托盘图标也要退出）。
- 把 `Release\` 里的 `OpenSteamTool.dll`、`dwmapi.dll`、`xinput1_4.dll` 覆盖到 Steam 根目录（`steam.exe` 所在文件夹），覆盖前先备份原文件。
- 重新启动 Steam。配置、lua 与已入库游戏都不受影响，无需重新入库。

---

[![](https://img.shields.io/badge/powered_by-dsh-4D6BFE?style=flat-square&logo=deepseek&logoColor=white)](https://github.com/deepseek-ai/deepseek-harness)
