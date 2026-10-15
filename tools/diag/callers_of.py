"""反查：谁调用了这个函数（扫 .text 的 E8/E9 相对跳转），并把调用者所在函数引用到的字符串列出来。
用法： python callers_of.py <dll> <函数起始RVA(hex)> [...]   （每个地址再交回 func_summary 人工看）
"""
import struct
import sys
import subprocess
import os

path = sys.argv[1]
d = open(path, 'rb').read()


def sections(d):
    e = struct.unpack('<I', d[0x3C:0x40])[0]
    coff = e + 4
    nsec, = struct.unpack('<H', d[coff + 2:coff + 4])
    optsize, = struct.unpack('<H', d[coff + 16:coff + 18])
    sec = coff + 20 + optsize
    out = []
    for i in range(nsec):
        s = sec + i * 40
        name = d[s:s + 8].rstrip(b'\0').decode('latin1')
        vsize, vaddr, rawsize, rawptr = struct.unpack('<IIII', d[s + 8:s + 24])
        out.append((name, vaddr, vsize, rawptr, rawsize))
    return out


secs = sections(d)
text = next(s for s in secs if s[0] == '.text')
_, vaddr, vsize, rawptr, rawsize = text
blob = d[rawptr:rawptr + rawsize]
pdata = next(s for s in secs if s[0] == '.pdata')
_, pv, pvs, prp, prs = pdata
pents = sorted(struct.unpack('<III', d[prp + i * 12:prp + i * 12 + 12]) for i in range(pvs // 12))


def owner(rva):
    for b, e, u in pents:
        if b <= rva < e:
            return b
    return None


for arg in sys.argv[2:]:
    target = int(arg, 16)
    hits = []
    for i in range(len(blob) - 5):
        if blob[i] in (0xE8, 0xE9):
            rel, = struct.unpack('<i', blob[i + 1:i + 5])
            if vaddr + i + 5 + rel == target:
                hits.append((vaddr + i, 'call' if blob[i] == 0xE8 else 'jmp'))
    print(f'\n#### 调用 0x{target:X} 的位置: {len(hits)} 个')
    seen = set()
    for a, k in hits[:12]:
        f = owner(a)
        print(f'  {k} @ 0x{a:X}   所在函数 0x{f:X}' if f else f'  {k} @ 0x{a:X}   (无 pdata)')
        if f and f not in seen:
            seen.add(f)
            print(f'     → 看它认人: python func_summary.py "{path}" {f:X} 0')
