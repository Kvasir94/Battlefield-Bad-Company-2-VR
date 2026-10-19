# Fresh BC2 -> fast start + SPAS give -> replay a one-shell tape with -PhysicalReload -MagazineReload -GunEvents.
param([string]$Repo=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),[string]$Stage=$(if($env:FVR_BC2_STAGE){$env:FVR_BC2_STAGE}else{Join-Path $env:USERPROFILE 'fvr-testrange-stage'}),[int]$Seconds=30,[string]$Tape='spas-one-shell-racked',[float]$Pitch=0,[switch]$NoPhysical,[string]$Range='sp02-xm8-spas',[switch]$Magazine)
$ErrorActionPreference='Stop'
$env:FVR_BC2_CHAMBER_RACK='0'
for($w=0;$w -lt 30 -and (Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue);$w++){Start-Sleep 1}
if(Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue){throw 'BC2 or a trace is already running'}
$game='C:\Program Files (x86)\Electronic Arts\Battlefield Bad Company 2'
& "$Stage\Start-BC2TestRange.ps1" -GamePath $game -Bin $Stage -InjectDelayMs 3000 -Seconds 30 -KeepRunning -Config "$Stage\ranges\$Range.cfg" | Out-Null
# The test range logs "give: holding" after 5 s with SPAS still selected (it re-selects on a move).
if($Range -ne 'sp02-xm8-only'){
$g=$null;for($w=0;$w -lt 40 -and -not $g;$w++){Start-Sleep -Milliseconds 500;$g=Get-Content "$Stage\BC2TestRange.log" | Select-String 'give: holding|selection NOT held'}
if(-not $g -or $g.Line -match 'NOT held'){Get-Content "$Stage\BC2TestRange.log" -Tail 8;Get-Process BFBC2Game -ErrorAction SilentlyContinue|Stop-Process -Force;throw 'SPAS not held before the tape'}
Get-Content "$Stage\BC2TestRange.log" | Select-String 'give: (done|selection moved|reselect|holding)' | ForEach-Object Line}else{Start-Sleep 5}
& "$Stage\Capture-BC2.ps1" -Out "$Stage\spas-pre.png" | Out-Null
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$rx=Join-Path $Repo "reports\spas-rx-$stamp"
$r=Start-Process -FilePath (Join-Path $Repo 'build\x64\BC2NativeIpcProbe.exe') -ArgumentList @('--output',$rx,'--pairs','240','--static-pose','--async','--sink-seconds',"$Seconds",'--eye-size','1024','768','--head-pitch-deg',"$Pitch") -PassThru
$t=0;while(-not (Test-Path "$rx\channel.txt") -and $t -lt 60){Start-Sleep -Milliseconds 250;$t++}
$raw=[IO.File]::ReadAllBytes("$rx\channel.txt")
$tok=$(if($raw[0]-eq 0xff){[Text.Encoding]::Unicode.GetString($raw,2,$raw.Length-2)}else{[Text.Encoding]::UTF8.GetString($raw)}).Trim()
& "$Stage\Focus-BC2.ps1" | Out-Null
Push-Location $Repo
try { & .\Start-NativeTrace.ps1 -Seconds $Seconds -FrameChannel $tok -InputTape (Join-Path $Repo "tools\replay\tapes\$Tape.fvrtape") -GunEvents -PhysicalReload:(-not $NoPhysical) -MagazineReload:$Magazine -TwoHandGrip:$NoPhysical 2>&1 | Select-Object -Last 2 } catch { "trace: $_" }
Pop-Location
$r.WaitForExit(60000)|Out-Null
$folder=Get-ChildItem (Join-Path $Repo 'reports') -Directory -Filter 'native-trace-*' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Copy-Item (Join-Path $Stage "BC2TestRange.log") (Join-Path $folder.FullName "BC2TestRange.log") -ErrorAction SilentlyContinue
"folder $($folder.FullName)"
Get-Process BFBC2Game -ErrorAction SilentlyContinue | Stop-Process -Force








Get-Process BFBC2Game,BC2NativeTrace -ErrorAction SilentlyContinue | Wait-Process -Timeout 30 -ErrorAction SilentlyContinue
