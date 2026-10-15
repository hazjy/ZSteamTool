"""最小 minidump 解析器：抓异常点 -> 模块+RVA -> 用内核缓存签名表翻译成函数名。
只依赖标准库。用法： python minidump_scan.py <dump> [更多 dump...]
"""
import struct
import sys
import glob
import os

SIG = 0x504D444D  # 'MDMP'


def rd(f, rva, size):
    f.seek(rva)
    return f.read(size)


def parse_string(f, rva):
    n = struct.unpack('<I', rd(f, rva, 4))[0]
    return rd(f, rva + 4, n).decode('utf-16-le', 'replace')


def load_tables(root):
    """把内核缓存的签名表读成 {模块: [(rva, name), ...]}"""
    tables = {'steamclient64.dll': [], 'steamui.dll': []}
    for sub, mod in (('pattern/steamclient', 'steamclient64.dll'),
                     ('ipc/steamclient', 'steamclient64.dll'),
                     ('pattern/steamui', 'steamui.dll')):
        for p in glob.glob(os.path.join(root, sub, '*.toml')):
            name = None
            for line in open(p, encoding='utf-8', errors='replace'):
                line = line.strip()
                if line.startswith('name = '):
                    name = line.split('"')[1]
                elif line.startswith('rva = ') and name:
                    try:
                        tables[mod].append((int(line.split('"')[1], 16), name))
                    except ValueError:
                        pass
    for mod in tables:
        tables[mod].sort()
    return {m: v for m, v in tables.items() if v}


def annotate(addr, modules, tables, base_key='steamclient64.dll'):
    for b, size, name in modules:
        if b <= addr < b + size:
            mod = os.path.basename(name).lower()
            rva = addr - b
            hit = ''
            for key, rows in tables.items():
                if key.lower() in mod or (key == base_key and 'steamclient64' in mod):
                    for r, n in rows:
                        if r <= rva < r + 0x400:
                            hit = n
                            break
            return f'{mod}+0x{rva:X}' + (f'  <{hit}>' if hit else '')
    return f'0x{addr:X} (未映射)'


def scan(path, tables):
    with open(path, 'rb') as f:
        data = rd(f, 0, 32)
        sig, ver, nstreams, dirrva = struct.unpack('<IIII', data[:16])
        if sig != SIG:
            return f'{os.path.basename(path)}: 不是 minidump'
        streams = {}
        for i in range(nstreams):
            t, sz, rva = struct.unpack('<III', rd(f, dirrva + i * 12, 12))
            streams[t] = (sz, rva)

        out = [f'== {os.path.basename(path)} ==']

        # 模块表：MINIDUMP_MODULE 的对齐在不同头文件版本里不一致（112 或 108 字节步长），
        # 两种都试，挑能把所有模块名解析成合法文件名的那个
        modules = []
        if 4 in streams:
            _, rva = streams[4]
            n = struct.unpack('<I', rd(f, rva, 4))[0]
            for stride, nameoff, namefmt in ((112, 24, '<Q'), (108, 20, '<I')):
                cand = []
                try:
                    for i in range(n):
                        m = rd(f, rva + 4 + i * stride, stride)
                        base, size = struct.unpack('<QI', m[:12])
                        namerva = struct.unpack(namefmt, m[nameoff:nameoff + struct.calcsize(namefmt)])[0]
                        nm = parse_string(f, namerva)
                        if not nm or '.' not in nm:
                            raise ValueError('bad name')
                        cand.append((base, size, nm))
                except Exception:
                    continue
                if len(cand) == n:
                    modules = cand
                    out.append(f'模块步长判定: {stride} 字节')
                    break
        out.append(f'模块数: {len(modules)}')

        # 异常
        if 6 in streams:
            _, rva = streams[6]
            tid, align = struct.unpack('<II', rd(f, rva, 8))
            ex = rd(f, rva + 8, 8 + 8 + 8 + 4 + 4 + 8 * 15)
            code, flags, rec, addr, nparam = struct.unpack('<IIQQI', ex[:28])
            out.append(f'异常线程 tid={tid}  异常码=0x{code:08X}  异常地址=0x{addr:X}')
            out.append(f'  异常地址归属: {annotate(addr, modules, tables)}')
            params = struct.unpack('<' + 'Q' * nparam, ex[32:32 + 8 * nparam]) if nparam <= 15 else ()
            if nparam:
                out.append('  参数: ' + ', '.join(f'0x{p:X}' for p in params))
            # 访问违例的 read/write 语义
            if code == 0xC0000005 and nparam >= 2:
                kind = {0: '读取', 1: '写入', 8: '执行(DEP)'}.get(params[0], str(params[0]))
                out.append(f'  → 访问违例：{kind} 0x{params[1]:X}')

            # 线程上下文里的 RSP/RIP
            ctx_rva = struct.unpack('<II', rd(f, rva + 8 + 8 + 8 + 8 + 4 + 4 + 8 * 15, 0) or b'') if False else None
        else:
            out.append('无异常流（可能是 assert/手工 dump）')

        # 线程表：找异常线程的栈
        threads = []
        if 3 in streams:
            _, rva = streams[3]
            n = struct.unpack('<I', rd(f, rva, 4))[0]
            for i in range(n):
                t = rd(f, rva + 4 + i * 48, 48)
                tid, sus, pc, pri = struct.unpack('<IIII', t[:16])
                start, msize, mrva = struct.unpack('<QII', t[24:40])   # Stack 描述符紧跟在 Teb(8) 之后
                ctxsize, ctxrva = struct.unpack('<II', t[40:48])
                threads.append((tid, start, msize, mrva, ctxsize, ctxrva))
        fault_tid = struct.unpack('<I', rd(f, streams[6][1], 4))[0] if 6 in streams else None
        for (tid, start, msize, mrva, ctxsize, ctxrva) in threads:
            if tid != fault_tid:
                continue
            ctx = rd(f, ctxrva, min(ctxsize, 0x200))
            rsp, rbp, rip = (struct.unpack('<Q', ctx[o:o + 8])[0] for o in (0x98, 0xA0, 0xF8))
            out.append(f'崩溃线程上下文: RSP=0x{rsp:X} RBP=0x{rbp:X} RIP=0x{rip:X}')
            out.append(f'  (RIP 归属: {annotate(rip, modules, tables)})')
            # 栈回溯：在栈内存里扫落进模块的返回地址
            if mrva and msize:
                span = min(msize, 0x800)
                st = rd(f, mrva, span)
                off = rsp - start
                if 0 <= off < span:
                    out.append(f'  栈扫描（RSP 起 { (span - off)//8 } 个槽，取落进模块内的）：')
                    n = 0
                    for i in range(off, span - 8, 8):
                        v = struct.unpack('<Q', st[i:i + 8])[0]
                        a = annotate(v, modules, tables)
                        if '未映射' not in a and v > 0x10000:
                            out.append(f'    rsp+0x{i - off:03X} 0x{v:X}  {a}')
                            n += 1
                            if n >= 24:
                                break
        # 模块里有没有内核
        for b, size, name in modules:
            if 'OpenSteamTool' in name or 'dwmapi' in name or 'xinput' in name:
                out.append(f'已加载: {name} base=0x{b:X} size=0x{size:X}')
        return '\n'.join(out)


def main():
    root = r'd:\steam\opensteamtool'
    tables = load_tables(root)
    print('签名表载入：', {k: len(v) for k, v in tables.items()})
    for p in sys.argv[1:]:
        print()
        print(scan(p, tables))


main()
