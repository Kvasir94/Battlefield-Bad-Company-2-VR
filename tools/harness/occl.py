# Per-pixel occlusion check: dropped-magazine origin depth vs the scene depth saved the same frame/eye.
import json,sys,os,struct
run=sys.argv[1];d=os.path.join(run,'depth')
rows={(r['frame'],r['eye']):r for r in map(json.loads,open(os.path.join(d,'depth.jsonl')))}
props=[json.loads(l) for l in open(os.path.join(d,'props.jsonl'))] if os.path.exists(os.path.join(d,'props.jsonl')) else []
print(os.path.basename(run),'depth frames',len(rows),'prop rows',len(props))
for p in props:
    r=rows.get((p['frame'],p['eye']))
    if not r:print(' no depth row',p['frame'],p['eye']);continue
    x,y,z,w=p['clip']
    if w<=0:print(' behind camera',p);continue
    W,H,pitch=r['width'],r['height'],r['depth_pitch'];nx,ny,nz=x/w,y/w,z/w
    px=int((nx*.5+.5)*W);py=int((1-(ny*.5+.5))*H)
    raw=open(os.path.join(d,'depth-%d-eye%d.raw'%(p['frame'],p['eye'])),'rb').read()
    def dep(ix,iy):
        if not(0<=ix<W and 0<=iy<H):return None
        v=struct.unpack_from('<I',raw,iy*pitch+ix*4)[0];return (v&0xffffff)/16777215.0
    win=[dep(px+dx,py+dy) for dx in (-2,0,2) for dy in (-2,0,2)];win=[v for v in win if v is not None]
    scene=dep(px,py)
    def lin(zn):  # standard-Z: zn=1.00002-0.100002/vz
        return 0.100002/(1.00002-zn) if zn is not None and zn<1.00002 else None
    print(' frame',p['frame'],'eye',p['eye'],'slot',p['slot'],'tint',p['tint'],'sink',p['sink_m'],'px',(px,py),'mag z',round(nz,6),'vz %.3f'%lin(nz),
          'scene z',None if scene is None else round(scene,6),'vz',None if scene is None else '%.3f'%lin(scene),
          'occluded' if scene is not None and nz>scene else 'visible','win min/max',round(min(win),6) if win else None,round(max(win),6) if win else None,'world y',round(p['world'][13],3))
# Scene point under each untinted mag's origin pixel, in native world (floor height there).
import numpy as np
for p in props:
    r=rows.get((p['frame'],p['eye']))
    if not r or p['tint']:continue
    x,y,z,w=p['clip'];W,H,pitch=r['width'],r['height'],r['depth_pitch'];nx,ny=x/w,y/w
    px=int((nx*.5+.5)*W);py=int((1-(ny*.5+.5))*H)
    if not(0<=px<W and 0<=py<H):continue
    raw=open(os.path.join(d,'depth-%d-eye%d.raw'%(p['frame'],p['eye'])),'rb').read()
    zn=(struct.unpack_from('<I',raw,py*pitch+px*4)[0]&0xffffff)/16777215.0
    V=np.array(r['view']).reshape(4,4);P=np.array(r['projection']).reshape(4,4)
    vz=-P[3][2]/(zn+P[2][2]) # native RH: ndc z = (vz*m22+m32)/(-vz) with vz<0 -> solve for view z
    # native: clip_z = vzN*m22 + m32, clip_w = -vzN ; zn = clip_z/clip_w
    vzN=P[3][2]/(-zn-P[2][2])
    v=np.array([nx*(-vzN)/P[0][0],ny*(-vzN)/P[1][1],vzN,1.0])
    wpos=v@np.linalg.inv(V);cam=np.array([0,0,0,1.0])@np.linalg.inv(V)
    print('  floor@mag frame',p['frame'],'eye',p['eye'],'scene world',np.round(wpos[:3],3),'mag world',np.round(p['world'][12:15],3),'camera',np.round(cam[:3],3))
# Pre/post color around each untinted mag pixel: did the in-game draw change anything?
for p in props:
    r=rows.get((p['frame'],p['eye']))
    post=os.path.join(d,'color-post-%d-eye%d.raw'%(p['frame'],p['eye']))
    if not r or p['tint'] or not os.path.exists(post):continue
    pre=open(os.path.join(d,'color-%d-eye%d.raw'%(p['frame'],p['eye'])),'rb').read();po=open(post,'rb').read()
    W,H,cp=r['color_width'],r['height'],r['color_pitch'];x,y,z,w=p['clip']
    px=int((x/w*.5+.5)*W);py=int((1-(y/w*.5+.5))*H)
    near=sum(1 for yy in range(max(0,py-60),min(H,py+60)) for xx in range(max(0,px-60),min(W,px+60)) if pre[yy*cp+xx*4:yy*cp+xx*4+3]!=po[yy*cp+xx*4:yy*cp+xx*4+3])
    total=sum(1 for i in range(0,min(len(pre),len(po)),4) if pre[i:i+3]!=po[i:i+3])
    print('  color frame',p['frame'],'eye',p['eye'],'sink',p['sink_m'],'changed px near mag',near,'changed px total',total)
