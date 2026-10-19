"""Compile exact authored parts and explicit VR rack contacts into a catalog.

Uses the existing magazine/Granny extraction metadata. Contacts and strokes are
explicit design data; optional sampled native motion remains separate evidence.
No game bytes, native calls, timing or ammunition grants are emitted.
"""
from __future__ import annotations
import argparse,hashlib,json,math
from pathlib import Path
import bc2_authored_magazine_geometry as geometry
from bc2_authored_grip_bindings import checked_hash,path_key

def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()

def build(inputs,contacts):
    if not isinstance(inputs,list) or len(inputs)>256 or not isinstance(contacts,list) or not 0<len(contacts)<=256:
        raise ValueError('Bounded mechanism inputs and contacts required')
    entries=[];keys=set();ids=set()
    for contact in contacts:
        key=(contact['asset'],contact['mesh']);path_key(key[1])
        if key in keys:raise ValueError('Duplicate exact contact')
        keys.add(key)
        candidates=[r for r in inputs if (r['profile']['native_asset_name'],r['profile']['configured_mesh_path'])==key]
        if not candidates:raise ValueError('Missing exact extracted mechanism')
        def signature(row):
            return (row['skeleton']['rig_fingerprint'],row['closed']['weapon_relative'],
                    {k:{a:b for a,b in v.items() if a!='points'} for k,v in row['parts'].items()})
        if any(signature(r)!=signature(candidates[0]) for r in candidates):raise ValueError('Conflicting exact closed geometry')
        row=candidates[0];profile=row['profile'];checked_hash(profile['binding_digest'])
        fingerprint=row['skeleton']['rig_fingerprint']
        if not isinstance(fingerprint,str) or not fingerprint.startswith('fnv1a64:') or len(fingerprint)!=24:
            raise ValueError('Exact rig fingerprint required')
        rig=int(fingerprint.split(':')[1],16)
        names=contact['parts'];knob=contact['contact_bone']
        if not isinstance(names,list) or not 0<len(names)<=32 or len(set(names))!=len(names) or knob not in names:
            raise ValueError('Unique named mechanism parts including contact required')
        parts=[]
        for name in names:
            if name=='jntWpn_1' or name not in row['parts'] or name not in row['closed']['weapon_relative']:
                raise ValueError('Exact named weapon part required')
            if row['closed']['bone_evaluation_status'].get(name)!='static_authored_pose':
                raise ValueError('Closed pose needs constant authored ancestor chain')
            part=row['parts'][name]
            if part['mixed_triangles'] or part['triangles']<=0:raise ValueError('Independent measured rigid geometry required')
            matrix=geometry.matrix(row['closed']['weapon_relative'][name])
            parts.append({'bone':name,'closed_in_weapon_m':matrix})
        part=row['parts'][knob];center=[(part['minimum'][n]+part['maximum'][n])*.5 for n in range(3)]
        point=geometry.point(center,row['closed']['weapon_relative'][knob])
        direction=contact['pull_direction'];stroke=contact['stroke_m']
        if not isinstance(direction,list) or len(direction)!=3 or any(type(v) not in (float,int) or not math.isfinite(v) for v in direction) or abs(geometry.norm(direction)-1)>1e-4:
            raise ValueError('Finite unit pull direction required')
        if type(stroke) not in (float,int) or not math.isfinite(stroke) or not .01<=stroke<=.3:
            raise ValueError('Explicit bounded VR stroke required')
        source={'asset':key[0],'mesh':key[1],'rig_fingerprint':fingerprint,'binding_digest':profile['binding_digest'],
                'skeleton_sha256':row['skeleton']['resource_sha256'],'parts':parts,'handle_point_m':point,
                'contact':contact,'native_stroke_claim':False,'native_action_admission':False}
        source_digest=digest(source);identifier=int(source_digest[:16],16)
        if not identifier or identifier in ids:raise ValueError('Mechanism identifier collision')
        ids.add(identifier);entries.append((source,identifier,rig,source_digest))
    lines=['#pragma once','#include "Bc2MagazineMechanismProfile.h"','namespace fvr::bc2::generated {']
    def vector(values):return '{'+','.join(f'{float(v):.9g}f' if '.' in f'{float(v):.9g}' or 'e' in f'{float(v):.9g}' else f'{float(v):.9g}.0f' for v in values)+'}'
    def matrix(values):return '{{{'+','.join(vector(values[n:n+4]) for n in range(0,16,4))+'}}}'
    for n,(source,identifier,rig,source_digest) in enumerate(entries):
        lines.append(f'inline constexpr std::array<MagazineMechanismPart,{len(source["parts"])}> MagazineMechanismParts{n}{{{{')
        for part in source['parts']:lines.append(' {'+json.dumps(part['bone'])+','+matrix(part['closed_in_weapon_m'])+'},')
        lines.append('}};')
    lines.append(f'inline constexpr std::array<MagazineMechanismProfile,{len(entries)}> MagazineMechanisms{{{{')
    for n,(source,identifier,rig,source_digest) in enumerate(entries):
        contact=source['contact'];handle='{'+f'0x{identifier:016x}ull,1,'+vector(source['handle_point_m'])+','+vector(contact['pull_direction'])+f',{float(contact["stroke_m"]):.9f}f,.06f,.01f,.08f,interaction::MotionGuardConfig::Rack(),false'+'}'
        lines.append(' {'+json.dumps(source['asset'])+','+json.dumps(source['mesh'])+f',0x{rig:016x}ull,'+handle+f',MagazineMechanismParts{n},"{source_digest}"'+'},')
    lines+=['}};','} // namespace fvr::bc2::generated','']
    return '\n'.join(lines),{'schema':'fvr.bc2.magazine_mechanism_catalog.v1','rows':[s for s,*_ in entries],
                            'native_runtime_test_claim':False,'headset_test_claim':False}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('inputs','contacts','header','receipt'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();inputs=json.loads(args.inputs.read_text());contacts=json.loads(args.contacts.read_text())
    header,receipt=build(inputs,contacts);args.header.parent.mkdir(parents=True,exist_ok=True);args.header.write_text(header,encoding='utf-8')
    receipt['header_sha256']=hashlib.sha256(header.encode()).hexdigest();args.receipt.write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'profiles':len(receipt['rows']),'header_sha256':receipt['header_sha256']}))
if __name__=='__main__':main()
