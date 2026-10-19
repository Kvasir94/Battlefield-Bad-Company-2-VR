"""Unit tests for bc2_typeinfo_dump.py against a synthetic PE32 image.

Run: python -m unittest discover -s tests -v   (from /home/claude/work/typeinfo)
"""
from __future__ import annotations

import ast
import base64
import csv
import io
import json
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
import bc2_typeinfo_dump as td  # noqa: E402

BASE = 0x400000
ALIGN = 0x1000
FALIGN = 0x200
EXEC, READ, WRITE, CODE, IDATA = 0x20000000, 0x40000000, 0x80000000, 0x20, 0x40


class Sec:
    def __init__(self, name, rva, flags, vsize=None):
        self.name, self.rva, self.flags, self.buf, self.vsize = name, rva, flags, bytearray(), vsize

    def va(self, off=None):
        return BASE + self.rva + (len(self.buf) if off is None else off)

    def put(self, data, align=4):
        while len(self.buf) % align:
            self.buf.append(0)
        at = self.va()
        self.buf += data
        return at


def write(path, data):
    with open(path, "wb" if isinstance(data, bytes) else "w") as fh:
        fh.write(data)


def read(path):
    with open(path) as fh:
        return fh.read()


class PeBuilder:
    """Lays out .text / ctr / .rdata / fieldinf / .data / typeinfo like build 795745."""

    def __init__(self):
        self.text = Sec(".text", 0x1000, EXEC | READ | CODE)
        self.rdata = Sec(".rdata", 0x3000, READ | IDATA)
        self.fieldinf = Sec("fieldinf", 0x5000, READ | IDATA)
        self.data = Sec(".data", 0x6000, READ | WRITE | IDATA)
        # TypeInfo storage: no raw bytes (BSS-like) and no write bit in the header, as a
        # worst case for the "typeinfo" section-name rule.
        self.typeinfo = Sec("typeinfo", 0x7000, READ | IDATA | 0x80, vsize=0x1000)
        self.sections = [self.text, self.rdata, self.fieldinf, self.data, self.typeinfo]
        self.strings: dict[str, int] = {}
        self.next_info = self.typeinfo.va(0x10)
        # class ctor with the exact repo stub; plus one generic ctor for other kinds
        self.class_ctor = self.text.put(td.pattern_bytes(td.CLASS_CTOR_HEAD) + b"\0" * 4 + b"\x90" * 9 +
                                        td.pattern_bytes(td.CLASS_CTOR_TAIL), 16)
        self.other_ctor = self.text.put(b"\x8B\x44\x24\x04\x89\x41\x04\xC2\x04\x00", 16)
        self.text.put(b"\xCC" * 16)

    def s(self, text):
        if text not in self.strings:
            self.strings[text] = self.rdata.put(text.encode() + b"\0", 1)
        return self.strings[text]

    def alloc_info(self):
        v = self.next_info
        self.next_info += 0x40
        return v

    def fields(self, recs):
        """recs: (name, type_info, element_info, offset)"""
        blob = b"".join(struct.pack("<6I", self.s(n), 0, ti, el, off & 0xFFFFFFFF, 0) for n, ti, el, off in recs)
        return self.fieldinf.put(blob) if recs else 0

    def meta(self, name, flags, size, count, fields_ptr=0, name_ptr=None):
        m = bytearray(32)
        struct.pack_into("<IHH", m, 0, self.s(name) if name_ptr is None else name_ptr, flags, size)
        m[13] = count
        struct.pack_into("<I", m, 24, fields_ptr)
        return self.rdata.put(bytes(m))

    def call(self, target):
        at = BASE + self.text.rva + len(self.text.buf)
        return b"\xE8" + struct.pack("<i", target - (at + 5))

    def register_other(self, info, meta):
        code = b"\x68" + struct.pack("<I", meta) + b"\xB9" + struct.pack("<I", info)
        site = self.text.va()
        self.text.buf += code
        self.text.buf += self.call(self.other_ctor)
        return site

    def register_class(self, info, meta, parent, fields, arg3=0x12345678):
        code = b"".join(b"\x68" + struct.pack("<I", v) for v in (fields, arg3, parent, meta))
        code += b"\xB9" + struct.pack("<I", info)
        site = self.text.va()
        self.text.buf += code
        self.text.buf += self.call(self.class_ctor)
        return site

    def build(self, machine=0x14C):
        nsec = len(self.sections)
        hdr_size = (0x40 + 4 + 20 + 224 + 40 * nsec + FALIGN - 1) // FALIGN * FALIGN
        raw = hdr_size
        table = b""
        bodies = b""
        for s in self.sections:
            vsize = s.vsize if s.vsize is not None else max(len(s.buf), 1)
            if s is self.typeinfo:
                rsize, rptr = 0, 0
            else:
                rsize = (len(s.buf) + FALIGN - 1) // FALIGN * FALIGN
                rptr = raw
                bodies += bytes(s.buf) + b"\0" * (rsize - len(s.buf))
                raw += rsize
            table += struct.pack("<8sIIIIIIHHI", s.name.encode(), vsize, s.rva, rsize, rptr, 0, 0, 0, 0, s.flags)
        dos = bytearray(0x40)
        dos[0:2] = b"MZ"
        struct.pack_into("<I", dos, 0x3C, 0x40)
        coff = struct.pack("<HHIIIHH", machine, nsec, 0x4B8A1234, 0, 0, 224, 0x102)
        opt = bytearray(224)
        struct.pack_into("<H", opt, 0, 0x10B)
        struct.pack_into("<I", opt, 16, 0x1000)
        struct.pack_into("<I", opt, 28, BASE)
        struct.pack_into("<II", opt, 32, ALIGN, FALIGN)
        struct.pack_into("<I", opt, 56, 0x9000)
        struct.pack_into("<I", opt, 60, hdr_size)
        head = bytes(dos) + b"PE\0\0" + coff + bytes(opt) + table
        return head + b"\0" * (hdr_size - len(head)) + bodies


def build_fixture():
    """Synthetic registry: primitives, enum, value type, 3-deep class chain, bad sites."""
    b = PeBuilder()
    ids = {}
    for name, size in (("Int32", 4), ("Float32", 4), ("Boolean", 1), ("String", 4), ("ArrayBase", 16)):
        ids[name] = b.alloc_info()
        b.register_other(ids[name], b.meta(name, 0xF1 if name != "ArrayBase" else 0x41, size, 0))
    ids["TestEnum"] = b.alloc_info()
    ef = b.fields([("teA", 0, 0, 0), ("teB", 0, 0, 1), ("teNeg", 0, 0, -2), ("teC", 0, 0, 5)])
    b.register_other(ids["TestEnum"], b.meta("TestEnum", td.FLAGS_ENUM, 4, 4, ef))
    ids["Vec3"] = b.alloc_info()
    vf = b.fields([("x", ids["Float32"], 0, 0), ("y", ids["Float32"], 0, 4), ("z", ids["Float32"], 0, 8)])
    b.register_other(ids["Vec3"], b.meta("Vec3", td.FLAGS_VALUE, 12, 3, vf))
    # classes: DataContainer (root) <- GameData <- WeaponData
    ids["DataContainer"] = b.alloc_info()
    b.register_class(ids["DataContainer"], b.meta("DataContainer", td.FLAGS_CLASS, 8, 0), ids["DataContainer"], 0)
    ids["GameData"] = b.alloc_info()
    ids["WeaponData"] = b.alloc_info()
    gf = b.fields([("Name", ids["String"], 0, 8), ("default", ids["Int32"], 0, 0xC)])  # keyword name on purpose
    b.register_class(ids["GameData"], b.meta("GameData", td.FLAGS_CLASS, 0x10, 2, 0), ids["DataContainer"], gf)
    wf = b.fields([("Kind", ids["TestEnum"], 0, 0x10), ("Origin", ids["Vec3"], 0, 0x14),
                   ("Owner", ids["GameData"], 0, 0x20), ("Items", ids["ArrayBase"], ids["Vec3"], 0x24),
                   ("Flag", ids["Boolean"], 0, 0x34)])
    b.register_class(ids["WeaponData"], b.meta("WeaponData", td.FLAGS_CLASS, 0x40, 5, 0), ids["GameData"], wf)
    # rejected candidates
    bad_name = b.rdata.put(b"Bad\x01Name\0", 1)
    b.register_other(b.alloc_info(), b.meta("x", td.FLAGS_VALUE, 4, 0, name_ptr=bad_name))     # not looks_like_meta -> silent
    b.register_class(b.alloc_info(), b.meta("BrokenFields", td.FLAGS_CLASS, 0x20, 3, 0), ids["DataContainer"],
                     0x0BAD0000)                                                                   # fields outside image
    b.register_other(b.alloc_info(), b.meta("ValueNoFields", td.FLAGS_VALUE, 8, 2, 0x0BAD0000))   # meta+24 bad
    b.register_class(b.alloc_info(), b.meta("BadOffset", td.FLAGS_CLASS, 8, 1, 0), ids["DataContainer"],
                     b.fields([("Far", ids["Int32"], 0, 0x40)]))                                   # offset >= size
    ids["Lonely"] = b.alloc_info()
    b.register_other(ids["Lonely"], b.meta("Lonely", 0x179, 4, 0))
    # duplicate registration of Int32 (same info, same meta) should be merged
    return b, ids


class Base(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.b, cls.ids = build_fixture()
        cls.image = cls.b.build()
        cls.tmp = tempfile.mkdtemp(prefix="bc2ti_")
        cls.exe = os.path.join(cls.tmp, "BFBC2Game.exe")
        with open(cls.exe, "wb") as fh:
            fh.write(cls.image)
        cls.out = os.path.join(cls.tmp, "out")
        cls.r = td.run(cls.exe, cls.out, quiet=True)
        cls.doc = cls.r["doc"]

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def cls_(self, name):
        return next(c for c in self.doc["classes"] if c["name"] == name)


class TestPe(Base):
    def test_sections_and_mapping(self):
        pe = td.PeImage(self.image)
        self.assertEqual(pe.image_base, BASE)
        self.assertEqual(pe.machine, 0x14C)
        self.assertEqual([s.name for s in pe.sections], [".text", ".rdata", "fieldinf", ".data", "typeinfo"])
        va = self.b.strings["WeaponData"]
        self.assertEqual(pe.read(va, 11), b"WeaponData\0")
        self.assertIsNone(pe.va_to_off(self.ids["Int32"]))      # BSS: virtual only
        self.assertIsNotNone(pe.section_for_va(self.ids["Int32"]))
        self.assertIsNone(pe.read(BASE - 4, 4))

    def test_bad_headers(self):
        for blob in (b"", b"MZ" + b"\0" * 10, b"XX" + self.image[2:]):
            with self.assertRaises(td.PeError):
                td.PeImage(blob)
        pe64 = bytearray(self.image)
        nt = struct.unpack_from("<I", pe64, 0x3C)[0]
        struct.pack_into("<H", pe64, nt + 24, 0x20B)
        with self.assertRaises(td.PeError):
            td.PeImage(bytes(pe64))


class TestDump(Base):
    def test_identity(self):
        e = self.doc["exe"]
        self.assertEqual(e["imageBase"], "0x00400000")
        self.assertEqual(e["timestamp"], 0x4B8A1234)
        self.assertEqual(len(e["sha256"]), 64)

    def test_classes_and_parent_chain(self):
        w, g, dc = self.cls_("WeaponData"), self.cls_("GameData"), self.cls_("DataContainer")
        self.assertEqual((w["size"], w["parent"], w["kind"], w["flags"]), (0x40, "GameData", "class", 0x35))
        self.assertEqual(g["parent"], "DataContainer")
        self.assertTrue(dc["isRoot"])
        self.assertIsNone(dc["parent"])
        self.assertTrue(w["registration"]["ctorVerified"])
        self.assertEqual(w["typeInfo"], td.hx(self.ids["WeaponData"]))
        self.assertEqual(w["arg3"], "0x12345678")

    def test_fields(self):
        f = {x["name"]: x for x in self.cls_("WeaponData")["fields"]}
        self.assertEqual((f["Kind"]["offset"], f["Kind"]["typeName"], f["Kind"]["kind"]), (0x10, "TestEnum", "enum"))
        self.assertEqual((f["Origin"]["typeName"], f["Origin"]["kind"]), ("Vec3", "inline"))
        self.assertEqual((f["Owner"]["typeName"], f["Owner"]["kind"]), ("GameData", "ref"))
        self.assertEqual((f["Items"]["kind"], f["Items"]["typeName"], f["Items"]["elementTypeName"]), ("array", "ArrayBase", "Vec3"))
        self.assertEqual((f["Flag"]["typeName"], f["Flag"]["offset"]), ("Boolean", 0x34))
        v = self.cls_("Vec3")
        self.assertEqual((v["kind"], v["size"], [x["name"] for x in v["fields"]]), ("valuetype", 12, ["x", "y", "z"]))

    def test_enum(self):
        e = next(x for x in self.doc["enums"] if x["name"] == "TestEnum")
        self.assertEqual([(v["name"], v["value"]) for v in e["values"]], [("teA", 0), ("teB", 1), ("teNeg", -2), ("teC", 5)])
        self.assertTrue(any(x["name"] == "Lonely" for x in self.doc["enums"]))

    def test_primitives(self):
        names = {p["name"]: p for p in self.doc["primitives"]}
        self.assertEqual(set(names), {"Int32", "Float32", "Boolean", "String", "ArrayBase"})
        self.assertEqual(names["Boolean"]["size"], 1)

    def test_rejections(self):
        reasons = " | ".join(r["reason"] for r in self.doc["summary"]["rejected"])
        self.assertIn("BrokenFields: fields dropped", reasons)
        self.assertIn("ValueNoFields", reasons)
        self.assertIn("BadOffset: fields dropped", reasons)
        self.assertFalse(any(c["name"] == "ValueNoFields" for c in self.doc["classes"]))
        bf = self.cls_("BrokenFields")
        self.assertEqual(bf["fields"], [])
        self.assertIn("fieldsError", bf)
        self.assertNotIn("x", [c["name"] for c in self.doc["classes"]])

    def test_section_stats(self):
        used = self.doc["summary"]["sectionsUsed"]
        self.assertEqual(set(used["typeInfo"]), {"typeinfo"})
        self.assertEqual(used["fields"].get("fieldinf"), 5)   # Vec3, TestEnum, GameData, WeaponData, BadOffset
        self.assertEqual(used["fields"].get("?"), 1)          # BrokenFields points outside the image

    def test_anchors_absent_in_synthetic(self):
        st = {a["type"]: a["status"] for a in self.doc["anchors"]}
        self.assertEqual(st.pop("Boolean"), "pass")  # fixture Boolean has size 1, like the real one
        self.assertTrue(all(v == "absent" for v in st.values()))


class TestEmitters(Base):
    def test_header_text(self):
        h = read(self.r["paths"]["header"])
        self.assertIn("enum class TestEnum : int32_t", h)
        self.assertIn("teNeg = -2,", h)
        # topological: parent before child, value type before user
        self.assertLess(h.index("struct DataContainer {"), h.index("struct GameData {"))
        self.assertLess(h.index("struct GameData {"), h.index("struct WeaponData {"))
        self.assertLess(h.index("struct Vec3 {"), h.index("struct WeaponData {"))
        self.assertIn("GameData base;", h)
        self.assertIn("int32_t default_;", h)
        self.assertIn("static_assert(offsetof(WeaponData, Flag) == 0x34", h)
        self.assertIn("static_assert(sizeof(WeaponData) == 0x40", h)

    def _compile(self, compiler, flags):
        h = self.r["paths"]["header"]
        src = os.path.join(self.tmp, "use.cpp")
        with open(src, "w") as fh:
            fh.write(f'#include "{h}"\nint main(){{ bc2::WeaponData w{{}}; (void)w; return 0; }}\n')
        res = subprocess.run([compiler, "-std=c++17", "-fsyntax-only", *flags, src], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stderr)

    @unittest.skipUnless(shutil.which("g++"), "g++ not installed")
    def test_header_compiles_gcc(self):
        self._compile("g++", ["-Wall", "-Wno-invalid-offsetof"])

    @unittest.skipUnless(shutil.which("clang++"), "clang++ not installed")
    def test_header_compiles_clang(self):
        self._compile("clang++", ["-Wall"])

    @unittest.skipUnless(shutil.which("g++"), "g++ not installed")
    def test_header_static_assert_catches_mismatch(self):
        h = read(self.r["paths"]["header"]).replace("== 0x34, \"WeaponData::Flag\"", "== 0x35, \"WeaponData::Flag\"")
        p = os.path.join(self.tmp, "bad.h")
        write(p, h)
        src = os.path.join(self.tmp, "bad.cpp")
        write(src, f'#include "{p}"\nint main(){{return 0;}}\n')
        res = subprocess.run(["g++", "-std=c++17", "-fsyntax-only", src], capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)

    def test_ghidra_script(self):
        text = read(self.r["paths"]["ghidra"])
        ast.parse(text)  # Python 3 syntax; template avoids f-strings for Jython 2.7
        self.assertNotRegex(text, r"\bf\"")
        blob = json.loads(base64.b64decode(re.search(r'b64decode\("([^"]+)"\)', text).group(1)))
        self.assertEqual(blob["imageBase"], BASE)
        names = [s["name"] for s in blob["structs"]]
        self.assertLess(names.index("GameData"), names.index("WeaponData"))
        labels = {l[1]: l[0] for l in blob["labels"]}
        self.assertEqual(labels["TypeInfo_WeaponData"], self.ids["WeaponData"])
        self.assertIn("TypeMeta_WeaponData", labels)
        self.assertIn("TypeFields_WeaponData", labels)
        self.assertTrue(any(k.startswith("RegisterType_WeaponData_") for k in labels))
        w = next(s for s in blob["structs"] if s["name"] == "WeaponData")
        self.assertEqual(sum(m[1] for m in w["members"]), 0x40)

    def test_reclass(self):
        with zipfile.ZipFile(self.r["paths"]["reclass"]) as z:
            root = ET.fromstring(z.read("Data.xml"))
        self.assertEqual(root.tag, "reclass")
        sizes = {"Hex8Node": 1, "Hex32Node": 4, "Int32Node": 4, "FloatNode": 4, "BoolNode": 1, "UInt32Node": 4}
        classes = {c.get("name"): c for c in root.iter("class")}
        uuids = {c.get("uuid"): c.get("name") for c in classes.values()}
        size_of = {}

        def total(name):
            if name not in size_of:
                n = 0
                for nd in classes[name]:
                    t = nd.get("type")
                    n += total(uuids[nd.get("reference")]) if t == "ClassInstanceNode" else sizes[t]
                size_of[name] = n
            return size_of[name]
        for c in self.doc["classes"]:
            self.assertEqual(total(c["cppName"]), c["size"], c["name"])

    def test_csv(self):
        rows = list(csv.DictReader(io.StringIO(read(self.r["paths"]["csv"]))))
        w = [r for r in rows if r["class"] == "WeaponData"]
        self.assertEqual([r["field"] for r in w], ["Kind", "Origin", "Owner", "Items", "Flag"])
        self.assertEqual(next(r for r in w if r["field"] == "Items")["element"], "Vec3")
        self.assertTrue(any(r["class"] == "TestEnum" and r["field"] == "teNeg" and r["offset"] == "-2" for r in rows))


class TestRobustness(unittest.TestCase):
    def test_cli_and_exit_codes(self):
        b, _ = build_fixture()
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, "a.exe")
            write(exe, b.build())
            self.assertEqual(td.main([exe, "-o", os.path.join(tmp, "o"), "--quiet"]), 0)
            self.assertEqual(td.main([exe, "-o", os.path.join(tmp, "o"), "--quiet", "--strict"]), 3)
            write(exe, b"not a pe")
            self.assertEqual(td.main([exe, "-o", os.path.join(tmp, "o"), "--quiet"]), 1)
            empty = PeBuilder()
            write(exe, empty.build())
            self.assertEqual(td.main([exe, "-o", os.path.join(tmp, "o"), "--quiet"]), 2)

    def test_static_scan_flag_runs(self):
        b, ids = build_fixture()
        d = td.Dumper(b.build(), static_scan=True)
        d.scan()
        self.assertIn(ids["WeaponData"], d.types)

    def test_store_form(self):
        b = PeBuilder()
        info = b.alloc_info()
        meta = b.meta("StoredEnum", td.FLAGS_ENUM, 4, 1, b.fields([("one", 0, 0, 1)]))
        b.text.buf += b"\xC7\x05" + struct.pack("<II", info + 4, meta)
        d = td.Dumper(b.build())
        d.scan()
        self.assertEqual(d.types[info].source, "store")
        self.assertEqual([(f.name, f.offset) for f in d.types[info].fields], [("one", 1)])

    def test_fuzz_no_crash(self):
        b, _ = build_fixture()
        img = b.build()
        rnd = random.Random(1234)
        hdr = 0x400
        for _ in range(150):
            m = bytearray(img)
            for _ in range(rnd.randint(1, 40)):
                m[rnd.randrange(hdr, len(m))] = rnd.randrange(256)
            d = td.Dumper(bytes(m))
            d.scan()
            model = td.Model(d, "fuzz.exe")
            td.emit_header(model, "0" * 64)
            td.emit_ghidra(model, "0" * 64)
            td.emit_reclass(model)
            td.emit_csv(model)
            for info in model.order:
                self.assertEqual(sum(mm.size for mm in model.layouts[info]), d.types[info].meta.size)


if __name__ == "__main__":
    unittest.main()
