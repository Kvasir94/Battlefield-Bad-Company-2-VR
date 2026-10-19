$ErrorActionPreference='Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\BC2RenderSize.ps1')
$good=[ordered]@{state='renderer_observed';pid=123;graphics_api='D3D11';width=1920;height=1080;stable=$true;pending_reset=$false}
if(-not (Test-BC2RenderObservation ([pscustomobject]$good) 123 1920 1080)){throw 'Verified matching renderer rejected'}
foreach($change in @(
    @{pid=124},@{width=1024;height=768},@{graphics_api='D3D9'},@{stable=$false},
    @{pending_reset=$true},@{state='game_not_running'},@{stable='true'},@{pending_reset='false'}
)){
    $candidate=[ordered]@{};foreach($key in $good.Keys){$candidate[$key]=$good[$key]}
    foreach($key in $change.Keys){$candidate[$key]=$change[$key]}
    if(Test-BC2RenderObservation ([pscustomobject]$candidate) 123 1920 1080){throw "Unsafe renderer observation accepted: $($change|ConvertTo-Json -Compress)"}
}
if(Test-BC2RenderObservation $null 123 1920 1080){throw 'Missing observation accepted'}
if(Test-BC2RenderObservation ([pscustomobject]@{state='renderer_observed';pid=123;width=1920;height=1080}) 123 1920 1080){throw 'Window dimensions without native proof accepted'}
# Exercise actual executable invocation and stderr handling on both Windows
# PowerShell 5.1 and PowerShell 7. Never launches or reads a game executable.
$fixtureRoot=Join-Path ([IO.Path]::GetTempPath()) ('bc2-render-observer-tests-'+[Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
$observer=Join-Path $fixtureRoot 'BC2RenderObserver.exe'
$currentProcess=[Diagnostics.Process]::GetCurrentProcess()
function Assert-RenderFixture([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Invoke-RenderFixture([string]$Name,[string]$Modes,[bool]$ShouldPass,[int]$Seconds=2,[bool]$TrailingSlash=$false){
    # Spaces in the fixture path also verify native argument quoting.
    $folder=Join-Path $fixtureRoot ($Name+' fixture with spaces')
    [IO.Directory]::CreateDirectory($folder) | Out-Null
    [IO.File]::WriteAllText((Join-Path $folder 'mode.txt'),$Modes)
    [IO.File]::WriteAllText((Join-Path $folder 'pid.txt'),[string]$currentProcess.Id)
    $reportPath=Join-Path $folder 'report.json'
    $timer=[Diagnostics.Stopwatch]::StartNew();$failed=$false
    try {
        $gameArgument=if($TrailingSlash){$folder+'\'}else{$folder}
        $null=Wait-BC2RenderSize -Process $currentProcess -Observer $observer -GamePath $gameArgument -Width 1920 -Height 1080 -ReportPath $reportPath -TimeoutSeconds $Seconds
    } catch {$failed=$true}
    $timer.Stop()
    Assert-RenderFixture ($failed -ne $ShouldPass) "$Name returned an unexpected success/failure."
    Assert-RenderFixture (Test-Path -LiteralPath $reportPath) "$Name did not write a report."
    $report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    $attempts=[int]([IO.File]::ReadAllText((Join-Path $folder 'count.txt')))
    # A final child started at the timeout boundary may be terminated before
    # managed Main writes its count. Every completed/fatal invocation must match.
    $countMatches=$report.attempts -eq $attempts
    if($report.failure_kind -eq 'timeout'){$countMatches=$report.attempts -ge $attempts -and $report.attempts -le ($attempts+1)}
    Assert-RenderFixture $countMatches "$Name attempt count differs from executable invocations (report=$($report.attempts), entered Main=$attempts)."
    Assert-RenderFixture ($report.verified -eq $ShouldPass) "$Name verified flag differs from outcome."
    Assert-RenderFixture (-not [string]::IsNullOrWhiteSpace([string]$report.detail)) "$Name has no diagnostic detail."
    [pscustomobject]@{Report=$report;Attempts=$attempts;ElapsedSeconds=$timer.Elapsed.TotalSeconds;Folder=$folder}
}
try {
    $compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    if(-not (Test-Path -LiteralPath $compiler)){$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'}
    if(-not (Test-Path -LiteralPath $compiler)){throw 'Windows .NET Framework csc.exe is required for executable observer fixtures.'}
    $source=Join-Path $PSScriptRoot 'fixtures\BC2RenderObserver.cs'
    $compilerOutput=@(& $compiler /nologo /target:exe ("/out:"+$observer) $source 2>&1)
    if($LASTEXITCODE -ne 0){throw "Observer fixture compilation failed: $($compilerOutput -join [Environment]::NewLine)"}

    $first=Invoke-RenderFixture 'transient startup' 'transient,match,match' $true
    Assert-RenderFixture ($first.Attempts -eq 3 -and $first.Report.consecutive_matches -eq 2 -and $null -eq $first.Report.failure_kind) 'Transient startup must recover after two matches.'
    $trailing=Invoke-RenderFixture 'trailing backslash' 'match,match' $true 2 $true
    Assert-RenderFixture ($trailing.Attempts -eq 2 -and $trailing.Report.consecutive_matches -eq 2) 'Native argv quoting must preserve a spaced game path ending in a backslash.'
    $reset=Invoke-RenderFixture 'streak reset' 'match,transient,match,match' $true
    Assert-RenderFixture ($reset.Attempts -eq 4 -and $reset.Report.consecutive_matches -eq 2) 'An invocation failure must reset a preceding matching streak.'
    foreach($mode in @('fatal','wrongpid','unsupported','malformed')){
        $fatal=Invoke-RenderFixture $mode $mode $false
        Assert-RenderFixture ($fatal.Attempts -eq 1 -and $fatal.Report.failure_kind -eq 'fatal') "$mode must fail on its first invocation."
        if($mode -in @('fatal','malformed')){Assert-RenderFixture ($null -eq $fatal.Report.observation) "$mode must not retain an observation from failed invocation."}
    }
    $stale=Invoke-RenderFixture 'stale observation' 'match,fatal' $false
    Assert-RenderFixture ($stale.Attempts -eq 2 -and $stale.Report.failure_kind -eq 'fatal' -and $stale.Report.consecutive_matches -eq 0 -and $null -eq $stale.Report.observation) 'Failed invocation must clear the preceding valid observation and streak.'
    $mismatch=Invoke-RenderFixture 'mismatch' 'mismatch' $false 1
    Assert-RenderFixture ($mismatch.Report.failure_kind -eq 'timeout' -and $mismatch.Report.consecutive_matches -eq 0 -and $mismatch.Attempts -ge 2) 'Mismatched dimensions should retry until timeout.'
    Assert-RenderFixture ($mismatch.ElapsedSeconds -lt 5) 'Mismatch timeout exceeded its bound.'
    $hung=Invoke-RenderFixture 'hang' 'hang' $false 1
    Assert-RenderFixture ($hung.Report.failure_kind -eq 'timeout' -and $null -eq $hung.Report.observation) 'Hung executable must produce a timeout without stale observation.'
    Assert-RenderFixture ($hung.ElapsedSeconds -lt 5) 'Hung observer exceeded its wall-clock bound.'
    $childPid=[int]([IO.File]::ReadAllText((Join-Path $hung.Folder 'child-pid.txt')))
    Assert-RenderFixture ($null -eq (Get-Process -Id $childPid -ErrorAction SilentlyContinue)) 'Hung observer child was not terminated.'
    $large=Invoke-RenderFixture 'large stderr' 'bigtransient' $false
    Assert-RenderFixture ($large.Attempts -eq 1 -and $large.Report.failure_kind -eq 'fatal' -and $null -eq $large.Report.observation -and $large.ElapsedSeconds -lt 5) 'Large unknown stderr must drain without deadlock and fail immediately.'
    'BC2 native render-size predicate and executable invocation checks passed.'
} finally {
    # Terminate only fixture executables, including a child leaked by a failing
    # implementation under test, before removing the isolated fixture folder.
    Get-Process BC2RenderObserver -ErrorAction SilentlyContinue | Where-Object {
        try {$_.Path -eq $observer} catch {$false}
    } | Stop-Process -Force -ErrorAction SilentlyContinue
    $absolute=[IO.Path]::GetFullPath($fixtureRoot)
    $tempPrefix=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if(-not $absolute.StartsWith($tempPrefix,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($absolute) -notlike 'bc2-render-observer-tests-*'){throw 'Unexpected observer fixture cleanup path.'}
    Remove-Item -LiteralPath $absolute -Recurse -Force
}
