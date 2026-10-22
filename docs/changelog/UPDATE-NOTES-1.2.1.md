# ZSteamTool v1.2.1

> 适用版本：v1.2.1（对比 v1.2.0）
> 建议搭配 GUI **OSTGUI v1.8.1 及以上**使用。

## 修复与改进

- **未安装的游戏整批不进库**（本版唯一代码改动）：注入许可只让客户端"认为拥有"，库的可见性实际由 steamui 侧 CSteamApp 的 OwnershipFlags 决定，Steam 不会替未安装的 app 主动重建库条目 —— 表现为启动后只有已安装（有 `appmanifest`）的游戏出现，未安装的全部缺席。
  现在注入完成后会主动把 app 推进库（`QueueAdditions` → 主线程每帧最多 64 个 → 补 `OwnsLicense | LicensePermanent` → 通知 steamui 变更），`FillInAppOverview` 内同步补写；lua 热重载新增的 app 一并推送。
  查不到的 id 一律跳过，绝不凭空创建 app 条目（那会造出没有 appinfo 的空壳，正是 1.1.x 修掉的崩溃来源）。
- **版本号**：DLL 版本资源升到 1.2.1（GUI「信息」页可读）。

## 升级方式

- 完全退出 Steam（托盘图标也要退出）。
- 把 `Release\` 里的 `OpenSteamTool.dll`、`dwmapi.dll`、`xinput1_4.dll` 覆盖到 Steam 根目录（`steam.exe` 所在文件夹），覆盖前先备份原文件。
- 重新启动 Steam。配置、lua 与已入库游戏都不受影响，无需重新入库。

---

[![](https://img.shields.io/badge/powered_by-dsh-4D6BFE?style=flat-square&logo=deepseek&logoColor=white)](https://github.com/deepseek-ai/deepseek-harness)
