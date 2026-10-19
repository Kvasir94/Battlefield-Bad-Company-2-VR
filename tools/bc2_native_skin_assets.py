"""Build private BC2NSK01/02/03 native skin packs; never distribute game assets.

The renderer consumes fixed little-endian records and original section-local
bone IDs. Existing corner tails round-trip exactly. Missing cap attributes are
not guessed: without a separately verified cap contract the pack is diagnostic
(header flags=0) and must not be admitted by the runtime reader.

V2 keeps the 128-byte header and 320-byte record, supports 1..8 records.
V3 records explicit layout kind at record byte56: 0=half48/16,
1=NVA float64/32, 2=Beach half64/16. Indexed depth corners concatenate
color replacements by original section order, preserving native materials.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

MAGIC = b"BC2NSK01"
HEADER = 128
RECORD = 320
MAX_FILE = 8 * 1024 * 1024
RIG = 0xA7F219A1426216AB


def need(value, why):
    if not value:
        raise ValueError(why)


def digest(data):
    return hashlib.sha256(data).digest()


def read_json(path, limit=MAX_FILE):
    raw = Path(path).read_bytes()
    need(len(raw) <= limit, "input exceeds byte bound")
    return json.loads(raw), raw


def unit(v):
    need(len(v) == 3 and all(math.isfinite(x) for x in v), "finite vector")
    length = math.sqrt(sum(x*x for x in v))
    need(length > 1e-8, "zero vector")
    return [x/length for x in v]


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def palette(section, names, kind=0):
    """Recover LOCAL order, never use geometry()'s hash-sorted bone_names."""
    result = {}
    count = len(section['layout']['palette'])
    need(0 < count <= 64, "palette bound")
    for vertex in section['vertices']:
        raw = bytes.fromhex(vertex['raw'])
        need(len(raw) == section['layout']['stride'], "source record size")
        at = 12 if kind==1 else 8
        active = [(i, w) for i, w in zip(raw[at:at+4], raw[at+4:at+8]) if w]
        need(sum(raw[at+4:at+8]) == 255, "source normalized byte weights")
        need(len(active) == len(vertex['weights']), "source weight correspondence")
        for (index, byte), (name, weight) in zip(active, vertex['weights'].items()):
            need(index < count and name in names and abs(byte/255-weight) < 1e-12,
                 "source decoded weight correspondence")
            need(index not in result or result[index] == name, "ambiguous local bone")
            result[index] = name
    return result


def cap_contract(path):
    if not path:
        return None
    contract, raw = read_json(path, 65536)
    need(contract.get('schema') in ('bc2.native_cap_attributes.v1','bc2.native_cap_contract.v2'), "cap schema")
    need(contract.get('native_layout_verified') is True and
         contract.get('shader_semantics_verified') is True and
         isinstance(contract.get('evidence'), str) and len(contract['evidence']) >= 16,
         "cap requires recorded native layout and shader proof")
    if contract.get('schema')=='bc2.native_cap_contract.v2':
        kind = {'nva_float3_explicit_frame64':1,'beach_half4_implicit_frame64':2}.get(contract.get('layout_kind'))
        need(kind is not None, 'native explicit layout kind')
        contract['layout_kind'] = kind
        contract['stride'] = contract['color_stride']
        contract['uv'] = [[.5,.5] for _ in contract['uv_offsets']]
        need(contract['bone_index_offset']==(12 if kind==1 else 8) and
             contract['bone_weight_offset']==(16 if kind==1 else 12) and
             contract['depth_stride']==(32 if kind==1 else 16), 'native skin offsets')
        if kind==1:
            need(contract['unconsumed_half4']==[{'offset':20,'value':[1,1,1,1]}], 'NVA unused channel')
            contract['vertex_color_offset']=20
        else:
            need(contract['stride']==64 and contract['normal_offset']==24 and contract['tangent_offset']==32 and
                 contract['uv_offsets']==[40,44,48] and contract['reserved_offset']==52 and
                 contract['position_w']==1 and contract['stored_tangent_sign']==-1 and
                 contract['normal_w']==contract['tangent_w']==0, 'Beach shader-proved contract')
            contract['sha256']=digest(raw).hex()
            return contract
    if contract.get('layout_kind') == 1:
        need(contract.get('stride')==64 and contract.get('normal_offset')==28 and
             contract.get('bitangent_offset')==36 and contract.get('tangent_offset')==44 and
             contract.get('uv_offsets')==[52,56] and contract.get('reserved_offset')==60 and
             contract.get('vertex_color_offset')==20, 'NVA shader-proved layout')
        need(contract.get('stored_tangent_sign')==-1 and contract.get('normal_w')==0 and
             contract.get('tangent_w')==0, 'NVA frame convention')
        contract['sha256'] = digest(raw).hex()
        return contract
    need(contract.get('stride') == 48 and
         sorted([contract.get('normal_offset'), contract.get('tangent_offset')]) == [16, 24],
         "cap normal/tangent field contract")
    need(contract.get('uv_offsets') == [32, 36, 40] and
         contract.get('reserved_offset') == 44, "cap remaining field contract")
    need(contract.get('normal_w') in (-1, 0, 1) and contract.get('tangent_w') in (-1, 0, 1),
         "cap unused normal/tangent channels")
    need(contract.get('position_w') in (-1, 1), 'cap packed position sign')
    need(contract.get('stored_tangent_sign') in (-1, 1), 'cap shader tangent convention')
    need(contract.get('material_policy') == 'native_glove_constant_uv', "cap material policy")
    uv = contract.get('uv')
    need(isinstance(uv, list) and len(uv) == 3 and all(len(p) == 2 and
         all(math.isfinite(x) and 0 <= x <= 1 for x in p) for p in uv), "cap UVs")
    contract['sha256'] = digest(raw).hex()
    return contract


def cap_tail(triangle, contract, corner=None):
    if contract is None:
        return bytes(32)  # Explicitly unrenderable diagnostic payload (flags=0).
    normal = unit(corner.get('cap_normal', triangle['geometric_normal']) if corner else triangle['geometric_normal'])
    axis = [0., 0., 0.]
    axis[min(range(3), key=lambda i: abs(normal[i]))] = 1.
    actual_tangent = unit(cross(axis, normal))
    tangent = [x*contract['stored_tangent_sign'] for x in actual_tangent]
    start = 20 if contract.get('layout_kind')==1 else 16
    result = bytearray(contract['stride']-start)
    if contract.get('layout_kind')==1:
        struct.pack_into('<4e', result, 0, 1.,1.,1.,1.)
        struct.pack_into('<4e', result, contract['bitangent_offset']-start, *cross(normal,actual_tangent), 0.)
    elif contract.get('layout_kind')==2:
        control = triangle['cap_material_control']
        need(len(control)==4 and all(math.isfinite(x) for x in control), 'Beach material control')
        struct.pack_into('<4e', result, 0, *control)
    struct.pack_into('<4e', result, contract['normal_offset']-start,
                     *normal, contract['normal_w'])
    struct.pack_into('<4e', result, contract['tangent_offset']-start,
                     *tangent, contract['tangent_w'])
    # Cap UVs form one coherent authored cuff tuple. Original glove corners
    # never use these synthesized attributes.
    cap_uv = triangle.get('cap_uv', contract['uv'])
    need(len(cap_uv)==len(contract['uv_offsets']) and all(len(uv)==2 and all(math.isfinite(x) and abs(x) <= 65504 for x in uv)
                              for uv in cap_uv), 'cap atlas coordinates')
    for offset, uv in zip(contract['uv_offsets'], cap_uv):
        struct.pack_into('<2e', result, offset-start, *uv)
    return bytes(result)


def select_mesh(source, target_mesh):
    aliases = {'standard': 'US_FP_ArmsStandard_Mesh.res', 'assault': 'US_FP_Assault_Mesh.res'}
    key = aliases.get(target_mesh, target_mesh).casefold()
    matches = [m for m in source['meshes'] if m['resource'].casefold() == key or
               m['resource'].rsplit('/', 1)[-1].casefold() == key]
    need(len(matches)==1, 'unique native mesh resource')
    return matches[0]


def layout_kind(mesh):
    if mesh['resource'].endswith('/NVA_Recon_FP_Mesh.res'):
        return 1
    if mesh['resource'].endswith('/1PUs_Infantry_Mesh.res'):
        return 2
    return 0


def section_roles(sections):
    need(1 <= len(sections) <= 8, 'bounded native section count')
    color = [i for i,s in enumerate(sections) if s['layout']['name'].endswith(('_Gloves', '_Fingers','_FirstP_Hands')) and s['layout']['stride'] in (48,64)]
    if not color and sections[0]['layout']['name']=='FPShape_US_Assault_FP_Main':
        color = [0, 1]  # Native capped skin plus authored open hand-bandage overlay.
    if not color and sections[0]['layout']['name']=='NVA_1pArmsShape_NVA_Recon_FP_Main':
        color = [0]  # Capped bare hands; proximal equipment straps suppressed.
    depth = [i for i,s in enumerate(sections) if s['layout']['stride'] in (16,32) and s['layout']['name'].endswith('_ZOnly')]
    need(1 <= len(color) <= 4 and len(depth)==1, 'native hand color and depth sections')
    return color, depth[0]


def hand_section(sections, color_parts):
    vertices, triangles, parts, vertex_parts = [], [], [], []
    for part in color_parts:
        section = sections[part]
        base = len(vertices)
        vertices.extend(section['vertices'])
        vertex_parts.extend([part]*len(section['vertices']))
        triangles.extend([[base+i for i in t] for t in section['triangles']])
        parts.extend([part]*len(section['triangles']))
    return dict(vertices=vertices, triangles=triangles, parts=parts, vertex_parts=vertex_parts)


def native_export(source, target_mesh, lod_index=0):
    """Cap this variant's own glove; retain exact per-corner native attributes."""
    mesh = select_mesh(source, target_mesh)
    kind = layout_kind(mesh)
    prefix_bytes = 12 if kind==1 else 8
    tail_start = prefix_bytes+8
    def native_tail(i):
        raw = bytes.fromhex(section['vertices'][i]['raw'])[tail_start:]
        return list(struct.unpack('<'+'e'*(len(raw)//2),raw))
    need(0 <= lod_index < len(mesh['lods']), 'native LOD index')
    sections = mesh['lods'][lod_index]['sections']
    color_parts, _ = section_roles(sections)
    section = hand_section(sections, color_parts)
    vietnam_us = mesh['resource'].endswith('/US_Assault_FP_Mesh.res')
    sk = source['skeleton']
    hands = []
    for side in ('Left', 'Right'):
        wrist = side+'Hand'
        wp = sk['world'][sk['names'].index(wrist)][12:15]
        fp = sk['world'][sk['names'].index(side+'ForeArm')][12:15]
        axis = unit([a-b for a,b in zip(wp,fp)])
        vertices, triangles, welded, source_ids = [], [], {}, {}
        for i, v in enumerate(section['vertices']):
            if sum(w for n, w in v['weights'].items() if n.startswith(wrist)) <= 0:
                continue
            if sum((x-y)*a for x,y,a in zip(v['p'],wp,axis)) < -.015:
                continue
            need(not any(n.startswith('Right' if side == 'Left' else 'Left') for n in v['weights']),
                 'mixed-side native skin')
            weights = {n:w for n,w in v['weights'].items() if n.startswith(wrist)}
            proximal = sum(w for n,w in v['weights'].items() if not n.startswith(wrist))
            if proximal:
                weights[wrist] = weights.get(wrist, 0)+proximal
            key = tuple(v['p'])
            if vietnam_us:
                key = (key, section['vertex_parts'][i])
            if key not in welded:
                welded[key] = len(vertices)
                vertices.append(dict(position=v['p'], weights=weights, native_source_vertex=i))
            need(vietnam_us or vertices[welded[key]]['weights'] == weights, 'coincident native skin mismatch')
            source_ids[i] = welded[key]
        for source_part, tri in zip(section['parts'], section['triangles']):
            if vietnam_us and (source_part != 0 or not all(sum(w for n,w in section['vertices'][i]['weights'].items()
                                               if n.startswith(wrist)) >= .25 for i in tri)):
                continue
            if not all(i in source_ids for i in tri):
                continue
            if len({source_ids[i] for i in tri}) != 3:
                continue  # Native zero-area seam triangles have no rendered surface.
            triangles.append(dict(material='native_glove', corners=[dict(vertex=source_ids[i], native_position_hex=section['vertices'][i]['raw'][:prefix_bytes*2],
                native_tail_half=native_tail(i)) for i in tri]))
            if len(color_parts)>1:
                triangles[-1]['native_part'] = source_part
            if vietnam_us:
                for corner,i in zip(triangles[-1]['corners'],tri):
                    corner['native_source_vertex'] = i
        edges = {}
        for tri in triangles:
            ids = [c['vertex'] for c in tri['corners']]
            for a,b in zip(ids, ids[1:]+ids[:1]):
                edges.setdefault(tuple(sorted((a,b))), []).append((a,b))
        need(all(len(v) in (1,2) for v in edges.values()), 'native manifold edge incidence')
        boundary = [v[0] for v in edges.values() if len(v)==1]
        need(3 <= len(boundary) <= 64, 'bounded native cuff')
        # Reverse boundary traversal to give new faces opposite shared edges.
        following = {b:a for a,b in boundary}
        need(len(following)==len(boundary), 'simple cuff boundary')
        ring = [boundary[0][0]]
        while following[ring[-1]] != ring[0]:
            ring.append(following[ring[-1]])
            need(len(ring)<=len(boundary), 'cuff loop')
        need(len(ring)==len(boundary), 'one cuff per hand')
        cuff_tails = [native_tail(vertices[i]['native_source_vertex']) for i in ring]
        uv_start, uv_count = (16,2) if kind==1 else ((12,3) if kind==2 else (8,3))
        # Choose one actual native tuple. Averaging independent atlas islands
        # can land on unrelated color/material-mask pixels.
        representative = min(cuff_tails, key=lambda t:sum((t[j]-u[j])**2 for u in cuff_tails
                              for j in range(uv_start,uv_start+2*uv_count)))
        cap_uv = [[representative[uv_start+2*j+k] for k in range(2)] for j in range(uv_count)]
        center = [sum(vertices[i]['position'][k] for i in ring)/len(ring) for k in range(3)]
        wp = sk['world'][sk['names'].index(wrist)][12:15]
        fp = sk['world'][sk['names'].index(side+'ForeArm')][12:15]
        axis = unit([a-b for a,b in zip(wp,fp)])
        inner = []
        for i in ring:
            inner.append(len(vertices))
            vertices.append(dict(position=[center[k]+.90*(vertices[i]['position'][k]-center[k])+.008*axis[k] for k in range(3)],
                                 weights={wrist:1.}, native_source_vertex=-1))
        ci = len(vertices)
        vertices.append(dict(position=[center[k]+.008*axis[k] for k in range(3)], weights={wrist:1.}, native_source_vertex=-1))
        def cap(ids):
            a,b,c = [vertices[i]['position'] for i in ids]
            normal = unit(cross([b[k]-a[k] for k in range(3)], [c[k]-a[k] for k in range(3)]))
            triangles.append(dict(material='authored_wrist_cap', geometric_normal=normal, cap_uv=cap_uv,
                                  corners=[dict(vertex=i,native_tail_half=None) for i in ids]))
            if kind==2:
                triangles[-1]['cap_material_control'] = representative[:4]
            if len(color_parts)>1:
                triangles[-1]['native_part'] = color_parts[0]
        for n,a in enumerate(ring):
            j=(n+1)%len(ring)
            cap([a,ring[j],inner[j]])
            cap([a,inner[j],inner[n]])
            cap([inner[n],inner[j],ci])
        cap_normals = {}
        for triangle in triangles:
            if triangle['material'] != 'authored_wrist_cap':
                continue
            ids = [c['vertex'] for c in triangle['corners']]
            a,b,c = [vertices[i]['position'] for i in ids]
            area_normal = cross([b[k]-a[k] for k in range(3)], [c[k]-a[k] for k in range(3)])
            for i in ids:
                total = cap_normals.setdefault(i,[0.,0.,0.])
                for k in range(3): total[k] += area_normal[k]
        for triangle in triangles:
            if triangle['material']=='authored_wrist_cap':
                for corner in triangle['corners']:
                    corner['cap_normal'] = unit(cap_normals[corner['vertex']])
        if vietnam_us:
            # Preserve the existing left-hand bandage as an open surface over
            # the independently closed main hand. It is not a body cap.
            for source_part, tri in zip(section['parts'], section['triangles']):
                if source_part != 1 or not all(i in source_ids for i in tri):
                    continue
                if len({source_ids[i] for i in tri}) != 3:
                    continue
                need(all(i in source_ids for i in tri), 'cross-hand accessory triangle')
                need(all(sum(w for n,w in section['vertices'][i]['weights'].items() if n.startswith(wrist))>0 for i in tri),
                     'native accessory outside hand influence')
                triangles.append(dict(material='native_glove', native_part=source_part, native_overlay=True,
                    corners=[dict(vertex=source_ids[i], native_source_vertex=i, native_position_hex=section['vertices'][i]['raw'][:16],
                        native_tail_half=list(struct.unpack('<16e',bytes.fromhex(section['vertices'][i]['raw'])[16:]))) for i in tri]))
        hands.append(dict(wrist=wrist, vertices=vertices, triangles=triangles))
    result = dict(schema='bc2.private.hand_render_input.v1', rig=sk, hands=hands,
                  geometry_source_resource=mesh['resource'])
    result['wrist_boundary_policy'] = 'retain native triangles wholly distal to wrist-axis -0.015m; cap resulting native edge loop'
    if lod_index:
        result['geometry_source_lod'] = lod_index
    return result


def prepare(source, exported, contract, target_mesh='standard', lod_index=0):
    sk = source['skeleton']
    need(sk['bone_count'] == 147 and sk['rig_fingerprint'] == f'fnv1a64:{RIG:016x}',
         'original 147-bone rig identity')
    for key in ('names', 'parents', 'world', 'inverse_bind'):
        need(exported['rig'][key] == sk[key], 'original rig '+key)
    names = sk['names']
    need(len(names) == 147 and len(set(names)) == 147, 'bone names')
    source_mesh = next(m for m in source['meshes'] if m['resource'] ==
                       exported.get('geometry_source_resource', source['meshes'][0]['resource']))
    mesh = select_mesh(source, target_mesh)
    kind = layout_kind(mesh)
    prefix_bytes = 12 if kind==1 else 8
    tail_start = prefix_bytes+8
    need(not kind or contract is not None and contract.get('layout_kind')==kind, 'matching verified native layout contract')
    need(0 <= lod_index < len(mesh['lods']), 'native LOD index')
    sections = mesh['lods'][lod_index]['sections']
    color_parts, depth_part = section_roles(sections)
    pals, observed, global_names = [], [], {}
    for i, section in enumerate(sections):
        layout = section['layout']
        need(layout['stride'] == ((32 if kind==1 else 16) if i == depth_part else (64 if kind else 48)),
             'native section identity/layout')
        need(len(section['vertices']) == layout['vertices'] <= 65535 and
             len(section['triangles']) == layout['triangles'] <= 200000, 'source section counts')
        mapping = palette(section, names, kind)
        observed.append(mapping)
        for local_index, name in mapping.items():
            mesh_id = layout['palette'][local_index]
            need(mesh_id not in global_names or global_names[mesh_id] == name, 'cross-section palette identity')
            global_names[mesh_id] = name
    for section, mapping in zip(sections, observed):
        ids = section['layout']['palette']
        need(all(i in global_names for i in ids), 'incomplete mesh palette evidence')
        pals.append([global_names[i] for i in ids])
    hands = exported['hands']
    need(len(hands) == 2 and {h['wrist'] for h in hands} == {'LeftHand', 'RightHand'}, 'bilateral hands')
    for hand in hands:
        edges, volume = {}, 0.
        for triangle in hand['triangles']:
            ids = [c['vertex'] for c in triangle['corners']]
            need(len(ids) == 3 and len(set(ids)) == 3 and all(isinstance(i, int) and
                 0 <= i < len(hand['vertices']) for i in ids), 'triangle vertex indices')
            fmt = '<3f' if kind==1 else '<3e'
            points = [struct.unpack(fmt, struct.pack(fmt, *hand['vertices'][i]['position'])) for i in ids]
            a, b, c = points
            normal = cross([b[k]-a[k] for k in range(3)], [c[k]-a[k] for k in range(3)])
            need(sum(x*x for x in normal) > 1e-20, 'half-position triangle degenerates')
            if triangle.get('native_overlay'):
                need(source_mesh['resource'].endswith('/US_Assault_FP_Mesh.res') and triangle.get('native_part')==1,
                     'identified native hand accessory')
                continue
            volume += sum(a[k]*cross(b, c)[k] for k in range(3))/6
            for x, y in zip(ids, ids[1:]+ids[:1]):
                key = (min(x, y), max(x, y))
                count, winding = edges.get(key, (0, 0))
                edges[key] = (count+1, winding+(1 if x < y else -1))
        need(volume > 0 and all(value == (2, 0) for value in edges.values()), 'closed outward hand topology')
    # Recover exact corner data from the declared geometry source, which may
    # differ from the admission target only for an explicitly supplied export.
    original_sections = source_mesh['lods'][exported.get('geometry_source_lod', 0)]['sections']
    original = hand_section(original_sections, section_roles(original_sections)[0])['vertices']
    tails = {}
    for vertex in original:
        raw = bytes.fromhex(vertex['raw'])
        key = (tuple(vertex['p']), raw[tail_start:])
        tails.setdefault(key, set()).add(raw[:prefix_bytes])
    outputs = {}
    cap_corners = 0
    for part in (*color_parts, depth_part):
        color = part in color_parts
        local = {n: i for i, n in enumerate(pals[part])}
        records, indices, unique = [], [], {}
        for source_part, hand in ((sp, h) for sp in ([part] if color else color_parts) for h in hands):
            need(3 <= len(hand['vertices']) <= 65535 and 1 <= len(hand['triangles']) <= 200000,
                 'bounded capped asset counts')
            for triangle in hand['triangles']:
                if triangle.get('native_part', color_parts[0]) != source_part:
                    continue
                corners = triangle['corners']
                need(len(corners) == 3 and triangle['material'] in
                     ('native_glove', 'authored_wrist_cap'), 'triangle contract')
                for corner in corners:
                    vi = corner['vertex']
                    need(isinstance(vi, int) and 0 <= vi < len(hand['vertices']), 'corner index')
                    vertex = hand['vertices'][vi]
                    p, weights = vertex['position'], vertex['weights']
                    src = corner.get('native_source_vertex', vertex['native_source_vertex'])
                    if 'native_source_vertex' in corner:
                        authored = original[src]['weights']
                        weights = {n:w for n,w in authored.items() if n.startswith(hand['wrist'])}
                        proximal = sum(w for n,w in authored.items() if not n.startswith(hand['wrist']))
                        if proximal:
                            weights[hand['wrist']] = weights.get(hand['wrist'],0)+proximal
                    need(len(p) == 3 and all(math.isfinite(x) and abs(x) < 8 for x in p), 'position')
                    need(1 <= len(weights) <= 4 and all(n.startswith(hand['wrist']) and n in local
                         and math.isfinite(w) and 0 < w <= 1 for n, w in weights.items()), 'hand influences')
                    need(abs(sum(weights.values())-1) < 1e-6, 'normalized influences')
                    weighted = sorted((local[n], round(w*255)) for n, w in weights.items())
                    need(sum(w for _, w in weighted) == 255 and all(w > 0 for _, w in weighted),
                         'lossless byte weight rounding')
                    if src == -1:
                        need(weights == {hand['wrist']: 1.}, 'cap vertex wrist only')
                    else:
                        need(0 <= src < len(original) and original[src]['p'] == p, 'original vertex position')
                        for n, w in original[src]['weights'].items():
                            if n.startswith(hand['wrist']) and n != hand['wrist']:
                                need(abs(weights.get(n, -1)-w) < 1e-7, 'authored finger weight')
                    ids = bytes([i for i, _ in weighted] + [0]*(4-len(weighted)))
                    ws = bytes([w for _, w in weighted] + [0]*(4-len(weighted)))
                    if triangle['material'] == 'native_glove':
                        tail = corner['native_tail_half']
                        need(isinstance(tail, list) and len(tail) == ((64 if kind else 48)-tail_start)//2 and all(math.isfinite(x) for x in tail),
                             'original native corner attributes')
                        tail = struct.pack('<'+'e'*len(tail), *tail)
                        need((tuple(p), tail) in tails, 'exact original corner tail correspondence')
                        candidates = tails[(tuple(p), tail)]
                        position = bytes.fromhex(corner['native_position_hex']) if 'native_position_hex' in corner else next(iter(candidates))
                        need(position in candidates and ('native_position_hex' in corner or len(candidates)==1),
                             'exact original packed position sign')
                        if not color:
                            position = struct.pack('<3f', *p) if kind==1 else struct.pack('<4e', *p, 1.)
                    else:
                        need(corner['native_tail_half'] is None, 'cap should not masquerade as original tail')
                        need(src != -1 or weights == {hand['wrist']: 1.}, 'new cap corners wrist-only')
                        tail = cap_tail(triangle, contract, corner)
                        position = (struct.pack('<3f', *p) if kind==1 else
                                    struct.pack('<4e', *p, contract['position_w'] if contract and color else 1.))
                        if color:
                            cap_corners += 1
                    record = position + ids + ws
                    if color:
                        record += tail
                    elif kind==1:
                        record += bytes(12)
                    if record not in unique:
                        unique[record] = len(records)
                        records.append(record)
                    indices.append(unique[record])
        expected = sum(3 for h in hands for t in h['triangles'] if not color or t.get('native_part',color_parts[0])==part)
        need(len(records) < 65536 and len(indices) == expected, 'replacement bilateral bounds')
        outputs[part] = (b''.join(records), struct.pack('<'+'H'*len(indices), *indices))
    return sections, pals, outputs, cap_corners


def build(source_path, export_path, output_path, contract_path=None, target_mesh='standard', lod_index=0):
    source, native_raw = read_json(source_path, 64*1024*1024)
    if export_path:
        exported, export_raw = read_json(export_path)
    else:
        exported = native_export(source, target_mesh, lod_index)
        export_raw = json.dumps(exported, separators=(',', ':')).encode('utf-8')
    need(source['schema'] == 'bc2.private.hand_intake.v1' and
         exported['schema'] == 'bc2.private.hand_render_input.v1', 'input schema')
    contract = cap_contract(contract_path)
    sections, palettes, outputs, caps = prepare(source, exported, contract, target_mesh, lod_index)
    payload = bytearray()
    table = bytearray()
    report = {'schema': 'bc2.native_skin_pack_receipt.v1', 'native_input_sha256': digest(native_raw).hex(),
              'export_sha256': digest(export_raw).hex(), 'cap_contract_sha256': contract['sha256'] if contract else None,
              'cap_attributes_verified': contract is not None, 'runtime_admitted': False,
              'cap_corners': caps, 'rig_fingerprint': f'{RIG:016x}', 'sections': [],
              'target_mesh': target_mesh,
              'target_resource': select_mesh(source, target_mesh)['resource'], 'target_lod': lod_index,
              'geometry_source_resource': exported.get('geometry_source_resource', source['meshes'][0]['resource']),
              'material_policy': 'retain current target native binding; exact source corner UV payload preserved'}
    report['cap_uv_policy'] = ('one actual native cuff corner tuple selected as UV medoid; cap-only smooth normals; 8mm recessed lining'
                               if not export_path else 'exported cap UVs or explicit cap contract')
    for part, section in enumerate(sections):
        layout = section['layout']
        source_vb = b''.join(bytes.fromhex(v['raw']) for v in section['vertices'])
        source_indices = [i for triangle in section['triangles'] for i in triangle]
        need(all(isinstance(i, int) and 0 <= i < layout['vertices'] for i in source_indices), 'source index bounds')
        source_ib = struct.pack('<'+'H'*len(source_indices), *source_indices)
        need(digest(source_ib).hex() == layout['index_sha256'], 'source index digest')
        vb, ib = outputs.get(part, (b'', b''))
        base = HEADER+len(sections)*RECORD+len(payload)
        fields = [part, int(part in outputs), layout['stride'], layout['vertices'], len(source_indices),
                  layout['vertex_offset'], layout['first_index'], len(palettes[part]),
                  len(vb)//layout['stride'], len(ib)//2, base if vb else 0, len(vb),
                  base+len(vb) if ib else 0, len(ib), layout_kind(select_mesh(source,target_mesh)), 0]
        hashes = [digest(x) for x in (source_vb, source_ib, vb, ib)]
        remap = [source['skeleton']['names'].index(n) for n in palettes[part]]
        table.extend(struct.pack('<16I', *fields) + b''.join(hashes) +
                     struct.pack('<64H', *(remap+[65535]*(64-len(remap)))))
        payload.extend(vb+ib)
        report['sections'].append({'part': part, 'name': layout['name'], 'fields': fields,
            'source_vb_sha256': hashes[0].hex(), 'source_ib_sha256': hashes[1].hex(),
            'replacement_vb_sha256': hashes[2].hex(), 'replacement_ib_sha256': hashes[3].hex(),
            'rig_remap': remap})
    body = bytes(table+payload)
    version = (3 if layout_kind(select_mesh(source,target_mesh)) else
               1 if len(sections)==4 and len(outputs)==2 else 2)
    magic = {1: MAGIC, 2: b'BC2NSK02', 3: b'BC2NSK03'}[version]
    header = magic+struct.pack('<IIQII', version, HEADER+len(body), RIG, len(sections), int(contract is not None))
    header += digest(native_raw)+digest(export_raw)+digest(body)
    need(len(header) == HEADER and len(header+body) <= MAX_FILE, 'binary bound')
    output = Path(output_path)
    receipt = output.with_suffix(output.suffix+'.receipt.json')
    need(not output.exists() and not receipt.exists(), 'output already exists; preserve prior asset')
    output.parent.mkdir(parents=True, exist_ok=True)
    data = header+body
    output.write_bytes(data)
    report.update(bytes=len(data), sha256=digest(data).hex(), output=str(output.resolve()))
    receipt.write_text(json.dumps(report, indent=2)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-input', required=True)
    parser.add_argument('--export', help='Existing capped export; omit to cap target-native glove geometry')
    parser.add_argument('--output', required=True)
    parser.add_argument('--cap-contract')
    parser.add_argument('--target-mesh', default='standard', help='standard, assault, exact resource path or unique resource basename')
    parser.add_argument('--lod', type=int, default=0)
    args = parser.parse_args()
    result = build(args.native_input, args.export, args.output, args.cap_contract, args.target_mesh, args.lod)
    print(json.dumps({k: v for k, v in result.items() if k != 'sections'}, indent=2))
