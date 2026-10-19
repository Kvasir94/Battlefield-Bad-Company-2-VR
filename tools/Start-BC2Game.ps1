[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('VDXR','SteamVR')][string]$Runtime,
    # Overrides the runtime manifest path for the selected runtime (process-local only; registry untouched).
    # Start BC2 normally in campaign first; attach without rewriting its graphics INI.
    [switch]$AttachToRunningGame,
    # Start the installed game normally under this verified feature environment; no INI rewrite.
    [switch]$PrepareGameOnly,
    # Requires a fresh game prepared with this flag; diagnostic only, no recoil transfer.
    [switch]$AimAngleTrace,
    # Observe native recoil ordering and shot composition; does not alter recoil.
    [switch]$NativeRecoilTrace,
    # Native weapon/hand recoil, preserving the stable headset and native shot rotation.
    [switch]$NoNativeRecoil,
    [switch]$NoNativeXm8Rack,
    [switch]$NoReloadAudio,
    [string]$ReloadAudioDevice='',
    [ValidateRange(0.0,1.0)][double]$ReloadAudioGain=0.8,
    # Native fore-end travel per metre of physical hand travel.
    [ValidateRange(0.5,3.0)][double]$PumpTravelScale=1.35,
    # Optional local exact-profile audition: one rack cue attempt per process.
    # Empty disables the audition. Profiles are never included in public packages.
    [string]$RackAudioProfileFile,
    [switch]$CameraResponseTrace,
    [switch]$SprintInputLog,
    [string]$ReportFolder,
    [string]$RuntimeJson,
    [ValidateSet('smooth','snap')][string]$TurnMode='smooth',
    [ValidateRange(30,360)][int]$SmoothTurnDegPerSec=90,
    # Kill switch: FVR_XR_ASYNC_CONSUMER_COPY=0 (synchronous eye copy).
    [switch]$SynchronousEyeCopy,
    # Kill switch: FVR_BC2_VIEWPORT_SYNC=0.
    [switch]$NoViewportSync,
    # Kill switch: FVR_XR_RETAIN_ON_COPY_TIMEOUT=0 (host drops instead of retaining the last pair on an eye-copy timeout).
    [switch]$NoRetainOnCopyTimeout,
    # Bounded late stereo capture (diagnostic, native stream mode only). Code default is Off;
    # this test build defaults to ByRequest (orchestrator decision).
    #   Off       FVR_BC2_STEREO_CAPTURE=0
    #   AtStart   FVR_BC2_STEREO_CAPTURE=1  armed at startup
    #   ByRequest FVR_BC2_STEREO_CAPTURE=2  armed by auto arms and by creating
    #             <native-trace folder>\stereo-capture.request (consumed into stereo-capture\request-N.txt)
    [ValidateSet('Off','AtStart','ByRequest')][string]$StereoCapture='ByRequest',
    [ValidateRange(0,4)][int]$StereoCaptureAutoArms=1,      # FVR_BC2_STEREO_CAPTURE_AUTO (code default 1)
    [ValidateRange(1,16)][int]$StereoCaptureMaxArms=6,      # FVR_BC2_STEREO_CAPTURE_MAX_ARMS (code default 6)
    # Stick-cycle weapon switching. Kill switches: FVR_BC2_STICK_CYCLE=0, FVR_BC2_STICK_CYCLE_MELEE_GRENADE=0.
    [switch]$NoStickCycle,
    [switch]$NoStickCycleMeleeGrenade,
    [ValidateRange(250,1500)][int]$StickCycleBackMs=450,    # FVR_BC2_STICK_CYCLE_BACK_MS (code default 450)
    # First-person weapon-effect anchoring. Kill switch: FVR_BC2_EFFECT_ANCHOR=0.
    [switch]$NoEffectAnchor,
    [ValidateRange(0,3000)][int]$EffectAnchorMs=400,        # FVR_BC2_EFFECT_ANCHOR_MS (code default 400)
    # Dropped magazine physics. Kill switch: FVR_BC2_DROPPED_MAGS=0.
    [switch]$NoDroppedMags,
    [ValidateRange(1,600)][int]$DroppedMagLifetimeS=30,     # FVR_BC2_DROPPED_MAG_LIFETIME_S (code default 30)
    [ValidateRange(1,6)][int]$DroppedMagMax=4,              # FVR_BC2_DROPPED_MAG_MAX (code default 4)
    [ValidateRange(1,50)][int]$DroppedMagVisibleM=8,        # FVR_BC2_DROPPED_MAG_VISIBLE_M (code default 8)
    # Interaction zones / magnetism / hover cues / support kept on flick.
    # Kill switches: FVR_INTERACT_ZONES=0, FVR_INTERACT_MAGNET=0, FVR_INTERACT_HOVER_CUES=0, FVR_BC2_SUPPORT_KEEP_ON_FLICK=0.
    [switch]$NoInteractZones,
    [ValidateRange(0.5,2.0)][double]$InteractZoneScale=1.0, # FVR_INTERACT_ZONE_SCALE (code default 1)
    [switch]$NoInteractMagnet,
    [switch]$NoInteractHoverCues,
    # Visible pouch and charging-handle guidance. Kill switch: FVR_BC2_RELOAD_GUIDANCE=0.
    [switch]$NoReloadGuidance,
    [switch]$NoSupportKeepOnFlick,
    # FVR_HANDS_MODE: ik (code default, existing IK arms) or floating (floating hands).
    [ValidateSet('ik','floating')][string]$HandsMode='ik',
    # Kill switch: FVR_AUTHORED_CONTACTS=0 (no authored contact library).
    [switch]$NoAuthoredContacts,
    # Kill switch: FVR_XM8_BOLT=0 (no bolt asset load, draw, palette edit or cycle).
    # Otherwise FVR_XM8_BOLT_ASSET is set to the hash-checked absolute render-input path.
    [switch]$NoXm8Bolt,
    # Kill switch: FVR_BC2_SHORT_B_RACK_AFTER_INSERT=0 (old behaviour: short B ejects a seated, ready magazine).
    [switch]$NoShortBRackAfterInsert,
    # Test-range stage folder. Empty = same resolution as Play-BC2VR.ps1
    # (FVR_BC2_STAGE, then <repo>\stage if it holds BC2TestRange.dll, then %USERPROFILE%\fvr-testrange-stage).
    [string]$Stage,
    [string]$Config='ranges/sp02-xm8-only.cfg',
    [ValidateRange(128,8192)][int]$RenderWidth=1920,
    [ValidateRange(128,8192)][int]$RenderHeight=1080,
    [ValidateSet('Preserve','HighCompatible')][string]$GraphicsProfile='Preserve'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$inv=[Globalization.CultureInfo]::InvariantCulture

$candidateReceipt='package-manifest.json'
$receipt=Get-Content -LiteralPath (Join-Path $root $candidateReceipt) -Raw|ConvertFrom-Json
$candidateCommit=$receipt.source_commit
$actual=[ordered]@{}
foreach($entry in $receipt.package_files){$actual[$entry.relative_path]=$entry.sha256}
if(-not $Stage){$Stage=Join-Path $root 'stage'}
$contactsSetting=if($NoAuthoredContacts){'0'}else{Join-Path $root 'build/x86/authored-contacts-v1.txt'}
$boltSetting=if($NoXm8Bolt){''}else{Join-Path $root 'assets/xm8-bolt-v1-render-input.json'}
. (Join-Path $PSScriptRoot 'BC2Package.ps1')
$rackAudioProfiles=if($RackAudioProfileFile){Read-BC2RackAudioProfiles $RackAudioProfileFile}else{''}

# ---- Runtime selection: explicit, process-local, never the OpenXR registry ----
if(-not $RuntimeJson){
    $RuntimeJson=Resolve-BC2Runtime $Runtime
}
if(-not (Test-Path -LiteralPath $RuntimeJson -PathType Leaf)){throw 'Choose an installed OpenXR runtime in the launcher.'}

# ---- Environment for this launch (restored afterwards so the calling shell is not left modified) ----
function OnOff([bool]$off){if($off){'0'}else{'1'}}
$settings=[ordered]@{
    FVR_BC2_RENDER_WIDTH             =[string]$RenderWidth
    FVR_BC2_RENDER_HEIGHT            =[string]$RenderHeight
    FVR_BC2_RACK_AUDIO_AUDITION       =$(if($rackAudioProfiles){'1'}else{'0'})
    FVR_BC2_RACK_AUDIO_PROFILES       =$rackAudioProfiles
    FVR_BC2_M9_RACK_AUDIO_AUDITION    =''
    FVR_BC2_M9_RACK_AUDIO_PROFILE     =''
    FVR_BC2_RELOAD_GUIDANCE          =OnOff $NoReloadGuidance
    FVR_BC2_CAMERA_RESPONSE_TRACE     =$(if($CameraResponseTrace){'1'}else{'0'})
    FVR_BC2_TEST_INPUT_LOG            =$(if($SprintInputLog){'1'}else{'0'})
    FVR_BC2_AIM_ANGLE_TRACE           =$(if($AimAngleTrace){'1'}else{'0'})
    FVR_BC2_NATIVE_RECOIL_OBSERVE     =$(if($NativeRecoilTrace){'1'}else{'0'})
    FVR_BC2_NATIVE_RECOIL_TRANSFER    =OnOff $NoNativeRecoil
    FVR_BC2_NATIVE_XM8_RACK           =OnOff $NoNativeXm8Rack
    FVR_BC2_RELOAD_AUDIO              =OnOff $NoReloadAudio
    FVR_BC2_RELOAD_AUDIO_DEVICE       =$ReloadAudioDevice
    FVR_BC2_RELOAD_AUDIO_GAIN         =$ReloadAudioGain.ToString($inv)
    FVR_BC2_PUMP_TRAVEL_SCALE         =$PumpTravelScale.ToString($inv)
    XR_RUNTIME_JSON                  =$RuntimeJson
    FVR_TURN_MODE                    =$TurnMode
    FVR_SMOOTH_TURN_DEG_PER_SEC      ="$SmoothTurnDegPerSec"
    FVR_XR_ASYNC_CONSUMER_COPY       =OnOff $SynchronousEyeCopy
    FVR_BC2_VIEWPORT_SYNC            =OnOff $NoViewportSync
    # Copy-timeout retain fix
    FVR_XR_RETAIN_ON_COPY_TIMEOUT    =OnOff $NoRetainOnCopyTimeout
    # Bounded late stereo capture
    FVR_BC2_STEREO_CAPTURE           =$(switch($StereoCapture){'Off'{'0'};'AtStart'{'1'};'ByRequest'{'2'}})
    FVR_BC2_STEREO_CAPTURE_AUTO      ="$StereoCaptureAutoArms"
    FVR_BC2_STEREO_CAPTURE_MAX_ARMS  ="$StereoCaptureMaxArms"
    # Stick-cycle weapon switching
    FVR_BC2_STICK_CYCLE              =OnOff $NoStickCycle
    FVR_BC2_STICK_CYCLE_MELEE_GRENADE=OnOff $NoStickCycleMeleeGrenade
    FVR_BC2_STICK_CYCLE_BACK_MS      ="$StickCycleBackMs"
    # Weapon-effect anchor
    FVR_BC2_EFFECT_ANCHOR            =OnOff $NoEffectAnchor
    FVR_BC2_EFFECT_ANCHOR_MS         ="$EffectAnchorMs"
    # Dropped magazine physics
    FVR_BC2_DROPPED_MAGS             =OnOff $NoDroppedMags
    FVR_BC2_DROPPED_MAG_LIFETIME_S   ="$DroppedMagLifetimeS"
    FVR_BC2_DROPPED_MAG_MAX          ="$DroppedMagMax"
    FVR_BC2_DROPPED_MAG_VISIBLE_M    ="$DroppedMagVisibleM"
    # Interaction zones, magnetism, hover cues, support grip kept on weapon flick
    FVR_INTERACT_ZONES               =OnOff $NoInteractZones
    FVR_INTERACT_ZONE_SCALE          =$InteractZoneScale.ToString($inv)
    FVR_INTERACT_MAGNET              =OnOff $NoInteractMagnet
    FVR_INTERACT_HOVER_CUES          =OnOff $NoInteractHoverCues
    FVR_BC2_SUPPORT_KEEP_ON_FLICK    =OnOff $NoSupportKeepOnFlick
    # Hands presentation and authored contacts
    FVR_HANDS_MODE                   =$HandsMode
    FVR_AUTHORED_CONTACTS            =$contactsSetting
    # XM8 VR bolt (presentation + left-hand-gated charging-handle cycle)
    FVR_XM8_BOLT                     =OnOff $NoXm8Bolt
    FVR_XM8_BOLT_ASSET               =$boltSetting
    # Short B after a completed magazine insertion racks/completes instead of ejecting
    FVR_BC2_SHORT_B_RACK_AFTER_INSERT=OnOff $NoShortBRackAfterInsert
}

# Resolve the stage exactly as Play-BC2VR.ps1 does, then pass it explicitly so the manifest names it.
if(-not $AttachToRunningGame -and -not $PrepareGameOnly){
    if(-not $Stage){$Stage=$(if($env:FVR_BC2_STAGE){$env:FVR_BC2_STAGE}elseif(Test-Path (Join-Path $root 'stage\BC2TestRange.dll')){Join-Path $root 'stage'}else{Join-Path $env:USERPROFILE 'fvr-testrange-stage'})}
    if(-not (Test-Path -LiteralPath (Join-Path $Stage 'BC2TestRange.dll') -PathType Leaf)){throw "Test-range stage has no BC2TestRange.dll: $Stage"}
}else{$Stage=$null}
$saved=@{}
foreach($k in $settings.Keys){$saved[$k]=[Environment]::GetEnvironmentVariable($k,'Process')}

if($PrepareGameOnly -and $AttachToRunningGame){throw 'Choose preparation or attachment'}
if($ReportFolder -and -not $AttachToRunningGame){throw '-ReportFolder requires attachment'}
$preparedRecord=Join-Path $root 'reports/combined-prepared-game.json'
if($PrepareGameOnly -and @(Get-Process BFBC2Game,BC2XrHost,BC2NativeTrace -ErrorAction SilentlyContinue).Count){throw 'Close the previous game/session normally before preparing a fresh game'}
if(($AimAngleTrace -or $NativeRecoilTrace -or $rackAudioProfiles -or $CameraResponseTrace -or $SprintInputLog) -and $AttachToRunningGame){
    if(-not (Test-Path $preparedRecord)){throw 'First prepare a fresh game with -PrepareGameOnly and the same diagnostic flags'}
    $prepared=Get-Content $preparedRecord -Raw | ConvertFrom-Json
    $game=Get-Process -Id $prepared.game_pid -ErrorAction Stop
    if($game.ProcessName -ne 'BFBC2Game' -or $game.StartTime.ToUniversalTime() -ne ([DateTime]$prepared.game_started_utc).ToUniversalTime() -or $game.Path -ne $prepared.game_path -or $prepared.candidate_commit -ne $candidateCommit -or ($AimAngleTrace -and $prepared.environment.FVR_BC2_AIM_ANGLE_TRACE -ne '1') -or ($CameraResponseTrace -and $prepared.environment.FVR_BC2_CAMERA_RESPONSE_TRACE -ne '1') -or ($SprintInputLog -and $prepared.environment.FVR_BC2_TEST_INPUT_LOG -ne '1')){throw 'Prepared diagnostic game identity does not match'}
    foreach($key in $settings.Keys){if($key -like 'FVR_*' -and [string]$prepared.environment.$key -ne [string]$settings[$key]){throw "Keep the prepared game feature/profile setting: $key"}}
}
$manifest=[ordered]@{
    prepared_utc=[DateTime]::UtcNow.ToString('o')
    candidate_commit=$candidateCommit
    candidate_receipt=$candidateReceipt
    runtime=$Runtime
    environment=$settings
    render_size="${RenderWidth}x${RenderHeight}"
    stage=$Stage
    stage_testrange_sha256=$(if($Stage){(Get-FileHash -LiteralPath (Join-Path $Stage 'BC2TestRange.dll') -Algorithm SHA256).Hash}else{$null})
    attach_to_running_game=[bool]$AttachToRunningGame
    binary_sha256=$actual
    headset_tested=$false
    prepare_game_only=[bool]$PrepareGameOnly
    angle_trace_requested=[bool]$AimAngleTrace
    native_recoil_trace_requested=[bool]$NativeRecoilTrace
    camera_response_trace_requested=[bool]$CameraResponseTrace
    sprint_input_log_requested=[bool]$SprintInputLog
    recoil_transfer_implemented=$true
    session_report_folder=$ReportFolder
}
$manifestPath=Join-Path $root ('reports\headset-candidate-launch-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'.json')
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath
Write-Host "Candidate $candidateCommit on $Runtime ($RuntimeJson); turn=$TurnMode/$SmoothTurnDegPerSec; hands=$HandsMode; stereo capture=$StereoCapture; stage=$Stage; manifest $manifestPath"

try{
    foreach($k in $settings.Keys){
        if([string]::IsNullOrEmpty($settings[$k])){Remove-Item -LiteralPath "Env:$k" -ErrorAction SilentlyContinue}
        else{Set-Item -LiteralPath "Env:$k" -Value $settings[$k]}
    }
    if($PrepareGameOnly){
        # Fixture switches are removed only from the fresh game's inherited environment.
        foreach($entry in @(Get-ChildItem Env: | Where-Object { ($_.Name -like 'FVR_BC2_PROBE_*' -or $_.Name -like 'FVR_BC2_TEST_*') -and -not ($SprintInputLog -and $_.Name -eq 'FVR_BC2_TEST_INPUT_LOG') })){
            if(-not $saved.ContainsKey($entry.Name)){$saved[$entry.Name]=$entry.Value}
            Remove-Item -LiteralPath ('Env:'+$entry.Name)
        }
        $preparedGameConfig=Get-Content (Join-Path $root 'config/local.json') -Raw | ConvertFrom-Json
        $exe=Join-Path $preparedGameConfig.game_path 'BFBC2Game.exe'
        if(-not (Test-Path -LiteralPath $exe -PathType Leaf)){throw 'Configured installed game is missing'}
        $game=Start-Process -FilePath $exe -WorkingDirectory $preparedGameConfig.game_path -WindowStyle Hidden -PassThru
        [ordered]@{game_pid=$game.Id;game_started_utc=$game.StartTime.ToUniversalTime().ToString('o');game_path=$exe;candidate_commit=$candidateCommit;environment=$settings;graphics_ini_changed=$false;launch_manifest=$manifestPath} | ConvertTo-Json -Depth 5 | Set-Content $preparedRecord
        Write-Host 'Game prepared. Enter campaign gameplay, then invoke this launcher with -AttachToRunningGame and the same feature/profile options.'
    }elseif($AttachToRunningGame){
        & (Join-Path $root 'Start-BC2VRSession.ps1') -ReportFolder $ReportFolder -MagazineReload -GunEvents -RenderWidth $RenderWidth -RenderHeight $RenderHeight
    }else{
        & (Join-Path $root 'Play-BC2VR.ps1') -Stage $Stage -Config $Config -RenderWidth $RenderWidth -RenderHeight $RenderHeight -GraphicsProfile $GraphicsProfile
    }
}finally{
    foreach($k in $saved.Keys){
        if($null -eq $saved[$k]){Remove-Item -LiteralPath "Env:$k" -ErrorAction SilentlyContinue}
        else{Set-Item -LiteralPath "Env:$k" -Value $saved[$k]}
    }
}
