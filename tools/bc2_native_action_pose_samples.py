"""Sample original BC2 curves into a private, bounded action-pose catalog.

The explicit spec binds archive/clip/skeleton/mesh identities. Samples retain
the native spline evaluator's provenance. Semantic reload windows preserve
the authored curve and map its travelled distance to physical progress;
runtime may interpolate the sampled transforms without inventing a trajectory.
"""
import argparse, hashlib, json, math
from pathlib import Path
from inspect_bc2_mesh_asset import Archive
import inspect_bc2_mesh_asset
from bc2_weapon_animation_pipeline import Clip, Skeleton, multiply, inverse_rigid

FINGERS=[f'LeftHand{digit}{joint}' for digit in ('Thumb','Index','Middle','Ring','Pinky') for joint in (1,2,3)]
RELOAD_ACTIONS={'ReloadInsert','ReloadRemove','ReloadCarry','ReloadSettle'}

def frame_times(requested,duration):
    """Evaluate exact window endpoints too, even off the regular sample grid."""
    action=requested['action']
    if action not in {'Pump','Rack','Reload',*RELOAD_ACTIONS}:raise ValueError('Unknown action')
    if action in RELOAD_ACTIONS and not str(requested.get('semantic_evidence','')).strip():
        raise ValueError('Reload action requires explicit native contact/window evidence')
    rate=requested.get('sampling_hz',60)
    if not isinstance(rate,int) or not 1<=rate<=240:raise ValueError('Sampling rate bounds')
    if action=='ReloadCarry':
        time=requested['carry_time']
        if not isinstance(time,(int,float)) or not math.isfinite(time) or not 0<=time<=duration:raise ValueError('Carry sample outside original clip')
        return [time],rate
    begin,end=requested.get('time_window',(0,duration))
    if not 0<=begin<end<=duration:raise ValueError('Native semantic window outside original clip')
    if action in RELOAD_ACTIONS and 'time_window' not in requested:raise ValueError('Reload action requires an explicit semantic window')
    times=[begin]+[n/rate for n in range(math.floor(begin*rate)+1,math.ceil(end*rate)) if begin<n/rate<end]+[end]
    if len(times)>4096:raise ValueError('Action frame count bound')
    return times,rate

def reload_progress(frames,parts,requested):
    """The authored item path advances monotonically even when it curves."""
    if requested['action']=='ReloadCarry':frames[0]['progress']=0.;return 0.
    driven=parts.index(requested['driven_part'])
    distances=[0.]
    for previous,current in zip(frames,frames[1:]):
        distances.append(distances[-1]+math.dist(previous['parts_in_weapon'][driven][12:15],current['parts_in_weapon'][driven][12:15]))
    length=distances[-1]
    if length<.005:raise ValueError('Reload semantic window has no sampled item motion')
    for frame,distance in zip(frames,distances):
        frame['progress']=(frame['time_seconds']-frames[0]['time_seconds'])/(frames[-1]['time_seconds']-frames[0]['time_seconds']) if requested['action']=='ReloadSettle' else distance/length
    return length

def calibrate_reload_input(profile,carry,item_from_insertion,item_from_bone=None,terminal_anchor=None):
    """Match the recognizer's raw contact to the same original wrist samples.

    A visual action may curve while its physical guide is a line. Project the
    actual recognizer contact (not the ammo bone or bare wrist) onto that line
    and use the result as the action's frame progress. Reject reversals: they
    indicate a release/withdraw interval, not a continuous insertion stroke.
    """
    if profile['action']!='ReloadInsert' or carry['action']!='ReloadCarry' or any(profile[k]!=carry[k] for k in ('asset_name','mesh_path','rig_fingerprint','part_bones')):
        raise ValueError('Matching insertion and carry profiles required')
    identity=[float(n//4==n%4) for n in range(16)]
    center=item_from_bone or identity
    cf=carry['frames'][0]
    grasp=multiply(cf['wrist_in_weapon'],inverse_rigid(multiply(center,cf['parts_in_weapon'][0])))
    correction=multiply(inverse_rigid(profile['frames'][-1]['parts_in_weapon'][0]),terminal_anchor) if terminal_anchor else identity
    inverse_grasp=inverse_rigid(grasp)
    contacts=[multiply(multiply(item_from_insertion,inverse_grasp),multiply(f['wrist_in_weapon'],correction)) for f in profile['frames']]
    entry=contacts[0];inverse_entry=inverse_rigid(entry)
    local=[multiply(c,inverse_entry) for c in contacts]
    end=local[-1][12:15];length=math.sqrt(sum(v*v for v in end))
    if not .005<=length<=.3:raise ValueError('Native hand/contact guide travel bound')
    direction=[v/length for v in end]
    progress=[sum(v*u for v,u in zip(m[12:15],direction))/length for m in local]
    if any(not -1e-5<=v<=1+1e-5 for v in progress) or any(a>b+1e-5 for a,b in zip(progress,progress[1:])):
        raise ValueError('Native hand/contact window reverses: trim release/withdrawal')
    radial=[math.sqrt(sum((v-length*t*u)**2 for v,u in zip(m[12:15],direction))) for m,t in zip(local,progress)]
    for f,t in zip(profile['frames'],progress):f['progress']=max(0.,min(1.,t))
    profile['frames'][0]['progress']=0.;profile['frames'][-1]['progress']=1.
    profile['progress_parameterization']='native_input_contact_projection'
    calibration={'item_from_hand':grasp,'item_from_insertion':item_from_insertion,'weapon_from_entry':entry,
                 'travel_direction':direction,'travel_meters':length,'max_native_contact_radial_m':max(radial),
                 'presentation_terminal_correction':correction,'native_contact_progress':progress}
    profile['input_calibration']=calibration
    return calibration

def support_contacts(game,skeleton_path,spec,base):
    """Append exact original HandsIK time-zero references; keep existing rows."""
    sk=Skeleton(skeleton_path.read_bytes());out=base.rstrip()+'\n'
    number=lambda m:' '.join(format(v,'.9g') for v in m)
    for item in spec:
        ref=item.get('support_reference')
        if not ref:continue
        archive=Archive(game/ref['archive']);raw=archive.read_selected([ref['resource']])[ref['resource']];clip=Clip(raw)
        evaluated=clip.evaluate(sk,0,['LeftHand',*FINGERS,ref['part']])
        world=evaluated['weapon_relative'];wrist=world['LeftHand'];inverse=inverse_rigid(wrist)
        asset=item['asset'].replace('%','%25').replace(' ','%20')
        if any(line.startswith('contact support left '+asset+' ') for line in out.splitlines()):raise ValueError('Existing support contact would be overwritten')
        out+='\n# exact original native time-zero reference '+ref['resource']+'; controls_constant='+str(evaluated['all_required_controls_constant']).lower()+'; skeleton '+sk.sha256+'\n'
        out+='contact support left '+asset+' '+ref['part']+' '+sk.fingerprint.split(':')[1]+' '+clip.sha256+' 0 '+number(multiply(wrist,inverse_rigid(world[ref['part']])))+'\n'
        for name in FINGERS:out+='finger '+name+' '+number(multiply(world[name],inverse))+'\n'
        out+='end\n'
    return out

def sample(game,skeleton_path,spec):
    raw=skeleton_path.read_bytes();sk=Skeleton(raw);profiles=[];gaps=[]
    if not 0<len(spec)<=64:raise ValueError('Bounded explicit asset spec required')
    for item in spec:
        archive_path=(game/item['archive']).resolve()
        if not archive_path.is_relative_to(game.resolve()):raise ValueError('Archive outside game')
        inspect_bc2_mesh_asset.MAX_PAYLOAD=640*1024*1024
        archive=Archive(archive_path)
        clips=item['clips']
        if not 0<len(clips)<=16:raise ValueError('Bounded clips required')
        entries={e.name:e for e in archive.entries if e.flags==65536 and e.kind=='GrannyAnimation'}
        resources=[c['resource'] for c in clips if c['resource'] in entries]
        if item.get('static_base_resource'):resources.append(item['static_base_resource'])
        blobs=archive.read_selected(list(dict.fromkeys(resources)))
        for requested in clips:
            try:
                name=requested['resource'];clip=Clip(blobs[name]);parts=list(requested['parts']);omitted=[];base_tracks=[]
                if item.get('static_base_resource'):
                    base=Clip(blobs[item['static_base_resource']])
                    for bone in item.get('static_base_bones',[]):
                        if bone in clip.tracks or bone in clip.unsupported:continue
                        curves=base.tracks[bone]
                        if not all(all(c.controls[0]==v for v in c.controls) for c in curves):raise ValueError('Missing track base is not original static authored pose')
                        clip.tracks[bone]=curves;base_tracks.append({'bone':bone,'resource':item['static_base_resource'],'sha256':base.sha256})
                # Do not invent missing animated local transforms. Omit only
                # those requested part chains; hand/finger curves stay exact.
                for part in list(parts):
                    chain=[];at=sk.names.index(part)
                    while at>=0:chain.append(sk.names[at]);at=sk.parents[at]
                    absent=[bone for bone in chain if bone not in clip.tracks]
                    if absent:parts.remove(part);omitted.append({'part':part,'missing_tracks':absent})
                if not parts or len(parts)>32 or not 0<clip.duration<=30:raise ValueError('Clip/part bounds')
                names=['LeftHand',*FINGERS,*parts]
                times,rate=frame_times(requested,clip.duration)
                frames=[]
                for t in times:
                    evaluated=clip.evaluate(sk,t,names)['weapon_relative'];wrist=evaluated['LeftHand'];inv=inverse_rigid(wrist)
                    frames.append({'time_seconds':t,'progress':t/clip.duration,'returning':False,'wrist_in_weapon':wrist,
                        'fingers_in_wrist':[multiply(evaluated[f],inv) for f in FINGERS],
                        'parts_in_weapon':[evaluated[p] for p in parts]})
                path_length=None
                if requested['action'] in RELOAD_ACTIONS:
                    path_length=reload_progress(frames,parts,requested)
                if requested['action'] in ('Pump','Rack'):
                    driven=parts.index(requested['driven_part']);origin=frames[0]['parts_in_weapon'][driven][12:15]
                    displacement=[math.dist(f['parts_in_weapon'][driven][12:15],origin) for f in frames]
                    peak=max(range(len(frames)),key=displacement.__getitem__);stroke=displacement[peak]
                    if stroke<.005:raise ValueError('Native driven part has no sampled stroke')
                    for n,f in enumerate(frames):f['progress']=min(1,displacement[n]/stroke);f['returning']=n>peak
                else:peak=None;stroke=None
                common={'asset_name':item['asset'],'rig_fingerprint':sk.metadata()['rig_fingerprint'],'clip_resource':name,
                    'archive':item['archive'],'archive_index_sha256':archive.index_sha256,'clip_sha256':clip.sha256,
                    'skeleton_sha256':hashlib.sha256(raw).hexdigest(),'action':requested['action'],'part_bones':parts,
                    'frame_count':len(frames),'duration_seconds':clip.duration,'peak_frame':peak,'native_part_stroke_m':stroke,
                    'sampling_hz':rate,'evaluation_status':'original_decoded_spline_samples','target_runtime_interpolation_verified':False,
                    'animation_wrapper':clip.wrapper,'native_item_path_m':path_length,
                    'progress_parameterization':'clip_time' if requested['action']=='ReloadSettle' else 'original_item_path_length' if path_length else 'carry_pose' if requested['action']=='ReloadCarry' else 'native_part_displacement' if requested['action'] in ('Pump','Rack') else 'clip_time',
                    'omitted_part_chains':omitted,
                    'original_static_base_tracks':base_tracks,
                    'semantic_window':requested.get('time_window'),'semantic_evidence':requested.get('semantic_evidence'),'carry_time':requested.get('carry_time'),
                    'runtime_frame_policy':'sampled_native_curve; interpolate_rigid_transforms','frames':frames}
                for mesh in item['meshes']:profiles.append({**common,'mesh_path':mesh})
            except (KeyError,ValueError,TypeError,OverflowError) as exc:gaps.append({'asset':item['asset'],'resource':requested['resource'],'action':requested['action'],'reason':str(exc)})
    return {'schema':'fvr.bc2.native_action_pose_samples','profiles':profiles,'gaps':gaps,'game_run':False}

def header(data):
    lines=['#pragma once','// PRIVATE original game animation samples; never commit this generated header.','#include "Bc2NativeActionPose.h"','namespace fvr::bc2::generated {']
    matrix=lambda v:'math::Matrix4{{{'+','.join('{{'+','.join(format(x,'.9g')+'f' if '.' in format(x,'.9g') or 'e' in format(x,'.9g') else format(x,'.9g')+'.f' for x in v[n:n+4])+'}}' for n in range(0,16,4))+'}}}'
    quote=lambda s:json.dumps(s)
    for n,p in enumerate(data['profiles']):
        lines.append(f'inline constexpr std::array<std::string_view,{len(p["part_bones"])}> ActionParts{n}{{'+','.join(quote(x) for x in p['part_bones'])+'};')
        for k,f in enumerate(p['frames']):lines.append(f'inline constexpr std::array<math::Matrix4,{len(f["parts_in_weapon"])}> ActionPartFrames{n}_{k}{{'+','.join(matrix(v) for v in f['parts_in_weapon'])+'};')
        lines.append(f'inline const std::array<NativeActionPoseFrame,{len(p["frames"])}> ActionFrames{n}{{{{')
        for k,f in enumerate(p['frames']):lines.append('{'+f'{f["time_seconds"]:.9f}f,{f["progress"]:.9f}f,'+('true' if f['returning'] else 'false')+','+matrix(f['wrist_in_weapon'])+',{{'+','.join(matrix(v) for v in f['fingers_in_wrist'])+'}},ActionPartFrames'+f'{n}_{k}'+'},')
        lines.append('}};')
    lines.append(f'inline const std::array<NativeActionPoseProfile,{len(data["profiles"])}> NativeActionPoses{{{{')
    for n,p in enumerate(data['profiles']):
        source=p.get('source_kind','AuthoredClip');capture=p.get('capture_sha256','')
        if source not in ('AuthoredClip','EffectiveNativeCapture'):raise ValueError('Unknown native action source')
        if source=='EffectiveNativeCapture' and (p['clip_sha256'] or len(capture)!=64):raise ValueError('Effective capture cannot assert a clip identity')
        lines.append('{'+','.join(quote(p[k]) for k in ['asset_name','mesh_path','clip_sha256','skeleton_sha256'])+','+p['rig_fingerprint'].split(':')[1].join(['0x','ull'])+',NativeActionPoseKind::'+p['action']+f',ActionParts{n},ActionFrames{n},NativeActionPoseSource::{source},'+quote(capture)+'},')
    lines.extend(['}};','}']);return '\n'.join(lines)+'\n'

def main():
    p=argparse.ArgumentParser();p.add_argument('--game',type=Path,required=True);p.add_argument('--skeleton',type=Path,required=True);p.add_argument('--spec',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--header',type=Path,required=True);p.add_argument('--contacts-base',type=Path);p.add_argument('--contacts-output',type=Path);a=p.parse_args()
    data=sample(a.game,a.skeleton,json.loads(a.spec.read_text()));a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(data,separators=(',',':')),encoding='utf-8');a.header.write_text(header(data),encoding='utf-8');print(json.dumps({'profiles':len(data['profiles']),'gaps':data['gaps'],'frames':sum(p['frame_count'] for p in data['profiles'])}))
    if bool(a.contacts_base)!=bool(a.contacts_output):raise ValueError('Contact base/output pair required')
    if a.contacts_output:a.contacts_output.write_text(support_contacts(a.game,a.skeleton,json.loads(a.spec.read_text()),a.contacts_base.read_text()),encoding='utf-8')
if __name__=='__main__':main()
