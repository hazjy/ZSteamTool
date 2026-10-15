"""用 dbghelp 把 RVA 翻译成 函数名 + 源码文件:行。用法： python pdb_lookup.py <dll> <rva...>
dll 旁边（或 dll 调试目录里记录的路径上）要有 PDB。
"""
import ctypes
import ctypes.wintypes as wt
import os
import sys

dll = sys.argv[1]
rvAs = [int(a, 16) for a in sys.argv[2:]]

dbghelp = ctypes.WinDLL('dbghelp', use_last_error=True)

MAX_SYM_NAME = 2000


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [
        ('SizeOfStruct', wt.ULONG), ('TypeIndex', wt.ULONG), ('Reserved', ctypes.c_ulonglong * 2),
        ('Index', wt.ULONG), ('Size', wt.ULONG), ('ModBase', ctypes.c_ulonglong),
        ('Flags', wt.ULONG), ('Value', ctypes.c_ulonglong), ('Address', ctypes.c_ulonglong),
        ('Register', wt.ULONG), ('Scope', wt.ULONG), ('Tag', wt.ULONG),
        ('NameLen', wt.ULONG), ('MaxNameLen', wt.ULONG), ('Name', ctypes.c_char * (MAX_SYM_NAME + 1)),
    ]


class IMAGEHLP_LINE64(ctypes.Structure):
    _fields_ = [('SizeOfStruct', wt.DWORD), ('Key', ctypes.c_void_p), ('LineNumber', wt.DWORD),
                ('FileName', ctypes.c_char_p), ('Address', ctypes.c_ulonglong)]


h = ctypes.c_void_p(0x1337)
dbghelp.SymSetOptions(0x2 | 0x4 | 0x400)  # UNDNAME | DEFERRED_LOADS | LOAD_LINES
if not dbghelp.SymInitialize(h, None, False):
    print('SymInitialize 失败', ctypes.get_last_error())
    sys.exit(1)

base = dbghelp.SymLoadModuleEx(h, None, dll.encode(), None, 0, 0, None, 0)
print(f'模块基址 0x{base:X}  ({dll})')
if not base:
    print('SymLoadModuleEx 失败，err=', ctypes.get_last_error())

for rva in rvAs:
    addr = base + rva
    sym = SYMBOL_INFO()
    sym.SizeOfStruct = 88
    sym.MaxNameLen = MAX_SYM_NAME
    disp = ctypes.c_ulonglong(0)
    ok = dbghelp.SymFromAddr(h, ctypes.c_ulonglong(addr), ctypes.byref(disp), ctypes.byref(sym))
    line = IMAGEHLP_LINE64()
    line.SizeOfStruct = ctypes.sizeof(IMAGEHLP_LINE64)
    ldisp = wt.DWORD(0)
    lok = dbghelp.SymGetLineFromAddr64(h, ctypes.c_ulonglong(addr), ctypes.byref(ldisp), ctypes.byref(line))
    print(f'\nRVA 0x{rva:X}  (VA 0x{addr:X})')
    if ok:
        print(f'  函数 : {sym.Name.decode()}  +0x{disp.value:X}')
        print(f'  地址 : 0x{sym.Address:X}  大小 {sym.Size}')
    else:
        print(f'  函数 : 查不到 (err={ctypes.get_last_error()})')
    if lok:
        print(f'  源码 : {line.FileName.decode()} : {line.LineNumber}  (+0x{ldisp.value:X})')
    else:
        print(f'  源码 : 查不到 (err={ctypes.get_last_error()})')
