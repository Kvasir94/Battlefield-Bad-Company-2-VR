# Closed-gate review rows that need scripted input instead of the magazine probe:
# empty-switch (XM8 slot 0, F2000 slot 1) and reload-no-left. Magazine reload session (request mode), gun events on.
param([ValidateSet('empty-switch','reload-no-left','switch-only','empty-only','equipment')][string]$Scenario,[string]$Repo=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),[string]$Stage=$(if($env:FVR_BC2_STAGE){$env:FVR_BC2_STAGE}else{Join-Path $env:USERPROFILE 'fvr-testrange-stage'}),[switch]$FailClosed,[switch]$Plain,[switch]$EquipProbe,[string]$Range='',[switch]$InputLog)
$ErrorActionPreference='Stop'
for($w=0;$w -lt 30 -and (Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue);$w++){Start-Sleep 1}
if(Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue){throw 'BC2 or a trace is already running'}
$env:FVR_BC2_CHAMBER_RACK='0' # rack is opt-out from e602ada; these scenarios don't test it
foreach($v in 'FVR_BC2_CAPTURE_HIDE_LEFT_HAND','FVR_BC2_PROBE_BOLT_RELEASE','FVR_BC2_TEST_HITCH_MS'){Set-Item "env:$v" ''}
$env:FVR_BC2_EMPTY_GATE_FAILCLOSED=$(if($FailClosed){'1'}else{''})
if(-not $env:FVR_BC2_COPY_AUTHORITY){$env:FVR_BC2_COPY_AUTHORITY=''}
$env:FVR_BC2_TEST_INPUT_LOG=$(if($InputLog){'1'}else{''})
$range=$(if($Range){$Range}elseif($Scenario -ne 'reload-no-left'){'sp02-xm8-f2000'}else{'sp02-xm8-only'})
$game='C:\Program Files (x86)\Electronic Arts\Battlefield Bad Company 2'
& "$Stage\Start-BC2TestRange.ps1" -GamePath $game -Bin $Stage -InjectDelayMs 3000 -Seconds 30 -KeepRunning -Config "$Stage\ranges\$range.cfg" | Select-Object -Last 1
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$rx=Join-Path $Repo "reports\gate-rx-$stamp"
$rxArgs=@('--output',$rx,'--pairs','240','--static-pose','--async','--gate-scenario',$Scenario,'--eye-size','1024','768')
$r=Start-Process -FilePath (Join-Path $Repo 'build\x64\BC2NativeIpcProbe.exe') -ArgumentList $rxArgs -PassThru
$t=0;while(-not (Test-Path "$rx\channel.txt") -and $t -lt 60){Start-Sleep -Milliseconds 250;$t++}
$raw=[IO.File]::ReadAllBytes("$rx\channel.txt")
$tok=$(if($raw[0]-eq 0xff){[Text.Encoding]::Unicode.GetString($raw,2,$raw.Length-2)}else{[Text.Encoding]::UTF8.GetString($raw)}).Trim()
& "$Stage\Focus-BC2.ps1" | Out-Null
$shots=Start-Job { param($st) Start-Sleep 11; foreach($n in 0..7){ & "$st\Capture-BC2.ps1" -Out ("$st\gate-{0:d2}.png" -f $n) | Out-Null; Start-Sleep -Milliseconds 1500 } } -ArgumentList $Stage
Push-Location $Repo
try { & .\Start-NativeTrace.ps1 -Seconds 25 -FrameChannel $tok -PhysicalReload:(-not $Plain) -MagazineReload:(-not $Plain) -EquipProbe:$EquipProbe -Controllers -Hands -GunEvents 2>&1 | Select-Object -Last 2 } catch { "trace: $_" }
Pop-Location
$r.WaitForExit(60000)|Out-Null
$folder=Get-ChildItem (Join-Path $Repo 'reports') -Directory -Filter 'native-trace-*' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Get-ChildItem $rx -Filter *.rgba -ErrorAction SilentlyContinue | Remove-Item -Force
Copy-Item (Join-Path $Stage "BC2TestRange.log") (Join-Path $folder.FullName "BC2TestRange.log") -ErrorAction SilentlyContinue
"rx $rx"
"folder $($folder.FullName)"
Get-Process BFBC2Game -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue | Wait-Process -Timeout 30 -ErrorAction SilentlyContinue
