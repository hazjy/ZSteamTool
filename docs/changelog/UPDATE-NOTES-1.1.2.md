# ZSteamTool v1.1.2 更新说明

> 适用版本：v1.1.2（对比 v1.1.1；只改 Lua 目录解析，无其他功能变更）

## 修复

- **相对路径写了等于没写**。`opensteamtool.toml` 里相对路径（如 `config/lua`）之前是按**进程当前工作目录**解析的，而内核注入进 Steam 后工作目录并不保证是安装目录，于是它去找一个别的地方、多半不存在 → **一个 lua 都读不到**。现在相对路径一律以 **Steam 目录**为基准解析。
- **`[lua] paths` 现在只认一处**。之前是「你写的目录 + 默认 `config\lua`」两处全都扫，同一批 lua 可能被加载两次、定义互相覆盖。现在：
  - 写了 `[lua] paths` → **只扫这里**（不再顺带扫 `config\lua`）；
  - 没写 / 写空 → 用默认 `<Steam>\config\lua`。
- **默认目录不再读 `config\stplug-in`**。v1.1.1 为兼容旧配置会同时读取该目录，现按"单一来源"原则取消；仍留在里面的 lua 请移到 `config\lua`。

## 与 GUI 的配合

OSTGUI 设置页的「Lua 路径」写的就是 `[lua] paths`：填 Steam 目录内的目录会写成相对路径（如 `config/lua`），盘外目录才写绝对路径；保存后内核**热重载，不必重启 Steam**。GUI 未改动该设置时，内核使用默认 `config\lua`。

## 升级方式

替换 Steam 根目录下的 `OpenSteamTool.dll` / `dwmapi.dll` / `xinput1_4.dll`（日常用 `Release\`，排障用 `Debug\`，日志在 `<Steam>\opensteamtool\`），重启 Steam 生效。

已有的 `opensteamtool.toml` **无需改动**，但请确认一处：

- 里面若还写着 `paths = [".../config/stplug-in"]`（或任何空目录），新内核会**只扫那个目录**——把该行删掉或改成 `config/lua` 即可。

> 相关文档：上游基线与本地改动见 `docs/dev/UPSTREAM-SYNC.md`；构建 / Lua API 等技术细节见 `docs/upstream/README-en.md`。
