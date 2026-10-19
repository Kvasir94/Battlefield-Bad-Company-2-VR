param([string]$Repo=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$S=$PSScriptRoot;$log=Join-Path $S 'e9-batch.txt';Remove-Item $log -ErrorAction SilentlyContinue
$env:FVR_BC2_COPY_AUTHORITY='1'
function Last([string]$f){Get-ChildItem (Join-Path $Repo 'reports') -Directory -Filter $f | Sort LastWriteTime -Desc | Select -First 1}
$rows=@(@{n='three';a=@{Rounds=3}},@{n='cancel-between';a=@{Rounds=3;Cancel='between'}},@{n='abandon';a=@{Rounds=4;AbandonAfter=2}},@{n='abandon-hitch';a=@{Rounds=4;AbandonAfter=2;Hitch='90,0,stop'}})
foreach($k in 1,2,3,4){foreach($row in $rows){$a=$row.a; & "$S\run-spas-probe.ps1" @a 2>&1 | Out-Null; "$($row.n) $((Last 'native-trace-*').Name) $((Last 'spasprobe-rx-*').Name)" | Add-Content $log}}
$env:FVR_BC2_COPY_AUTHORITY=''
