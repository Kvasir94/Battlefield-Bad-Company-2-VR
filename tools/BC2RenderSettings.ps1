# Dot-source this helper before calling Set-BC2RenderSettings.
function Set-BC2RenderSettings {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Path,
        [ValidateRange(1,16384)][int]$Width=1920,
        [ValidateRange(1,16384)][int]$Height=1080,
        [ValidateSet('Preserve','HighCompatible')][string]$GraphicsProfile='Preserve'
    )
    $resolved=(Get-Item -LiteralPath $Path -ErrorAction Stop).FullName
    if(-not [IO.File]::Exists($resolved)){throw 'BC2 settings must be an existing file.'}
    $bytes=[IO.File]::ReadAllBytes($resolved)
    $offset=0
    $encoding=[Text.UTF8Encoding]::new($false,$true)
    if($bytes.Length -ge 4 -and $bytes[0] -eq 255 -and $bytes[1] -eq 254 -and $bytes[2] -eq 0 -and $bytes[3] -eq 0){$encoding=[Text.UTF32Encoding]::new($false,$true,$true);$offset=4}
    elseif($bytes.Length -ge 4 -and $bytes[0] -eq 0 -and $bytes[1] -eq 0 -and $bytes[2] -eq 254 -and $bytes[3] -eq 255){$encoding=[Text.UTF32Encoding]::new($true,$true,$true);$offset=4}
    elseif($bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191){$offset=3}
    elseif($bytes.Length -ge 2 -and $bytes[0] -eq 255 -and $bytes[1] -eq 254){$encoding=[Text.UnicodeEncoding]::new($false,$true,$true);$offset=2}
    elseif($bytes.Length -ge 2 -and $bytes[0] -eq 254 -and $bytes[1] -eq 255){$encoding=[Text.UnicodeEncoding]::new($true,$true,$true);$offset=2}
    try {$text=$encoding.GetString($bytes,$offset,$bytes.Length-$offset)}
    catch {
        if($offset){throw 'BC2 settings encoding is malformed.'}
        $encoding=[Text.Encoding]::GetEncoding(1252)
        $text=$encoding.GetString($bytes)
    }
    if($text.Contains([char]0)){throw 'BC2 settings contain unexpected NUL characters.'}
    $newline=if($text.Contains("`r`n")){"`r`n"}else{"`n"}
    $trailing=$text.EndsWith("`n")
    $lines=@([regex]::Split($text,"\r\n|\n"))
    if($trailing){$lines=$lines[0..($lines.Count-2)]}
    $targets=@{WindowSettings=[ordered]@{Width=[string]$Width;Height=[string]$Height;Fullscreen='false'};Graphics=[ordered]@{DxVersion='11'}}
    if($GraphicsProfile -eq 'HighCompatible'){
        foreach($key in @('Effects','Soldiers','Vehicles','Overgrowth','Undergrowth','StaticObjects','Terrain','Shadows','Water','Texture','Detail')){$targets.Graphics[$key]='high'}
        # Native preset High rewrites category fields and resets MSAA/Aniso to
        # index 2. Custom retains the individually authored settings instead.
        $targets.Graphics['MainQuality']='custom'
        # Native apply uses 1 << Aniso, capped by hardware capability: 4 = 16x.
        $targets.Graphics['Aniso']='4'
        $targets.Graphics['Bloom']='true'
        # The shared color/depth bridge is single-sample. Select the lowest
        # native DX11 device option (ordinary/fallback {1,0}); index 1 is
        # typically 2x, not a literal single-sample count. No driver CSAA override.
        $targets.Graphics['MSAA']='0'
        # Preserve the accepted ambient-occlusion setting until its stereo path
        # is validated. HSAO is intentionally not part of this override.
    }
    $output=[Collections.Generic.List[string]]::new()
    $seenSections=@{};$seenKeys=@{};$before=[Collections.Generic.List[object]]::new();$section=''
    foreach($line in $lines){
        if($line -match '^\s*\['){
            if($line -notmatch '^\s*\[([^\[\]]+)\]\s*(?:[;#].*)?$'){throw "Ambiguous INI section: $line"}
            $nextSection=$Matches[1].Trim()
            if(-not $nextSection){throw 'INI section name is empty.'}
            if($targets.ContainsKey($section)){
                foreach($key in $targets[$section].Keys){if(-not $seenKeys.ContainsKey($key)){$output.Add("$key=$($targets[$section][$key])")}}
            }
            $section=$nextSection;$seenKeys=@{}
            $seenSections[$section]=$true
            $output.Add($line);continue
        }
        if($targets.ContainsKey($section) -and $line -match '^(\s*([^=;#]+?)\s*=\s*)(.*)$'){
            $prefix=$Matches[1];$key=$Matches[2].Trim();$value=$Matches[3]
            if($targets[$section].Contains($key)){
                $before.Add([pscustomobject]@{Section=$section;Key=$key;Value=$value})
                $comment=''
                if($value -match '(\s*[;#].*)$'){$comment=$Matches[1]}
                $output.Add($prefix+$targets[$section][$key]+$comment);$seenKeys[$key]=$true;continue
            }
        }
        $output.Add($line)
    }
    if($targets.ContainsKey($section)){
        foreach($key in $targets[$section].Keys){if(-not $seenKeys.ContainsKey($key)){$output.Add("$key=$($targets[$section][$key])")}}
    }
    foreach($name in @('WindowSettings','Graphics')){
        if(-not $seenSections.ContainsKey($name)){
            if($output.Count -and $output[$output.Count-1] -ne ''){$output.Add('')}
            $output.Add("[$name]")
            foreach($key in $targets[$name].Keys){$output.Add("$key=$($targets[$name][$key])")}
        }
    }
    $updated=[string]::Join($newline,$output)
    if($trailing){$updated+=$newline}
    $changed=$updated -cne $text;$backup=$null
    if($changed){
        $id=[Guid]::NewGuid().ToString('N')
        $staged=$resolved+'.'+$id+'.tmp';$backup=$resolved+'.'+$id+'.bak'
        try {
            $payload=$encoding.GetBytes($updated)
            $encoded=[byte[]]::new($offset+$payload.Length)
            if($offset){[Array]::Copy($bytes,0,$encoded,0,$offset)}
            [Array]::Copy($payload,0,$encoded,$offset,$payload.Length)
            [IO.File]::WriteAllBytes($staged,$encoded)
            # Refuse to overwrite edits made while this helper prepared its update.
            $current=[IO.File]::ReadAllBytes($resolved)
            if([Convert]::ToBase64String($current) -cne [Convert]::ToBase64String($bytes)){throw 'BC2 settings changed during the update.'}
            [IO.File]::Replace($staged,$resolved,$backup)
        } finally {if([IO.File]::Exists($staged)){[IO.File]::Delete($staged)}}
    }
    [pscustomobject]@{Path=$resolved;Changed=$changed;Before=@($before.ToArray());After=[pscustomobject]@{Width=$Width;Height=$Height;Fullscreen=$false;DxVersion=11;GraphicsProfile=$GraphicsProfile;Graphics=$targets.Graphics};BackupPath=$backup}
}
