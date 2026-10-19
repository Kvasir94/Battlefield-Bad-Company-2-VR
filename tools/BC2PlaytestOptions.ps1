# Read declarative parameter metadata only; never evaluate wrapper source.
function Get-PlaytestWeaponRanges {
    return [ordered]@{
        XM8=''; 'XM8 + SPAS'=''; 'SPAS-12'='SP_shg_SPAS12'; M9=''; Arsenal=''
        M416='UL_rif_HK416'
        'XM8 Compact'='US_rif_XM8c'
        MG36='BU_lmg_MG36'
        'XM8 LMG'='US_rif_XM8lmg'
        VSS='BU_sni_VSSVintores'
        'AKS-74U'='RU_rif_AKS74u'
        UZI='BU_smg_IMIUZI'
    }
}
function Get-PlaytestOptionDefinitions([string]$Package){
    $path=Join-Path $Package 'tools/Start-BC2Game.ps1'
    if(-not (Test-Path -LiteralPath $path)){$path=Join-Path $Package 'reports/Start-Installed-BC2VR.ps1'}
    if(-not (Test-Path -LiteralPath $path)){return @()}
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
    if($errors){throw 'The package launch wrapper could not be parsed.'}
    $reserved=@('Runtime','RuntimeJson','AttachToRunningGame','PrepareGameOnly','ReportFolder','Stage','Config','RenderWidth','RenderHeight','GraphicsProfile','HandsMode')
    foreach($param in $ast.ParamBlock.Parameters){
        $name=$param.Name.VariablePath.UserPath
        if($name -in $reserved){continue}
        $type=$param.StaticType
        if($type -notin @([string],[int],[double],[Management.Automation.SwitchParameter])){continue}
        $default=if($type -eq [Management.Automation.SwitchParameter]){$false}elseif($param.DefaultValue){$param.DefaultValue.SafeGetValue()}else{''}
        if($name -eq 'StereoCapture'){$default='Off'}
        $choices=@();$minimum=$null;$maximum=$null
        foreach($attr in $param.Attributes){
            if($attr.TypeName.Name -eq 'ValidateSet'){$choices=@($attr.PositionalArguments|ForEach-Object {$_.SafeGetValue()})}
            if($attr.TypeName.Name -eq 'ValidateRange'){$minimum=$attr.PositionalArguments[0].SafeGetValue();$maximum=$attr.PositionalArguments[1].SafeGetValue()}
        }
        [pscustomobject]@{Name=$name;Label=($name -creplace '([a-z0-9])([A-Z])','$1 $2');Type=$type;Default=$default;Choices=$choices;Minimum=$minimum;Maximum=$maximum}
    }
}
function Convert-PlaytestOptions([string]$Package,$Values){
    $defs=@{};foreach($d in @(Get-PlaytestOptionDefinitions $Package)){$defs[$d.Name]=$d}
    $result=@{}
    foreach($name in $Values.Keys){
        if(-not $defs.ContainsKey($name)){throw "Unsupported test option: $name"}
        $d=$defs[$name];$v=$Values[$name]
        if($d.Type -eq [Management.Automation.SwitchParameter]){
            if([string]$v -notin @('True','False')){throw "$($d.Label) must be True or False."}
            $v=[bool]::Parse([string]$v)
        }elseif($d.Type -eq [string]){$v=[string]$v}
        else{$v=[Convert]::ChangeType($v,$d.Type,[Globalization.CultureInfo]::InvariantCulture)}
        if($d.Choices.Count -and $v -notin $d.Choices){throw "$($d.Label): choose $($d.Choices -join ', ')."}
        if($null -ne $d.Minimum -and ($v -lt $d.Minimum -or $v -gt $d.Maximum)){throw "$($d.Label) must be $($d.Minimum)-$($d.Maximum)."}
        $result[$name]=$v
    }
    return $result
}
