[CmdletBinding()]
param([Parameter(Mandatory)][string]$Scenario,[ValidateSet('sim','live','ipc')][string]$Mode='sim',[ValidateRange(1,20)][int]$Repeat=1,[string]$Tape,[switch]$Sim,[ValidateRange(30,600)][int]$TimeoutSeconds=180)
# Developer replay runner (docs/REPLAY_INPUT.md). 'ipc' is an alias of 'live': the DLL replays the
# tape at the gather and BC2NativeIpcProbe is only the stereo sink. -Sim runs the offline simulator
# first and stops when it already fails. Writes reports\replay\<scenario>-<mode>-<utc>\summary.json.
$ErrorActionPreference='Stop'
if($Mode -eq 'ipc'){$Mode='live'}
$python=(Get-Command python -ErrorAction SilentlyContinue)
if(-not $python){$python=(Get-Command py -ErrorAction Stop)}
$runner=Join-Path $PSScriptRoot 'tools\replay\run.py'
$common=@('--scenario',$Scenario,'--timeout',[string]$TimeoutSeconds)
if($Tape){$common+=@('--tape',[IO.Path]::GetFullPath($Tape))}
if($Sim -and $Mode -eq 'live'){
    & $python.Source -B $runner @common '--mode' 'sim'
    if($LASTEXITCODE -ne 0){throw "Offline simulator failed for $Scenario; not starting the game (exit $LASTEXITCODE)"}
}
& $python.Source -B $runner @common '--mode' $Mode '--repeat' ([string]$Repeat)
exit $LASTEXITCODE
