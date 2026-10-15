"""把崩溃 RVA 落到具体函数：pdata 定界 -> capstone 反汇编 -> 标注 RIP 相对引用（导入函数 / 我们自己的日志字符串）。
用法： python crash_site.py <dll> <RVA(hex)> [上下文指令数]
"""
import struct
import sys
import capstone
from capstone import x86

path = sys.argv[1]
target = int(sys.argv[2], 16)
ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 60

d = open(path, 'rb').read()


def sections(d):
    e = struct.unpack('<I', d[0x3C:0x40])[0]
    coff = e + 4
    nsec, = struct.unpack('<H', d[coff + 2:coff + 4])
    optsize, = struct.unpack('<H', d[coff + 16:coff + 18])
    opt = coff + 20
    sec = opt + optsize
    out = []
    for i in range(nsec):
        s = sec + i * 40
        name = d[s:s + 8].rstrip(b'\0').decode('latin1')
        vsize, vaddr, rawsize, rawptr = struct.unpack('<IIII', d[s + 8:s + 24])
        out.append((name, vaddr, vsize, rawptr, rawsize))
    return out


secs = sections(d)


def off(rva):
    for name, va, vs, rp, rs in secs:
        if va <= rva < va + max(vs, rs):
            return rp + (rva - va)
    return None


def secname(rva):
    for name, va, vs, rp, rs in secs:
        if va <= rva < va + max(vs, rs):
            return name
    return None


def cstr(rva, limit=160):
    o = off(rva)
    if o is None:
        return None
    raw = d[o:o + limit]
    end = raw.find(b'\0')
    if end <= 0:
        return None
    raw = raw[:end]
    try:
        s = raw.decode('ascii')
    except UnicodeDecodeError:
        return None
    printable = sum(1 for c in s if 32 <= ord(c) < 127)
    if printable < len(s) * 0.85 or len(s) < 4:
        return None
    return s


def imports():
    """IAT 槽地址 -> 'dll!函数'"""
    idata = next((s for s in secs if s[0] == '.idata'), None)
    if not idata:
        return {}
    _, vaddr, vsize, rawptr, rawsize = idata
    out = {}
    i = 0
    while True:
        desc = d[off(vaddr + i * 20):off(vaddr + i * 20) + 20]
        if len(desc) < 20 or desc == b'\0' * 20:
            break
        oft, tds, fwd, nameRva, ft = struct.unpack('<IIIII', desc)
        dll = cstr(nameRva, 64)
        thunk_rva = oft or ft
        k = 0
        while True:
            slot = off(thunk_rva + k * 8)
            if slot is None:
                break
            val, = struct.unpack('<Q', d[slot:slot + 8])
            if val == 0:
                break
            if not (val & (1 << 63)):
                nm = cstr(val + 2, 64)
                if nm:
                    out[ft + k * 8] = f'{dll}!{nm}' if dll else nm
            k += 1
        i += 1
    return out


IAT = imports()

# --- .pdata 定界 ---
pdata = next((s for s in secs if s[0] == '.pdata'), None)
func_begin = func_end = None
if pdata:
    _, vaddr, vsize, rawptr, rawsize = pdata
    n = vsize // 12
    lo, hi = 0, n - 1
    entries = [struct.unpack('<III', d[rawptr + i * 12:rawptr + i * 12 + 12]) for i in range(n)]
    entries.sort()
    best = None
    for b, e, u in entries:
        if b <= target < e:
            best = (b, e, u)
    if best:
        func_begin, func_end, unwind = best
        print(f'pdata: 函数区间 0x{func_begin:X}..0x{func_end:X}  (大小 0x{func_end-func_begin:X}, unwind 0x{unwind:X})')
    else:
        print('pdata: 未找到包含该地址的函数条目')

start = func_begin if func_begin is not None else max(0x1000, target - 0x400)

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
code = d[off(start):off(start) + (target - start) + 0x300]
insns = list(md.disasm(code, start))

print(f'\n崩溃点 0x{target:X} 处 {secname(target)} 段\n')
for ins in insns:
    mark = ''
    if ins.address == target:
        mark = '   <<<<<< 崩溃在这里'
    elif abs(ins.address - target) > ctx * 4 and ins.address != start:
        continue
    ann = []
    for op in ins.operands:
        if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP:
            tgt = ins.address + ins.size + op.mem.disp
            if tgt in IAT:
                ann.append(f'[IAT] {IAT[tgt]}')
            else:
                s = cstr(tgt)
                if s:
                    ann.append(f'"{s}"')
                else:
                    ann.append(f'-> 0x{tgt:X} ({secname(tgt)})')
    print(f'  0x{ins.address:X}: {ins.mnemonic} {ins.op_str}' + (f'   ; {" | ".join(ann)}' if ann else '') + mark)
    if ins.address > target + ctx * 4:
        break

# --- 谁调用这个函数 ---
if func_begin is not None:
    text = next((s for s in secs if s[0] == '.text'), None)
    _, vaddr, vsize, rawptr, rawsize = text
    blob = d[rawptr:rawptr + rawsize]
    callers = []
    for i in range(len(blob) - 5):
        if blob[i] in (0xE8, 0xE9):
            rel, = struct.unpack('<i', blob[i + 1:i + 5])
            tgt = vaddr + i + 5 + rel
            if tgt == func_begin:
                callers.append((vaddr + i, 'call' if blob[i] == 0xE8 else 'jmp'))
    print(f'\n指向函数入口 0x{func_begin:X} 的调用点: {len(callers)}')
    for a, k in callers[:12]:
        print(f'  0x{a:X}  {k}')
