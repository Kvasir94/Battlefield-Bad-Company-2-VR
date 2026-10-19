param([string]$Repo=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),[string]$Stage=$(if($env:FVR_BC2_STAGE){$env:FVR_BC2_STAGE}else{Join-Path $env:USERPROFILE 'fvr-testrange-stage'}))
$S=$PSScriptRoot;$log=Join-Path $S 'hud-batch.txt';Remove-Item $log -ErrorAction SilentlyContinue
$env:FVR_BC2_COPY_AUTHORITY='1'
function Last([string]$f){Get-ChildItem (Join-Path $Repo 'reports') -Directory -Filter $f | Sort LastWriteTime -Desc | Select -First 1}
$env:FVR_BC2_TEST_SERVER_LOADED='30,8000'
& "$S\run-gate-scenario.ps1" -Scenario empty-only 2>&1 | Out-Null
$t=Last 'native-trace-*';"hud $($t.Name)" | Add-Content $log
New-Item -ItemType Directory -Force (Join-Path $t.FullName 'shots') | Out-Null
Get-ChildItem $Stage -Filter 'gate-*.png' | Copy-Item -Destination (Join-Path $t.FullName 'shots')
$env:FVR_BC2_TEST_SERVER_LOADED='';$env:FVR_BC2_COPY_AUTHORITY=''
