<#
.SYNOPSIS
One command to play the current proto/reload build: fresh BC2, fast start into
SP_02 with the opening cutscene skipped, then the VR session (headset) with
magazine reload and gun events.

.EXAMPLE
  .\Play-BC2VR.ps1           # headset (start Virtual Desktop / SteamVR first)
  .\Play-BC2VR.ps1 -Flat     # monitor only: fast start, no VR session
  .\Play-BC2VR.ps1 -Mirror   # add the separate headset left-eye window for debug/streaming
  .\Play-BC2VR.ps1 -NoSteadyWindow # game window shows the game's own frames between headset frames

The optional mirror window shows the left eye exactly as the headset gets it
(dropped magazines and other host-drawn props included). The game's own
steady eye mirror remains enabled unless -NoSteadyWindow is specified.
-NoMirror is retained for compatibility and overrides -Mirror.

Logs: the test-range log is <Stage>\BC2TestRange.log; the VR session writes
reports\native-xr-session-<utc>\ (gun-events.jsonl, native trace, host logs).
#>
[CmdletBinding()]
param(
    [switch]$Flat,
    [switch]$Mirror,
    [switch]$NoMirror,
    [switch]$NoSteadyWindow,
    # 0 x 0 (default): the headset runtime's recommended per-eye size (BC2XrHost
    # --probe), as on the packaged playtest path. Flat play uses 1920x1080.
    [ValidateRange(0,8192)][int]$RenderWidth=0,
    [ValidateRange(0,8192)][int]$RenderHeight=0,
    [ValidateSet('Preserve','HighCompatible')][string]$GraphicsProfile='Preserve',
    [string]$SettingsPath=(Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BFBC2\settings.ini'),
    [string]$OpenXrLoader,
    [string]$Stage,
    [string]$Config='ranges\sp02-xm8-only.cfg',
    [string]$GamePath
)
$ErrorActionPreference='Stop'
# Test-range stage: -Stage, else $env:FVR_BC2_STAGE, else a stage\ folder beside
# this script (checkpoint copies), else %USERPROFILE%\fvr-testrange-stage.
if(-not $Stage){$Stage=$(if($env:FVR_BC2_STAGE){$env:FVR_BC2_STAGE}elseif(Test-Path (Join-Path $PSScriptRoot 'stage\BC2TestRange.dll')){Join-Path $PSScriptRoot 'stage'}else{Join-Path $env:USERPROFILE 'fvr-testrange-stage'})}
if(Get-Process BFBC2Game -ErrorAction SilentlyContinue){throw 'BC2 is already running; close it first.'}
if(Get-Process BC2XrHost,BC2NativeTrace -ErrorAction SilentlyContinue){throw 'A BC2 VR session or trace is already active.'}
if(-not $GamePath){$GamePath=(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'config\local.json') -Raw | ConvertFrom-Json).game_path}
foreach($f in 'BC2TestRange.dll','BC2TestRangeInject.exe','Start-BC2TestRange.ps1',$Config){
    if(-not (Test-Path -LiteralPath (Join-Path $Stage $f))){throw "Missing $f in $Stage"}}
if(-not $Flat){foreach($f in 'build\x86\BC2NativeProbe.dll','build\x86\BC2NativeTrace.exe','build\x64\BC2XrHost.exe','build\x64\BC2Observe.exe'){
    if(-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $f))){throw "Missing $f; build x86 and x64 first"}}}
# Fail before launching BC2 when the host has no OpenXR loader (BUILDING.md).
if(-not $Flat -and -not $OpenXrLoader -and -not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'build/x64/runtime/openxr/win64/openxr_loader.dll')) -and -not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'runtime/openxr/win64/openxr_loader.dll'))){
    throw 'No OpenXR loader: put the Khronos x64 openxr_loader.dll in runtime/openxr/win64/ (BUILDING.md), or pass -OpenXrLoader <path>.'}

if(($RenderWidth -eq 0) -ne ($RenderHeight -eq 0)){throw 'Set both -RenderWidth and -RenderHeight, or neither (headset recommendation).'}
if(($RenderWidth -gt 0 -and $RenderWidth -lt 128) -or ($RenderHeight -gt 0 -and $RenderHeight -lt 128)){throw 'Render size must be 128..8192.'}
if($RenderWidth -eq 0){
    if($Flat){$RenderWidth=1920;$RenderHeight=1080}
    else{
        . (Join-Path $PSScriptRoot 'tools\BC2EyeRenderProfile.ps1')
        $probeArgs=@('--probe');if($OpenXrLoader){$probeArgs+=@('--loader',$OpenXrLoader)}
        $probeText=(& (Join-Path $PSScriptRoot 'build\x64\BC2XrHost.exe') @probeArgs 2>$null) -join "`n"
        $readiness=$null;try{$readiness=$probeText|ConvertFrom-Json}catch{}
        if(-not $readiness -or $readiness.state -ne 'runtime_ready'){throw 'Headset unavailable: BC2XrHost --probe did not report the recommended eye size. Start the headset runtime, or pass -RenderWidth/-RenderHeight. No game was started.'}
        $eyeProfile=Resolve-BC2EyeRenderProfile -Readiness $readiness
        $RenderWidth=$eyeProfile.Width;$RenderHeight=$eyeProfile.Height
        Write-Host "Eye size: ${RenderWidth}x${RenderHeight} (runtime recommended)."
    }
}
. (Join-Path $PSScriptRoot 'tools\BC2RenderSettings.ps1')
$renderSettings=Set-BC2RenderSettings -Path $SettingsPath -Width $RenderWidth -Height $RenderHeight -GraphicsProfile $GraphicsProfile
Write-Host "BC2 render settings: ${RenderWidth}x${RenderHeight}, windowed DX11."
Write-Host 'Starting BC2 and fast-starting SP_02 (about 30 s)...'
$startup=@(& (Join-Path $Stage 'Start-BC2TestRange.ps1') -GamePath $GamePath -Bin $Stage -InjectDelayMs 3000 -Seconds 30 -KeepRunning `
    -Config (Join-Path $Stage $Config) -PassThru)
$startup | Where-Object {$_ -isnot [Diagnostics.Process]} | Select-Object -Last 2
$started=@($startup | Where-Object {$_ -is [Diagnostics.Process]})
if($started.Count -ne 1){throw 'Fast start did not return exactly one owned game process; VR attachment was not started.'}
$game=$started[0];$game.Refresh()
if($game.HasExited){throw "Started BC2 PID $($game.Id) exited during fast start (code $($game.ExitCode)); see $Stage\BC2TestRange.log"}
if($game.Path -ne [IO.Path]::GetFullPath((Join-Path $GamePath 'BFBC2Game.exe'))){throw 'Started game path does not match the configured installation.'}
$gameStartedUtc=$game.StartTime.ToUniversalTime().ToString('o')
if($Flat){Write-Host "In game (flat). Test-range log: $Stage\BC2TestRange.log";return}

Write-Host 'Starting the VR session (put the headset on)...'
& (Join-Path $PSScriptRoot 'Start-BC2VRSession.ps1') -GameProcessId $game.Id -GameStartedUtc $gameStartedUtc -MagazineReload -GunEvents -Mirror:($Mirror -and -not $NoMirror) -NoDesktopEyeMirror:$NoSteadyWindow -OpenXrLoader $OpenXrLoader -RenderWidth $RenderWidth -RenderHeight $RenderHeight
