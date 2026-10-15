# Steam 客户端排查脚本

> 用途：Steam 客户端大更新后，内核失配或崩溃时的**定位链**。全部只读，不修改任何文件。
> 依赖：Python 3 + `capstone`（`pip install capstone`）；`pdb_lookup.py` 还需要部署件带 PDB（Debug 构建）。
> 不参与构建（`tools/CMakeLists.txt` 未引用本目录），也不随发布包分发。

| 脚本 | 作用 |
|---|---|
| `verify_patterns.py` | 核对签名表：表里每条 `sig` 在它标的 `rva` 处还对不对；不对就全盘扫出真实位置 |
| `minidump_scan.py` | 解析 minidump：异常码 / 异常地址 → 模块 + RVA → 用签名表翻成函数名 |
| `pdb_lookup.py` | 用 dbghelp 把 RVA 翻成「函数名 + 源码文件:行」 |
| `crash_site.py` | 用 `.pdata` 给函数定界 + 反汇编 + 标注 RIP 相对引用（导入函数 / 本项目日志字符串） |
| `func_summary.py` | 列出某函数引用的字符串与导入函数（无符号时"认人"用） |
| `callers_of.py` | 反查谁调用了某函数 |

**典型顺序**：`verify_patterns.py` 判"签名面坏没坏" → 坏则取表 / 产表；没坏则读 `<Steam>\dumps`，用 `minidump_scan.py` 取异常 RVA → `pdb_lookup.py` 翻到源码行 → 需要看上下文再用 `crash_site.py` / `func_summary.py` / `callers_of.py`。用法与参数见各脚本头部 docstring。
