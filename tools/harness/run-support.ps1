# Fresh BC2 -> SP_02 XM8 -> support-grip fixture (grab 3.5-5.5 s and release by squeeze, grab 6.5 s with
# tracking loss 7.0-7.5 s, grab 8.8-10 s) with per-pair poses and every-pair left-eye flicker frames.
param([string]$Repo=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),[string]$Stage=$(if($env:FVR_BC2_STAGE){$env:FVR_BC2_STAGE}else{Join-Path $env:USERPROFILE 'fvr-testrange-stage'}),[string]$Range='sp02-xm8-only')
$ErrorActionPreference='Stop'
for($w=0;$w -lt 30 -and (Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue);$w++){Start-Sleep 1}
if(Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue){throw 'BC2 or a trace is already running'}
$env:FVR_BC2_CHAMBER_RACK='0' # rack is opt-out from e602ada; these scenarios don't test it
foreach($v in 'FVR_BC2_CAPTURE_HIDE_LEFT_HAND','FVR_BC2_PROBE_BOLT_RELEASE','FVR_BC2_EMPTY_GATE_FAILCLOSED'){Set-Item "env:$v" ''}
$game='C:\Program Files (x86)\Electronic Arts\Battlefield Bad Company 2'
& "$Stage\Start-BC2TestRange.ps1" -GamePath $game -Bin $Stage -InjectDelayMs 3000 -Seconds 30 -KeepRunning -Config "$Stage\ranges\$Range.cfg" | Select-Object -Last 1
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$rx=Join-Path $Repo "reports\support-rx-$stamp"
$rxArgs=@('--output',$rx,'--pairs','240','--static-pose','--async','--controls-support-grip','--support-primary-direction','1','--controls-hands','--controls-observe','--capture-poses','--eye-size','1024','768','--flicker-capture')
$r=Start-Process -FilePath (Join-Path $Repo 'build\x64\BC2NativeIpcProbe.exe') -ArgumentList $rxArgs -PassThru
$t=0;while(-not (Test-Path "$rx\channel.txt") -and $t -lt 60){Start-Sleep -Milliseconds 250;$t++}
$raw=[IO.File]::ReadAllBytes("$rx\channel.txt")
$tok=$(if($raw[0]-eq 0xff){[Text.Encoding]::Unicode.GetString($raw,2,$raw.Length-2)}else{[Text.Encoding]::UTF8.GetString($raw)}).Trim()
& "$Stage\Focus-BC2.ps1" | Out-Null
Push-Location $Repo
try { & .\Start-NativeTrace.ps1 -Seconds 15 -FrameChannel $tok -Controllers -Hands -TwoHandGrip -GunEvents -DesktopCapture 2>&1 | Select-Object -Last 2 } catch { "trace: $_" }
Pop-Location
$r.WaitForExit(60000)|Out-Null
$folder=Get-ChildItem (Join-Path $Repo 'reports') -Directory -Filter 'native-trace-*' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
"rx $rx"
"folder $($folder.FullName)"
Get-Process BFBC2Game -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue | Wait-Process -Timeout 30 -ErrorAction SilentlyContinue
