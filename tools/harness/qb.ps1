param([string]$Root,[string]$Tests='',[string]$Target='',[string]$Arch='x86')
$ErrorActionPreference='Continue'
$vs=& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -VsInstallationPath $vs -Arch $(if($Arch -eq 'x64'){'amd64'}else{'x86'}) -HostArch amd64 -SkipAutomaticLocation | Out-Null
Set-Location $Root
# A log of our own per run: a shared log can return another build's stale "BUILD OK".
$log=Join-Path $PSScriptRoot ('qb-'+$Arch+'-'+[guid]::NewGuid().ToString('N')+'.txt')
$args2=@('--build',"build\$Arch",'--parallel','6'); if($Target){$args2+=@('--target',$Target)}
cmake @args2 *> $log
if($LASTEXITCODE -ne 0){'BUILD FAILED';Select-String -Path $log -Pattern 'error' | Select-Object -First 15 | % Line;exit 1}
$dll=Join-Path $Root "build\$Arch\BC2NativeProbe.dll"
if(Test-Path $dll){"BUILD OK $((Get-FileHash $dll).Hash.Substring(0,12)) $((Get-Item $dll).LastWriteTime.ToString('HH:mm:ss'))"}else{'BUILD OK'}
Remove-Item $log -ErrorAction SilentlyContinue
if($Tests){ctest --test-dir "build\$Arch" -R $Tests --output-on-failure 2>&1 | Select-Object -Last 12}
