# The observer uses the verified native renderer getters, never window bounds.
function Test-BC2RenderObservation {
    param($Observation,[int]$ExpectedProcessId,[int]$Width,[int]$Height)
    return $null -ne $Observation -and $Observation.state -eq 'renderer_observed' -and
        $Observation.pid -eq $ExpectedProcessId -and $Observation.graphics_api -eq 'D3D11' -and
        $Observation.stable -is [bool] -and $Observation.stable -and
        $Observation.pending_reset -is [bool] -and -not $Observation.pending_reset -and
        $Observation.width -eq $Width -and $Observation.height -eq $Height
}

function ConvertTo-BC2ObserverArgument {
    param([string]$Value)
    # ProcessStartInfo.ArgumentList is unavailable in Windows PowerShell 5.1.
    # Quote one Windows argv value; no command shell is involved.
    $escaped=[regex]::Replace($Value,'(\\*)("|$)',{
        param($match)
        $slashes=$match.Groups[1].Value
        if($match.Groups[2].Value -eq '"'){return $slashes+$slashes+'\"'}
        return $slashes+$slashes
    })
    return '"'+$escaped+'"'
}

function Invoke-BC2RenderObserver {
    param([string]$Observer,[string]$GamePath,[int]$TimeoutMilliseconds)
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$Observer
    $start.Arguments='--game '+(ConvertTo-BC2ObserverArgument $GamePath)+' --render-only'
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $start.StandardOutputEncoding=[Text.Encoding]::UTF8;$start.StandardErrorEncoding=[Text.Encoding]::UTF8
    $observerProcess=[Diagnostics.Process]::new();$observerProcess.StartInfo=$start
    $started=$false
    try {
        $started=$observerProcess.Start()
        if(-not $started){throw 'Could not start the read-only BC2 render observer.'}
        # Drain both pipes concurrently. PowerShell never interprets native
        # stderr as ErrorRecords, regardless of version or caller preferences.
        $stdout=$observerProcess.StandardOutput.ReadToEndAsync()
        $stderr=$observerProcess.StandardError.ReadToEndAsync()
        $timedOut=-not $observerProcess.WaitForExit([Math]::Max(1,$TimeoutMilliseconds))
        if($timedOut){
            $observerProcess.Kill()
            if(-not $observerProcess.WaitForExit(2000)){throw 'Timed-out BC2 observer did not terminate.'}
        }
        $drained=[Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($stdout,$stderr),200)
        if(-not $drained){return [pscustomobject]@{exit_code=$null;stdout='';stderr='BC2 observer output did not close';timed_out=$true}}
        return [pscustomobject]@{exit_code=$observerProcess.ExitCode;stdout=$stdout.Result;stderr=$stderr.Result;timed_out=$timedOut}
    } finally {
        # Only this newly created observer process is a cleanup target.
        if($started -and -not $observerProcess.HasExited){$observerProcess.Kill();$null=$observerProcess.WaitForExit(2000)}
        $observerProcess.Dispose()
    }
}

function Wait-BC2RenderSize {
    param([Diagnostics.Process]$Process,[string]$Observer,[string]$GamePath,
          [int]$Width,[int]$Height,[string]$ReportPath,[ValidateRange(1,120)][int]$TimeoutSeconds=15)
    if(-not (Test-Path -LiteralPath $Observer -PathType Leaf)){throw 'Build x64 BC2Observe.exe before starting VR.'}
    $null=$Process.Handle # Retain the original process identity throughout retries.
    $clock=[Diagnostics.Stopwatch]::StartNew();$matches=0;$attempts=0
    $report=[ordered]@{requested_width=$Width;requested_height=$Height;pid=$Process.Id;verified=$false;consecutive_matches=0;attempts=0;failure_kind=$null;observation=$null;detail='No native observation'}
    $transientErrors=@(
        'Read-only BC2 observation failed: Renderer not initialized yet',
        'Read-only BC2 observation failed: Native object changed or became unreadable; retry observation',
        'Read-only BC2 observation failed: Renderer ownership or dimensions changed during observation; retry'
    )
    do {
        $remaining=[int][Math]::Ceiling($TimeoutSeconds*1000-$clock.Elapsed.TotalMilliseconds)
        if($remaining -le 0){break}
        $observation=$null;$failureKind=$null;$attempts++
        try {
            if($Process.HasExited){throw 'BC2 exited before its render size could be verified.'}
            $result=Invoke-BC2RenderObserver -Observer $Observer -GamePath $GamePath -TimeoutMilliseconds $remaining
            if($result.timed_out -or $clock.Elapsed.TotalSeconds -ge $TimeoutSeconds){
                $failureKind='timeout';throw 'BC2 render observer exceeded the startup deadline.'
            }
            if($result.exit_code -ne 0){
                $matches=0;$detail=$result.stderr.Trim()
                if(-not $detail){$detail="BC2 observer exited $($result.exit_code): $($result.stdout.Trim())"}
                if($result.exit_code -ne 1 -or $detail -notin $transientErrors){$failureKind='fatal'}
            }else{
                $observation=$result.stdout | ConvertFrom-Json -ErrorAction Stop
                if($null -eq $observation -or $observation.state -ne 'renderer_observed' -or
                   $observation.pid -ne $Process.Id -or $observation.graphics_api -ne 'D3D11' -or
                   $observation.stable -isnot [bool] -or $observation.pending_reset -isnot [bool]){
                    throw 'BC2 render observer returned an invalid profile or process identity.'
                }
                $detail="Native renderer reports $($observation.width)x$($observation.height), pending_reset=$($observation.pending_reset)"
                if(Test-BC2RenderObservation $observation $Process.Id $Width $Height){$matches++}else{$matches=0}
            }
        } catch {
            $matches=0;$observation=$null;$detail=$_.Exception.Message
            if(-not $failureKind){$failureKind='fatal'}
        }
        $report.verified=($matches -ge 2);$report.consecutive_matches=$matches;$report.attempts=$attempts
        $report.observation=$observation;$report.detail=$detail;$report.failure_kind=$failureKind
        $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ReportPath
        if($failureKind -eq 'fatal'){throw "BC2 render verification failed: $detail"}
        if($failureKind -eq 'timeout'){break}
        if($report.verified){return $report}
        $remaining=[int][Math]::Ceiling($TimeoutSeconds*1000-$clock.Elapsed.TotalMilliseconds)
        if($remaining -gt 0){Start-Sleep -Milliseconds ([Math]::Min(150,$remaining))}
    } while($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds)
    $report.failure_kind='timeout';$report.verified=$false
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ReportPath
    throw "BC2 render verification timed out waiting for ${Width}x${Height}. $($report.detail). XR was not started."
}
