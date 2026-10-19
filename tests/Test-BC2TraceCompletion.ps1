$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\tools\BC2TraceCompletion.ps1')
function Assert-TraceCompletion([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
$fixtureRoot=Join-Path ([IO.Path]::GetTempPath()) ('bc2-trace-completion-tests-'+[Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
try {
    $path=Join-Path $fixtureRoot 'native-trace.json'
    $completionInputs=@{BootstrapExit=0;GameExited=$true;GameExitCode=0;UntilHostExit=$true;NewCrashReport=$false;ReportPath=$path}
    foreach($content in @($null,''," `r`n")){
        if($null -eq $content){if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path}}
        else {[IO.File]::WriteAllText($path,$content)}
        $result=Get-BC2TraceCompletion @completionInputs
        Assert-TraceCompletion (-not $result.failed -and $result.outcome -eq 'game_exited_without_final_report') 'Zero-exit continuous session may end, but must report incomplete final evidence.'
        Assert-TraceCompletion ($result.game_exit_cause -eq 'unknown' -and -not $result.final_report_available -and -not $result.final_report_parse_valid -and $null -eq $result.native_report) 'Missing/empty final report must not imply user intent or successful probe completion.'
    }
    foreach($content in @('{invalid','null','[]','{}','{"state":123}')){
        [IO.File]::WriteAllText($path,$content)
        $result=Get-BC2TraceCompletion @completionInputs
        Assert-TraceCompletion ($result.failed -and $result.final_report_state -eq 'invalid' -and $result.final_report_available -and -not $result.final_report_parse_valid -and $null -eq $result.native_report) 'Malformed final reports must fail explicitly.'
        Assert-TraceCompletion (-not [string]::IsNullOrWhiteSpace($result.final_report_parse_error)) 'Malformed report must retain a parse diagnostic.'
    }
    [IO.File]::WriteAllText($path,'{"state":"observed","hooks_disabled":true}')
    $result=Get-BC2TraceCompletion @completionInputs
    Assert-TraceCompletion (-not $result.failed -and $result.final_report_parse_valid -and $result.outcome -eq 'final_report_available' -and $result.game_exit_cause -eq 'unknown') 'Valid report does not establish why the game exited.'
    $completionInputs.GameExited=$false;$completionInputs.GameExitCode=$null
    $result=Get-BC2TraceCompletion @completionInputs
    Assert-TraceCompletion (-not $result.failed -and $null -eq $result.game_exit_cause) 'Runtime end with a valid report and living game should succeed.'
    [IO.File]::WriteAllText($path,'')
    Assert-TraceCompletion (Get-BC2TraceCompletion @completionInputs).failed 'Living game with no final report must fail.'
    $completionInputs.GameExited=$true;$completionInputs.GameExitCode=0
    foreach($change in @(@{GameExitCode=1},@{GameExitCode=$null},@{BootstrapExit=2},@{NewCrashReport=$true},@{UntilHostExit=$false})){
        $candidate=@{};foreach($key in $completionInputs.Keys){$candidate[$key]=$completionInputs[$key]}
        foreach($key in $change.Keys){$candidate[$key]=$change[$key]}
        Assert-TraceCompletion (Get-BC2TraceCompletion @candidate).failed 'Nonzero/unavailable exit code, bootstrap failure, new crash, or unexpected timed-run exit must fail.'
    }
    [IO.File]::WriteAllText($path,'{"state":"failed","error":"probe failed"}')
    Assert-TraceCompletion (Get-BC2TraceCompletion @completionInputs).failed 'A parsed native failure must remain a failure.'
    'BC2 trace completion classification: unknown exit cause, incomplete evidence, report parsing and failure gates passed.'
} finally {
    $absolute=[IO.Path]::GetFullPath($fixtureRoot)
    $tempPrefix=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if(-not $absolute.StartsWith($tempPrefix,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($absolute) -notlike 'bc2-trace-completion-tests-*'){throw 'Unexpected completion fixture cleanup path.'}
    Remove-Item -LiteralPath $absolute -Recurse -Force
}
