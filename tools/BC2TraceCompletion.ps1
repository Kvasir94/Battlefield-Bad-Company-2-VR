# Read-only completion classification. Process termination alone cannot identify
# a user quit, runtime failure, or another caller of ExitProcess.
function Get-BC2TraceCompletion {
    param([int]$BootstrapExit,[bool]$GameExited,$GameExitCode,
          [bool]$UntilHostExit,[bool]$NewCrashReport,[string]$ReportPath)
    $reportState='missing';$parsed=$null;$parseError=$null
    if(Test-Path -LiteralPath $ReportPath -PathType Leaf){
        try {
            $raw=Get-Content -LiteralPath $ReportPath -Raw -ErrorAction Stop
            if([string]::IsNullOrWhiteSpace($raw)){$reportState='empty'}
            else {
                $parsed=$raw | ConvertFrom-Json -ErrorAction Stop
                if($null -eq $parsed -or $parsed -is [Array] -or
                    -not ($parsed.state -is [string]) -or [string]::IsNullOrWhiteSpace($parsed.state)){
                    throw 'Final native report has no valid state.'
                }
                $reportState='valid'
            }
        } catch {$reportState='invalid';$parsed=$null;$parseError=$_.Exception.Message}
    }
    $failed=$BootstrapExit -ne 0 -or $NewCrashReport -or
        ($GameExited -and ($null -eq $GameExitCode -or $GameExitCode -ne 0 -or -not $UntilHostExit)) -or
        $reportState -eq 'invalid' -or ($null -ne $parsed -and $parsed.state -eq 'failed') -or
        (-not $GameExited -and $reportState -ne 'valid')
    $outcome=if($failed){'failed'}elseif($reportState -ne 'valid'){'game_exited_without_final_report'}else{'final_report_available'}
    [pscustomobject]@{
        outcome=$outcome;failed=[bool]$failed;game_exit_cause=$(if($GameExited){'unknown'}else{$null})
        final_report_state=$reportState;final_report_available=($reportState -in @('valid','invalid'))
        final_report_parse_valid=($reportState -eq 'valid');final_report_parse_error=$parseError
        native_report=$parsed
    }
}
