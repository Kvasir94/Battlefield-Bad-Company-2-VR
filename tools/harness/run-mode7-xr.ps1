# Unattended XM8 fixture through the real OpenXR compositor. All gameplay
# input is synthetic; this never blends gestures with a person's controllers.
[CmdletBinding()]
param([string]$Repo=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),[string]$Stage=$(if($env:FVR_BC2_STAGE){$env:FVR_BC2_STAGE}else{Join-Path $env:USERPROFILE 'fvr-testrange-stage'}),[string]$InputTape,
 [ValidateRange(128,8192)][int]$RenderWidth=1920,[ValidateRange(128,8192)][int]$RenderHeight=1080,[switch]$DesktopCapture,[switch]$RealControllers)
$ErrorActionPreference='Stop'
if($RealControllers -and $InputTape){throw 'Real controllers and synthetic tape replay are separate runs'}
if(Get-Process BFBC2Game,BC2NativeTrace,BC2XrHost -ErrorAction SilentlyContinue){throw 'A game or VR session is already running'}
$folder=Join-Path $Repo ('reports\physical-xr-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $folder | Out-Null
$channel=Join-Path $folder 'channel.txt';$ready=Join-Path $folder 'ready.json'
$gamePath=(Get-Content -LiteralPath (Join-Path $Repo 'config\local.json') -Raw | ConvertFrom-Json).game_path
$previousRack=$env:FVR_BC2_CHAMBER_RACK
$env:FVR_BC2_CHAMBER_RACK='1'
$priorTraces=@(Get-ChildItem (Join-Path $Repo 'reports') -Directory -Filter 'native-trace-*' | ForEach-Object FullName)
$gameProcess=$null;$xrProcess=$null
try {
 & (Join-Path $Repo 'Play-BC2VR.ps1') -Flat -GamePath $gamePath -Stage $Stage -RenderWidth $RenderWidth -RenderHeight $RenderHeight | Select-Object -Last 1
 $gameProcess=Get-Process BFBC2Game -ErrorAction Stop
 $null=$gameProcess.Handle
 & python (Join-Path $Repo 'tools\game_window.py') --pid $gameProcess.Id --left-monitor --client-size $RenderWidth $RenderHeight --report (Join-Path $folder 'window.json')
 if($LASTEXITCODE -ne 0){throw 'Chair-test client sizing failed'}
 . (Join-Path $Repo 'tools\BC2RenderSize.ps1')
 $null=Wait-BC2RenderSize -Process $gameProcess -Observer (Join-Path $Repo 'build\x64\BC2Observe.exe') -GamePath $gamePath -Width $RenderWidth -Height $RenderHeight -ReportPath (Join-Path $folder 'native-render.json')
 $xrArgs=@('--ipc','--seconds','45','--mirror-window','--eye-width',[string]$RenderWidth,'--eye-height',[string]$RenderHeight,
  '--channel-file',('"'+$channel+'"'),'--ready-file',('"'+$ready+'"'),
  '--record-tape',('"'+(Join-Path $folder 'host-input.fvrtape')+'"'))
 if($RealControllers){$xrArgs+=@('--controllers','--roomscale')}
 else {$xrArgs+=@('--synthetic-hands','--ignore-presence')}
 $cache=Join-Path $Repo 'build\body-ammo-assets.fvrprop'
 if(Test-Path -LiteralPath $cache){$xrArgs+=@('--body-props-cache',('"'+$cache+'"'))}
 $xrProcess=Start-Process -FilePath (Join-Path $Repo 'build\x64\BC2XrHost.exe') -ArgumentList $xrArgs -WindowStyle Hidden -PassThru `
  -RedirectStandardOutput (Join-Path $folder 'host.json') -RedirectStandardError (Join-Path $folder 'host.stderr.log')
 $null=$xrProcess.Handle
 # The channel exists before headset discovery. It is not session readiness.
 $requirements=$null
 for($n=0;$n -lt 120 -and !$xrProcess.HasExited -and $null -eq $requirements;$n++){
  if(Test-Path -LiteralPath $ready){try {
   $candidate=Get-Content -LiteralPath $ready -Raw | ConvertFrom-Json -ErrorAction Stop
   if($candidate.session_created -and $candidate.width -eq $RenderWidth -and $candidate.height -eq $RenderHeight){$requirements=$candidate}
  }catch{}}
  if($null -eq $requirements){Start-Sleep -Milliseconds 250}
 }
 if($xrProcess.HasExited -or $null -eq $requirements -or !(Test-Path -LiteralPath $channel)){
  throw "OpenXR session unavailable; no native probe attached. Inspect $folder\host.json"
 }
 $token=(Get-Content -LiteralPath $channel -Raw).Trim()
 & "$Stage\Focus-BC2.ps1" | Out-Null
 Push-Location $Repo
 try {
  $traceArgs=@{Seconds=30;FrameChannel=$token;GunEvents=$true;DesktopEyeMirror=$true;DesktopCapture=[bool]$DesktopCapture;UncapMirror=$true}
  if($RealControllers){$traceArgs.MagazineReload=$true;$traceArgs.RecordTape=Join-Path $folder 'gather-input.fvrtape'}
  elseif($InputTape){$traceArgs.InputTape=$InputTape;$traceArgs.MagazineReload=$true}
  else {$traceArgs.MagazineSliderReleaseProbe=$true;$traceArgs.RecordTape=Join-Path $folder 'gather-input.fvrtape'}
  & .\Start-NativeTrace.ps1 @traceArgs | Select-Object -Last 4
 }
 finally {Pop-Location}
 $xrProcess.WaitForExit(20000) | Out-Null
 $newTraces=@(Get-ChildItem (Join-Path $Repo 'reports') -Directory -Filter 'native-trace-*' | Where-Object {$_.FullName -notin $priorTraces})
 if($newTraces.Count -ne 1){throw 'Cannot uniquely bind this run to its native trace'}
 $trace=$newTraces[0]
 [ordered]@{trace=$trace.FullName;game_pid=$gameProcess.Id;input_mode=$(if($RealControllers){'real_controllers_no_automated_gestures'}elseif($InputTape){'fully_synthetic_tape_replay'}else{'fully_synthetic_fixture_recording'});worn_acceptance=$false;
  native_sha256=(Get-FileHash (Join-Path $Repo 'build\x86\BC2NativeProbe.dll')).Hash;
  host_sha256=(Get-FileHash (Join-Path $Repo 'build\x64\BC2XrHost.exe')).Hash} | ConvertTo-Json | Set-Content (Join-Path $folder 'run.json')
 Get-Content (Join-Path $folder 'host.json') -Raw
 "folder $folder"
} finally {
 $env:FVR_BC2_CHAMBER_RACK=$previousRack
 if($xrProcess -and !$xrProcess.HasExited){$xrProcess.Kill();$null=$xrProcess.WaitForExit(2000)}
 if($gameProcess -and !$gameProcess.HasExited){$gameProcess.Kill();$null=$gameProcess.WaitForExit(2000)}
}
