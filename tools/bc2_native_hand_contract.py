"""Local-only decoder for the bounded native Standard hand capture.

Inputs can contain game bytes; they never go to stdout or the curated receipt.
Only hashes, section/slot/layout facts and explicit gaps leave this program.
This is evidence collection, not permission to hide arms or enable a replacement.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct

SOURCE_SHA = "928eccbeb0f42f2d195f2323843f5c0fc80969532aca67af742b71988d198741"
EXPORT_SHA = "e1399ffd9ef3e7b79600cf15374a0a0a39f32081d10125f614d440725ef7e49b"
VARIANT = "6a9e83c77b51f172705b1f17cecb6f5d0068e1e53f0a2e8601b1eb53e0ab7d62"
SECTIONS = ("ArmStandard", "MOLLE", "Gloves", "ArmStandard_ZOnly")
SIGNATURES = (
    (4794, 48, 0xb1b4919cd9ced567, 0xbf112b93fc653fe0),
    (1020, 48, 0x2fd3dcfd7c41a964, 0x3c7d7479fd286159),
    (10164, 48, 0xa95276bf7113e308, 0x801c7a6b7e9e5d8e),
    (15978, 16, 0x8fc17b721610ba0a, 0x73922dde79914067),
)
OWNER_KEYS = ("world", "request", "actor", "weak", "weapon", "owner", "equipment", "space", "rig_pose", "input", "packed_hash", "frame")
PACK_KEYS = ("actor", "weak", "weapon", "owner", "equipment", "space", "rig_pose", "input")
FORMAT_BYTES = {2: 16, 6: 12, 10: 8, 11: 8, 12: 8, 16: 8, 28: 4, 30: 4, 34: 4, 35: 4, 37: 4, 41: 4, 42: 4, 49: 2, 50: 2, 54: 2, 57: 2, 61: 1, 62: 1}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fnv(data: bytes) -> int:
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value


def bounded(path: Path, cap: int) -> bytes:
    if not path.is_file() or path.stat().st_size > cap:
        raise ValueError("missing or oversized local input")
    return path.read_bytes()


def captured(folder: Path, name: str, cap: int) -> bytes:
    if not name:
        return b""
    # No path traversal/symlink escape from a malformed capture manifest.
    file = (folder / name).resolve()
    if file.parent != folder.resolve():
        raise ValueError("capture reference leaves local folder")
    return bounded(file, cap)


def load_pinned(path: Path, digest: str) -> dict:
    data = bounded(path, 16 * 1024 * 1024)
    if sha(data) != digest:
        raise ValueError("private input provenance mismatch")
    return json.loads(data)


def dds_top(path: Path) -> dict:
    data = bounded(path, 32 * 1024 * 1024)
    if len(data) < 128 or data[:4] != b"DDS " or struct.unpack_from("<I", data, 4)[0] != 124:
        raise ValueError("invalid local DDS")
    height, width = struct.unpack_from("<II", data, 12)
    fourcc, offset = data[84:88], 128
    formats = {b"DXT1": 71, b"DXT3": 74, b"DXT5": 77, b"ATI1": 80, b"BC4U": 80, b"ATI2": 83, b"BC5U": 83}
    if fourcc == b"DX10":
        if len(data) < 148:
            raise ValueError("short DDS extension")
        fmt, dimension, flags, array = struct.unpack_from("<IIII", data, 128)
        if dimension != 3 or array != 1 or flags & 4:
            raise ValueError("unsupported DDS texture view")
        offset = 148
    else:
        fmt = formats.get(fourcc)
        if fmt is None:
            # Only the exact documented RGBA/BGRA masks; no guessed channel order.
            bits, red, green, blue, alpha = struct.unpack_from("<IIIII", data, 88)
            if bits != 32 or (green, alpha) != (0x0000ff00, 0xff000000):
                raise ValueError("unsupported DDS layout")
            if (red, blue) == (0x000000ff, 0x00ff0000):
                fmt = 28
            elif (red, blue) == (0x00ff0000, 0x000000ff):
                fmt = 87
            else:
                raise ValueError("unsupported DDS masks")
    if not 0 < width <= 2048 or not 0 < height <= 2048:
        raise ValueError("unsupported DDS dimensions")
    if fmt in (70, 71, 72, 79, 80, 81):
        size = ((width + 3) // 4) * ((height + 3) // 4) * 8
    elif fmt in (73, 74, 75, 76, 77, 78, 82, 83, 84, 94, 95, 96, 97, 98, 99):
        size = ((width + 3) // 4) * ((height + 3) // 4) * 16
    elif fmt in (28, 29, 87, 91):
        size = width * height * 4
    else:
        raise ValueError("unsupported DDS format")
    if len(data) - offset < size:
        raise ValueError("truncated DDS top mip")
    return {"resource_sha256": sha(data), "top_mip_sha256": sha(data[offset:offset + size]), "width": width, "height": height, "format": fmt}


def format_family(fmt: int) -> int:
    for family in ((70, 71, 72), (73, 74, 75), (76, 77, 78), (79, 80, 81), (82, 83, 84), (94, 95, 96), (97, 98, 99), (28, 29), (87, 91)):
        if fmt in family:
            return family[0]
    return fmt


def geometry(folder: Path, draw: dict, section: int) -> tuple[bool, list[int], bytes]:
    bindings = {b["binding"]: b for b in draw["buffers"]}
    vertices = captured(folder, bindings[0]["file"], 2 * 1024 * 1024)
    indices = captured(folder, bindings[1]["file"], 400000)
    count, stride, skin, positions = SIGNATURES[section]
    if draw["count"] != count or draw["stride"] != stride or draw["index_bytes"] not in (2, 4) or len(indices) != count * draw["index_bytes"]:
        return False, [], vertices
    code = "H" if draw["index_bytes"] == 2 else "I"
    offsets, skin_bytes, position_bytes = [], bytearray(), bytearray()
    for index, in struct.iter_unpack("<" + code, indices):
        vertex = index + draw["base_vertex"]
        at = draw["vertex_offset"] + vertex * stride
        if vertex < 0 or at < 0 or at + stride > len(vertices):
            return False, [], vertices
        offsets.append(at)
        skin_bytes.extend(vertices[at:at + 16])
        position_bytes.extend(vertices[at:at + 8])
    return fnv(skin_bytes) == skin and fnv(position_bytes) == positions, offsets, vertices


def archive_section(source: dict, section: int) -> dict:
    meshes = [m for m in source["meshes"] if m["sha256"] == VARIANT]
    if len(meshes) != 1:
        raise ValueError("native Standard archive variant mismatch")
    sections = meshes[0]["lods"][0]["sections"]
    matches = [s for s in sections if s["layout"]["name"].endswith("Shape_" + SECTIONS[section])]
    if len(matches) != 1:
        raise ValueError("native archive section is missing/ambiguous")
    return matches[0]


def attributes_match(source: dict, section: int, offsets: list[int], vertices: bytes, stride: int) -> bool:
    native_records = set()
    for vertex in archive_section(source, section)["vertices"]:
        raw = vertex["raw"]
        if not isinstance(raw, str) or len(raw) != stride * 2:
            return False
        try:
            record = bytes.fromhex(raw)
        except ValueError:
            return False
        if len(record) != stride:
            return False
        native_records.add(record)
    return bool(offsets) and all(vertices[at:at + stride] in native_records for at in offsets)


def palette_facts(folder: Path, draw: dict, source: dict, section: int, packs: list[dict], frozen: dict) -> list[dict]:
    names = source["skeleton"]["names"]
    palette = archive_section(source, section)["layout"]["bone_names"]
    if len(set(names)) != 147 or not palette or any(n not in names for n in palette):
        raise ValueError("native archive palette name mapping malformed")
    matches = []
    bindings = [{**b, "stage": "VS_CB", "slot": b["binding"] - 2} for b in draw["buffers"] if b["binding"] >= 2]
    bindings += [{**b, "stage": "VS_SRV"} for b in draw["srvs"] if b["stage"] == 0]
    for pack in packs:
        if not pack["paired"] or pack["hash"] != frozen["packed_hash"] or any(pack.get(k) != frozen[k] for k in PACK_KEYS):
            continue
        packed = captured(folder, pack["file"], 147 * 48)
        if len(packed) != 147 * 48 or fnv(packed) != pack["hash"]:
            continue
        ordered = b"".join(packed[names.index(name) * 48:(names.index(name) + 1) * 48] for name in palette)
        for binding in bindings:
            data = captured(folder, binding["file"], 128 * 1024)
            if not data:
                continue
            start = data.find(ordered)
            while start >= 0:
                # Matching bytes alone remain correspondence. The observed SDK
                # upload must preserve the exact paired full source at this draw.
                exact_upload = any(u["committed"] and u["resource"] == binding["resource"] and u["serial"] == pack["serial"] and
                    u["hash"] == pack["hash"] and u["bytes"] == len(packed) and data[u["offset"]:u["offset"] + u["bytes"]] == packed for u in draw["uploads"])
                matches.append({"stage": binding["stage"], "slot": binding["slot"], "offset": start, "bones": len(palette),
                    "ordered_palette_sha256": sha(ordered), "exact_packed_upload": exact_upload})
                if len(matches) >= 32:
                    return matches
                start = data.find(ordered, start + 1)
    return matches


def layout_facts(draw: dict, layouts: dict[int, dict], shaders: dict[int, dict]) -> dict:
    layout, vertex_shader = layouts.get(draw["layout"]), shaders.get(draw["vs"])
    if not layout or not vertex_shader or not vertex_shader["reflection"].get("reflection_complete"):
        return {"creation_observed": False, "reason": "layout or VS existed before registrar / creation budget exhausted", "uv_candidates": [], "normal_candidates": []}
    signature = {(i["semantic"].upper(), i["index"]) for i in vertex_shader["reflection"].get("inputs", [])}
    next_offset, candidates = {}, []
    for element in layout["elements"]:
        slot = element["slot"]
        if element["format"] not in FORMAT_BYTES:
            return {"creation_observed": True, "reason": "unsupported exact input format", "uv_candidates": [], "normal_candidates": []}
        offset = next_offset.get(slot, 0) if element["offset"] == 0xffffffff else element["offset"]
        next_offset[slot] = offset + FORMAT_BYTES[element["format"]]
        if slot == 0 and element["classification"] == 0 and element["step"] == 0 and next_offset[slot] <= draw["stride"] and (element["semantic"].upper(), element["index"]) in signature:
            candidates.append({"semantic": element["semantic"], "index": element["index"], "format": element["format"], "offset": offset,
                "native_tail_half_offset": (offset - 16) // 2 if offset >= 16 and offset % 2 == 0 else None})
    return {"creation_observed": True, "vs_sha256": vertex_shader["reflection"]["sha256"],
        "uv_candidates": [e for e in candidates if e["semantic"].upper() == "TEXCOORD"],
        "normal_candidates": [e for e in candidates if e["semantic"].upper() == "NORMAL"],
        "vs_transform_interpretation_verified": False}


def material_facts(folder: Path, draw: dict, shaders: dict[int, dict], textures: dict[str, dict]) -> dict:
    pixel = shaders.get(draw["ps"])
    resources = pixel["reflection"].get("resources", []) if pixel else []
    matches = []
    for srv in draw["srvs"]:
        if srv["stage"] != 1 or not srv["done"] or srv["mip"] != 0:
            continue
        digest = sha(captured(folder, srv["file"], 4 * 1024 * 1024))
        for label, texture in textures.items():
            if digest == texture["top_mip_sha256"] and (srv["width"], srv["height"], format_family(srv["format"])) == (texture["width"], texture["height"], format_family(texture["format"])):
                declared = [r["name"] for r in resources if r["type"] == 2 and r["slot"] <= srv["slot"] < r["slot"] + r["count"]]
                matches.append({"resource": label, "resource_sha256": texture["resource_sha256"], "top_mip_sha256": digest, "ps_slot": srv["slot"], "srv_format": srv["format"], "reflected_texture_names": declared})
    return {"ps_creation_observed": bool(pixel), "ps_reflection_complete": bool(pixel and pixel["reflection"].get("reflection_complete")),
        "ps_sha256": pixel["reflection"].get("sha256") if pixel else None, "texture_matches": matches, "uv_sample_interpretation_verified": False}


def decode(args: argparse.Namespace) -> dict:
    folder = args.capture.resolve()
    manifest_bytes = bounded(folder / "metadata.json", 4 * 1024 * 1024)
    manifest = json.loads(manifest_bytes)
    if manifest.get("schema") != "bc2.private.native_hand_draw_capture.v1" or not manifest.get("read_only"):
        raise ValueError("unsupported capture schema")
    if len(manifest.get("draws", [])) > 32 or len(manifest.get("packs", [])) > 16 or len(manifest.get("layouts", [])) > 64 or len(manifest.get("shaders", [])) > 96:
        raise ValueError("capture manifest exceeds bounded source schema")
    source = load_pinned(args.native_input, SOURCE_SHA)
    load_pinned(args.export, EXPORT_SHA)
    if len(source["skeleton"]["names"]) != 147:
        raise ValueError("wrong original skeleton")
    textures = {}
    for value in args.dds:
        label, path = value.split("=", 1)
        if not label or len(label) > 128 or label in textures:
            raise ValueError("invalid/duplicate DDS provenance label")
        textures[label] = dds_top(Path(path))
    frozen = manifest.get("frozen")
    facts, gaps = [], []
    inventory_complete = False
    eye_binding_facts = []
    if not frozen or any(not frozen.get(k) for k in OWNER_KEYS):
        gaps.append("no exact current player/request/rig/pair frozen")
    else:
        layouts = {l["id"]: l for l in manifest["layouts"]}
        shaders = {s["id"]: s for s in manifest["shaders"]}
        for draw in manifest["draws"]:
            section = draw["section"]
            if not draw["geometry_unique"] or section not in range(4) or draw["eye"] not in (0, 1) or any(draw.get(k) != frozen[k] for k in OWNER_KEYS):
                continue
            exact_geometry, offsets, vertices = geometry(folder, draw, section)
            instance_supported = (draw.get("api_observed") is True and
                draw.get("instances") == 1 and draw.get("start_instance") == 0 and
                draw.get("instance_transform_contract_supported") is True)
            if not instance_supported:
                gaps.append("draw API/instance transform is missing or unsupported; geometry bytes alone do not establish its native placement")
            facts.append({"section": SECTIONS[section], "eye": draw["eye"], "geometry_signature_verified": exact_geometry,
                "instance_transform_contract_supported": instance_supported,
                "indexed_instanced": draw.get("indexed_instanced"),
                "archive_vertex_attributes_exact": attributes_match(source, section, offsets, vertices, draw["stride"]),
                "original_returned": draw["original_returned"], "color_target_present": bool(draw["target"]), "depth_target_present": bool(draw["depth"]),
                "depth_write": draw["depth_write"], "palette": palette_facts(folder, draw, source, section, manifest["packs"], frozen),
                "layout": layout_facts(draw, layouts, shaders), "material": material_facts(folder, draw, shaders, textures)})
        present = {(f["section"], f["eye"]) for f in facts if f["geometry_signature_verified"] and f["original_returned"] and f["instance_transform_contract_supported"]}
        views = [{d["view"] for d in manifest["draws"] if d["eye"] == eye and d["geometry_unique"] and all(d.get(k) == frozen[k] for k in OWNER_KEYS)} for eye in (0, 1)]
        distinct_views = len(views[0]) == len(views[1]) == 1 and views[0].isdisjoint(views[1])
        inventory_complete = len(present) == 8 and distinct_views
        for eye in (0, 1):
            selected = [d for d in manifest["draws"] if d["eye"] == eye and d["geometry_unique"] and all(d.get(k) == frozen[k] for k in OWNER_KEYS)]
            color = {(d["target"], d["depth"]) for d in selected if d["section"] in (0, 1, 2)}
            depth = {d["depth"] for d in selected if d["section"] == 3}
            associated = len(color) == 1 and len(depth) == 1 and next(iter(color))[1] in depth and all(t and z for t, z in color)
            eye_binding_facts.append({"eye": eye, "color_depth_same_binding": associated,
                "color_binding_sha256": [sha(json.dumps(pair).encode()) for pair in sorted(color)],
                "depth_binding_sha256": [sha(str(value).encode()) for value in sorted(depth)]})
            if not associated:
                gaps.append(f"color/depth target association is missing or differs in eye{eye}; no whole-arm substitution proof")
        for eye in (0, 1):
            for section in SECTIONS:
                if (section, eye) not in present:
                    gaps.append(f"missing original exact geometry {section} eye{eye}")
        if any(not f["palette"] or not any(p["exact_packed_upload"] for p in f["palette"]) for f in facts):
            gaps.append("ordered palette correspondence lacks direct exact packed destination upload; intermediate native remap/copy remains unverified")
        glove = [f for f in facts if f["section"] == "Gloves"]
        if not glove or any(not f["layout"].get("creation_observed") for f in glove):
            gaps.append("late injection missed glove input layout/VS creation; level transition may reuse cached objects")
        if not glove or any(not f["material"]["ps_reflection_complete"] or not f["material"]["texture_matches"] for f in glove):
            gaps.append("actual glove PS reflection or archive texture binding unavailable")
    gaps.extend(("interpret native VS normal/UV conversion and PS sampled UV locally before producing asset-reader flags", "cap UV/normal/material choice and loader remain unimplemented", "same-frame bilateral replacement plus original color/depth removal has not executed"))
    return {"schema": "bc2.curated.native_hand_draw_contract.v1", "capture_sha256": sha(manifest_bytes), "source_sha256": SOURCE_SHA, "export_sha256": EXPORT_SHA,
        "frozen_identity_sha256": sha(json.dumps(frozen, sort_keys=True).encode()) if frozen else None, "native_rig_bones": 147,
        "native_attribute_semantics_verified": False, "material_to_texture_mapping_verified": False, "native_draw_ownership_verified": False,
        "same_frame_original_inventory_complete": inventory_complete, "eye_bindings": eye_binding_facts,
        "replacement_draw_active": False, "runtime_ready": False, "facts": facts, "gaps": sorted(set(gaps)),
        "capture_counters": {key: manifest.get(key, 0) for key in ("errors", "busy", "rejected")}}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", required=True, type=Path)
    parser.add_argument("--native-input", required=True, type=Path)
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--dds", action="append", default=[], metavar="RESOURCE=LOCAL_DDS")
    parser.add_argument("--receipt", required=True, type=Path)
    args = parser.parse_args()
    try:
        receipt = decode(args)
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    except (OSError, ValueError, KeyError, TypeError, struct.error):
        # Exception text can contain paths or payload fragments: keep it local.
        raise SystemExit("Local decode incomplete: input/schema/provenance gap; no verified contract emitted.")
    print(json.dumps({"runtime_ready": False, "facts": len(receipt["facts"]), "gaps": len(receipt["gaps"])}))


if __name__ == "__main__":
    main()
