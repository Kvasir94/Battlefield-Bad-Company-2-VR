# Frozen playtest copy: sources at the tag (git archive, no worktree), built binaries, own stage, Start-menu shortcut.
param([string]$Repo=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),[string]$Tag='checkpoint-2026-10-08',[string]$Dest=(Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))) 'BC2VR-checkpoint-20261008'),[string]$Stage=$(if($env:FVR_BC2_STAGE){$env:FVR_BC2_STAGE}else{Join-Path $env:USERPROFILE 'fvr-testrange-stage'}))
$ErrorActionPreference='Stop'
if(Test-Path $Dest){throw "$Dest already exists"}
New-Item -ItemType Directory $Dest | Out-Null
Push-Location $Repo
$zip=Join-Path $env:TEMP "bc2vr-$Tag.zip"; git archive --format=zip -o $zip $Tag; Expand-Archive $zip $Dest; Remove-Item $zip
Pop-Location
foreach($a in 'x86','x64'){
  $src=Join-Path $Repo "build\$a"; $dst=Join-Path $Dest "build\$a"; New-Item -ItemType Directory $dst -Force | Out-Null
  Get-ChildItem $src -File | Where-Object { $_.Extension -in '.exe','.dll','.json','.fvrprop' } | Copy-Item -Destination $dst
  if(Test-Path "$src\runtime"){Copy-Item "$src\runtime" $dst -Recurse}
}
Copy-Item (Join-Path $Repo 'build\body-ammo-assets.fvrprop') (Join-Path $Dest 'build') -ErrorAction SilentlyContinue
if(Test-Path (Join-Path $Repo 'runtime')){Copy-Item (Join-Path $Repo 'runtime') $Dest -Recurse}
New-Item -ItemType Directory (Join-Path $Dest 'config') -Force | Out-Null
Copy-Item (Join-Path $Repo 'config\local.json') (Join-Path $Dest 'config\local.json')
$st=Join-Path $Dest 'stage'; New-Item -ItemType Directory "$st\ranges" -Force | Out-Null
foreach($f in 'BC2TestRange.dll','BC2TestRangeInject.exe','Start-BC2TestRange.ps1','Capture-BC2.ps1','Focus-BC2.ps1'){Copy-Item (Join-Path $Stage $f) $st}
foreach($c in 'sp02-xm8-only','sp02-f2000','sp02-aek','sp02-xm8-spas'){Copy-Item (Join-Path $Stage "ranges\$c.cfg") "$st\ranges"}
$ws=New-Object -ComObject WScript.Shell
$menu=[Environment]::GetFolderPath('Programs')
foreach($v in @(@{n='BC2 VR Checkpoint 2026-10-08';a=''},@{n='BC2 VR Checkpoint 2026-10-08 (Flat)';a=' -Flat'})){
  $l=$ws.CreateShortcut((Join-Path $menu ($v.n+'.lnk')))
  $l.TargetPath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
  $l.Arguments="-NoProfile -ExecutionPolicy Bypass -NoExit -File `"$Dest\Play-Checkpoint.ps1`"$($v.a)"
  $l.WorkingDirectory=$Dest; $l.Save()
}
"probe dll $((Get-FileHash "$Dest\build\x86\BC2NativeProbe.dll").Hash.Substring(0,12)), host $((Test-Path "$Dest\build\x64\BC2XrHost.exe")), loader $((Test-Path "$Dest\runtime\openxr\win64\openxr_loader.dll") -or (Test-Path "$Dest\build\x64\runtime\openxr\win64\openxr_loader.dll"))"
