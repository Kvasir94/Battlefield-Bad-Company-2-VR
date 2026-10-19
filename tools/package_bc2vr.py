"""Bundle an existing verified candidate and the portable GUI. No build, game run or upload."""
import argparse, hashlib, json, re, struct, subprocess, zipfile
from pathlib import Path
from package_preview import pe_info

GUI = ('DevLauncher.ps1', 'Launch-BC2VR.vbs', 'tools/BC2PlaytestPanel.ps1',
       'tools/BC2PlaytestOptions.ps1', 'tools/BC2Package.ps1', 'tools/Start-BC2Game.ps1',
       'tools/Start-BC2Playtest.ps1', 'tools/Start-BC2LauncherTask.ps1', 'LICENSE')
SUPPORT = ('tools/BC2RenderSize.ps1', 'tools/BC2TraceCompletion.ps1', 'tools/game_window.py', 'tools/read_bc2.py')
EXCLUDE = {'config/local.json', 'Start-BC2-Playtest.ps1', 'reports/Start-Installed-BC2VR.ps1'}
# Session scripts that load the native DLL; a rebuilt DLL ships with the scripts from its own source.
RUNTIME = ('Play-BC2VR.ps1', 'Start-BC2VRSession.ps1', 'Stop-BC2VRSession.ps1', 'Start-NativeTrace.ps1')

def sha(data): return hashlib.sha256(data).hexdigest()
def encoded(obj): return (json.dumps(obj, indent=2)+'\n').encode()
def scrub(obj):
    if isinstance(obj,dict): return {k:scrub(v) for k,v in obj.items()}
    if isinstance(obj,list): return [scrub(v) for v in obj]
    if isinstance(obj,str) and re.match(r'^[A-Za-z]:[\\/]',obj): return re.split(r'[\\/]',obj)[-1]
    return obj

# These object-name versions are the actual IPC namespaces compiled into both
# peers. Source commit equality is neither necessary nor sufficient: an older
# host may be reused only while its wire contracts still match the native DLL.
IPC_CHANNELS = ('Control', 'Feedback', 'Menu', 'Pose', 'Texture')

def ipc_protocol_versions(data, name):
    versions={}
    for channel in IPC_CHANNELS:
        prefix=('Local\\FrostbiteVR.'+channel+'.v').encode('utf-16-le')
        matches=re.findall(re.escape(prefix)+rb'((?:[0-9]\x00){1,4})'+re.escape('.'.encode('utf-16-le')),data)
        found={int(value.decode('utf-16-le')) for value in matches}
        if len(found)!=1:
            raise ValueError(f'Missing or ambiguous {channel} IPC protocol marker in {name}')
        versions[channel]=found.pop()
    audio_prefix='Local\\FrostbiteVR.Audio.v'.encode('utf-16-le')
    audio_matches=re.findall(re.escape(audio_prefix)+rb'((?:[0-9]\x00){1,4})'+re.escape('.'.encode('utf-16-le')),data)
    if audio_matches:
        found={int(value.decode('utf-16-le')) for value in audio_matches}
        if len(found)!=1:raise ValueError('Ambiguous Audio IPC protocol marker in '+name)
        versions['Audio']=found.pop()
    return versions

def validate_ipc_pair(files):
    peers=(('build/x86/BC2NativeProbe.dll','0x14c'),('build/x64/BC2XrHost.exe','0x8664'))
    protocols=[]
    for name,machine in peers:
        if name not in files:raise ValueError('Missing IPC peer: '+name)
        data=files[name]
        if pe_info(data)['machine']!=machine:raise ValueError('Wrong binary architecture: '+name)
        protocols.append(ipc_protocol_versions(data,name))
    mismatches=[f'{channel}: native v{protocols[0].get(channel, "missing")}, host v{protocols[1].get(channel, "missing")}'
                for channel in sorted(protocols[0].keys()|protocols[1].keys()) if protocols[0].get(channel)!=protocols[1].get(channel)]
    if mismatches:
        raise ValueError('Incompatible native/host IPC protocols ('+'; '.join(mismatches)+'). Build a compatible host before packaging.')
    return protocols[0]

RELOAD_AUDIO_CUES=('xm8_mag_out','xm8_mag_in','xm8_handle_back','xm8_handle_forward',
                   'spas_shell_1_cue','spas_shell_2_cue','spas_shell_3_cue','spas_pump_back','spas_pump_forward')
def load_reload_audio(directory):
    manifest=json.loads((directory/'cue-manifest.json').read_text(encoding='utf-8-sig'))
    records=manifest.get('records',[])
    if len(records)!=len(RELOAD_AUDIO_CUES) or {r.get('cue') for r in records}!=set(RELOAD_AUDIO_CUES):
        raise ValueError('Incomplete or duplicate reload audio cues')
    files={}
    for record in records:
        name=record['cue'];data=(directory/(name+'.wav')).read_bytes()
        if not 44<len(data)<=288044 or data[:4]!=b'RIFF' or data[8:16]!=b'WAVEfmt ' or data[36:40]!=b'data':
            raise ValueError('Invalid reload cue container: '+name)
        if struct.unpack_from('<I',data,16)[0]!=16 or struct.unpack_from('<HHIIHH',data,20)!=(1,1,48000,96000,2,16):
            raise ValueError('Invalid reload cue PCM format: '+name)
        if struct.unpack_from('<I',data,40)[0]!=len(data)-44 or struct.unpack_from('<I',data,4)[0]!=len(data)-8 or len(data)%2:
            raise ValueError('Invalid reload cue length: '+name)
        if sha(data[44:])!=record.get('pcm_sha256'):
            raise ValueError('Changed reload audio cue: '+name)
        files['assets/reload-audio/'+name+'.wav']=data
    files['assets/reload-audio/cue-manifest.json']=encoded(manifest)
    return files

def package(repo, candidate, output, stage_dir, stage_commit, native_dll=None, host_exe=None, source_commit=None, host_source_commit=None, authored_contacts=None, native_props=None, body_ammo=None, extract=None, reload_audio=None):
    if any((native_dll,host_exe,source_commit)) and not all((native_dll,host_exe,source_commit)):
        raise ValueError('A rebuilt native/host pair requires both binaries and their source commit')
    if source_commit and not re.fullmatch(r'[0-9a-f]{40}',source_commit):
        raise ValueError('Expected full native/host source commit')
    if host_source_commit and not re.fullmatch(r'[0-9a-f]{40}',host_source_commit):
        raise ValueError('Expected full host source commit')
    receipt_path=candidate/'package-manifest.json'
    if not receipt_path.is_file():receipt_path=candidate/'reports/playtest-manifest.private.json'
    receipt=json.loads(receipt_path.read_text(encoding='utf-8-sig'))
    files={}
    for entry in receipt['package_files']:
        name=entry['relative_path'];p=(candidate/name).resolve()
        if not p.is_relative_to(candidate.resolve()): raise ValueError('Outside manifest path')
        data=p.read_bytes()
        if sha(data).lower()!=entry['sha256'].lower(): raise ValueError('Changed candidate: '+name)
        if name in EXCLUDE or name in GUI or name in SUPPORT or name in ('README.md','THIRD_PARTY_NOTICES.md') or name.startswith('licenses/'): continue
        if name.startswith('reports/xm8-bolt-intake/'):
            name='assets/'+p.name;data=encoded(scrub(json.loads(data)))
        elif not (name.startswith(('assets/','build/','stage/','tools/')) or name in ('Play-BC2VR.ps1','Start-BC2VRSession.ps1','Stop-BC2VRSession.ps1','Start-NativeTrace.ps1')):
            raise ValueError('Unexpected payload: '+name)
        if Path(name).suffix.lower() not in ('.ps1','.exe','.dll','.txt','.json','.cfg','.fvrskin','.fvrprop','.fvrnprop','.wav'): raise ValueError('Unexpected payload type: '+name)
        files[name]=data
    if native_dll:
        for name,path,machine in (('build/x86/BC2NativeProbe.dll',native_dll,'0x14c'),
                                  ('build/x64/BC2XrHost.exe',host_exe,'0x8664')):
            data=path.read_bytes();info=pe_info(data)
            if info['machine']!=machine: raise ValueError('Wrong binary architecture: '+name)
            files[name]=data
        for name in RUNTIME: files[name]=(repo/name).read_bytes()
        # Candidate-provided tool scripts follow the same source as the rebuilt DLL.
        for name in [n for n in files if n.startswith('tools/')]:
            if (repo/name).is_file(): files[name]=(repo/name).read_bytes()
    # Validate the final selected bytes, including untouched candidate peers,
    # before writing an archive or an extracted installation.
    ipc_versions=validate_ipc_pair(files)
    if reload_audio:
        if ipc_versions.get("Audio")!=1:raise ValueError("Reload cues require matching Audio.v1 peers")
        files.update(load_reload_audio(reload_audio))
    if body_ammo:
        data=body_ammo.read_bytes()
        if not 0<len(data)<=67108864: raise ValueError('Invalid body ammo cache')
        files['build/body-ammo-assets.fvrprop']=data
    if authored_contacts:
        files['build/x86/authored-contacts-v1.txt']=authored_contacts.read_bytes()
    if native_props:
        manifest_path=native_props/'manifest.fvrnprop'
        manifest_data=manifest_path.read_bytes()
        packs=sorted(native_props.glob('*.fvrskin'))
        if not 0<len(manifest_data)<=131072 or manifest_data[:8]!=b'BC2NPR01' or not 1<=len(packs)<=32:
            raise ValueError('Invalid native prop bundle')
        files['build/x86/native-skin-props/manifest.fvrnprop']=manifest_data
        total=0
        for path in packs:
            data=path.read_bytes();total+=len(data)
            if not 0<len(data)<=8388608 or total>67108864: raise ValueError('Native prop bundle exceeds size limit')
            files['build/x86/native-skin-props/'+path.name]=data
    for name in ('BC2TestRange.dll', 'BC2TestRangeInject.exe'):
        data=(stage_dir/name).read_bytes()
        info=pe_info(data)
        if info['machine']!='0x14c' or any(x.lower().startswith(('vcruntime','msvcp','ucrtbase')) for x in info['imports']):
            raise ValueError('Helper must be x86 with static CRT: '+name)
        files['stage/'+name]=data
    if not re.fullmatch(r'[0-9a-f]{40}',stage_commit): raise ValueError('Expected full helper source commit')
    # Native bolt validation authenticates the sanitized render input, not its machine-local provenance paths.
    validation=json.loads(files['assets/xm8-bolt-v1-validation.json'])
    validation['export']['sha256']=sha(files['assets/xm8-bolt-v1-render-input.json'])
    files['assets/xm8-bolt-v1-validation.json']=encoded(validation)
    for name in SUPPORT: files[name]=((repo if native_dll else candidate)/name).read_bytes()
    for name in GUI: files[name]=(repo/name).read_bytes()
    files['stage/ranges/sp02-arsenal.cfg']=(repo/'tools/testrange/ranges/sp02-arsenal.cfg').read_bytes()
    files['stage/ranges/sp02-xm8-spas.cfg']=(repo/'tools/testrange/ranges/sp02-xm8-spas.cfg').read_bytes()
    for p in sorted((repo/'licenses').glob('*.txt')): files['licenses/'+p.name]=p.read_bytes()
    files['README.md']=(repo/'release/PORTABLE-README.md').read_bytes()
    notice=(repo/'release/THIRD_PARTY_NOTICES.md').read_text(encoding='utf-8-sig')
    notice=re.sub(r'`a69729a3348bc4dcb9be126a49d594074e477be7312f65180b0d082bf100fa5f`', '`'+sha(files['build/x64/runtime/openxr/win64/openxr_loader.dll'])+'`', notice)
    notice=notice.replace('Python, and a legally installed', 'and a legally installed').replace('`binary-manifest.json`','`package-manifest.json`').replace('game assets, or Python runtime','game executables/archives, or Python runtime')
    notice+='\nPython is optional for desktop window recovery; normal VR launch does not require it.\n'
    files['THIRD_PARTY_NOTICES.md']=notice.encode()
    for name,data in files.items():
        if Path(name).suffix in ('.ps1','.py','.json','.md','.txt','.cfg','.vbs'):
            text=data.decode('utf-8-sig')
            if re.search(r'[A-Za-z]:[\\/]+(?:Users|FrostbiteVR)(?:[\\/]|$)',text,re.I): raise ValueError('Local path in '+name)
    local_config=candidate/'config/local.json'
    if local_config.is_file():
        game=Path(json.loads(local_config.read_text(encoding='utf-8-sig'))['game_path'])/'BFBC2Game.exe'
        game_sha256=sha(game.read_bytes())
    else:
        # A portable package already carries the exact supported executable
        # identity. Repackaging does not require a private machine-local config.
        game_sha256=receipt.get('game_sha256','')
        if not re.fullmatch(r'[0-9a-fA-F]{64}',game_sha256):raise ValueError('Missing candidate game identity')
    manifest={'schema':1,'source_commit':source_commit or receipt['source_commit'],
              'host_source_commit':host_source_commit or source_commit or receipt['source_commit'],
              'base_candidate_source_commit':receipt['source_commit'],
              'helper_source_commit':stage_commit,
              'launcher_commit':subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),
              'game_sha256':game_sha256.lower(),'selected':False,
              'ipc_protocol_versions':ipc_versions,
              'package_files':[{'relative_path':name,'sha256':sha(data)} for name,data in sorted(files.items())]}
    files['package-manifest.json']=encoded(manifest)
    # One archive, no mirrored staging tree or copied development checkout.
    output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for name,data in sorted(files.items()): z.writestr('BC2-VR/'+name,data)
    if extract:
        # Unselected installable folder next to the archive; never overwrite an existing package.
        if (extract/'BC2-VR').exists(): raise ValueError('Extract target already has BC2-VR')
        with zipfile.ZipFile(output) as z: z.extractall(extract)
    print(json.dumps({'archive':str(output),'files':len(files),'sha256':sha(output.read_bytes()),
                      'extracted':str(extract/'BC2-VR') if extract else None}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--candidate',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--stage-dir',type=Path,required=True);p.add_argument('--stage-commit',required=True)
    p.add_argument('--native-dll',type=Path);p.add_argument('--host-exe',type=Path);p.add_argument('--source-commit')
    p.add_argument('--host-source-commit')
    p.add_argument('--authored-contacts',type=Path)
    p.add_argument('--native-props',type=Path,help='Private native material prop pack directory')
    p.add_argument('--body-ammo',type=Path,help='Private pouch/holster geometry cache (build/body-ammo-assets.fvrprop)')
    p.add_argument('--reload-audio-directory',type=Path,help='Locally extracted original physical reload cues')
    p.add_argument('--extract',type=Path,help='Also extract BC2-VR/ into this folder (not selected)')
    a=p.parse_args();package(Path(__file__).resolve().parents[1],a.candidate,a.output,a.stage_dir,a.stage_commit,a.native_dll,a.host_exe,a.source_commit,a.host_source_commit,a.authored_contacts,a.native_props,a.body_ammo,a.extract,a.reload_audio_directory)
