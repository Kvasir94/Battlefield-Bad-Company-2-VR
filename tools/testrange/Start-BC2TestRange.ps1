<#
Launch BC2 windowed and inject BC2TestRange.dll at process start so the
settings in BC2TestRange.cfg (beside the DLL) apply before the menu decides
whether to load. Writes nothing to the game folder.

  .\Start-BC2TestRange.ps1 -GamePath 'C:\...\Battlefield Bad Company 2' -Bin <stage folder> [-Seconds 60]
    [-Config ranges\sp02-xm8-spas.cfg]   (copied to <Bin>\BC2TestRange.cfg first)
#>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$GamePath,[Parameter(Mandatory)][string]$Bin,[ValidateRange(10,600)][int]$Seconds=60,[ValidateRange(0,60000)][int]$InjectDelayMs=0,[switch]$KeepRunning,[string]$Config,[switch]$PassThru)
$ErrorActionPreference='Stop'
$game=Join-Path $GamePath 'BFBC2Game.exe'
if(-not (Test-Path -LiteralPath $game)){throw "BFBC2Game.exe not found in $GamePath"}
if(Get-Process BFBC2Game -ErrorAction SilentlyContinue){throw 'BC2 is already running; close it first.'}
if($Config){Copy-Item -LiteralPath $Config -Destination (Join-Path $Bin 'BC2TestRange.cfg') -Force}
$dll=Join-Path $Bin 'BC2TestRange.dll';$inject=Join-Path $Bin 'BC2TestRangeInject.exe';$log=Join-Path $Bin 'BC2TestRange.log'
foreach($f in $dll,$inject,(Join-Path $Bin 'BC2TestRange.cfg')){if(-not (Test-Path -LiteralPath $f)){throw "missing $f"}}
if(Test-Path -LiteralPath $log){Remove-Item -LiteralPath $log}
$clock=[Diagnostics.Stopwatch]::StartNew()
$p=Start-Process -FilePath $game -WorkingDirectory $GamePath -PassThru
$null=$p.Handle # Retain this exact process, including its exit code after it exits.
# Inject as soon as the process exists; the DLL itself waits for the registry.
for($n=0;$n -lt 200;$n++){try{$null=$p.Modules.Count;break}catch{Start-Sleep -Milliseconds 25}}
if($InjectDelayMs){Start-Sleep -Milliseconds $InjectDelayMs}
if($p.HasExited){throw "BC2 exited before injection (code $($p.ExitCode))"}
$result=& $inject $p.Id $dll
if($LASTEXITCODE -ne 0){throw "Test-range injection failed for started BC2 PID $($p.Id); VR attachment was not started."}
Write-Output "inject at $($clock.ElapsedMilliseconds) ms: $result"
$deadline=$clock.Elapsed.TotalSeconds+$Seconds
while(-not $p.HasExited -and $clock.Elapsed.TotalSeconds -lt $deadline){Start-Sleep -Milliseconds 500}
$p.Refresh()
if($p.HasExited){throw "Started BC2 PID $($p.Id) exited during fast start (code $($p.ExitCode)); see $log. VR attachment was not started."}
if(Test-Path -LiteralPath $log){Get-Content -LiteralPath $log}
Write-Output "elapsed $([int]$clock.Elapsed.TotalSeconds) s, game running: $(-not $p.HasExited)"
if(-not $KeepRunning -and -not $p.HasExited){Stop-Process -Id $p.Id -Force}
if($PassThru -and $KeepRunning){Write-Output $p}
