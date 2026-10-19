<#
.SYNOPSIS
Playtest checkpoint launcher (docs/PLAYTEST-CHECKPOINT-20261008.md): fixed
playtest settings, then Play-BC2VR.ps1 from this folder.

.EXAMPLE
  .\Play-Checkpoint.ps1              # headset, XM8
  .\Play-Checkpoint.ps1 -Gun F2000   # headset, F2000 (AEK, SPAS likewise)
  .\Play-Checkpoint.ps1 -Flat        # monitor only: fast start, no VR session
#>
[CmdletBinding()]
param([switch]$Flat,[ValidateSet('XM8','F2000','AEK','SPAS')][string]$Gun='XM8',[switch]$NoMirror,[string]$Stage)
$ErrorActionPreference='Stop'
# This process (and the game it starts) only: playtest defaults, every probe/test switch off.
Get-ChildItem env: | Where-Object { $_.Name -like 'FVR_BC2_PROBE_*' -or $_.Name -like 'FVR_BC2_TEST_*' } | ForEach-Object { Remove-Item "env:$($_.Name)" }
foreach($v in 'FVR_BC2_CAPTURE_HIDE_LEFT_HAND','FVR_BC2_RELOAD_MAILBOX','FVR_BC2_EMPTY_GATE_FAILCLOSED'){Remove-Item "env:$v" -ErrorAction SilentlyContinue}
$env:FVR_BC2_COPY_AUTHORITY='1'
$env:FVR_BC2_CHAMBER_RACK='1'
if(-not $Stage){$Stage=$(if(Test-Path (Join-Path $PSScriptRoot 'stage\BC2TestRange.dll')){Join-Path $PSScriptRoot 'stage'}elseif($env:FVR_BC2_STAGE){$env:FVR_BC2_STAGE}else{Join-Path $env:USERPROFILE 'fvr-testrange-stage'})}
$config=@{XM8='ranges\sp02-xm8-only.cfg';F2000='ranges\sp02-f2000.cfg';AEK='ranges\sp02-aek.cfg';SPAS='ranges\sp02-xm8-spas.cfg'}[$Gun]
Write-Host "Checkpoint $(Split-Path -Leaf $PSScriptRoot): $Gun, stage $Stage$(if($Flat){', flat'})"
& (Join-Path $PSScriptRoot 'Play-BC2VR.ps1') -Flat:$Flat -NoMirror:$NoMirror -Stage $Stage -Config $config
