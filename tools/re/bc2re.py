"""Small static-analysis helpers for BFBC2Game.exe (x86, base 0x400000).

Static reads only; nothing here touches a running game. The exe is not part of
this repo: set BC2_EXE to a local copy (build 795745).

Usage: BC2_EXE=... python3 -I -c "import sys; sys.path.insert(0, 'tools/re'); import bc2re as r; r.pdis(0x6bcf80)"
"""
import os
import re
import struct

import capstone
import pefile

EXE = os.environ["BC2_EXE"]
_pe = pefile.PE(EXE, fast_load=True)
BASE = _pe.OPTIONAL_HEADER.ImageBase
IMAGE = _pe.get_memory_mapped_image()  # bytes indexed by RVA
SECTIONS = {s.Name.rstrip(b"\0").decode(): (BASE + s.VirtualAddress, BASE + s.VirtualAddress + s.Misc_VirtualSize) for s in _pe.sections}
TEXT = SECTIONS[".text"]
CODE_RANGES = [SECTIONS[".text"], SECTIONS["ctr"]]
_md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
_md.detail = False


def rd(va, n):
    o = va - BASE
    return IMAGE[o:o + n]


def u32(va):
    return struct.unpack("<I", rd(va, 4))[0]


def cstr(va, limit=256):
    b = rd(va, limit)
    return b.split(b"\0", 1)[0].decode("latin-1")


def dis(va, n=0x80, stop_at_ret=True, max_insns=400):
    """Disassemble from va; returns list of (addr, mnemonic, op_str, bytes)."""
    out = []
    for i in _md.disasm(rd(va, n), va):
        out.append((i.address, i.mnemonic, i.op_str, i.bytes.hex()))
        if len(out) >= max_insns:
            break
        if stop_at_ret and i.mnemonic in ("ret", "retn"):
            break
    return out


def pdis(va, n=0x80, **kw):
    for a, m, o, b in dis(va, n, **kw):
        print(f"{a:08x}  {b:<24} {m} {o}")


def _code_bytes():
    for lo, hi in CODE_RANGES:
        yield lo, rd(lo, hi - lo)


def find_bytes(pattern, ranges=None):
    """pattern: hex string with ?? wildcards. Returns list of VAs."""
    toks = pattern.split()
    rx = re.compile(b"".join(b"." if t == "??" else re.escape(bytes([int(t, 16)])) for t in toks), re.S)
    hits = []
    for lo, data in (_code_bytes() if ranges is None else ((lo, rd(lo, hi - lo)) for lo, hi in ranges)):
        hits += [lo + m.start() for m in rx.finditer(data)]
    return hits


def callers(target):
    """Direct E8 call / E9 jmp rel32 sites targeting `target`."""
    hits = []
    for lo, data in _code_bytes():
        for m in re.finditer(b"[\xe8\xe9]", data):
            p = m.start()
            if p + 5 > len(data):
                continue
            rel = struct.unpack_from("<i", data, p + 1)[0]
            if (lo + p + 5 + rel) & 0xFFFFFFFF == target:
                hits.append(lo + p)
    return hits


def imm_refs(value):
    """Sites where the 32-bit little-endian value appears in code (push/mov imm, disp32)."""
    pat = struct.pack("<I", value)
    hits = []
    for lo, data in _code_bytes():
        hits += [lo + m.start() for m in re.finditer(re.escape(pat), data)]
    return hits


def data_refs(value):
    """Occurrences of the 32-bit value in .rdata/.data (vtables, tables)."""
    pat = struct.pack("<I", value)
    hits = []
    for name in (".rdata", ".data"):
        lo, hi = SECTIONS[name]
        data = rd(lo, hi - lo)
        hits += [lo + m.start() for m in re.finditer(re.escape(pat), data)]
    return hits


def find_string(s):
    pat = s.encode() + b"\0"
    lo, hi = SECTIONS[".rdata"]
    data = rd(lo, hi - lo)
    out = [lo + m.start() for m in re.finditer(re.escape(pat), data)]
    lo, hi = SECTIONS[".data"]
    data = rd(lo, hi - lo)
    out += [lo + m.start() for m in re.finditer(re.escape(pat), data)]
    return out


def func_start(va, limit=0x4000):
    """Heuristic: walk back to the nearest preceding padding (CC/90 run) boundary."""
    p = va
    while p > va - limit:
        if rd(p - 1, 1) in (b"\xcc", b"\x90") and rd(p, 1) not in (b"\xcc", b"\x90"):
            return p
        p -= 1
    return None


def disp_writes(disp, regs=None):
    """Find instructions in code that WRITE to [reg+disp] (mov/lea-free scan via capstone on candidate sites)."""
    pat = struct.pack("<I", disp)
    hits = []
    for lo, data in _code_bytes():
        for m in re.finditer(re.escape(pat), data):
            p = lo + m.start()
            # try decoding an instruction starting 1..6 bytes before the disp
            for back in range(2, 7):
                start = p - back
                ins = list(_md.disasm(rd(start, 16), start, count=1))
                if not ins:
                    continue
                i = ins[0]
                if i.address + i.size > p + 4 - 1 and f"0x{disp:x}" in i.op_str:
                    first = i.op_str.split(",")[0]
                    if "[" in first and f"0x{disp:x}" in first and i.mnemonic in ("mov", "add", "sub", "and", "or", "xor", "inc", "dec", "lea", "movss", "movd", "stosd"):
                        if i.mnemonic != "lea":
                            hits.append((i.address, i.mnemonic, i.op_str))
                    break
    return sorted(set(hits))


def vtable_of_typeinfo_getter(ti_va):
    """Find `B8 <ti> C3` getter functions and the vtables that reference them."""
    getters = find_bytes("B8 " + " ".join(f"{b:02X}" for b in struct.pack("<I", ti_va)) + " C3")
    return [(g, data_refs(g)) for g in getters]
