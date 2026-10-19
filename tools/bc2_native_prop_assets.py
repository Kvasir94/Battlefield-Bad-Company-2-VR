"""Extract private rigid magazine/shell native-material packs; read-only game access.

Original vertex records and section/material identities are retained byte for
byte for color sections. Depth uses those same original indexed corners. The output folder must NOT be configured as a
native hand replacement set: these packs belong to additional prop submissions.
"""
from __future__ import annotations
import argparse, hashlib, json, math, struct
from pathlib import Path
from bc2_body_ammo_assets import archive, text, SKELETON_ARCHIVE, SKELETON_RESOURCE, SKELETON_SHA
from bc2_mesh_geometry import metadata, geometry
from bc2_weapon_animation_pipeline import Skeleton, canonical
from inspect_bc2_mesh_asset import bone_hash, sha

HEADER=128
RECORD=320
MAX_PACK=8*1024*1024
MAX_SET=64*1024*1024

def need(ok,why):
    if not ok:raise ValueError(why)

def digest(data):return hashlib.sha256(data).digest()

def corners_key(v,stride,remap):
    half=stride in (16,48);at=8 if half else 12
    p=struct.unpack_from('<3e' if half else '<3f',v)
    need(all(math.isfinite(x) and abs(x)<8 for x in p),'finite bounded native position')
    weights={}
    for bone,w in zip(v[at:at+4],v[at+4:at+8]):
        if w:
            need(bone<len(remap),'skin index outside native palette')
            weights[remap[bone]]=weights.get(remap[bone],0)+w
    need(sum(weights.values())==255,'native weights')
    return (p,tuple(sorted(weights.items())))

def pack(game,spec,skeleton):
    asset,path,mesh,part=spec
    a=archive(game,path);resources=[mesh+'.res',mesh+'_lod0_data.res'];blobs=a.read_selected(resources)
    lod=metadata(blobs[resources[0]])[0];data=blobs[resources[1]]
    names=skeleton.names;known={bone_hash(n):n for n in names};need(len(known)==len(names)==147,'exact native 147-bone rig')
    need(part in names,'rigid part bone');target=names.index(part)
    sections=geometry(data,lod,known);need(1<=len(sections)<=8,'native section bound')
    vb_size=sum(s['vertices']*s['stride'] for s in sections);ib_size=sum(s['triangles']*6 for s in sections)
    start=len(data)-vb_size-ib_size
    need(start>=0 and 0<vb_size<=MAX_PACK and 0<ib_size<=MAX_PACK,'native buffer bounds')
    depths=[i for i,s in enumerate(sections) if s['name'].endswith('_ZOnly')]
    need(len(depths)==1,'one native depth section');depth=depths[0]
    sources=[];outputs={};color_corners=[];report=[]
    for i,s in enumerate(sections):
        need(s['stride'] in ((16,32) if i==depth else (48,64)),'supported native position/skin layout')
        remap=[names.index(known[lod['palette'][n]]) for n in s['palette']]
        need(0<len(remap)<=64 and 0<s['vertices']<=65535 and 0<s['triangles']<=200000,'native section bounds')
        need(0<=s['vertex_offset']<=vb_size-s['vertices']*s['stride'] and
             0<=s['first_index']*2<=ib_size-s['triangles']*6,'native section ranges')
        vb=data[start+s['vertex_offset']:start+s['vertex_offset']+s['vertices']*s['stride']]
        ib=data[start+vb_size+s['first_index']*2:start+vb_size+(s['first_index']+s['triangles']*3)*2]
        ids=struct.unpack('<'+'H'*(len(ib)//2),ib)
        need(all(n<s['vertices'] for n in ids),'native index range')
        keys=[corners_key(vb[n*s['stride']:(n+1)*s['stride']],s['stride'],remap) for n in range(s['vertices'])]
        sources.append((vb,ib,ids,keys,remap))
        if i==depth:continue
        kept=[]
        for n in range(0,len(ids),3):
            tri=list(ids[n:n+3]);ks=[keys[k] for k in tri]
            touches=any(any(b==target for b,w in k[1]) for k in ks)
            if not touches:continue
            need(all(k[1]==((target,255),) for k in ks),'target intersects blended/cross-part triangle')
            kept.extend(tri);color_corners.extend(ks)
        if kept:outputs[i]=(vb,struct.pack('<'+'H'*len(kept),*kept))
    need(color_corners,'part has no native color triangles')
    dvb,dib,dids,dkeys,dremap=sources[depth]
    # Native depth LOD omits some original color detail (XM8 Plastic, for
    # example). Use the existing hand-pack contract: identical color corners
    # with native depth position/skin declaration and original palette.
    need(target in dremap,'rigid target absent from native depth palette')
    depth_stride=sections[depth]['stride'];records=[];reordered=[];unique={}
    for position,weights in color_corners:
        raw_position=struct.pack('<3f',*position) if depth_stride==32 else struct.pack('<4e',*position,1.)
        record=raw_position+bytes((dremap.index(target),0,0,0,255,0,0,0))
        if depth_stride==32:record+=bytes(12)
        need(corners_key(record,depth_stride,dremap)==(position,weights),'lossless depth corner conversion')
        if record not in unique:unique[record]=len(records);records.append(record)
        reordered.append(unique[record])
    need(len(records)<=65535,'depth vertex bound')
    outputs[depth]=(b''.join(records),struct.pack('<'+'H'*len(reordered),*reordered))
    payload=bytearray();table=bytearray()
    for i,s in enumerate(sections):
        source_vb,source_ib,_,_,remap=sources[i];vb,ib=outputs.get(i,(b'',b''))
        offset=HEADER+RECORD*len(sections)+len(payload);layout=1 if s['stride'] in (32,64) else 0
        fields=[i,int(i in outputs),s['stride'],s['vertices'],s['triangles']*3,s['vertex_offset'],s['first_index'],len(remap),
                len(vb)//s['stride'],len(ib)//2,offset if vb else 0,len(vb),offset+len(vb) if ib else 0,len(ib),layout,0]
        hashes=[digest(x) for x in (source_vb,source_ib,vb,ib)]
        table+=struct.pack('<16I',*fields)+b''.join(hashes)+struct.pack('<64H',*(remap+[65535]*(64-len(remap))))
        payload+=vb+ib
        report.append(dict(section=s['name'],part_index=i,material_paths=[m[i] for m in lod['materials']],
            source_vertices=s['vertices'],source_indices=s['triangles']*3,kept_indices=len(ib)//2,stride=s['stride'],
            original_vertex_records_unchanged=(not vb or vb==source_vb),source_vb_sha256=hashes[0].hex(),source_ib_sha256=hashes[1].hex(),
            replacement_vb_sha256=hashes[2].hex(),replacement_ib_sha256=hashes[3].hex(),rig_remap=remap))
    body=bytes(table+payload);rig=int(skeleton.fingerprint.split(':')[1],16)
    source_hash=digest(blobs[resources[0]]+data)
    selection_hash=digest(json.dumps(dict(asset=asset,mesh=mesh,part=part,bone=target,policy='exact_native_rigid_part_indices'),sort_keys=True).encode())
    header=b'BC2NSK03'+struct.pack('<IIQII',3,HEADER+len(body),rig,len(sections),1)+source_hash+selection_hash+digest(body)
    need(len(header)==HEADER and len(header+body)<=MAX_PACK,'native pack size')
    return header+body,dict(asset=asset,mesh=mesh,part=part,archive=path,archive_index_sha256=a.index_sha256,
        mesh_sha256=sha(blobs[resources[0]]),lod_sha256=sha(data),rig_fingerprint=rig,global_bone_index=target,palette_count=147,
        inverse_bind=canonical(skeleton.bones[target]['InverseWorldTransform']),sections=report,
        native_material_policy='retain selected native LOD variant/material binding and every original vertex attribute',
        original_color_triangles=len(color_corners)//3,depth_policy='same original color corners with native depth declaration and palette',native_calls=False,runtime_admitted=False)

def derive(game,specs,output):
    need(0<len(specs)<=32,'prop count');need(len({(s[0],s[2],s[3]) for s in specs})==len(specs),'duplicate prop identity')
    sk=archive(game,SKELETON_ARCHIVE);raw=sk.read_selected([SKELETON_RESOURCE])[SKELETON_RESOURCE]
    need(sha(raw)==SKELETON_SHA,'installed skeleton content changed');skeleton=Skeleton(raw)
    need(not output.exists(),'preserve existing output folder');prepared=[];size=0
    for spec in specs:
        data,row=pack(game,spec,skeleton);row['filename']=sha(data)+'.fvrskin';row['sha256']=sha(data);row['bytes']=len(data)
        prepared.append((data,row));size+=len(data);need(size<=MAX_SET,'total native prop set bound')
    index=bytearray(b'BC2NPR01'+struct.pack('<II',1,len(prepared)))
    for data,row in prepared:
        index+=b''.join(text(row[k]) for k in ('asset','mesh','part','filename'))
        index+=struct.pack('<QII16f',row['rig_fingerprint'],row['global_bone_index'],row['palette_count'],*row['inverse_bind'])+bytes.fromhex(row['sha256'])
    output.mkdir(parents=True)
    for data,row in prepared:(output/row['filename']).write_bytes(data)
    (output/'manifest.fvrnprop').write_bytes(index)
    receipt=dict(schema='fvr.bc2.native_prop_packs.v1',skeleton_sha256=sha(raw),manifest_sha256=sha(index),bytes=size,
        packs=[r for d,r in prepared],runtime_admitted=False)
    output.with_name(output.name+'.receipt.private.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    return receipt

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--game',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--spec',type=Path,help='Explicit private [asset,archive,mesh,part] rows; defaults to shared ammo/shell specs')
    args=parser.parse_args()
    if args.spec:specs=json.loads(args.spec.read_text(encoding='utf-8'))
    else:
        from bc2_body_ammo_assets import SPECS
        specs=SPECS
    result=derive(args.game,specs,args.output)
    print(json.dumps(dict(output=str(args.output),packs=len(result['packs']),bytes=result['bytes'],triangles=sum(p['original_color_triangles'] for p in result['packs']),runtime_admitted=False)))
if __name__=='__main__':main()
