param([string]$Repo=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$S=$PSScriptRoot;$log=Join-Path $S 'r583-batch.txt';Remove-Item $log -ErrorAction SilentlyContinue
$env:FVR_BC2_COPY_AUTHORITY='1'
function Last([string]$f){Get-ChildItem (Join-Path $Repo 'reports') -Directory -Filter $f | Sort LastWriteTime -Desc | Select -First 1}
& "$S\run-spas-probe.ps1" -Rounds 4 -AbandonAfter 2 -Hitch '90,1000,hold' 2>&1 | Out-Null; "abandon-hitch-hold $((Last 'native-trace-*').Name) $((Last 'spasprobe-rx-*').Name)" | Add-Content $log
& "$S\run-spas-probe.ps1" -Rounds 3 2>&1 | Out-Null; "three $((Last 'native-trace-*').Name) $((Last 'spasprobe-rx-*').Name)" | Add-Content $log
& "$S\run-mode7.ps1" -SaveFrom 1 -SaveTo 0 2>&1 | Out-Null; "xm8 $((Last 'native-trace-*').Name) $((Last 'slider-rx-*').Name)" | Add-Content $log
Get-ChildItem (Last 'slider-rx-*').FullName,(Last 'native-trace-*').FullName -Filter *.rgba -ErrorAction SilentlyContinue | Remove-Item -Force
$env:FVR_BC2_COPY_AUTHORITY=''
