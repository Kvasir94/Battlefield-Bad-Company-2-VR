[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\tools\BC2RenderSettings.ps1')
function Assert-Settings([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
$fixtureRoot=Join-Path ([IO.Path]::GetTempPath()) ('bc2-settings-tests-'+[Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
try {
    $path=Join-Path $fixtureRoot 'settings.ini'
    $original="; retain this comment`r`n[Audio]`r`nWidth=123`r`nVolume=0.8`r`n[WindowSettings]`r`n  Width = 800 ; window note`r`nHeight=600`r`nFullscreen=true; fullscreen note`r`nWidth=640`r`nTitle=Example`r`n[Graphics]`r`nDxVersion=9`r`nTexture=high`r`n[windowsettings]`r`nWidth=1024`r`n[graphics]`r`nDxVersion=auto`r`n"
    [IO.File]::WriteAllText($path,$original,[Text.UTF8Encoding]::new($true))
    $beforeBytes=[IO.File]::ReadAllBytes($path)
    $result=Set-BC2RenderSettings -Path $path
    $text=[IO.File]::ReadAllText($path)
    Assert-Settings $result.Changed 'First update should change settings.'
    Assert-Settings ([IO.File]::Exists($result.BackupPath)) 'Replacement must preserve a backup.'
    Assert-Settings ([Convert]::ToBase64String([IO.File]::ReadAllBytes($result.BackupPath)) -ceq [Convert]::ToBase64String($beforeBytes)) 'Backup must match original bytes.'
    Assert-Settings ($text.Contains("[Audio]`r`nWidth=123`r`nVolume=0.8") -and $text.Contains('Title=Example') -and $text.Contains('Texture=high')) 'Unrelated settings must survive.'
    Assert-Settings ($text.Contains('; retain this comment') -and $text.Contains('  Width = 1920 ; window note') -and $text.Contains('Fullscreen=false; fullscreen note')) 'Comments and key formatting must survive.'
    Assert-Settings (-not ($text -match '(?im)^\s*(?:Height\s*=\s*600|Fullscreen\s*=\s*true|DxVersion\s*=\s*(?:9|auto)|Width\s*=\s*(?:800|640|1024))\s*$')) 'Duplicate sections and keys must not retain contradictory values.'
    Assert-Settings ([regex]::Matches($text,'(?im)^Height=1080\r?$').Count -eq 2) 'Each duplicated window section needs all required keys.'
    $changedBytes=[IO.File]::ReadAllBytes($path)
    Assert-Settings ($changedBytes[0] -eq 239 -and $changedBytes[1] -eq 187 -and $changedBytes[2] -eq 191) 'UTF-8 BOM must survive.'
    $repeat=Set-BC2RenderSettings -Path $path
    Assert-Settings (-not $repeat.Changed -and -not $repeat.BackupPath) 'Repeated update must be idempotent.'
    Assert-Settings ([Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -ceq [Convert]::ToBase64String($changedBytes)) 'Repeated update must preserve exact bytes.'

    $quality=Join-Path $fixtureRoot 'quality.ini'
    [IO.File]::WriteAllText($quality,"[WindowSettings]`nWidth=1920`nHeight=1080`nRefreshRate=90`n[Graphics]`nTexture=medium`nDetail=low`nShadows=medium`nMSAA=4`nHSAO=false`nAniso=2`nRenderAheadLimit=2`n[Sound]`nQuality=medium",[Text.UTF8Encoding]::new($false))
    $q=Set-BC2RenderSettings -Path $quality -Width 1832 -Height 1920 -GraphicsProfile HighCompatible
    $qt=[IO.File]::ReadAllText($quality)
    Assert-Settings ($qt.Contains('Texture=high') -and $qt.Contains('Detail=high') -and $qt.Contains('Shadows=high') -and $qt.Contains('MSAA=0')) 'High compatible profile must apply native quality and the lowest native AA device option.'
    Assert-Settings ($qt.Contains('Aniso=4') -and $qt.Contains('MainQuality=custom')) 'Maximum filtering must use native exponent 4 and custom preset to avoid a reset to 4x.'
    Assert-Settings ($qt.Contains('HSAO=false') -and $qt.Contains('RefreshRate=90') -and $qt.Contains('[Sound]') -and $qt.Contains('Quality=medium')) 'Profile must preserve ambient occlusion, refresh and audio.'
    Assert-Settings (-not (Set-BC2RenderSettings -Path $quality -Width 1832 -Height 1920 -GraphicsProfile HighCompatible).Changed) 'High profile must be idempotent.'

    $missing=Join-Path $fixtureRoot 'missing-sections.ini'
    [IO.File]::WriteAllText($missing,"# keep`n[Audio]`nVolume=1",[Text.UnicodeEncoding]::new($false,$true))
    $added=Set-BC2RenderSettings -Path $missing -Width 1600 -Height 900
    $addedText=[IO.File]::ReadAllText($missing)
    Assert-Settings ($addedText.Contains("[WindowSettings]`nWidth=1600`nHeight=900`nFullscreen=false") -and $addedText.Contains("[Graphics]`nDxVersion=11")) 'Absent sections should be added.'
    $addedBytes=[IO.File]::ReadAllBytes($missing)
    Assert-Settings ($addedBytes[0] -eq 255 -and $addedBytes[1] -eq 254 -and -not $addedText.EndsWith("`n")) 'UTF-16 BOM and absence of final newline must survive.'
    Assert-Settings (-not (Set-BC2RenderSettings -Path $missing -Width 1600 -Height 900).Changed) 'Added sections should also be idempotent.'

    $bad=Join-Path $fixtureRoot 'ambiguous.ini'
    [IO.File]::WriteAllText($bad,"[WindowSettings] trailing garbage`nWidth=800",[Text.UTF8Encoding]::new($false))
    $badBytes=[IO.File]::ReadAllBytes($bad);$rejected=$false
    try {Set-BC2RenderSettings -Path $bad | Out-Null} catch {$rejected=$true}
    Assert-Settings $rejected 'Ambiguous sections must be rejected.'
    Assert-Settings ([Convert]::ToBase64String([IO.File]::ReadAllBytes($bad)) -ceq [Convert]::ToBase64String($badBytes)) 'Malformed input must remain untouched.'
    $rejected=$false
    try {Set-BC2RenderSettings -Path (Join-Path $fixtureRoot 'does-not-exist.ini') | Out-Null} catch {$rejected=$true}
    Assert-Settings $rejected 'Missing settings must not be created implicitly.'
    'BC2 render settings: preservation, duplicates, encoding, idempotence and malformed input passed.'
} finally {
    $absolute=[IO.Path]::GetFullPath($fixtureRoot)
    $tempPrefix=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if(-not $absolute.StartsWith($tempPrefix,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($absolute) -notlike 'bc2-settings-tests-*'){throw 'Unexpected fixture cleanup path.'}
    Remove-Item -LiteralPath $absolute -Recurse -Force
}
