<#
.SYNOPSIS
Re-arm the bounded stereo capture in a running BC2 VR session (no relaunch).

.DESCRIPTION
The game must have been started with FVR_BC2_STEREO_CAPTURE=1 (armed at
startup) or 2 (armed only on request/auto). The native probe polls
<native-trace folder>\stereo-capture.request about every 100 ms, moves it to
stereo-capture\request-<n>.txt and captures the next four consecutive complete
stereo pairs into stereo-capture\arm-<nn>\. See stereo-capture\manifest.json.

.EXAMPLE
  .\Request-BC2StereoCapture.ps1 -Note 'left-eye ghost lines after explosion'
#>
[CmdletBinding()]
param([string]$ReportFolder,[string]$Note='')
$ErrorActionPreference='Stop'
if(-not $ReportFolder){
    $candidate=Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'reports') -Directory -Filter 'native-trace-*' |
        Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'stereo-capture') -PathType Container} |
        Sort-Object Name -Descending | Select-Object -First 1
    if(-not $candidate){throw 'No native-trace folder with stereo-capture\ found; was the game started with FVR_BC2_STEREO_CAPTURE=1 or 2?'}
    $ReportFolder=$candidate.FullName
}
$folder=[IO.Path]::GetFullPath($ReportFolder)
if(-not (Test-Path -LiteralPath (Join-Path $folder 'stereo-capture') -PathType Container)){throw "Stereo capture is not enabled for $folder"}
$request=Join-Path $folder 'stereo-capture.request'
if(Test-Path -LiteralPath $request){throw 'A capture request is already pending (the probe has not consumed it yet).'}
[IO.File]::WriteAllText($request,("requested_utc="+[DateTime]::UtcNow.ToString('o')+"`nnote="+$Note+"`n"))
[ordered]@{requested=$true;request_file=$request;manifest=(Join-Path $folder 'stereo-capture\manifest.json')} | ConvertTo-Json
