<#
.SYNOPSIS
Incremental developer compile. Tests and cache reconfiguration are explicit.
.EXAMPLE
.\Build.ps1 -Targets ControllerInputTests -TestRegex '^ControllerInput$'
#>
[CmdletBinding()]
param(
    [ValidateSet('x86','x64')][string]$Architecture='x86',
    [ValidateRange(1,16)][int]$Jobs=2,
    [string[]]$Targets=@(),
    [string]$TestRegex,
    [switch]$FullTests,
    [string]$BuildDirectory,
    [switch]$Configure,
    [AllowEmptyString()][string]$MagazineGeometryHeader)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
if(-not $BuildDirectory){$BuildDirectory="build/dev/$Architecture"}
if(-not [IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory=Join-Path $PSScriptRoot $BuildDirectory}
$build=[IO.Path]::GetFullPath($BuildDirectory)
$logs=Join-Path $build 'logs'
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$receipt=Join-Path $build 'build-receipt.json'
# A previous successful invocation must never certify this attempt.
if(Test-Path -LiteralPath $receipt){Remove-Item -LiteralPath $receipt}
$focused=$PSBoundParameters.ContainsKey('TestRegex')
if($FullTests -and $focused){throw 'Choose -TestRegex or -FullTests, not both.'}
if($focused -and [string]::IsNullOrWhiteSpace($TestRegex)){throw '-TestRegex must not be empty.'}
foreach($target in $Targets){if([string]::IsNullOrWhiteSpace($target)){throw '-Targets must not contain empty names.'}}
$fresh=-not (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt'))
$geometryOverride=$PSBoundParameters.ContainsKey('MagazineGeometryHeader')
if(-not $fresh -and $geometryOverride -and -not $Configure){throw 'Use -Configure to change geometry in an existing cache.'}
function InitializeCompiler {
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vsInstall=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(-not $vsInstall){throw 'Visual Studio C++ tools are required.'}
    & (Join-Path $vsInstall 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vsInstall -Arch $(if ($Architecture -eq 'x64') { 'amd64' } else { 'x86' }) -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
function Step([string]$Name,[string]$Exe,[string[]]$Arguments){
    $log=Join-Path $logs "$Name.log"
    & $Exe @Arguments *> $log
    if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $log -Tail 80;throw "$Name failed ($LASTEXITCODE)"}
    Get-Content -LiteralPath $log -Tail 4
}
InitializeCompiler
$cmake=(Get-Command cmake -ErrorAction Stop).Source
if($fresh -or $Configure){
    $configureArgs=@('-S',$PSScriptRoot,'-B',$build)
    if($fresh){
        $configureArgs+=@('-G','Ninja','-DCMAKE_BUILD_TYPE=RelWithDebInfo','-DBUILD_TESTING=ON')
        if(-not $geometryOverride){$MagazineGeometryHeader=Join-Path $PSScriptRoot 'profiles/checkpoint204/Bc2ExperimentalMagazineGeometry.h'}
    }
    # Existing profile/capability options stay untouched unless explicitly supplied.
    if($fresh -or $geometryOverride){
        if($MagazineGeometryHeader -and -not (Test-Path -LiteralPath $MagazineGeometryHeader -PathType Leaf)){throw "Missing $MagazineGeometryHeader"}
        $configureArgs+="-DBC2_EXPERIMENTAL_MAGAZINE_HEADER=$MagazineGeometryHeader"
    }
    Step 'configure' $cmake $configureArgs
}
if(-not $PSBoundParameters.ContainsKey('Targets') -and -not $FullTests){
    $Targets=if($Architecture -eq 'x86'){@('BC2NativeProbe')}else{@('BC2XrHost')}
}
$buildArgs=@('--build',$build,'--parallel',"$Jobs")
if($Targets.Count){$buildArgs+=@('--target')+$Targets}
Step 'build' $cmake $buildArgs
$testsRun=[bool]($FullTests -or $focused)
$scope='none'
$testsPassed=$null
if($testsRun){
    $ctest=(Get-Command ctest -ErrorAction Stop).Source
    $testArgs=@('--test-dir',$build,'--output-on-failure','--no-tests=error')
    if($FullTests){$scope='full'}else{$scope='focused';$testArgs+=@('-R',$TestRegex)}
    Step 'tests' $ctest $testArgs
    $testsPassed=$true
}
[ordered]@{
    architecture=$Architecture;build_directory=$build;completed_utc=[DateTime]::UtcNow.ToString('o')
    built=$true;targets=@($Targets);configured=[bool]($fresh -or $Configure)
    tests_run=$testsRun;tests_passed=$testsPassed;test_scope=$scope
    test_regex=$(if($focused){$TestRegex}else{$null})
    native_bc2_verified=$false;headset_tested=$false
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $receipt
Write-Output "Build receipt: $receipt"
