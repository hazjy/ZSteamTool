"""核对内核缓存的签名表：表里的 rva 处是否真能匹配表里的 sig。
用法： python verify_patterns.py <目标dll> <toml> [...]
"""
import struct
import sys
import re


def sections(path):
    d = open(path, 'rb').read()
    e_lfanew = struct.unpack('<I', d[0x3C:0x40])[0]
    assert d[e_lfanew:e_lfanew + 4] == b'PE\0\0', 'not PE'
    coff = e_lfanew + 4
    nsec, = struct.unpack('<H', d[coff + 2:coff + 4])
    optsize, = struct.unpack('<H', d[coff + 16:coff + 18])
    opt = coff + 20
    magic, = struct.unpack('<H', d[opt:opt + 2])
    imgsize_off = opt + (0x38 if magic == 0x20B else 0x38)
    image_size, = struct.unpack('<I', d[imgsize_off:imgsize_off + 4])
    sec = opt + optsize
    out = []
    for i in range(nsec):
        s = sec + i * 40
        name = d[s:s + 8].rstrip(b'\0').decode('latin1')
        vsize, vaddr, rawsize, rawptr = struct.unpack('<IIII', d[s + 8:s + 24])
        out.append((name, vaddr, vsize, rawptr, rawsize))
    return d, out, image_size


def rva_to_off(secs, rva):
    for name, vaddr, vsize, rawptr, rawsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rawsize):
            return rawptr + (rva - vaddr), name
    return None, None


def parse_toml(path):
    ents, cur = [], None
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.strip()
        m = re.match(r'name\s*=\s*"([^"]+)"', line)
        if m:
            cur = {'name': m.group(1)}
            ents.append(cur)
            continue
        m = re.match(r'(rva|wrapper_rva|fencepost)\s*=\s*"([^"]+)"', line)
        if m and cur is not None:
            cur[m.group(1)] = m.group(2)
        m = re.match(r'sig\s*=\s*"([^"]+)"', line)
        if m and cur is not None:
            cur['sig'] = m.group(1)
    return [e for e in ents if 'name' in e]


def match(d, off, sig):
    """sig 形如 '48 8B C4 ?? 55'，返回是否逐字节匹配"""
    if off is None or not sig:
        return None
    pat = sig.split()
    buf = d[off:off + len(pat)]
    if len(buf) < len(pat):
        return False
    for i, p in enumerate(pat):
        if p in ('??', '?', '**'):
            continue
        if buf[i] != int(p, 16):
            return False
    return True


def scan_for(d, secs, sig):
    """按 sig 在可执行段里扫描，返回全部命中 RVA"""
    pat = sig.split()
    first = next((i for i, p in enumerate(pat) if p not in ('??', '?', '**')), 0)
    try:
        needle = bytes(int(p, 16) for p in pat[:first + 1])
    except ValueError:
        return []
    hits = []
    for name, vaddr, vsize, rawptr, rawsize in secs:
        if not name.startswith('.text'):
            continue
        blob = d[rawptr:rawptr + rawsize]
        start = 0
        while True:
            i = blob.find(needle, start)
            if i < 0:
                break
            off = rawptr + i
            if match(d, off - first, sig):
                hits.append(vaddr + (off - rawptr) - first)
            start = i + 1
    return hits


for i in range(1, len(sys.argv), 2):
    dll, toml = sys.argv[i], sys.argv[i + 1]
    d, secs, imgsize = sections(dll)
    print(f'\n=== {dll}  (映像 0x{imgsize:X}) ===')
    print(f'    段: ' + ', '.join(f'{n}@0x{v:X}' for n, v, vs, rp, rs in secs))
    ents = parse_toml(toml)
    print(f'    表里 {len(ents)} 条')
    ok = bad = nosig = 0
    for e in ents:
        rva = e.get('rva') or e.get('wrapper_rva')
        if not rva:
            print(f'    {e["name"]:52s} 无 rva 字段')
            continue
        r = int(rva, 16)
        off, secname = rva_to_off(secs, r)
        m = match(d, off, e.get('sig'))
        if m is None:
            nosig += 1
            print(f'    {e["name"]:52s} rva=0x{r:X} 段={secname} 无 sig 可比对')
        elif m:
            ok += 1
            print(f'    ✓ {e["name"]:50s} rva=0x{r:X} 段={secname} sig 命中')
        else:
            bad += 1
            got = ' '.join(f'{b:02X}' for b in d[off:off + 10]) if off else '?'
            hits = scan_for(d, secs, e['sig'])
            print(f'    ✗ {e["name"]:50s} rva=0x{r:X} 段={secname}')
            print(f'        表里的 sig : {e["sig"]}')
            print(f'        该处的字节 : {got}')
            print(f'        sig 全盘扫描命中: ' + (', '.join(f'0x{h:X}' for h in hits[:4]) if hits else '无'))
    print(f'    → 命中 {ok} / 不符 {bad} / 无 sig {nosig}')
