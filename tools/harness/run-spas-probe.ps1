# Fresh BC2 -> SP_02 with the SPAS loaded 2 -> physical reload probe (shell by shell) with regression legs.
param([string]$Repo=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),[string]$Stage=$(if($env:FVR_BC2_STAGE){$env:FVR_BC2_STAGE}else{Join-Path $env:USERPROFILE 'fvr-testrange-stage'}),[int]$Rounds=1,[string]$Cancel='',[int]$FireAfter=0,[string]$Range='sp02-xm8-spas',[int]$GrabAfterShotMs=-1,[int]$AbandonAfter=0,[string]$Hitch='')
$ErrorActionPreference='Stop'
for($w=0;$w -lt 30 -and (Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue);$w++){Start-Sleep 1}
if(Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue){throw 'BC2 or a trace is already running'}
foreach($v in 'FVR_BC2_CAPTURE_HIDE_LEFT_HAND','FVR_BC2_PROBE_BOLT_RELEASE','FVR_BC2_TEST_HITCH_MS','FVR_BC2_RELOAD_MAILBOX'){Set-Item "env:$v" ''}
$env:FVR_BC2_TEST_HITCH_MS=$Hitch
$env:FVR_BC2_CHAMBER_RACK='0'
$env:FVR_BC2_PROBE_SPAS_GRAB_AFTER_SHOT_MS=$(if($GrabAfterShotMs -ge 0){"$GrabAfterShotMs"}else{''}); $env:FVR_BC2_PROBE_SPAS_ABANDON_AFTER=$(if($AbandonAfter){"$AbandonAfter"}else{''}); $env:FVR_BC2_PROBE_SPAS_ROUNDS="$Rounds"; $env:FVR_BC2_PROBE_CANCEL=$Cancel; $env:FVR_BC2_PROBE_SPAS_FIRE_AFTER=$(if($FireAfter){"$FireAfter"}else{''})
$game='C:\Program Files (x86)\Electronic Arts\Battlefield Bad Company 2'
& "$Stage\Start-BC2TestRange.ps1" -GamePath $game -Bin $Stage -InjectDelayMs 3000 -Seconds 30 -KeepRunning -Config "$Stage\ranges\$Range.cfg" | Select-Object -Last 1
$g=$null;for($w=0;$w -lt 40 -and -not $g;$w++){Start-Sleep -Milliseconds 500;$g=Get-Content "$Stage\BC2TestRange.log" | Select-String 'give: holding|selection NOT held'}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$rx=Join-Path $Repo "reports\spasprobe-rx-$stamp"
$rxArgs=[Collections.Generic.List[string]]@('--output',$rx,'--pairs','240','--static-pose','--async','--physical-reload-probe','--support-prepare-shots','--eye-size','1024','768')
if($Rounds -ge 3){$rxArgs.AddRange([string[]]@('--prep-shots',"$([Math]::Min(4,$Rounds))"))}
$r=Start-Process -FilePath (Join-Path $Repo 'build\x64\BC2NativeIpcProbe.exe') -ArgumentList $rxArgs -WindowStyle Hidden -PassThru
$t=0;while(-not (Test-Path "$rx\channel.txt") -and $t -lt 60){Start-Sleep -Milliseconds 250;$t++}
$raw=[IO.File]::ReadAllBytes("$rx\channel.txt")
$tok=$(if($raw[0]-eq 0xff){[Text.Encoding]::Unicode.GetString($raw,2,$raw.Length-2)}else{[Text.Encoding]::UTF8.GetString($raw)}).Trim()
& "$Stage\Focus-BC2.ps1" | Out-Null
Push-Location $Repo
try { & .\Start-NativeTrace.ps1 -Seconds 30 -FrameChannel $tok -PhysicalReloadProbe -GunEvents 2>&1 | Select-Object -Last 1 } catch { "trace: $_" }
Pop-Location
$r.WaitForExit(60000)|Out-Null
$folder=Get-ChildItem (Join-Path $Repo 'reports') -Directory -Filter 'native-trace-*' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Copy-Item (Join-Path $Stage "BC2TestRange.log") (Join-Path $folder.FullName "BC2TestRange.log") -ErrorAction SilentlyContinue
Get-ChildItem $rx -Filter *.rgba -ErrorAction SilentlyContinue | Remove-Item -Force
foreach($v in 'FVR_BC2_PROBE_SPAS_ROUNDS','FVR_BC2_PROBE_CANCEL','FVR_BC2_PROBE_SPAS_FIRE_AFTER'){Set-Item "env:$v" ''}
"folder $($folder.FullName)"
Get-Process BFBC2Game -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue | Wait-Process -Timeout 30 -ErrorAction SilentlyContinue

