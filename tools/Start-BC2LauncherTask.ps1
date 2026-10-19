# Internal launcher worker. Only compilation is serialized; game launch is user-controlled.
param([Parameter(Mandatory=$true)][string]$JobFile)
$ErrorActionPreference='Stop'
$spec=Get-Content -LiteralPath $JobFile -Raw|ConvertFrom-Json
if($spec.lease -notin @('build','game')){throw 'Unknown launcher job.'}
$mutex=$null;$held=$false
try {
    if($spec.lease -eq 'build'){
        $mutex=[Threading.Mutex]::new($false,'Local\KF2VR_NativeBuild')
        try{$held=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$held=$true}
        if(-not $held){throw 'Another build is running. Try again when it finishes.'}
    }
    $arguments=@($spec.arguments|ForEach-Object {[string]$_})
    & ([string]$spec.file) @arguments
    if($LASTEXITCODE){exit $LASTEXITCODE}
}finally{
    if($held){$mutex.ReleaseMutex()};if($mutex){$mutex.Dispose()}
    Remove-Item -LiteralPath $JobFile -ErrorAction SilentlyContinue
}
