param([string]$Root=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),[string]$Stage=$(if($env:FVR_BC2_STAGE){$env:FVR_BC2_STAGE}else{Join-Path $env:USERPROFILE 'fvr-testrange-stage'}))
$ErrorActionPreference='Continue'
$vs=& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -VsInstallationPath $vs -Arch x86 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$b=Join-Path $Root 'build\testrange-x86'
if(-not (Test-Path "$b\build.ninja")){cmake -S (Join-Path $Root 'tools\testrange') -B $b -G Ninja -DCMAKE_BUILD_TYPE=Release *> "$env:TEMP\tr-cfg.txt"; if($LASTEXITCODE){'CONFIGURE FAILED';Get-Content "$env:TEMP\tr-cfg.txt" -Tail 10;exit 1}}
cmake --build $b --target BC2TestRange *> "$env:TEMP\tr-build.txt"
if($LASTEXITCODE){'BUILD FAILED';Select-String -Path "$env:TEMP\tr-build.txt" -Pattern 'error' | Select -First 10 | % Line;exit 1}
'BUILD OK'
if(-not (Test-Path "$Stage\BC2TestRange-pre-switch.dll")){Copy-Item "$Stage\BC2TestRange.dll" "$Stage\BC2TestRange-pre-switch.dll"}
Copy-Item "$b\BC2TestRange.dll" "$Stage\BC2TestRange.dll" -Force
"staged $((Get-FileHash "$Stage\BC2TestRange.dll").Hash.Substring(0,12))"
