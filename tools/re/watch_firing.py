"""Read-only watch of BC2 firing objects (0xac bytes each). Prints every change
of the named fields and, per shot, every other byte that moved. No writes.
usage: python -I tools/re/watch_firing.py PID SECONDS [ADDR...]   (no ADDR: scan by vtable)
2026-10-08 live XM8 run, 79 shots: loaded drops on all three copies in the
same Update that enters state 9 (client1 first, the other two within ~11 ms),
with +0x84 burst 1->0, +0xa0 toggling 0x10000, +0x60/+0x6c/+0x70 floats and
+0x94 seed. Auto interval 80 ms. A magazine reload goes 2->11->12->2."""
import ctypes, ctypes.wintypes as W, struct, sys, time, json
pid, secs = int(sys.argv[1]), float(sys.argv[2]); addrs = [int(a, 0) for a in sys.argv[3:]]
SCAN = not addrs  # no addresses: find live firing objects by vtable 0x1425814 (ctor 0x6f1780)
k = ctypes.WinDLL('kernel32', use_last_error=True)
h = k.OpenProcess(0x0010 | 0x0400, False, pid)  # VM_READ | QUERY_INFORMATION
if not h: sys.exit(f'OpenProcess failed {ctypes.get_last_error()}')
buf = ctypes.create_string_buffer(0xac); n = ctypes.c_size_t()
class MBI(ctypes.Structure):
    _fields_ = [('Base', ctypes.c_void_p), ('AllocBase', ctypes.c_void_p), ('AllocProtect', W.DWORD), ('Pad', W.WORD),
                ('Size', ctypes.c_size_t), ('State', W.DWORD), ('Protect', W.DWORD), ('Type', W.DWORD)]
def scan():
    found = []; a = 0x10000; m = MBI(); vt = struct.pack('<I', 0x1425814)
    while a < 0x7fff0000 and k.VirtualQueryEx(h, ctypes.c_void_p(a), ctypes.byref(m), ctypes.sizeof(m)):
        size = m.Size; base = m.Base or 0
        if m.State == 0x1000 and m.Protect in (0x04, 0x40) and size < 256 << 20:
            b = ctypes.create_string_buffer(size); r = ctypes.c_size_t()
            if k.ReadProcessMemory(h, ctypes.c_void_p(base), b, size, ctypes.byref(r)):
                raw = b.raw[:r.value]; i = raw.find(vt)
                while i >= 0:
                    if i % 4 == 0 and i + 0xac <= len(raw):
                        cur, = struct.unpack_from('<I', raw, i + 0x3c); ld, rs = struct.unpack_from('<ii', raw, i + 0x7c)
                        if cur < 16 and 0 <= ld <= 1000 and -1 <= rs <= 100000: found.append((base + i, cur, ld, rs))
                    i = raw.find(vt, i + 4)
        a = base + size
    return found
def read(a):
    return buf.raw if k.ReadProcessMemory(h, ctypes.c_void_p(a), buf, 0xac, ctypes.byref(n)) and n.value == 0xac else None
F = {'cur': (0x3c, 'I'), 'prev': (0x40, 'I'), 'next': (0x44, 'I'), 'timer': (0x50, 'f'), 'loaded': (0x7c, 'i'),
     'reserve': (0x80, 'i'), 'burst': (0x84, 'i'), 'fireHeld89': (0x89, 'B'), 'overheat8a': (0x8a, 'B'),
     'latchA4': (0xa4, 'B'), 'latchA5': (0xa5, 'B'), 'flagsA8': (0xa8, 'B')}
if SCAN:
    objs = scan(); print(json.dumps({'scan': [[hex(a), c, l, r] for a, c, l, r in objs]}), flush=True)
    addrs = [a for a, *_ in objs][:12]
names = {a: hex(a) for a in addrs} if SCAN else {addrs[i]: n for i, n in enumerate(['client0', 'client1', 'server'][:len(addrs)])}
def fields(b): return {f: struct.unpack_from('<' + t, b, o)[0] for f, (o, t) in F.items()}
last = {a: read(a) for a in addrs}
t0 = time.perf_counter(); out = []
for a, b in last.items():
    print(json.dumps({'t': 0, 'copy': names[a], 'start': fields(b) if b else None}), flush=True)
while time.perf_counter() - t0 < secs:
    for a in addrs:
        b = read(a)
        if b is None or last[a] is None: last[a] = b; continue
        if b != last[a]:
            fo, fn = fields(last[a]), fields(b)
            changed = {f: [fo[f], fn[f]] for f in F if fo[f] != fn[f] and f != 'timer'}
            if changed:
                row = {'t': round(time.perf_counter() - t0, 4), 'copy': names[a], **changed}
                if 'loaded' in changed:  # shot or transfer: list every other moved dword
                    named = {o for o, _ in F.values()}
                    row['other'] = {hex(o): [struct.unpack_from('<I', last[a], o)[0], struct.unpack_from('<I', b, o)[0]]
                                    for o in range(0, 0xac, 4) if o not in named and last[a][o:o+4] != b[o:o+4]}
                print(json.dumps(row), flush=True)
            last[a] = b
    time.sleep(0.0005)
