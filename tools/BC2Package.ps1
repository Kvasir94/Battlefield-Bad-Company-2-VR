# Package paths are relative; installation choices belong to the user.
function Resolve-BC2Runtime([string]$Name,[string]$Override){
    if($Override){if(Test-Path -LiteralPath $Override -PathType Leaf){return [IO.Path]::GetFullPath($Override)};throw 'The selected runtime JSON does not exist.'}
    $paths=@()
    if($Name -eq 'Auto'){
        if($env:XR_RUNTIME_JSON){$paths+=$env:XR_RUNTIME_JSON}
        $active=Get-ItemProperty 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -ErrorAction SilentlyContinue
        if($active.ActiveRuntime){$paths+=$active.ActiveRuntime}
    }
    if($Name -in @('Auto','VDXR')){$paths+=Join-Path $env:ProgramFiles 'Virtual Desktop Streamer/OpenXR/virtualdesktop-openxr.json'}
    if($Name -in @('Auto','SteamVR')){
        $steam=Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue
        $steamRoots=@($steam.SteamPath,(Join-Path ${env:ProgramFiles(x86)} 'Steam'))|Where-Object {$_}
        foreach($base in $steamRoots){
            $paths+=Join-Path $base 'steamapps/common/SteamVR/steamxr_win64.json'
            $libraries=Join-Path $base 'steamapps/libraryfolders.vdf'
            if(Test-Path -LiteralPath $libraries){
                foreach($match in [regex]::Matches((Get-Content -LiteralPath $libraries -Raw),'"path"\s+"([^"]+)"')){
                    $paths+=Join-Path ($match.Groups[1].Value.Replace('\\','\')) 'steamapps/common/SteamVR/steamxr_win64.json'
                }
            }
        }
    }
    foreach($path in $paths){if(Test-Path -LiteralPath $path -PathType Leaf){return [IO.Path]::GetFullPath($path)}}
    throw 'OpenXR runtime not found. Start your VR software or choose its runtime JSON in the launcher.'
}
function Read-BC2RackAudioProfiles([string]$Path){
    $file=Get-Item -LiteralPath $Path -ErrorAction Stop
    if($file.PSIsContainer -or $file.Length -gt 11000){throw 'Rack audio profile must be a small text file.'}
    $text=[IO.File]::ReadAllText($file.FullName).Trim()
    $rows=@($text -split ';');$weapons=@{}
    if($rows.Count -gt 64 -or $text.Length -gt 10559){throw 'Rack audio supports at most 64 exact profiles.'}
    foreach($row in $rows){
        if($row -notmatch '^[0-9a-fA-F]{32}(:[0-9a-fA-F]{32}){4}$'){throw 'Rack audio profile needs five colon-separated 32-digit hexadecimal identities.'}
        $ids=@($row.ToLowerInvariant() -split ':')
        if($ids -contains ('0'*32) -or @($ids|Select-Object -Unique).Count -ne 5 -or $weapons.ContainsKey($ids[0])){throw 'Rack audio profile contains zero, repeated or conflicting identities.'}
        $weapons[$ids[0]]=$true
    }
    return $text
}
function Read-BC2Package([string]$Root){
    $manifest=Join-Path $Root 'package-manifest.json'
    if(-not (Test-Path -LiteralPath $manifest)){$manifest=Join-Path $Root 'reports/playtest-manifest.private.json'}
    $receipt=Get-Content -LiteralPath $manifest -Raw|ConvertFrom-Json
    $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    foreach($file in $receipt.package_files){
        $path=[IO.Path]::GetFullPath((Join-Path $Root $file.relative_path))
        if(-not $path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Package manifest contains an outside path.'}
        if(-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path).Hash -ne $file.sha256){throw "Package file missing or changed: $($file.relative_path)"}
    }
    return $receipt
}
