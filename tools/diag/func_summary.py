"""给一个 RVA：找出它所在的函数，输出该函数引用到的**字符串**与**导入函数**清单（用来认人），
并在调用点附近打印几行。用法： python func_summary.py <dll> <RVA(hex)> [窗口]
"""
import struct
import sys
import capstone
from capstone import x86

path, target = sys.argv[1], int(sys.argv[2], 16)
win = int(sys.argv[3]) if len(sys.argv) > 3 else 12
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


def off(rva):
    for name, va, vs, rp, rs in secs:
        if va <= rva < va + max(vs, rs):
            return rp + (rva - va)
    return None


def secname(rva):
    for name, va, vs, rp, rs in secs:
        if va <= rva < va + max(vs, rs):
            return name
    return '?'


def cstr(rva, limit=200):
    o = off(rva)
    if o is None:
        return None
    raw = d[o:o + limit]
    e = raw.find(b'\0')
    if e <= 3:
        return None
    raw = raw[:e]
    try:
        s = raw.decode('ascii')
    except UnicodeDecodeError:
        return None
    if sum(1 for c in s if 32 <= ord(c) < 127) < len(s) * 0.85:
        return None
    return s


def imports():
    idata = next((s for s in secs if s[0] == '.idata'), None)
    if not idata:
        return {}
    _, vaddr, vsize, rawptr, rawsize = idata
    out, i = {}, 0
    while True:
        o = off(vaddr + i * 20)
        desc = d[o:o + 20]
        if len(desc) < 20 or desc == b'\0' * 20:
            break
        oft, tds, fwd, nameRva, ft = struct.unpack('<IIIII', desc)
        dll = cstr(nameRva, 64)
        thunk = oft or ft
        k = 0
        while True:
            sl = off(thunk + k * 8)
            if sl is None:
                break
            val, = struct.unpack('<Q', d[sl:sl + 8])
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
pdata = next((s for s in secs if s[0] == '.pdata'), None)
fbegin = fend = None
if pdata:
    _, vaddr, vsize, rawptr, rawsize = pdata
    entries = sorted(struct.unpack('<III', d[rawptr + i * 12:rawptr + i * 12 + 12])
                     for i in range(vsize // 12))
    for b, e, u in entries:
        if b <= target < e:
            fbegin, fend = b, e

print(f'目标 0x{target:X} ({secname(target)})')
if fbegin is None:
    print('未找到 pdata 条目'); sys.exit(0)
print(f'所在函数 0x{fbegin:X}..0x{fend:X}  大小 0x{fend-fbegin:X}')

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
code = d[off(fbegin):off(fbegin) + (fend - fbegin)]
insns = list(md.disasm(code, fbegin))

strings, callees = [], set()
for ins in insns:
    for op in ins.operands:
        if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP:
            t = ins.address + ins.size + op.mem.disp
            if t in IAT:
                callees.add(IAT[t])
            else:
                s = cstr(t)
                if s:
                    strings.append((ins.address, s))
        if op.type == x86.X86_OP_IMM and ins.mnemonic in ('call', 'jmp'):
            callees.add(f'sub_{op.imm:X}')
print(f'指令数 {len(insns)}')
print(f'\n引用到的字符串 ({len(set(s for _, s in strings))} 条):')
seen = set()
for a, s in strings:
    if s in seen:
        continue
    seen.add(s)
    print(f'  0x{a:X}: "{s}"')
print(f'\n调用/跳转目标 ({len(callees)} 个):')
for c in sorted(callees):
    print('  ' + c)

print(f'\n调用点附近（目标 ±{win} 条）:')
idx = next((i for i, ins in enumerate(insns) if ins.address == target), None)
if idx is None:
    print('  该地址不是函数内指令');
else:
    for ins in insns[max(0, idx - win):idx + win]:
        ann = []
        for op in ins.operands:
            if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP:
                t = ins.address + ins.size + op.mem.disp
                if t in IAT:
                    ann.append(f'[IAT] {IAT[t]}')
                else:
                    s = cstr(t)
                    ann.append(f'"{s}"' if s else f'->0x{t:X}')
        print(f'  0x{ins.address:X}: {ins.mnemonic} {ins.op_str}' + (f'   ; {" | ".join(ann)}' if ann else ''))
