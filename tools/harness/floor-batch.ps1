param([string]$Repo=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$S=$PSScriptRoot;$log=Join-Path $S 'floor-batch.txt';Remove-Item $log -ErrorAction SilentlyContinue
$env:FVR_BC2_COPY_AUTHORITY='1'
function Last([string]$f){Get-ChildItem (Join-Path $Repo 'reports') -Directory -Filter $f | Sort LastWriteTime -Desc | Select -First 1}
function Clean{Get-ChildItem (Last 'slider-rx-*').FullName,(Last 'native-trace-*').FullName -Filter *.rgba -ErrorAction SilentlyContinue | Remove-Item -Force}
$env:FVR_BC2_GROUND_PROBE='1'
foreach($sink in 0,80){& "$S\run-mode7.ps1" -InGameProps -DepthCapture -SinkMm $sink -Pitch -60 -PitchAfterMs 12500 -SaveFrom 1 -SaveTo 0 2>&1 | Out-Null; "occl $sink $((Last 'native-trace-*').Name)" | Add-Content $log; Clean}
foreach($k in 1,2){foreach($gp in '','1'){$env:FVR_BC2_GROUND_PROBE=$gp; & "$S\run-mode7.ps1" -SaveFrom 1 -SaveTo 0 2>&1 | Out-Null; "cost probe=$gp $((Last 'native-trace-*').Name) $((Last 'slider-rx-*').Name)" | Add-Content $log; Clean}}
$env:FVR_BC2_GROUND_PROBE='';$env:FVR_BC2_COPY_AUTHORITY=''
