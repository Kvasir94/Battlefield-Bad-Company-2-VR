[CmdletBinding()]
param([int]$GameProcessId,[string]$GameStartedUtc,[string]$ReportFolder,[ValidateRange(1,200)][int]$RequestLifetimeMs=150,[switch]$Controllers,[switch]$Mirror,[switch]$NoDesktopEyeMirror,[string]$OpenXrLoader,[switch]$MotionAim,[switch]$BodyFollow,[switch]$Hands,[switch]$MuzzleFire,[switch]$TwoHandGrip,[switch]$SightFlip,[switch]$PhysicalReload,[switch]$MagazineReload,[switch]$BodyInventory,[switch]$BoatHeadAim,[switch]$BoatHeadFire,[switch]$Roomscale,[ValidateRange(0.01,1000)][float]$WorldUnitsPerMeter=1,[string]$Python,[switch]$RecordTape,[switch]$GunEvents,[ValidateRange(0,8192)][int]$RenderWidth=0,[ValidateRange(0,8192)][int]$RenderHeight=0,[switch]$SyntheticHands,[switch]$HeadsetAspect,[ValidateRange(0.25,2)][float]$RenderScale=1)
$ErrorActionPreference='Stop'
if($BoatHeadFire -and -not $BoatHeadAim){throw 'BoatHeadFire requires explicit BoatHeadAim'}
if($BoatHeadAim){$BodyFollow=$true}
# Candidate headset feature: opt-in native launcher sight interaction.
if($MagazineReload){$PhysicalReload=$true}
if($BodyInventory){$PhysicalReload=$true}
if($PhysicalReload){$TwoHandGrip=$true}
if($SightFlip){$TwoHandGrip=$true}
if($TwoHandGrip){$MuzzleFire=$true}
if($MuzzleFire){$Hands=$true}
if($Hands){$BodyFollow=$true}
if($BodyFollow){$MotionAim=$true; $Roomscale=$true}
if($MotionAim){$Controllers=$true}
$PSNativeCommandUseErrorActionPreference=$false
# Raw captures filled D: on 2026-10-08 and zeroed files mid-write: refuse below 5 GB free.
$reportsDrive=Get-PSDrive -Name ([IO.Path]::GetPathRoot($PSScriptRoot).Substring(0,1))
if($reportsDrive.Free -lt 5GB){throw ('Only {0:N1} GB free on {1}: free space before a capture run (old raw frames under reports\ are regenerable).' -f ($reportsDrive.Free/1GB),$reportsDrive.Root)}
# BC2XrHost loads runtime\openxr\win64\openxr_loader.dll beside itself. Source trees do not
# ship it (BUILDING.md); without it the host cannot open any headset runtime.
$loaderSource=Join-Path $PSScriptRoot 'runtime\openxr\win64\openxr_loader.dll'
$loaderHost=Join-Path $PSScriptRoot 'build\x64\runtime\openxr\win64\openxr_loader.dll'
if($OpenXrLoader){if(-not (Test-Path -LiteralPath $OpenXrLoader -PathType Leaf)){throw "No file at -OpenXrLoader $OpenXrLoader"}
    New-Item -ItemType Directory -Force (Split-Path $loaderSource) | Out-Null;Copy-Item -LiteralPath $OpenXrLoader -Destination $loaderSource -Force}
if(-not (Test-Path -LiteralPath $loaderHost -PathType Leaf) -and (Test-Path -LiteralPath $loaderSource -PathType Leaf)){
    New-Item -ItemType Directory -Force (Split-Path $loaderHost) | Out-Null;Copy-Item -LiteralPath $loaderSource -Destination $loaderHost}
if(-not (Test-Path -LiteralPath $loaderHost -PathType Leaf)){throw "No OpenXR loader for BC2XrHost. Put the Khronos x64 openxr_loader.dll at $loaderSource (see BUILDING.md), or pass -OpenXrLoader <path to openxr_loader.dll>."}
# -HeadsetAspect: size the BC2 window (= the per-eye render size; BC2 has no
# per-view targets independent of its back buffer, docs/re/BC2_LATE_LATCH_DEPTH_RE.md 5)
# to the runtime's recommended eye aspect instead of forcing 16:9 onto a ~0.9
# aspect FOV. RenderScale scales the recommended size; the result is fitted
# inside the left monitor's work area (window frame margin kept) and made even.
# 0 x 0 (default, without -HeadsetAspect): the exact runtime recommendation, the
# same rule as Play-BC2VR.ps1 and the packaged playtest path.
if(-not $HeadsetAspect -and ($RenderWidth -eq 0 -or $RenderHeight -eq 0)){
    if($RenderWidth -ne $RenderHeight -and ($RenderWidth -eq 0 -or $RenderHeight -eq 0)){throw 'Set both -RenderWidth and -RenderHeight, or neither.'}
    . (Join-Path $PSScriptRoot 'tools\BC2EyeRenderProfile.ps1')
    $probeArgs=@('--probe');if($OpenXrLoader){$probeArgs+=@('--loader',$OpenXrLoader)}
    $probeText=(& (Join-Path $PSScriptRoot 'build\x64\BC2XrHost.exe') @probeArgs 2>$null) -join "`n"
    $readiness=$null;try{$readiness=$probeText|ConvertFrom-Json}catch{}
    if(-not $readiness -or $readiness.state -ne 'runtime_ready'){throw 'BC2XrHost --probe did not report the recommended eye size (is the headset runtime running?). Pass -RenderWidth/-RenderHeight to override.'}
    $eyeProfile=Resolve-BC2EyeRenderProfile -Readiness $readiness
    $RenderWidth=$eyeProfile.Width;$RenderHeight=$eyeProfile.Height
}
if($HeadsetAspect){
    $probeJson=(& (Join-Path $PSScriptRoot 'build\x64\BC2XrHost.exe') --probe 2>$null) -join "`n"
    $probe=$null;try{$probe=$probeJson | ConvertFrom-Json}catch{}
    if(-not $probe -or $probe.state -ne 'runtime_ready' -or -not $probe.eye_width -or -not $probe.eye_height){throw 'HeadsetAspect: BC2XrHost --probe did not report the recommended eye size (is the headset runtime running?)'}
    Add-Type -AssemblyName System.Windows.Forms
    $area=([Windows.Forms.Screen]::AllScreens | Sort-Object {$_.Bounds.X} | Select-Object -First 1).WorkingArea
    $aspect=[double]$probe.eye_width/[double]$probe.eye_height
    $height=[Math]::Min([double]$probe.eye_height*$RenderScale,[double]($area.Height-64))
    $width=$height*$aspect
    if($width -gt $area.Width-16){$width=[double]($area.Width-16);$height=$width/$aspect}
    $RenderWidth=[int]([Math]::Floor($width/2)*2);$RenderHeight=[int]([Math]::Floor($height/2)*2)
    if($RenderWidth -lt 128 -or $RenderHeight -lt 128){throw "HeadsetAspect: fitted size ${RenderWidth}x${RenderHeight} is too small"}
    Write-Host ("HeadsetAspect: recommended {0}x{1} (aspect {2:N3}), scale {3}, render {4}x{5}" -f $probe.eye_width,$probe.eye_height,$aspect,$RenderScale,$RenderWidth,$RenderHeight)
}
$config=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'config\local.json') -Raw | ConvertFrom-Json
$expected=[IO.Path]::GetFullPath((Join-Path $config.game_path 'BFBC2Game.exe'))
if($GameProcessId){
    if(-not $GameStartedUtc){throw 'Expected game start time is required with GameProcessId.'}
    $targets=@(Get-Process -Id $GameProcessId -ErrorAction SilentlyContinue)
}else{
    $targets=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $expected})
}
if($targets.Count -ne 1){throw 'Leave BC2 running in campaign gameplay before starting VR.'}
$target=$targets[0];$null=$target.Handle;$target.Refresh()
if($target.HasExited -or $target.Path -ne $expected -or ($GameProcessId -and $target.StartTime.ToUniversalTime() -ne ([DateTime]$GameStartedUtc).ToUniversalTime())){throw 'Expected launched BC2 process exited or changed identity; VR attachment was not started.'}
if(Get-Process BC2XrHost,BC2NativeTrace -ErrorAction SilentlyContinue){throw 'A BC2 VR session or trace is already active.'}
if(-not $ReportFolder){$ReportFolder=Join-Path $PSScriptRoot ('reports\native-xr-session-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))}
$folder=[IO.Path]::GetFullPath($ReportFolder)
if(Test-Path -LiteralPath $folder){throw 'A new session report directory is required'}
New-Item -ItemType Directory -Path $folder | Out-Null
$channel=Join-Path $folder 'channel.txt';$ready=Join-Path $folder 'ready.json';$stopFile=Join-Path $folder 'stop.request'
$state=[ordered]@{state='starting';started_utc=[DateTime]::UtcNow.ToString('o');game_pid=$target.Id;host_pid=$null;supervisor_pid=$PID;stop_file=$stopFile;until_game_or_runtime_exit=$true;uncap_mirror=$true;controllers=[bool]$Controllers;motion_aim=[bool]$MotionAim;roomscale_reference=[bool]$Roomscale;world_units_per_meter=$WorldUnitsPerMeter;body_follow=[bool]$BodyFollow;collision_roomscale_headset_verified=$false;hand_translation=[bool]$Hands;hand_pose_headset_verified=$false;muzzle_fire=[bool]$MuzzleFire;two_hand_grip=[bool]$TwoHandGrip;sight_flip=[bool]$SightFlip;physical_reload=[bool]$PhysicalReload;magazine_reload=[bool]$MagazineReload;magazine_reload_headset_verified=$false;boat_head_aim=[bool]$BoatHeadAim;boat_head_fire=[bool]$BoatHeadFire;boat_head_aim_mode=$(if($BoatHeadFire){2}elseif($BoatHeadAim){1}else{0});boat_head_aim_headset_verified=$false;body_inventory=[bool]$BodyInventory;empty_hands_enabled=[bool]$BodyInventory;body_holster_profile_mask=$(if($BodyInventory){3}else{0});body_holster_headset_verified=$false;sight_flip_headset_verified=$false;two_hand_headset_verified=$false;muzzle_fire_headset_verified=$false;request_lifetime_ms=$RequestLifetimeMs;native_probe_started=$false;error=$null;binary_sha256=[ordered]@{}}
foreach($binary in @('build\x86\BC2NativeProbe.dll','build\x86\BC2NativeTrace.exe','build\x64\BC2XrHost.exe')){$state.binary_sha256[$binary]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $binary)).Hash}
$state.window_recovery=[ordered]@{startup=$null;shutdown=$null;original_hwnd=$null;python=$null}
$windowPython=$null
function Resolve-BC2WindowPython {
    if($Python){return [IO.Path]::GetFullPath($Python)}
    if($env:FVR_PYTHON){return [IO.Path]::GetFullPath($env:FVR_PYTHON)}
    foreach($command in @(Get-Command python.exe,python3.exe -CommandType Application -ErrorAction SilentlyContinue)){
        if($command.Source -and $command.Source -notmatch '[\\/]WindowsApps[\\/]'){return $command.Source}
    }
    throw 'Python was not found for BC2 window recovery; use -Python or FVR_PYTHON.'
}
function Invoke-BC2WindowRecovery([string]$Phase) {
    try {
        if(-not $script:windowPython){$script:windowPython=Resolve-BC2WindowPython}
        $state.window_recovery.python=$script:windowPython
        $windowReport=Join-Path $folder ('window-'+$Phase+'.json')
        $windowArgs=@('--pid',[string]$target.Id,'--show','--report',$windowReport,'--report-root',$folder)
        # Mirror placement is independent of native render targets; Windows can clamp a headset-sized window to the desktop.
        if($Phase -eq 'startup'){$windowArgs+='--left-monitor'}
        else{
            if(-not $state.window_recovery.original_hwnd){throw 'No original BC2 window was recorded at startup; cleanup will not select a replacement.'}
            $windowArgs+=@('--hwnd',[string]$state.window_recovery.original_hwnd)
        }
        $messages=@(& $script:windowPython (Join-Path $PSScriptRoot 'tools\game_window.py') @windowArgs 2>&1)
        if($LASTEXITCODE -ne 0){throw ('BC2 window recovery failed: '+($messages -join [Environment]::NewLine))}
        $result=Get-Content -LiteralPath $windowReport -Raw | ConvertFrom-Json
        $state.window_recovery[$Phase]=$result
        if($Phase -eq 'startup'){$state.window_recovery.original_hwnd=$result.after.hwnd}
    } catch {
        # Window recovery failure must neither steal focus nor mask native/XR
        # cleanup. Preserve it in the session report for an explicit follow-up.
        $state.window_recovery[$Phase]=[ordered]@{error=$_.Exception.Message;activation_requested=$false}
        Write-Warning ('BC2 '+$Phase+' window recovery: '+$_.Exception.Message)
    }
}
$state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $folder 'session.json')
$hostArgs=@('--ipc','--until-game-exit',$target.Id,'--request-lifetime',$RequestLifetimeMs,'--eye-width',[string]$RenderWidth,'--eye-height',[string]$RenderHeight,'--channel-file',('"'+$channel+'"'),'--ready-file',('"'+$ready+'"'),'--stop-file',('"'+$stopFile+'"'))
# Dropped magazines are host-composed props in every physical reload session, not only with body inventory.
$bodyPropsCache=Join-Path $PSScriptRoot 'build\body-ammo-assets.fvrprop'
if(($BodyInventory -or $PhysicalReload -or $MagazineReload) -and (Test-Path -LiteralPath $bodyPropsCache -PathType Leaf)){$hostArgs+=@('--body-props-cache',('"'+$bodyPropsCache+'"'))}
elseif($BodyInventory){throw 'Body inventory needs build\body-ammo-assets.fvrprop (tools\Prepare-BodyAmmoAssets.ps1)'}
if($SyntheticHands){$hostArgs+=@('--synthetic-hands','--ignore-presence')}
elseif($Controllers){$hostArgs+='--controllers'}
# Desktop window with the left eye exactly as the headset receives it (host props included).
if($Mirror){$hostArgs+='--mirror-window'}
# Developer input tape (docs/REPLAY_INPUT.md): the DLL records the gathered input, the host its companion.
if($RecordTape){if(-not $Controllers){throw 'RecordTape requires Controllers'};$hostArgs+=@('--record-tape',('"'+(Join-Path $folder 'host-input.fvrtape')+'"'))}
if($Roomscale){$hostArgs+='--roomscale'}
$hostArgs+=@('--world-units-per-meter',$WorldUnitsPerMeter.ToString([Globalization.CultureInfo]::InvariantCulture))
function Stop-BC2OwnedXrHost {
    param([Parameter(Mandatory=$true)][Diagnostics.Process]$Process,
          [Parameter(Mandatory=$true)][string]$StopFile,
          [ValidateRange(100,10000)][int]$WaitMilliseconds=10000)
    $result=[ordered]@{stop_requested=$false;graceful=$false;forced=$false;exited=$false;error=$null}
    try {
        # Retain the process handle before waiting; cleanup must not select a
        # different process if Windows later reuses the host PID.
        $null=$Process.Handle
        if($Process.HasExited){$result.exited=$true;return $result}
        try {
            [IO.File]::WriteAllText($StopFile,'Requested after BC2 session ended')
            $result.stop_requested=$true
        } catch {$result.error=$_.Exception.Message}
        if($Process.WaitForExit($WaitMilliseconds)){
            $result.graceful=$true;$result.exited=$true;return $result
        }
        # Preserve the previous bounded failure cleanup only after the host
        # had an opportunity to flush diagnostics through its normal exit.
        $Process.Kill();$result.forced=$true
        $result.exited=$Process.WaitForExit(2000)
    } catch {$result.error=$_.Exception.Message}
    return $result
}
$xr=$null
try {
    Invoke-BC2WindowRecovery 'startup'
    . (Join-Path $PSScriptRoot 'tools\BC2RenderSize.ps1')
    $state.native_render=Wait-BC2RenderSize -Process $target -Observer (Join-Path $PSScriptRoot 'build\x64\BC2Observe.exe') -GamePath $config.game_path -Width $RenderWidth -Height $RenderHeight -ReportPath (Join-Path $folder 'native-render.json')
    $state.synthetic_hands=[bool]$SyntheticHands
    $state.real_controllers=[bool]($Controllers -and -not $SyntheticHands)
    $xr=Start-Process -FilePath (Join-Path $PSScriptRoot 'build\x64\BC2XrHost.exe') -ArgumentList $hostArgs -RedirectStandardOutput (Join-Path $folder 'host.json') -RedirectStandardError (Join-Path $folder 'runtime.stderr.log') -WindowStyle Hidden -PassThru
    $null=$xr.Handle
    $state.host_pid=$xr.Id
    $watch=[Diagnostics.Stopwatch]::StartNew()
    # The host creates this file before writing/closing its JSON. Existence
    # alone can expose an empty/partial document; wait for the complete fields.
    $requirements=$null
    while($null -eq $requirements -and -not $xr.HasExited -and $watch.Elapsed.TotalSeconds -lt 30){
        if(Test-Path -LiteralPath $ready){
            try {
                $candidateReady=Get-Content -LiteralPath $ready -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
                if($null -ne $candidateReady){
                    $fields=$candidateReady.PSObject.Properties.Name
                    if($fields -contains 'session_created' -and $fields -contains 'width' -and $fields -contains 'height'){$requirements=$candidateReady}
                }
            } catch {
                # A concurrent writer can still hold or be finishing the file.
                # A malformed/absent report remains bounded by the startup timeout.
            }
        }
        if($null -eq $requirements){Start-Sleep -Milliseconds 100;$xr.Refresh()}
    }
    if($null -eq $requirements){throw 'OpenXR session did not become ready; no probe attached.'}
    if(-not $requirements.session_created -or $requirements.width -ne $RenderWidth -or $requirements.height -ne $RenderHeight){throw 'Unexpected XR session dimensions'}
    $token=(Get-Content -LiteralPath $channel -Raw).Trim()
    $state.state='running';$state.native_probe_started=$true
    $state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $folder 'session.json')
    Write-Output $folder
    & (Join-Path $PSScriptRoot 'Start-NativeTrace.ps1') -Controllers:$Controllers -MotionAim:$MotionAim -BodyFollow:$BodyFollow -Hands:$Hands -MuzzleFire:$MuzzleFire -TwoHandGrip:$TwoHandGrip -SightFlip:$SightFlip -PhysicalReload:$PhysicalReload -MagazineReload:$MagazineReload -BodyInventory:$BodyInventory -BoatHeadAim:$BoatHeadAim -BoatHeadFire:$BoatHeadFire -RecordTape $(if($RecordTape){Join-Path $folder 'input.fvrtape'}else{''}) -GunEvents:$GunEvents -FrameChannel $token -UntilHostExit -HostPid $xr.Id -UncapMirror -DesktopEyeMirror:(-not $NoDesktopEyeMirror)
    $state.state='ended'
} catch {
    $state.state='failed';$state.error=$_.Exception.Message
    # Save host diagnostics even when the game/probe fails first. The owned
    # process receives the same graceful stop request as a normal test end.
    # Game and SteamVR processes are never targets of this failure cleanup.
    if($xr){$state.host_shutdown=Stop-BC2OwnedXrHost -Process $xr -StopFile $stopFile}
    throw
} finally {
    # A game-first end can return before the host has flushed its report.
    # Wait on the retained owned handle, then use the same bounded cleanup.
    if($xr -and -not $xr.HasExited -and -not $xr.WaitForExit(2000)){
        $state.host_shutdown=Stop-BC2OwnedXrHost -Process $xr -StopFile $stopFile
    }
    $target.Refresh();$state.game_exited=$target.HasExited;$state.ended_utc=[DateTime]::UtcNow.ToString('o')
    if($xr){$state.host_exited=$xr.HasExited;if($xr.HasExited){$state.host_exit_code=$xr.ExitCode}}
    if(-not $state.game_exited){Invoke-BC2WindowRecovery 'shutdown'}
    else{$state.window_recovery.shutdown=[ordered]@{skipped='game_exited';activation_requested=$false}}
    $state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $folder 'session.json')
}
