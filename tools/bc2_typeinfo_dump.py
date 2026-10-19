#!/usr/bin/env python3
"""Static dump of Frostbite 1.5 reflection TypeInfo from BFBC2Game.exe (32-bit PE).

Reads the file only. It never loads, executes or patches the game. Python 3.10+,
standard library only.

    python bc2_typeinfo_dump.py "C:/.../BFBC2Game.exe" -o out/

Writes into the output directory:
    bc2_types.json           classes/value types, enums, primitives, exe identity, summary
    bc2_types.h              C++ structs with explicit padding and static_asserts
    ghidra_import_types.py   Ghidra script (Jython 2.7 or PyGhidra) creating types and labels
    bc2_types.rcnet          ReClass.NET project (zip with Data.xml), best effort
    bc2_types.csv            flat class/offset/field table (always reliable)

The layout assumptions, with the FrostbiteVR file:line each one rests on, are in
README.md next to this script. The constants below are the only place where they
are encoded.
"""
from __future__ import annotations

import argparse
import base64
import csv
import datetime as _dt
import hashlib
import io
import json
import re
import struct
import sys
import uuid
import zipfile
from dataclasses import dataclass, field
from xml.sax.saxutils import quoteattr

TOOL_VERSION = "1.0.0"

# ---------------------------------------------------------------------------
# Layout constants (see README "Exact layouts"; each one is cited there).
# ---------------------------------------------------------------------------
FLAGS_CLASS = 0x35       # DataContainer-derived class, fields via TypeInfo+0x24
FLAGS_VALUE = 0x29       # value type (struct), fields via meta+0x18
FLAGS_ENUM = 0x179       # enum, values via meta+0x18, value in record+0x10
KIND_BY_FLAGS = {FLAGS_CLASS: "class", FLAGS_VALUE: "valuetype", FLAGS_ENUM: "enum"}

TI_META = 0x04           # TypeInfo -> meta pointer
TI_ARG3 = 0x10           # class ctor arg3 (meaning unknown)
TI_PARENT = 0x14         # class parent TypeInfo (root: parent == self)
TI_FIELDS = 0x24         # class field record array

META_NAME = 0x00
META_FLAGS = 0x04        # u16
META_SIZE = 0x06         # u16
META_COUNT = 0x0D        # u8
META_FIELDS = 0x18       # value types / enums only
META_MIN_SIZE = 28

FIELD_RECORD_SIZE = 24   # name, flags, type TypeInfo, element TypeInfo, offset/value, unknown

# Class TypeInfo constructor body seen at registration call targets
# (Bc2OpticDiscovery.cpp:37-38). Used only to tag sites as "ctorVerified".
CLASS_CTOR_HEAD = "8B 44 24 10 8B 54 24 08 56 50 8B 44 24 0C 8B F1 8B 4C 24 14 51 52 50 8B CE E8"
CLASS_CTOR_TAIL_AT = 0x27
CLASS_CTOR_TAIL = "C2 10 00"

MAX_NAME = 192           # repo readers use 192-byte name buffers (Bc2SelectedMeshes1p.cpp:56)
MAX_PUSHES = 8

SCN_EXEC = 0x20000000
SCN_WRITE = 0x80000000

# Build 795745 has 9 sections: .text ctr CONST .rdata .data typeinfo fieldinf .tls .rsrc
# (reported from the real exe). `typeinfo` is accepted as TypeInfo storage even if its
# header lacks the write bit; `ctr` is scanned for registration code even if it lacks
# the execute bit. Every other non-exec section (.rdata, CONST, fieldinf, .data) is
# accepted for metas, names and field records.
TYPEINFO_SECTIONS = ("typeinfo",)
EXTRA_CODE_SECTIONS = ("ctr",)

PRIMITIVE_CTYPES = {  # name -> (C++ type, expected size)
    "Boolean": ("bool", 1), "Int8": ("int8_t", 1), "Uint8": ("uint8_t", 1),
    "Int16": ("int16_t", 2), "Uint16": ("uint16_t", 2), "Int32": ("int32_t", 4),
    "Uint32": ("uint32_t", 4), "Int64": ("int64_t", 8), "Uint64": ("uint64_t", 8),
    "Float32": ("float", 4), "Float64": ("double", 8),
    "String": ("bc2_ptr32", 4), "CString": ("bc2_ptr32", 4),
}
RECLASS_PRIM = {"Boolean": "BoolNode", "Int8": "Int8Node", "Uint8": "UInt8Node", "Int16": "Int16Node",
                "Uint16": "UInt16Node", "Int32": "Int32Node", "Uint32": "UInt32Node", "Int64": "Int64Node",
                "Uint64": "UInt64Node", "Float32": "FloatNode", "Float64": "DoubleNode"}

CPP_KEYWORDS = set("""alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t
char32_t class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield
decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if
inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected public
register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch
template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile
wchar_t while xor xor_eq offsetof NULL""".split())


def hx(v: int | None) -> str | None:
    return None if v is None else f"0x{v:08x}"


def pattern_bytes(text: str) -> bytes:
    return bytes(int(x, 16) for x in text.split())


# ---------------------------------------------------------------------------
# Minimal PE32 parser
# ---------------------------------------------------------------------------
class PeError(ValueError):
    pass


@dataclass
class Section:
    name: str
    rva: int
    vsize: int
    raw_off: int
    raw_size: int
    flags: int
    backed: int = 0  # bytes of the section actually present in the file

    @property
    def executable(self) -> bool:
        return bool(self.flags & SCN_EXEC)

    @property
    def writable(self) -> bool:
        return bool(self.flags & SCN_WRITE)

    @property
    def vend(self) -> int:
        return self.rva + max(self.vsize, self.raw_size)


class PeImage:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < 0x40 or data[:2] != b"MZ":
            raise PeError("missing MZ header")
        nt = struct.unpack_from("<I", data, 0x3C)[0]
        if nt + 24 > len(data) or data[nt:nt + 4] != b"PE\0\0":
            raise PeError("missing PE signature")
        (self.machine, nsec, self.timestamp, _, _, optsz, self.characteristics) = struct.unpack_from("<HHIIIHH", data, nt + 4)
        opt = nt + 24
        if opt + 60 > len(data):
            raise PeError("truncated optional header")
        if struct.unpack_from("<H", data, opt)[0] != 0x10B:
            raise PeError("not a PE32 (32-bit) image")
        self.entry_rva = struct.unpack_from("<I", data, opt + 16)[0]
        self.image_base = struct.unpack_from("<I", data, opt + 28)[0]
        self.size_of_image = struct.unpack_from("<I", data, opt + 56)[0]
        self.sections: list[Section] = []
        at = opt + optsz
        if nsec > 96 or at + nsec * 40 > len(data):
            raise PeError("bad section table")
        for i in range(nsec):
            name, vsize, va, rsize, rptr = struct.unpack_from("<8sIIII", data, at + i * 40)
            flags = struct.unpack_from("<I", data, at + i * 40 + 36)[0]
            s = Section(name.rstrip(b"\0").decode("latin-1"), va, vsize, rptr, rsize, flags)
            backed = rsize if not vsize else min(rsize, vsize)
            s.backed = max(0, min(backed, len(data) - rptr)) if rptr < len(data) else 0
            self.sections.append(s)

    def section_for_va(self, va: int) -> Section | None:
        rva = va - self.image_base
        for s in self.sections:
            if s.rva <= rva < s.vend:
                return s
        return None

    def va_to_off(self, va: int, n: int = 1) -> int | None:
        """File offset for [va, va+n) if fully file-backed inside one section."""
        rva = va - self.image_base
        if rva < 0 or n < 0:
            return None
        for s in self.sections:
            if s.rva <= rva and rva + n <= s.rva + s.backed:
                return s.raw_off + rva - s.rva
        return None

    def read(self, va: int, n: int) -> bytes | None:
        off = self.va_to_off(va, n)
        return None if off is None else self.data[off:off + n]

    def u32(self, va: int) -> int | None:
        b = self.read(va, 4)
        return None if b is None else struct.unpack("<I", b)[0]


# ---------------------------------------------------------------------------
# Records
# ---------------------------------------------------------------------------
@dataclass
class Meta:
    va: int
    name: str
    flags: int
    size: int
    count: int
    fields_ptr: int  # meta+0x18 (meaningful for value types / enums)
    raw: bytes

    @property
    def kind(self) -> str:
        return KIND_BY_FLAGS.get(self.flags, "other")


@dataclass
class FieldRec:
    name: str
    flags: int
    type_info: int
    element_info: int
    offset: int  # value for enums (signed)
    extra: int


@dataclass
class TypeRec:
    info: int
    meta: Meta
    source: str              # ctor | store | static
    site: int | None = None
    ctor: int | None = None
    ctor_verified: bool = False
    parent: int | None = None  # class only
    arg3: int | None = None
    fields_ptr: int | None = None
    fields: list[FieldRec] = field(default_factory=list)
    fields_error: str | None = None
    duplicate_sites: list[int] = field(default_factory=list)

    @property
    def name(self) -> str:
        return self.meta.name

    @property
    def kind(self) -> str:
        return self.meta.kind


# ---------------------------------------------------------------------------
# Scanner
# ---------------------------------------------------------------------------
CTOR_SITE_RE = re.compile(rb"(?=\xB9(.{4})\xE8(.{4}))", re.DOTALL)
STORE_RE = re.compile(rb"(?=\xC7\x05(.{4})(.{4}))", re.DOTALL)


class Dumper:
    def __init__(self, data: bytes, *, store_scan: bool = True, static_scan: bool = False):
        self.data = data
        self.pe = PeImage(data)
        self.base = self.pe.image_base
        self.store_scan = store_scan
        self.static_scan = static_scan
        self.types: dict[int, TypeRec] = {}
        self.rejected: list[dict] = []
        self.warnings: list[str] = []
        self.ctor_histogram: dict[int, dict] = {}
        self.stats = {"ctorCallSites": 0, "ctorCallSitesWithMetaPush": 0}
        self._meta_cache: dict[int, tuple[Meta | None, str]] = {}
        self._ctor_ok_cache: dict[int, bool] = {}

    # -- address predicates -------------------------------------------------
    def in_data(self, va: int, n: int = 1) -> bool:
        """File-backed, non-executable (.rdata/.data)."""
        s = self.pe.section_for_va(va)
        return bool(s and not s.executable and self.pe.va_to_off(va, n) is not None
                    and self.pe.section_for_va(va + n - 1) is s)

    def in_writable(self, va: int, n: int = 1) -> bool:
        """Writable non-exec section, virtual range (TypeInfo storage may be BSS)."""
        s = self.pe.section_for_va(va)
        return bool(s and not s.executable and (s.writable or s.name.lower() in TYPEINFO_SECTIONS)
                    and va + n - self.base <= s.vend)

    def read_name(self, va: int) -> tuple[str | None, str]:
        if not self.in_data(va, 1):
            return None, f"name pointer {hx(va)} not in a file-backed data section"
        off = self.pe.va_to_off(va, 1)
        s = self.pe.section_for_va(va)
        limit = min(MAX_NAME, s.raw_off + s.backed - off)
        chunk = self.data[off:off + limit]
        end = chunk.find(b"\0")
        if end <= 0:
            return None, f"name at {hx(va)} empty or unterminated within {MAX_NAME} bytes"
        raw = chunk[:end]
        if any(c < 0x20 or c > 0x7E for c in raw) or raw[:1] == b" ":
            return None, f"name at {hx(va)} not printable ASCII"
        return raw.decode("ascii"), ""

    # -- meta / fields ------------------------------------------------------
    def looks_like_meta(self, va: int) -> bool:
        if not self.in_data(va, META_MIN_SIZE):
            return False
        p = self.pe.u32(va)
        return p is not None and self.read_name(p)[0] is not None

    def parse_meta(self, va: int) -> tuple[Meta | None, str]:
        if va in self._meta_cache:
            return self._meta_cache[va]
        res = self._parse_meta(va)
        self._meta_cache[va] = res
        return res

    def _parse_meta(self, va: int) -> tuple[Meta | None, str]:
        if not self.in_data(va, META_MIN_SIZE):
            return None, f"meta {hx(va)} not in .rdata/.data"
        raw = self.pe.read(va, META_MIN_SIZE)
        name, why = self.read_name(struct.unpack_from("<I", raw, META_NAME)[0])
        if name is None:
            return None, why
        flags, size = struct.unpack_from("<HH", raw, META_FLAGS)
        count = raw[META_COUNT]
        fptr = struct.unpack_from("<I", raw, META_FIELDS)[0]
        m = Meta(va, name, flags, size, count, fptr, raw)
        if m.kind in ("class", "valuetype") and size == 0:
            return None, f"{name}: {m.kind} with size 0"
        if m.kind in ("valuetype", "enum") and count and not self.in_data(fptr, count * FIELD_RECORD_SIZE):
            return None, f"{name}: meta+0x18 field array {hx(fptr)} x{count} not in data"
        return m, ""

    def parse_fields(self, t: TypeRec) -> None:
        m = t.meta
        if m.kind not in ("class", "valuetype", "enum") or not m.count:
            return
        ptr = t.fields_ptr
        if ptr is None or not self.in_data(ptr, m.count * FIELD_RECORD_SIZE):
            t.fields_error = f"field array {hx(ptr)} x{m.count} not in data"
            return
        out = []
        for n in range(m.count):
            rec = self.pe.read(ptr + n * FIELD_RECORD_SIZE, FIELD_RECORD_SIZE)
            np_, fl, ti, el, off, ex = struct.unpack("<6I", rec)
            name, why = self.read_name(np_)
            if name is None:
                t.fields_error = f"record {n}: {why}"
                return
            if m.kind == "enum":
                off = struct.unpack("<i", struct.pack("<I", off))[0]
            else:
                if off >= m.size:  # repo invariant: offset < size (Bc2SelectedMeshes1p.cpp:82)
                    t.fields_error = f"record {n} '{name}': offset {off:#x} >= size {m.size:#x}"
                    return
                if ti and not self.in_writable(ti, 8):
                    t.fields_error = f"record {n} '{name}': type TypeInfo {hx(ti)} not in writable data"
                    return
            out.append(FieldRec(name, fl, ti, el, off, ex))
        t.fields = out

    def reject(self, site: int | None, reason: str, **extra) -> None:
        d = {"site": hx(site), "reason": reason}
        d.update({k: hx(v) if isinstance(v, int) else v for k, v in extra.items()})
        self.rejected.append(d)

    def ctor_verified(self, ctor: int) -> bool:
        if ctor not in self._ctor_ok_cache:
            head = pattern_bytes(CLASS_CTOR_HEAD)
            b = self.pe.read(ctor, CLASS_CTOR_TAIL_AT + 3)
            self._ctor_ok_cache[ctor] = bool(b and b.startswith(head) and
                                             b[CLASS_CTOR_TAIL_AT:CLASS_CTOR_TAIL_AT + 3] == pattern_bytes(CLASS_CTOR_TAIL))
        return self._ctor_ok_cache[ctor]

    def add_type(self, t: TypeRec) -> None:
        old = self.types.get(t.info)
        if old:
            if old.meta.va != t.meta.va:
                self.warnings.append(f"TypeInfo {hx(t.info)} registered with two metas: "
                                     f"{old.name} ({old.source}) and {t.name} ({t.source}); kept first")
            if t.site is not None:
                old.duplicate_sites.append(t.site)
            return
        self.types[t.info] = t

    # -- scans ----------------------------------------------------------------
    def scan(self) -> None:
        self.scan_ctor_sites()
        if self.store_scan:
            self.scan_store_sites()
        if self.static_scan:
            self.scan_static_data()
        for t in self.types.values():
            self.parse_fields(t)
            if t.fields_error:
                self.reject(t.site, f"{t.name}: fields dropped: {t.fields_error}", typeInfo=t.info)

    def _exec_blobs(self):
        for s in self.pe.sections:
            if s.backed and (s.executable or s.name.lower() in EXTRA_CODE_SECTIONS):
                yield s, self.data[s.raw_off:s.raw_off + s.backed]

    def scan_ctor_sites(self) -> None:
        for s, blob in self._exec_blobs():
            sbase = self.base + s.rva
            for mt in CTOR_SITE_RE.finditer(blob):
                i = mt.start()
                info = struct.unpack("<I", mt.group(1))[0]
                if not self.in_writable(info, 8):
                    continue
                self.stats["ctorCallSites"] += 1
                rel = struct.unpack("<i", mt.group(2))[0]
                ctor = (sbase + i + 10 + rel) & 0xFFFFFFFF
                pushes: list[int] = []  # nearest first == arg1 first
                j = i
                while len(pushes) < MAX_PUSHES:
                    if j >= 5 and blob[j - 5] == 0x68:
                        pushes.append(struct.unpack_from("<I", blob, j - 4)[0]); j -= 5
                    elif j >= 2 and blob[j - 2] == 0x6A:
                        pushes.append(struct.unpack_from("<b", blob, j - 1)[0] & 0xFFFFFFFF); j -= 2
                    else:
                        break
                site = sbase + j
                cand = [k for k, v in enumerate(pushes) if self.looks_like_meta(v)]
                if not cand:
                    continue
                self.stats["ctorCallSitesWithMetaPush"] += 1
                k = cand[0]
                meta, why = self.parse_meta(pushes[k])
                if meta is None:
                    self.reject(site, why, typeInfo=info, meta=pushes[k])
                    continue
                if k != 0:
                    self.reject(site, f"{meta.name}: meta is push arg{k + 1}, expected arg1", typeInfo=info)
                    continue
                if meta.kind == "class":
                    if len(pushes) < 4:
                        self.reject(site, f"{meta.name}: class meta without 4-argument ctor form ({len(pushes)} pushes)",
                                    typeInfo=info)
                        continue
                    parent, arg3, fptr = pushes[1], pushes[2], pushes[3]
                    if parent != info and not self.in_writable(parent, 8):
                        self.reject(site, f"{meta.name}: parent {hx(parent)} not in writable data", typeInfo=info)
                        continue
                    t = TypeRec(info, meta, "ctor", site, ctor, self.ctor_verified(ctor), parent, arg3, fptr)
                else:
                    t = TypeRec(info, meta, "ctor", site, ctor, False, fields_ptr=meta.fields_ptr)
                h = self.ctor_histogram.setdefault(ctor, {"ctor": hx(ctor), "count": 0, "kinds": {}, "pushes": {}})
                h["count"] += 1
                h["kinds"][meta.kind] = h["kinds"].get(meta.kind, 0) + 1
                h["pushes"][str(len(pushes))] = h["pushes"].get(str(len(pushes)), 0) + 1
                self.add_type(t)

    def scan_store_sites(self) -> None:
        """`mov dword ptr [info+4], meta` (C7 05) for types not registered via a ctor call."""
        for s, blob in self._exec_blobs():
            sbase = self.base + s.rva
            for mt in STORE_RE.finditer(blob):
                addr, imm = struct.unpack("<II", mt.group(1) + mt.group(2))
                info = (addr - TI_META) & 0xFFFFFFFF
                if info in self.types or not self.in_writable(info, 8) or not self.looks_like_meta(imm):
                    continue
                meta, why = self.parse_meta(imm)
                site = sbase + mt.start()
                if meta is None:
                    self.reject(site, why, typeInfo=info, form="store")
                elif meta.kind == "class":
                    self.reject(site, f"{meta.name}: class via store form (no parent/fields)", typeInfo=info)
                else:
                    self.add_type(TypeRec(info, meta, "store", site, fields_ptr=meta.fields_ptr))

    def scan_static_data(self) -> None:
        """Opt-in: TypeInfo already initialized in file-backed .data (info+4 == meta)."""
        for s in self.pe.sections:
            if s.executable or not s.backed or not (s.writable or s.name.lower() in TYPEINFO_SECTIONS):
                continue
            blob = self.data[s.raw_off:s.raw_off + s.backed]
            for off in range(4, len(blob) - 3, 4):
                v = struct.unpack_from("<I", blob, off)[0]
                if not self.in_data(v, META_MIN_SIZE):
                    continue
                info = self.base + s.rva + off - TI_META
                if info in self.types or not self.looks_like_meta(v):
                    continue
                meta, _ = self.parse_meta(v)
                if meta is None or meta.kind == "other":
                    continue
                if meta.kind == "class":
                    parent, fptr = self.pe.u32(info + TI_PARENT), self.pe.u32(info + TI_FIELDS)
                    if parent is None or fptr is None:
                        continue
                    self.add_type(TypeRec(info, meta, "static", parent=parent, fields_ptr=fptr))
                else:
                    self.add_type(TypeRec(info, meta, "static", fields_ptr=meta.fields_ptr))


# ---------------------------------------------------------------------------
# Model building (JSON) and layout planning
# ---------------------------------------------------------------------------
def c_ident(name: str) -> str:
    s = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if not s or s[0].isdigit():
        s = "_" + s
    if s in CPP_KEYWORDS:
        s += "_"
    return s


@dataclass
class Member:
    offset: int
    size: int
    name: str
    tag: str        # base | struct | enum | ptr | prim | bytes
    ref: str | None  # C++ name of referenced struct/enum, or primitive name
    comment: str = ""


class Model:
    def __init__(self, d: Dumper, path: str | None = None):
        self.d = d
        self.path = path
        t = d.types
        self.by_info = t
        # unique C++ names per TypeInfo
        self.cname: dict[int, str] = {}
        used: dict[str, int] = {}
        for info in sorted(t, key=lambda i: (t[i].name, i)):
            base = c_ident(t[info].name)
            n = used.get(base, 0)
            used[base] = n + 1
            self.cname[info] = base if n == 0 else f"{base}_{n + 1}"
        self.structs = [i for i in t if t[i].kind in ("class", "valuetype")]
        self.enums = [i for i in t if t[i].kind == "enum"]
        self.notes: dict[int, list[str]] = {}
        self.unresolved: dict[int, int] = {}
        self.order = self._topo()
        self.layouts = {i: self._layout(i) for i in self.order}

    def tname(self, info: int) -> str:
        if not info:
            return "<none>"
        t = self.by_info.get(info)
        if t:
            return t.name
        self.unresolved[info] = self.unresolved.get(info, 0) + 1
        return f"<unresolved {hx(info)}>"

    def parent_of(self, t: TypeRec) -> int | None:
        if t.kind != "class" or t.parent is None or t.parent == t.info:
            return None
        return t.parent

    def field_kind(self, f: FieldRec) -> str:
        ft = self.by_info.get(f.type_info)
        if (ft and ft.name == "ArrayBase") or (f.element_info and not ft):
            return "array"
        if not ft:
            return "unresolved"
        return {"class": "ref", "valuetype": "inline", "enum": "enum"}.get(ft.kind, "primitive")

    def _topo(self) -> list[int]:
        deps: dict[int, set[int]] = {}
        for i in self.structs:
            t = self.by_info[i]
            s = set()
            p = self.parent_of(t)
            if p in self.by_info and self.by_info[p].kind == "class":
                s.add(p)
            for f in t.fields:
                ft = self.by_info.get(f.type_info)
                if ft and ft.kind == "valuetype" and f.type_info != i:
                    s.add(f.type_info)
            deps[i] = s
        order: list[int] = []
        state: dict[int, int] = {}
        for root in sorted(self.structs, key=lambda i: self.cname[i]):
            stack = [(root, iter(sorted(deps[root], key=lambda i: self.cname[i])))]
            if state.get(root):
                continue
            state[root] = 1
            while stack:
                node, it = stack[-1]
                nxt = next(it, None)
                if nxt is None:
                    stack.pop(); state[node] = 2; order.append(node)
                elif not state.get(nxt):
                    state[nxt] = 1
                    stack.append((nxt, iter(sorted(deps[nxt], key=lambda i: self.cname[i]))))
                elif state[nxt] == 1:
                    self.d.warnings.append(f"dependency cycle {self.by_info[node].name} -> {self.by_info[nxt].name}; "
                                           "member emitted as bytes")
        return order

    def _layout(self, info: int) -> list[Member]:
        t = self.by_info[info]
        size = t.meta.size
        notes = self.notes.setdefault(info, [])
        done = set(self.order[:self.order.index(info)])
        out: list[Member] = []
        cur = 0
        p = self.parent_of(t)
        if p is not None:
            pt = self.by_info.get(p)
            if pt is None:
                notes.append(f"parent {hx(p)} unresolved; parent bytes emitted as padding")
            elif pt.meta.size > size:
                notes.append(f"parent {pt.name} size {pt.meta.size:#x} > own size {size:#x}; base not embedded")
            elif pt.meta.size and p in done:
                out.append(Member(0, pt.meta.size, "base", "base", self.cname[p], f"inherits {pt.name}"))
                cur = pt.meta.size
        fields = sorted(t.fields, key=lambda f: f.offset)
        used_names: set[str] = {"base"}
        for idx, f in enumerate(fields):
            nxt = next((g.offset for g in fields[idx + 1:] if g.offset > f.offset), size)
            if f.offset < cur:
                notes.append(f"field {f.name} @{f.offset:#x} overlaps previous member/base (ends {cur:#x}); not emitted")
                continue
            if f.offset > cur:
                out.append(Member(cur, f.offset - cur, f"_pad_{cur:04x}", "bytes", None))
                cur = f.offset
            room = nxt - f.offset
            mname = c_ident(f.name)
            while mname in used_names:
                mname += "_"
            used_names.add(mname)
            kind = self.field_kind(f)
            ft = self.by_info.get(f.type_info)
            tn = self.tname(f.type_info)
            comment = f"{tn}"
            if f.element_info:
                comment += f"<{self.tname(f.element_info)}>"
            want, tag, ref = 0, "bytes", None
            if kind == "ref":
                want, tag, ref = 4, "ptr", self.cname[f.type_info]
            elif kind == "enum":
                want = ft.meta.size
                tag, ref = ("enum", self.cname[f.type_info]) if want in (1, 2, 4, 8) else ("bytes", None)
            elif kind == "inline":
                want = ft.meta.size
                if f.type_info in done:
                    tag, ref = "struct", self.cname[f.type_info]
            elif kind == "primitive":
                want = ft.meta.size
                pc = PRIMITIVE_CTYPES.get(ft.name)
                if pc and pc[1] == want:
                    tag, ref = "prim", ft.name
            elif kind == "array":
                want = ft.meta.size if ft else 0
            if not want:
                want = room
                if tag != "bytes":
                    tag, ref = "bytes", None
            if want > room:
                notes.append(f"field {f.name} type size {want:#x} exceeds room {room:#x}; truncated to bytes")
                want, tag, ref = room, "bytes", None
            if want <= 0:
                notes.append(f"field {f.name} @{f.offset:#x} has no room; not emitted")
                continue
            out.append(Member(f.offset, want, mname, tag, ref, comment))
            cur = f.offset + want
        if cur < size:
            out.append(Member(cur, size - cur, f"_pad_{cur:04x}", "bytes", None))
        return out

    # -- JSON -----------------------------------------------------------------
    def type_json(self, t: TypeRec) -> dict:
        d = {"name": t.name, "cppName": self.cname[t.info], "kind": t.kind, "size": t.meta.size,
             "flags": t.meta.flags, "flagsHex": f"0x{t.meta.flags:x}",
             "typeInfo": hx(t.info), "typeInfoRva": hx(t.info - self.d.base), "meta": hx(t.meta.va),
             "metaRaw": t.meta.raw.hex(" "), "fieldCount": t.meta.count,
             "registration": {"source": t.source, "site": hx(t.site), "ctor": hx(t.ctor),
                              "ctorVerified": t.ctor_verified,
                              "duplicateSites": [hx(x) for x in t.duplicate_sites]}}
        if t.fields_error:
            d["fieldsError"] = t.fields_error
        return d

    def to_json(self, sha: str) -> dict:
        d, pe = self.d, self.d.pe
        classes, enums, prims = [], [], []
        for info in sorted(d.types, key=lambda i: (d.types[i].name, i)):
            t = d.types[info]
            j = self.type_json(t)
            if t.kind in ("class", "valuetype"):
                p = self.parent_of(t)
                j["parent"] = None if p is None else self.tname(p)
                j["parentTypeInfo"] = hx(p)
                j["isRoot"] = t.kind == "class" and t.parent == t.info
                j["arg3"] = hx(t.arg3)
                j["fieldsPtr"] = hx(t.fields_ptr)
                j["fields"] = [{"name": f.name, "offset": f.offset, "offsetHex": f"0x{f.offset:x}",
                                "typeName": self.tname(f.type_info), "kind": self.field_kind(f),
                                "elementTypeName": self.tname(f.element_info) if f.element_info else None,
                                "typeInfo": hx(f.type_info), "elementTypeInfo": hx(f.element_info) if f.element_info else None,
                                "recordFlags": hx(f.flags), "recordExtra": hx(f.extra)} for f in t.fields]
                j["layoutNotes"] = self.notes.get(info, [])
                classes.append(j)
            elif t.kind == "enum":
                j["values"] = [{"name": f.name, "value": f.offset} for f in t.fields]
                enums.append(j)
            else:
                prims.append(j)
        ts = pe.timestamp
        return {
            "tool": {"name": "bc2_typeinfo_dump", "version": TOOL_VERSION},
            "exe": {"file": self.path, "sha256": sha, "imageBase": hx(d.base), "timestamp": ts,
                    "timestampUtc": _dt.datetime.fromtimestamp(ts, _dt.timezone.utc).isoformat(),
                    "machine": hx(pe.machine), "sizeOfImage": hx(pe.size_of_image),
                    "sections": [{"name": s.name, "rva": hx(s.rva), "vsize": hx(s.vsize), "rawSize": hx(s.raw_size),
                                  "flags": hx(s.flags)} for s in pe.sections]},
            "layout": {"typeInfo": {"meta": TI_META, "arg3": TI_ARG3, "parent": TI_PARENT, "fields": TI_FIELDS},
                       "meta": {"name": META_NAME, "flags": META_FLAGS, "size": META_SIZE, "count": META_COUNT,
                                "fields": META_FIELDS},
                       "fieldRecord": {"size": FIELD_RECORD_SIZE, "name": 0, "flags": 4, "type": 8, "element": 12,
                                       "offsetOrValue": 16, "unknown": 20},
                       "flagsKinds": {hex(k): v for k, v in KIND_BY_FLAGS.items()}},
            "classes": classes, "enums": enums, "primitives": prims,
            "summary": self.summary(),
        }

    def summary(self) -> dict:
        d = self.d
        kinds: dict[str, int] = {}
        sources: dict[str, int] = {}
        for t in d.types.values():
            kinds[t.kind] = kinds.get(t.kind, 0) + 1
            sources[t.source] = sources.get(t.source, 0) + 1
        other_flags: dict[str, int] = {}
        for t in d.types.values():
            if t.kind == "other":
                k = f"0x{t.meta.flags:x}"
                other_flags[k] = other_flags.get(k, 0) + 1
        where: dict[str, dict[str, int]] = {"typeInfo": {}, "meta": {}, "name": {}, "fields": {}, "site": {}}
        def sec(va):
            s = d.pe.section_for_va(va) if va else None
            return s.name if s else "?"
        for t in d.types.values():
            for k, va in (("typeInfo", t.info), ("meta", t.meta.va), ("name", struct.unpack_from("<I", t.meta.raw, 0)[0]),
                          ("fields", t.fields_ptr if t.meta.count else None), ("site", t.site)):
                if va is not None:
                    where[k][sec(va)] = where[k].get(sec(va), 0) + 1
        return {"types": len(d.types), "byKind": kinds, "sectionsUsed": where, "bySource": sources, "otherFlags": other_flags,
                "stats": d.stats, "ctors": sorted(d.ctor_histogram.values(), key=lambda h: -h["count"]),
                "classCtorVerified": sum(1 for t in d.types.values() if t.ctor_verified),
                "unresolvedTypeRefs": [{"typeInfo": hx(k), "refs": v} for k, v in sorted(self.unresolved.items())],
                "rejected": d.rejected, "warnings": d.warnings}


# ---------------------------------------------------------------------------
# Emitters
# ---------------------------------------------------------------------------
ENUM_UNDERLYING = {1: "int8_t", 2: "int16_t", 4: "int32_t", 8: "int64_t"}


def emit_header(m: Model, sha: str) -> str:
    d = m.d
    L = ["// Generated by bc2_typeinfo_dump.py " + TOOL_VERSION + ". Do not edit.",
         f"// Source exe sha256 {sha}, image base {hx(d.base)}, timestamp {d.pe.timestamp}.",
         "// Layout is valid ONLY for that exe. Pointers are 32-bit target addresses (bc2_ptr32).",
         "#pragma once", "#include <cstddef>", "#include <cstdint>", "", "namespace bc2 {",
         "typedef std::uint32_t bc2_ptr32;", "using std::int8_t; using std::uint8_t; using std::int16_t; using std::uint16_t;",
         "using std::int32_t; using std::uint32_t; using std::int64_t; using std::uint64_t;", ""]
    for info in sorted(m.enums, key=lambda i: m.cname[i]):
        t = d.types[info]
        ut = ENUM_UNDERLYING.get(t.meta.size)
        if not ut:
            L.append(f"// enum {t.name}: size {t.meta.size} unsupported; fields use raw bytes")
            continue
        L.append(f"// TypeInfo {hx(info)} meta {hx(t.meta.va)}")
        L.append(f"enum class {m.cname[info]} : {ut} {{")
        seen: set[str] = set()
        for f in t.fields:
            n = c_ident(f.name)
            while n in seen:
                n += "_"
            seen.add(n)
            L.append(f"    {n} = {f.offset},")
        L += ["};", f"static_assert(sizeof({m.cname[info]}) == {t.meta.size}, \"{t.name}\");", ""]
    L.append("#pragma pack(push, 1)")
    # forward declarations so pointer comments stay meaningful
    for info in m.order:
        L.append(f"struct {m.cname[info]};")
    L.append("")
    for info in m.order:
        t = d.types[info]
        cn = m.cname[info]
        par = m.parent_of(t)
        L.append(f"// {t.kind} {t.name}  TypeInfo {hx(info)}  meta {hx(t.meta.va)}  size {t.meta.size:#x}"
                 + (f"  parent {m.tname(par)}" if par is not None else ""))
        for n in m.notes.get(info, []):
            L.append(f"//   note: {n}")
        L.append(f"struct {cn} {{")
        for mem in m.layouts[info]:
            if mem.tag == "base" or mem.tag == "struct":
                decl = f"{mem.ref} {mem.name};"
            elif mem.tag == "enum":
                decl = f"{mem.ref} {mem.name};"
            elif mem.tag == "ptr":
                decl = f"bc2_ptr32 {mem.name}; // -> {mem.ref}*"
            elif mem.tag == "prim":
                decl = f"{PRIMITIVE_CTYPES[mem.ref][0]} {mem.name};"
            else:
                decl = f"uint8_t {mem.name}[{mem.size:#x}];"
            cmt = f" // {mem.comment}" if mem.comment and mem.tag != "ptr" else ""
            L.append(f"    /* {mem.offset:#06x} */ {decl}{cmt}")
        L.append("};")
        L.append(f"static_assert(sizeof({cn}) == {t.meta.size:#x}, \"{cn} size\");")
        for mem in m.layouts[info]:
            if not mem.name.startswith("_pad_"):
                L.append(f"static_assert(offsetof({cn}, {mem.name}) == {mem.offset:#x}, \"{cn}::{mem.name}\");")
        L.append("")
    L += ["#pragma pack(pop)", "} // namespace bc2", ""]
    return "\n".join(L)


GHIDRA_TEMPLATE = r'''# Generated by bc2_typeinfo_dump.py __VERSION__ -- creates BC2 reflection types and labels.
# Source exe sha256 __SHA__
# Run from Ghidra Script Manager on the BFBC2Game.exe program (Jython 2.7 or PyGhidra).
#@category BC2
#@menupath
import base64, json
from ghidra.program.model.data import (CategoryPath, StructureDataType, EnumDataType, PointerDataType,
    ArrayDataType, ByteDataType, BooleanDataType, SignedByteDataType, ShortDataType, UnsignedShortDataType,
    IntegerDataType, UnsignedIntegerDataType, LongLongDataType, UnsignedLongLongDataType, FloatDataType,
    DoubleDataType, CharDataType, DataTypeConflictHandler)
from ghidra.program.model.symbol import SourceType

DATA = json.loads(base64.b64decode("__B64__").decode("utf-8"))
dtm = currentProgram.getDataTypeManager()
CAT = CategoryPath("/BC2_TypeInfo")
H = DataTypeConflictHandler.REPLACE_HANDLER
DELTA = currentProgram.getImageBase().getOffset() - DATA["imageBase"]
PRIMS = {"Boolean": BooleanDataType.dataType, "Int8": SignedByteDataType.dataType, "Uint8": ByteDataType.dataType,
         "Int16": ShortDataType.dataType, "Uint16": UnsignedShortDataType.dataType,
         "Int32": IntegerDataType.dataType, "Uint32": UnsignedIntegerDataType.dataType,
         "Int64": LongLongDataType.dataType, "Uint64": UnsignedLongLongDataType.dataType,
         "Float32": FloatDataType.dataType, "Float64": DoubleDataType.dataType,
         "String": PointerDataType(CharDataType.dataType, 4), "CString": PointerDataType(CharDataType.dataType, 4)}
errors = []

def at(va):
    return toAddr(va + DELTA)

enums = {}
for e in DATA["enums"]:
    try:
        ed = EnumDataType(CAT, e["name"], e["size"])
        for v in e["values"]:
            try:
                ed.add(v[0], v[1])
            except Exception as ex:
                errors.append("enum %s.%s: %s" % (e["name"], v[0], ex))
        enums[e["name"]] = dtm.addDataType(ed, H)
    except Exception as ex:
        errors.append("enum %s: %s" % (e["name"], ex))

structs = {}
for s in DATA["structs"]:
    structs[s["name"]] = dtm.addDataType(StructureDataType(CAT, s["name"], s["size"]), H)

for s in DATA["structs"]:
    st = structs[s["name"]]
    if s["comment"]:
        st.setDescription(s["comment"])
    for off, size, name, tag, ref, comment in s["members"]:
        dt = None
        if tag in ("base", "struct"):
            dt = structs.get(ref)
        elif tag == "enum":
            dt = enums.get(ref)
        elif tag == "ptr":
            dt = PointerDataType(structs[ref], 4) if ref in structs else PointerDataType(ByteDataType.dataType, 4)
        elif tag == "prim":
            dt = PRIMS.get(ref)
        elif tag == "pad":
            continue
        if dt is None or dt.getLength() != size:
            dt = ArrayDataType(ByteDataType.dataType, size, 1)
        try:
            st.replaceAtOffset(off, dt, size, name, comment)
        except Exception as ex:
            errors.append("%s.%s: %s" % (s["name"], name, ex))

labels = 0
for va, name, comment in DATA["labels"]:
    try:
        a = at(va)
        createLabel(a, name, True, SourceType.USER_DEFINED)
        if comment:
            setEOLComment(a, comment)
        labels += 1
    except Exception as ex:
        errors.append("label %s: %s" % (name, ex))

print("BC2 TypeInfo import: %d enums, %d structs, %d labels, %d errors" % (len(enums), len(structs), labels, len(errors)))
for e in errors[:50]:
    print("  " + e)
'''


def emit_ghidra(m: Model, sha: str) -> str:
    d = m.d
    enums = [{"name": m.cname[i], "size": d.types[i].meta.size if d.types[i].meta.size in (1, 2, 4, 8) else 4,
              "values": [[c_ident(f.name), f.offset] for f in d.types[i].fields]} for i in sorted(m.enums, key=lambda i: m.cname[i])]
    structs = []
    for info in m.order:
        t = d.types[info]
        structs.append({"name": m.cname[info], "size": t.meta.size,
                        "comment": f"{t.kind} {t.name} TypeInfo {hx(info)}",
                        "members": [[mm.offset, mm.size, mm.name, "pad" if mm.name.startswith("_pad_") else mm.tag,
                                     mm.ref, mm.comment] for mm in m.layouts[info]]})
    labels = []
    for info, t in sorted(d.types.items()):
        n = c_ident(t.name) if m.cname[info] == c_ident(t.name) else m.cname[info]
        labels.append([info, f"TypeInfo_{n}", f"{t.kind} {t.name} size {t.meta.size:#x}"])
        labels.append([t.meta.va, f"TypeMeta_{n}", None])
        if t.fields_ptr and t.meta.count and not t.fields_error:
            labels.append([t.fields_ptr, f"TypeFields_{n}", f"{t.meta.count} x 24-byte records"])
        if t.site is not None:
            labels.append([t.site, None, f"register TypeInfo {t.name}"])
    labels = [l for l in labels if l[1]] + [[l[0], f"RegisterType_{c_ident(l[2][18:])}_{l[0]:08x}", l[2]]
                                            for l in labels if not l[1]]
    blob = {"imageBase": d.base, "enums": enums, "structs": structs, "labels": labels}
    b64 = base64.b64encode(json.dumps(blob, separators=(",", ":")).encode()).decode()
    return (GHIDRA_TEMPLATE.replace("__VERSION__", TOOL_VERSION).replace("__SHA__", sha).replace("__B64__", b64))


def _rc_uuid(name: str) -> str:
    return base64.b64encode(uuid.uuid5(uuid.NAMESPACE_URL, "bc2:" + name).bytes_le).decode()


def emit_reclass(m: Model) -> bytes:
    """ReClass.NET project: zip containing Data.xml. Node coverage is contiguous per class."""
    d = m.d
    out = ['<?xml version="1.0" encoding="utf-8"?>',
           '<reclass version="65537" platform="x86" type="ReClass.NET">',
           "  <custom_data />", "  <type_mapping />", "  <enums />", "  <classes>"]

    def node(t, name, comment="", ref=None):
        r = f" reference={quoteattr(ref)}" if ref else ""
        return f"      <node type=\"{t}\" name={quoteattr(name)} comment={quoteattr(comment)} hidden=\"false\"{r} />"

    for info in m.order:
        t = d.types[info]
        cn = m.cname[info]
        out.append(f"    <class uuid=\"{_rc_uuid(cn)}\" name={quoteattr(cn)} "
                   f"comment={quoteattr(f'{t.kind} {t.name} TypeInfo {hx(info)} size {t.meta.size:#x}')} address=\"0\">")
        for mm in m.layouts[info]:
            if mm.tag in ("base", "struct"):
                out.append(node("ClassInstanceNode", mm.name, mm.comment, _rc_uuid(mm.ref)))
            elif mm.tag == "prim" and mm.ref in RECLASS_PRIM:
                out.append(node(RECLASS_PRIM[mm.ref], mm.name, mm.comment))
            elif mm.tag == "enum" and mm.size == 4:
                out.append(node("Int32Node", mm.name, f"enum {mm.ref}"))
            elif mm.tag in ("ptr", "prim") and mm.size == 4:
                out.append(node("Hex32Node", mm.name, f"-> {mm.ref}" if mm.tag == "ptr" else mm.comment))
            else:
                pos, end = mm.offset, mm.offset + mm.size
                k = 0
                while pos < end:
                    big = 4 if pos % 4 == 0 and end - pos >= 4 else 1
                    out.append(node("Hex32Node" if big == 4 else "Hex8Node", f"{mm.name}_{k}" if k else mm.name,
                                    mm.comment if k == 0 else ""))
                    pos += big
                    k += 1
        out.append("    </class>")
    out += ["  </classes>", "</reclass>", ""]
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("Data.xml", "\n".join(out))
    return buf.getvalue()


def emit_csv(m: Model) -> str:
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(["class", "kind", "class_size", "parent", "type_info", "offset", "size", "field", "type", "field_kind", "element"])
    d = m.d
    for info in m.order:
        t = d.types[info]
        par = m.parent_of(t)
        pname = m.tname(par) if par is not None else ""
        lay = {mm.name: mm for mm in m.layouts[info]}
        for f in sorted(t.fields, key=lambda f: f.offset):
            mm = lay.get(c_ident(f.name))
            w.writerow([t.name, t.kind, f"0x{t.meta.size:x}", pname, hx(info), f"0x{f.offset:x}",
                        f"0x{mm.size:x}" if mm else "", f.name, m.tname(f.type_info), m.field_kind(f),
                        m.tname(f.element_info) if f.element_info else ""])
        if not t.fields:
            w.writerow([t.name, t.kind, f"0x{t.meta.size:x}", pname, hx(info), "", "", "", "", "", ""])
    for info in sorted(m.enums, key=lambda i: m.cname[i]):
        t = d.types[info]
        for f in t.fields:
            w.writerow([t.name, "enum", f"0x{t.meta.size:x}", "", hx(info), f.offset, "", f.name, "", "enum_value", ""])
    return buf.getvalue()


# ---------------------------------------------------------------------------
# Known anchors from the FrostbiteVR repo (build 795745). Informational check.
# ---------------------------------------------------------------------------
# Executable SHA-256 the repo's addresses are pinned to (FrostbiteVR BUILDING.md,
# release/AUTOMATIC-STOCK-BOLT-DISPATCH.md:10). Anchor RVAs only hold for this hash.
REPO_EXE_SHA256 = "3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258"

ANCHORS = [
    # (type, expected size, expected flags, expected TypeInfo RVA, [(field, offset, typeName, elementTypeName)], source)
    ("ZoomLevelData", 0x74, None, 0x17FC7FC, [("FieldOfView", 0x48, None, None)],
     "Bc2OpticDiscovery.cpp:55-58, tests/Bc2OpticDiscoveryTests.cpp:18"),
    ("SoldierAimingSimulationData", 0x7C, None, 0x17FC898,
     [("ZoomLevels", 0x4C, None, "ZoomLevelData"), ("ZoomType", 0x48, None, None)], "Bc2OpticDiscovery.cpp:55-56"),
    ("ScopeFilterData", 0x30, None, None, [("ScissorRegion", 0x20, None, None)], "Bc2OpticDiscovery.cpp:55,58"),
    ("SniperLensScopeFilterData", 0xD0, None, 0x17F6B48,
     [("BlurScale", 0xC0, None, None), ("BlurCenter", 0x60, None, None)], "Bc2OpticDiscovery.cpp:55,58"),
    ("SoldierWeaponData", 0x130, FLAGS_CLASS, None,
     [("WeaponClass", 0x84, "WeaponClassEnum", None), ("WeaponStates", 0x88, "ArrayBase", "WeaponStateData")],
     "Bc2BodyInventory.cpp:39-46, Bc2SelectedMeshes1p.cpp:97"),
    ("WeaponClassEnum", 4, FLAGS_ENUM, None, [], "Bc2BodyInventory.cpp:46-51"),
    ("WeaponStateData", 0xC8, FLAGS_VALUE, None,
     [("IsPumpAction", 0xC0, "Boolean", None), ("Meshes1p", 0x80, "ArrayBase", "SkinnedMeshAsset")],
     "Bc2ReloadState.cpp:104-112, Bc2SelectedMeshes1p.cpp:98-99"),
    ("SkinnedMeshAsset", 0x44, FLAGS_CLASS, None, [], "Bc2SelectedMeshes1p.cpp:100"),
    ("Boolean", 1, None, None, [], "Bc2ReloadState.cpp:112"),
    ("GameObjectData", None, FLAGS_CLASS, None, [("Name", 12, "String", None)], "Bc2BoatProfile.cpp:50-52"),
    ("EntryComponentData", None, FLAGS_CLASS, None,
     [("EntryOrderNumber", 176, "Int32", None), ("EntryClass", 184, "EntryClass", None),
      ("InputMapping", 192, "InputActionMappingsData", None)], "Bc2BoatProfile.cpp:52-53"),
    ("VehicleEntityData", None, FLAGS_CLASS, None, [("Mesh", 500, "CompositeMeshAsset", None)], "Bc2BoatProfile.cpp:52"),
    ("InputActionMappingsData", None, FLAGS_CLASS, None, [("Mappings", 12, "ArrayBase", None)], "Bc2BoatProfile.cpp:56"),
    ("RotationBodyData", None, FLAGS_CLASS, None,
     [("RotationAxis", 28, "Int32", None), ("AngularConstraintMin", 20, "Float32", None),
      ("AngularMomentumMultiplier", 44, "Float32", None)], "Bc2BoatAim.cpp:65-69"),
    ("ChildRotationBodyData", None, FLAGS_CLASS, None,
     [("RotationInput", 56, "EntryInputActionEnum", None), ("UseLinearInput", 72, "Boolean", None)], "Bc2BoatAim.cpp:65-70"),
]


def check_anchors(m: Model) -> list[dict]:
    d = m.d
    by_name: dict[str, list[TypeRec]] = {}
    for t in d.types.values():
        by_name.setdefault(t.name, []).append(t)
    res = []
    for name, size, flags, rva, fields, src in ANCHORS:
        ts = by_name.get(name, [])
        if not ts:
            res.append({"type": name, "status": "absent", "source": src, "problems": ["type not found"]})
            continue
        t = ts[0]
        probs = []
        if len(ts) > 1:
            probs.append(f"{len(ts)} TypeInfos share this name")
        if size is not None and t.meta.size != size:
            probs.append(f"size {t.meta.size:#x} != {size:#x}")
        if flags is not None and t.meta.flags != flags:
            probs.append(f"flags {t.meta.flags:#x} != {flags:#x}")
        if rva is not None and t.info - d.base != rva:
            probs.append(f"TypeInfo RVA {t.info - d.base:#x} != {rva:#x}")
        for fname, off, tn, en in fields:
            fs = [f for f in t.fields if f.name == fname]
            if len(fs) != 1:
                probs.append(f"field {fname}: {len(fs)} matches")
                continue
            f = fs[0]
            if f.offset != off:
                probs.append(f"field {fname} offset {f.offset:#x} != {off:#x}")
            if tn and m.tname(f.type_info) != tn:
                probs.append(f"field {fname} type {m.tname(f.type_info)} != {tn}")
            if en and m.tname(f.element_info) != en:
                probs.append(f"field {fname} element {m.tname(f.element_info)} != {en}")
        if name == "WeaponClassEnum":
            want = {"wcAssault": 0, "wcShotgun": 1, "wcSmg": 2, "wcLmg": 3, "wcSniper": 4, "wcUgl": 5}
            got = {f.name: f.offset for f in t.fields}
            if any(got.get(k) != v for k, v in want.items()):
                probs.append(f"values {got} != {want}")
        if name == "SniperLensScopeFilterData" and m.tname(m.parent_of(t) or 0) != "ScopeFilterData":
            probs.append(f"parent {m.tname(m.parent_of(t) or 0)} != ScopeFilterData")
        res.append({"type": name, "status": "pass" if not probs else "FAIL", "source": src, "problems": probs})
    return res


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------
def run(exe_path: str, out_dir: str, *, prefix: str = "bc2_types", store_scan: bool = True,
        static_scan: bool = False, quiet: bool = False) -> dict:
    import os
    with open(exe_path, "rb") as fh:
        data = fh.read()
    sha = hashlib.sha256(data).hexdigest()
    d = Dumper(data, store_scan=store_scan, static_scan=static_scan)
    if d.pe.machine != 0x14C:
        d.warnings.append(f"machine {d.pe.machine:#x} is not i386 (0x14c)")
    d.scan()
    m = Model(d, os.path.basename(exe_path))
    doc = m.to_json(sha)
    doc["anchors"] = check_anchors(m)
    doc["exe"]["matchesRepoBuild"] = sha == REPO_EXE_SHA256
    os.makedirs(out_dir, exist_ok=True)
    paths = {
        "json": os.path.join(out_dir, prefix + ".json"),
        "header": os.path.join(out_dir, prefix + ".h"),
        "ghidra": os.path.join(out_dir, "ghidra_import_types.py"),
        "reclass": os.path.join(out_dir, prefix + ".rcnet"),
        "csv": os.path.join(out_dir, prefix + ".csv"),
    }
    header = emit_header(m, sha)
    ghidra = emit_ghidra(m, sha)
    rc = emit_reclass(m)
    cs = emit_csv(m)
    doc["summary"] = m.summary()  # refresh: emitters may add unresolved refs
    with open(paths["json"], "w", encoding="utf-8") as fh:
        json.dump(doc, fh, indent=1)
    for k, text in (("header", header), ("ghidra", ghidra), ("csv", cs)):
        with open(paths[k], "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
    with open(paths["reclass"], "wb") as fh:
        fh.write(rc)
    if not quiet:
        print_summary(doc, paths)
    return {"doc": doc, "paths": paths, "model": m}


def print_summary(doc: dict, paths: dict) -> None:
    s = doc["summary"]
    e = doc["exe"]
    if not e.get("matchesRepoBuild"):
        print(f"note: sha256 differs from the repo-pinned build {REPO_EXE_SHA256[:16]}...; anchor RVAs may not apply")
    print(f"exe sha256 {e['sha256']}  base {e['imageBase']}  timestamp {e['timestamp']} ({e['timestampUtc']})")
    print(f"types {s['types']}  by kind {s['byKind']}  by source {s['bySource']}")
    print(f"sections used: {s['sectionsUsed']}")
    print(f"ctor call sites scanned {s['stats']['ctorCallSites']}, with meta push {s['stats']['ctorCallSitesWithMetaPush']}, "
          f"class ctors verified {s['classCtorVerified']}")
    for h in s["ctors"][:8]:
        print(f"  ctor {h['ctor']}: {h['count']} sites  kinds {h['kinds']}  pushes {h['pushes']}")
    if s["otherFlags"]:
        print(f"non-class/value/enum flags: {s['otherFlags']}")
    print(f"unresolved type refs {len(s['unresolvedTypeRefs'])}, rejected {len(s['rejected'])}, warnings {len(s['warnings'])}")
    for r in s["rejected"][:15]:
        print(f"  rejected {r['site']}: {r['reason']}")
    for w in s["warnings"][:10]:
        print(f"  warning: {w}")
    a = doc.get("anchors", [])
    print("repo anchors: " + ", ".join(f"{x['status']}={sum(1 for y in a if y['status'] == x['status'])}"
                                       for x in {v['status']: v for v in a}.values()))
    for x in a:
        if x["status"] != "pass":
            print(f"  {x['status']} {x['type']}: {'; '.join(x['problems'])}  [{x['source']}]")
    for k, p in paths.items():
        print(f"wrote {k}: {p}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("exe", help="path to BFBC2Game.exe")
    ap.add_argument("-o", "--out-dir", default="bc2_typeinfo_out")
    ap.add_argument("--prefix", default="bc2_types")
    ap.add_argument("--no-store-scan", action="store_true", help="skip `mov [info+4], meta` registration form")
    ap.add_argument("--static-scan", action="store_true", help="also accept TypeInfo pre-initialized in .data (noisy)")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--strict", action="store_true", help="exit 3 if any repo anchor is absent or fails")
    a = ap.parse_args(argv)
    try:
        r = run(a.exe, a.out_dir, prefix=a.prefix, store_scan=not a.no_store_scan, static_scan=a.static_scan, quiet=a.quiet)
    except (OSError, PeError) as ex:
        print(f"error: {ex}", file=sys.stderr)
        return 1
    if not r["doc"]["summary"]["types"]:
        return 2
    if a.strict and any(x["status"] != "pass" for x in r["doc"]["anchors"]):
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
