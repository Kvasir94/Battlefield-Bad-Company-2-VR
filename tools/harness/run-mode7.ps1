# Fresh BC2 -> fast start to SP_02 -> mode-7 magazine slider-release probe with gun events.
param([string]$Repo=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),[string]$Stage=$(if($env:FVR_BC2_STAGE){$env:FVR_BC2_STAGE}else{Join-Path $env:USERPROFILE 'fvr-testrange-stage'}),[int]$Seconds=30,[float]$Pitch=0,[int]$PitchAfterMs=0,[string]$Range='sp02-xm8-only',[switch]$Eject,[int]$SaveFrom=140,[int]$SaveTo=230,[switch]$Flicker,[switch]$DesktopCapture,[switch]$SteadyWindow,[switch]$Rack,[float]$Yaw=0,[float]$GunYaw=0,[switch]$HideLeftHand,[switch]$FailClosed,[int]$InputGapAt=0,[int]$InputGapMs=0,[switch]$BoltRelease,[string]$Hitch='',[string]$Cancel='',[switch]$DepthCapture,[switch]$FlickAtHold,[switch]$InGameProps,[int]$SinkMm=0)
$ErrorActionPreference='Stop'
for($w=0;$w -lt 30 -and (Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue);$w++){Start-Sleep 1}
if(Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue){throw 'BC2 or a trace is already running'}
# The game process inherits this: the opt-in chamber rack (and the probe's rack leg).
$env:FVR_BC2_CHAMBER_RACK=$(if($Rack){'1'}else{'0'})
$env:FVR_BC2_CAPTURE_HIDE_LEFT_HAND=$(if($HideLeftHand){'1'}else{''})
$env:FVR_BC2_PROBE_BOLT_RELEASE=$(if($BoltRelease){'1'}else{''})
$env:FVR_BC2_TEST_HITCH_MS=$Hitch
$env:FVR_BC2_PROBE_CANCEL=$Cancel
$env:FVR_BC2_TEST_PROP_SINK_MM=$(if($SinkMm){"$SinkMm"}else{''})
$env:FVR_BC2_INGAME_PROPS=$(if($InGameProps){'1'}else{''})
$env:FVR_BC2_PROBE_FLICK_AT_HOLD=$(if($FlickAtHold){'1'}else{''})
$env:FVR_BC2_TEST_DEPTH_CAPTURE=$(if($DepthCapture){if($InGameProps){'2'}else{'1'}}else{''})
$env:FVR_BC2_EMPTY_GATE_FAILCLOSED=$(if($FailClosed){'1'}else{''})
$game='C:\Program Files (x86)\Electronic Arts\Battlefield Bad Company 2'
& "$Stage\Start-BC2TestRange.ps1" -GamePath $game -Bin $Stage -InjectDelayMs 3000 -Seconds 30 -KeepRunning -Config "$Stage\ranges\$Range.cfg" | Select-Object -Last 2
if($Range -ne 'sp02-xm8-only'){$g=$null;for($w=0;$w -lt 40 -and -not $g;$w++){Start-Sleep -Milliseconds 500;$g=Get-Content "$Stage\BC2TestRange.log" | Select-String 'give: holding|selection NOT held'}
 if(-not $g -or $g.Line -match 'NOT held'){Get-Content "$Stage\BC2TestRange.log" -Tail 8;Get-Process BFBC2Game -ErrorAction SilentlyContinue|Stop-Process -Force;throw 'Given weapon not held'}
 Get-Content "$Stage\BC2TestRange.log" | Select-String 'give: (slot|done|selection moved|reselect|holding)' | ForEach-Object Line}
& "$Stage\Capture-BC2.ps1" -Out "$Stage\pre-probe.png" | Out-Null
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$rx=Join-Path $Repo "reports\slider-rx-$stamp"
$rxArgs=[Collections.Generic.List[string]]@('--output',$rx,'--pairs','240','--static-pose','--async',$(if($Eject){'--magazine-eject-probe'}else{'--magazine-slider-release-probe'}),'--capture-poses','--eye-size','1024','768','--head-yaw-deg',"$Yaw",'--gun-yaw-deg',"$GunYaw",'--head-pitch-deg',"$Pitch",'--head-pitch-after-ms',"$PitchAfterMs",'--body-props-cache',(Join-Path $Repo 'build\body-ammo-assets.fvrprop'))
if($SaveTo -ge $SaveFrom){$rxArgs.AddRange([string[]]@('--save-pairs',"$SaveFrom","$SaveTo"))}
if($Flicker){$rxArgs.Add('--flicker-capture')}
if($InputGapMs -gt 0){$rxArgs.AddRange([string[]]@('--input-gap',"$InputGapAt","$InputGapMs"))}
$r=Start-Process -FilePath (Join-Path $Repo 'build\x64\BC2NativeIpcProbe.exe') -ArgumentList $rxArgs -WindowStyle Hidden -PassThru
$t=0;while(-not (Test-Path "$rx\channel.txt") -and $t -lt 60){Start-Sleep -Milliseconds 250;$t++}
$raw=[IO.File]::ReadAllBytes("$rx\channel.txt")
$tok=$(if($raw[0]-eq 0xff){[Text.Encoding]::Unicode.GetString($raw,2,$raw.Length-2)}else{[Text.Encoding]::UTF8.GetString($raw)}).Trim()
& "$Stage\Focus-BC2.ps1" | Out-Null
$shots=Start-Job { param($st) Start-Sleep 7; for($n=0;$n -lt 12;$n++){ & "$st\Capture-BC2.ps1" -Out ("$st\reload-{0:d2}.png" -f $n) | Out-Null; Start-Sleep -Milliseconds 700 } } -ArgumentList $Stage
Push-Location $Repo
try { & .\Start-NativeTrace.ps1 -Seconds $Seconds -FrameChannel $tok -MagazineSliderReleaseProbe:(-not $Eject) -MagazineEjectProbe:$Eject -DesktopCapture:$DesktopCapture -DesktopEyeMirror:$SteadyWindow -GunEvents 2>&1 | Select-Object -Last 3 } catch { "trace: $_" }
Pop-Location
& "$Stage\Capture-BC2.ps1" -Out "$Stage\post-probe.png" | Out-Null
$r.WaitForExit(60000)|Out-Null
$folder=Get-ChildItem (Join-Path $Repo 'reports') -Directory -Filter 'native-trace-*' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Copy-Item (Join-Path $Stage "BC2TestRange.log") (Join-Path $folder.FullName "BC2TestRange.log") -ErrorAction SilentlyContinue
"folder $($folder.FullName)"





Get-Process BFBC2Game -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue | Wait-Process -Timeout 30 -ErrorAction SilentlyContinue
