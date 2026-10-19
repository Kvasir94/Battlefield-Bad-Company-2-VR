<#
.SYNOPSIS
Developer launcher GUI for the BC2 VR mod: launch (VR / flat / test range only),
compose the test-range cfg, set mod environment switches, run incremental
builds, and keep named form profiles.

.EXAMPLE
  .\DevLauncher.cmd              # open the GUI
  .\DevLauncher.ps1 -SelfTest    # build the form off-screen, print generated cfgs, launch nothing

Nothing is written to the game folder. The composed cfg goes to
<stage>\ranges\_launcher.cfg; profiles to %LOCALAPPDATA%\BC2VR\launcher-profiles.json
(gitignored). Environment variables are set on the launched child process only.
-Task is internal: the Build tab re-runs this script with it for the test-range
DLL build.
#>
[CmdletBinding()]
param(
    [switch]$SelfTest,
    [ValidateSet('','BuildTestRange')][string]$Task='',
    [string]$Stage,
    [string]$PackageRoot
)
$ErrorActionPreference='Stop'
$Root=$PSScriptRoot
. (Join-Path $Root 'tools/BC2PlaytestPanel.ps1')
$script:HostExe=(Get-Process -Id $PID).Path
$script:ProfilePath=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'BC2VR\launcher-profiles.json'
# Retain existing local developer profiles on first use.
$legacyProfiles=Join-Path $Root 'config/launcher-profiles.json'
if(-not $SelfTest -and -not (Test-Path $script:ProfilePath) -and (Test-Path $legacyProfiles)){
    [void][IO.Directory]::CreateDirectory((Split-Path $script:ProfilePath));Copy-Item -LiteralPath $legacyProfiles -Destination $script:ProfilePath
}
$script:Loading=$false

# ---------------------------------------------------------------- data tables

# SP level folders from docs/BC2_TEST_LEVEL_ROUTES.md; only SP_02 is proven to load via newSP.
$script:Levels='Levels/SP_01','Levels/SP_02','Levels/SP_03','Levels/SP_04','Levels/SP_003_b','Levels/SP_004_b','Levels/SP_005','Levels/SP_005_b','Levels/SP_006','Levels/SP_007','Levels/SP_008','Levels/SP_008_b','Levels/SP_009'

# Mount presets, in the order they are emitted. The order inside a preset matters.
$script:MountPresets=[ordered]@{
    'MP weapons'=@('package:levels/mp2_common/level-00.fbrb','game:levels/mp_common/level-00.fbrb')
    'MP weapon shaders'=@('package:levels/mp_009sdm/level-00.fbrb')
    'Vietnam'=@('package:levels/nam_common/level-00.fbrb','package:levels/nam_mp_002sdm/level-00.fbrb')
}
$script:SlotNames='0 Primary1','1 Primary2','2 GL1','3 GL2','4 StaticGadget','5 DynGadget1','6 DynGadget2','7 Melee','8 HandGrenade'

# Mod environment variables. Kind: off = kill switch (default on, 0 disables),
# on = opt-in (default off, 1 enables), val = tuning value. The row's checkbox
# means "export this variable to the launched process".
function Send-ArsenalStep([string]$Which){
    if(-not ('FvrArsenalEvent' -as [type])){
        Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public static class FvrArsenalEvent{[DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]public static extern IntPtr OpenEventW(uint a,bool i,string n);[DllImport("kernel32.dll")]public static extern bool SetEvent(IntPtr h);[DllImport("kernel32.dll")]public static extern bool CloseHandle(IntPtr h);}'
    }
    $h=[FvrArsenalEvent]::OpenEventW(2,$false,"Local\BC2TestRange.$Which")
    if($h -eq [IntPtr]::Zero){Write-Log "arsenal: Local\BC2TestRange.$Which not found (is a test range with Arsenal.* running?)";return}
    [void][FvrArsenalEvent]::SetEvent($h);[void][FvrArsenalEvent]::CloseHandle($h)
    Write-Log "arsenal: $Which sent"
}
function New-EnvDef([string]$Name,[string]$Kind,[string]$Value,[string[]]$Choices,[string]$Hint){
    [pscustomobject]@{Name=$Name;Kind=$Kind;Value=$Value;Choices=$Choices;Hint=$Hint}
}
$script:EnvDefs=@(
    New-EnvDef 'FVR_BC2_DROPPED_MAGS' 'off' '0' @('0','1') 'Dropped magazines (default on)'
    New-EnvDef 'FVR_BC2_DROPPED_MAG_MAX' 'val' '4' @() 'Live dropped mags before the oldest recycles, 1..6'
    New-EnvDef 'FVR_BC2_DROPPED_MAG_LIFETIME_S' 'val' '30' @() 'Seconds a dropped mag stays, 1..600'
    New-EnvDef 'FVR_BC2_DROPPED_MAG_VISIBLE_M' 'val' '8' @() 'Horizontal draw radius of dropped mags, 1..50 m'
    New-EnvDef 'FVR_BC2_CHAMBER_RACK' 'off' '0' @('0','1') 'Charging-handle rack after reload (default on)'
    New-EnvDef 'FVR_BC2_SHORT_B_RACK_AFTER_INSERT' 'off' '0' @('0','1') '0 = short B ejects any seated ready mag again'
    New-EnvDef 'FVR_BC2_EMPTY_GATE_FAILCLOSED' 'off' '0' @('0','1') 'Fail-closed empty gate (default on, 0 opens it)'
    New-EnvDef 'FVR_BC2_COPY_AUTHORITY' 'val' '1' @('1','2') 'Server copy authority for reload (1 or 2)'
    New-EnvDef 'FVR_BC2_RELOAD_MAILBOX' 'on' '1' @('0','1') 'Reload mailbox request mode (default off)'
    New-EnvDef 'FVR_BC2_PHYSICAL_WEAPON_AUTHORITY' 'off' '0' @('0','1') '0 skips installing physical weapon authority'
    New-EnvDef 'FVR_BC2_STICK_CYCLE' 'off' '0' @('0','1') 'Stick weapon cycle (default on)'
    New-EnvDef 'FVR_BC2_STICK_CYCLE_MELEE_GRENADE' 'off' '0' @('0','1') 'Melee and grenade in the stick cycle (default on)'
    New-EnvDef 'FVR_BC2_STICK_CYCLE_BACK_MS' 'val' '600' @() 'Hold time for backward stick cycle, 250..1500 ms'
    New-EnvDef 'FVR_INTERACT_ZONES' 'off' '0' @('0','1') '0 = legacy hard-coded ranges, no magnetism'
    New-EnvDef 'FVR_INTERACT_MAGNET' 'off' '0' @('0','1') '0 = instant snap and release'
    New-EnvDef 'FVR_INTERACT_HOVER_CUES' 'off' '0' @('0','1') '0 = no haptic tick entering mag/handle zones'
    New-EnvDef 'FVR_INTERACT_ZONE_SCALE' 'val' '1' @() 'Uniform scale of capture/release distances, 0.5..2'
    New-EnvDef 'FVR_BC2_SUPPORT_KEEP_ON_FLICK' 'off' '0' @('0','1') '0 = a weapon flick cancels the support grip'
    New-EnvDef 'FVR_HANDS_MODE' 'val' 'floating' @('floating','ik') 'Hands presentation (default ik)'
    New-EnvDef 'FVR_TURN_MODE' 'val' 'smooth' @('smooth','snap') 'Turn mode'
    New-EnvDef 'FVR_SMOOTH_TURN_DEG_PER_SEC' 'val' '90' @() 'Smooth turn speed, 30..360 deg/s'
    New-EnvDef 'FVR_XM8_BOLT' 'off' '0' @('0','1') '0 disables the XM8 bolt asset feature'
    New-EnvDef 'FVR_XM8_BOLT_ASSET' 'val' '' @() 'Override path of the XM8 bolt asset'
    New-EnvDef 'FVR_AUTHORED_CONTACTS' 'val' '' @() 'Path of authored contacts data'
    New-EnvDef 'FVR_BC2_EFFECT_ANCHOR' 'off' '0' @('0','1') '0 = native fire-and-forget weapon effects'
    New-EnvDef 'FVR_BC2_EFFECT_ANCHOR_MS' 'val' '400' @() 'Weapon effect follows weapon root, 0..3000 ms'
    New-EnvDef 'FVR_BC2_PRESENT_BRIDGE_PUMP' 'off' '0' @('0','1') '0 disables the present bridge pump'
    New-EnvDef 'FVR_BC2_VIEWPORT_SYNC' 'off' '0' @('0','1') '0 disables viewport sync'
    New-EnvDef 'FVR_XR_ASYNC_CONSUMER_COPY' 'off' '0' @('0','1') 'XR host async consumer copy (default on)'
    New-EnvDef 'FVR_XR_RETAIN_ON_COPY_TIMEOUT' 'off' '0' @('0','1') 'XR host retain frame on copy timeout (default on)'
    New-EnvDef 'FVR_XR_DELIVERY_LOG' 'off' '0' @('0','1') 'XR host per-second delivery summary in xr-events.jsonl (default on)'
    New-EnvDef 'FVR_XR_OVERSCAN' 'val' '0' @() 'XR request FOV overscan 0..0.25 (default 0.08; 0 = full pixel density)'
    New-EnvDef 'FVR_XR_REFRESH_HZ' 'val' '72' @('0','72','80','90','120') 'Headset refresh request, XR_FB_display_refresh_rate runtimes (default 72; 0 = runtime setting)'
    New-EnvDef 'FVR_XR_PACE_TO_CONTENT' 'off' '0' @('0','1') 'XR host waits for the next game pair instead of re-submitting (default on)'
    New-EnvDef 'FVR_XR_PACE_MAX_WAIT_MS' 'val' '0' @() 'Pace-to-content wait limit 1..50 ms (default 0 = one display period)'
    New-EnvDef 'FVR_XR_PIPELINE_DEPTH' 'val' '4' @('1','2','3','4') 'Requests in flight, host and game (default 4; 2 = previous default)'
    New-EnvDef 'FVR_BC2_PER_PAIR_EYES' 'off' '0' @('0','1') 'Own private eye images per legacy pair (default on)'
    New-EnvDef 'FVR_BC2_EXPORT_QUEUE_AT_PUBLISH' 'off' '0' @('0','1') 'Queue the legacy pair copy at publish (default on)'
    New-EnvDef 'FVR_BC2_BRIDGE_LOCK_SPIN_US' 'val' '500' @() 'Bounded wait on a busy frame-bridge gate, 0..5000 us (default 500; 0 = drop)'
    New-EnvDef 'FVR_BC2_INGAME_PROPS' 'on' '1' @('0','1') 'Draw dropped mags in the game scene (opt-in)'
    New-EnvDef 'FVR_BC2_GROUND_PROBE' 'on' '1' @('0','1') 'Ground height from depth (opt-in)'
    New-EnvDef 'FVR_BC2_TEST_PROP_SINK_MM' 'val' '0' @() 'Test: sink in-game props, 1..1000 mm'
    New-EnvDef 'FVR_BC2_CAMERA_RESPONSE_TRACE' 'on' '1' @('0','1') 'Camera response diagnostic'
    New-EnvDef 'FVR_BC2_AIM_ANGLE_TRACE' 'on' '1' @('0','1') 'Aim angle trace (BC2_RECOIL_TRANSFER.md)'
    New-EnvDef 'FVR_BC2_TEST_INPUT_LOG' 'on' '1' @('0','1') 'Log test input'
    New-EnvDef 'FVR_BC2_CAPTURE_HIDE_LEFT_HAND' 'on' '1' @('0','1') 'Hide left hand in captures'
    New-EnvDef 'FVR_BC2_STEREO_CAPTURE' 'val' '1' @('0','1','2') 'Stereo-pair diagnostic (unset/0 = off)'
    New-EnvDef 'FVR_BC2_STEREO_CAPTURE_AUTO' 'val' '1' @() 'Stereo capture auto arms, 0..4'
    New-EnvDef 'FVR_BC2_STEREO_CAPTURE_MAX_ARMS' 'val' '6' @() 'Stereo capture arms per session, 1..16'
)

$script:EngineToggles=[ordered]@{
    devMenu='Client.IsMenuDevOptsEnabled'; lThumb='Client.DebugMenuOnLThumb'
    unlimitedAmmo='Server.HasUnlimitedAmmo'; restTool='Client.EnableRestTool'
}
# State keys that belong to the cfg composer (a template import replaces exactly these).
$script:CfgKeys='level','start','difficulty','waitFrames','skipCutscene','skipSeconds','presets','extraMounts','regNet','traceUi','shaderOrder','reequip','devMenu','lThumb','unlimitedAmmo','restTool','extraEngine','slots','select','loaded','reserve','arsenalAll','arsenalExclude','arsenalGuns','arsenalNext','arsenalPrev','arsenalInterval'

# ------------------------------------------------------------------- helpers

function Get-DefaultStage {
    # Same resolution order as Play-BC2VR.ps1.
    if($env:FVR_BC2_STAGE){return $env:FVR_BC2_STAGE}
    if(Test-Path (Join-Path $Root 'stage\BC2TestRange.dll')){return (Join-Path $Root 'stage')}
    return (Join-Path $env:USERPROFILE 'fvr-testrange-stage')
}
function Get-GamePathFromConfig {
    $p=Join-Path $Root 'config\local.json'
    if(Test-Path -LiteralPath $p){try{return (Get-Content -LiteralPath $p -Raw | ConvertFrom-Json).game_path}catch{}}
    return ''
}
function Get-LevelTarget([string]$Level){
    $folder=(($Level -split '[\\/]')[-1]).ToLowerInvariant()
    return "game:levels/$folder/level-00.fbrb"
}
function Quote-Arg([string]$a){
    if($a -eq ''){return '""'}
    if($a -match '[\s"]'){return '"'+($a -replace '"','\"')+'"'}
    return $a
}
function ConvertTo-Hashtable($o){
    if($null -eq $o){return $null}
    if($o -is [string]){return $o}
    if($o -is [System.Collections.IDictionary]){$h=@{};foreach($k in $o.Keys){$h[$k]=ConvertTo-Hashtable $o[$k]};return $h}
    if($o -is [pscustomobject]){$h=@{};foreach($p in $o.PSObject.Properties){$h[$p.Name]=ConvertTo-Hashtable $p.Value};return $h}
    if($o -is [System.Collections.IEnumerable]){return ,@(foreach($i in $o){ConvertTo-Hashtable $i})}
    return $o
}

function Get-DefaultState {
    @{
        mode='VR'; width=0; height=0; mirror=$false; steady=$true
        gamePath=(Get-GamePathFromConfig); stage=(Get-DefaultStage); openXr=''
        injectDelay=3000; seconds=60; useCfg=$true; template=''
        level='Levels/SP_02'; start=$true; difficulty=1; waitFrames=60; skipCutscene=$true; skipSeconds=60
        presets=@(); extraMounts=''; regNet=$false; traceUi=$false; shaderOrder=$true; reequip=$false
        devMenu=$false; lThumb=$false; unlimitedAmmo=$false; restTool=$false; extraEngine=''
        slots=@('','','','','','','','',''); select=''; loaded=''; reserve=''
        arsenalAll=$false; arsenalExclude=''; arsenalGuns=''; arsenalNext=''; arsenalPrev=''; arsenalInterval=''
        env=@{}; extraEnv=''
    }
}

# ------------------------------------------------------------- cfg generation

function New-RangeCfg($s){
    $L=New-Object System.Collections.Generic.List[string]
    $L.Add('# Generated by DevLauncher.ps1; edit in the launcher, not here.')
    if($s.level){$L.Add("Client.Level $($s.level)")}
    foreach($k in $script:EngineToggles.Keys){if($s[$k]){$L.Add("$($script:EngineToggles[$k]) true")}}
    foreach($line in ($s.extraEngine -split "`r?`n")){$t=$line.Trim();if($t -and -not $t.StartsWith('#')){$L.Add($t)}}
    if($s.start){
        $L.Add('TestRange.Start newSP')
        $L.Add("TestRange.Difficulty $([int]$s.difficulty)")
        $L.Add("TestRange.WaitFrames $([int]$s.waitFrames)")
    }
    if($s.skipCutscene){$L.Add('TestRange.SkipCutscene 1');$L.Add("TestRange.SkipSeconds $([int]$s.skipSeconds)")}
    else{$L.Add('TestRange.SkipCutscene 0')}
    $target=Get-LevelTarget $s.level
    $mounts=New-Object System.Collections.Generic.List[string]
    foreach($name in $script:MountPresets.Keys){
        if(@($s.presets) -contains $name){foreach($src in $script:MountPresets[$name]){$mounts.Add("$target $src")}}
    }
    foreach($line in ($s.extraMounts -split "`r?`n")){
        $t=$line.Trim();if(-not $t -or $t.StartsWith('#')){continue}
        if($t -notmatch '\s'){$t="$target $t"}   # a bare bundle mounts into the selected level
        $mounts.Add($t)
    }
    foreach($m in $mounts){$L.Add("TestRange.Mount $m")}
    if($s.regNet){$L.Add('TestRange.RegisterNetIds 1')}
    if($s.traceUi){$L.Add('TestRange.TraceUi 1')}
    if($mounts.Count){$L.Add("TestRange.ShaderOrder $(if($s.shaderOrder){1}else{0})")}   # DLL default is on
    if($s.reequip){$L.Add('TestRange.ReequipGuard 1')}                                     # DLL default is off
    $slots=@($s.slots)
    for($i=0;$i -lt 9;$i++){if($i -lt $slots.Count -and "$($slots[$i])".Trim()){$L.Add("Give.Slot$i $("$($slots[$i])".Trim())")}}
    if("$($s.select)".Trim() -ne ''){$L.Add("Give.Select $("$($s.select)".Trim())")}
    if("$($s.loaded)".Trim() -ne ''){$L.Add("Give.Loaded $("$($s.loaded)".Trim())")}
    if("$($s.reserve)".Trim() -ne ''){$L.Add("Give.Reserve $("$($s.reserve)".Trim())")}
    # Arsenal.* (tools/testrange/BC2TestRange.cpp ParseArsenal): cycle guns through Primary1/2 with hotkeys.
    foreach($g in ($s.arsenalGuns -split "`r?`n")){$t=$g.Trim();if($t -and -not $t.StartsWith('#')){$L.Add("Arsenal.Gun $t")}}
    if($s.arsenalAll){$L.Add('Arsenal.All 1')}
    if("$($s.arsenalExclude)".Trim()){$L.Add("Arsenal.Exclude $("$($s.arsenalExclude)".Trim())")}
    if("$($s.arsenalNext)".Trim()){$L.Add("Arsenal.NextKey $("$($s.arsenalNext)".Trim())")}
    if("$($s.arsenalPrev)".Trim()){$L.Add("Arsenal.PrevKey $("$($s.arsenalPrev)".Trim())")}
    if("$($s.arsenalInterval)".Trim()){$L.Add("Arsenal.MinIntervalMs $("$($s.arsenalInterval)".Trim())")}
    return (($L -join "`r`n")+"`r`n")
}

# Mirrors the key checks in Worker() of tools/testrange/BC2TestRange.cpp. Returns error strings.
function Test-RangeCfg([string]$Text){
    $errors=New-Object System.Collections.Generic.List[string]
    $testRange='Start','Difficulty','WaitFrames','SkipCutscene','SkipSeconds','TraceUi','RegisterNetIds','ReequipGuard','ShaderOrder','Mount'
    $groups='Client.','Server.','Game.','Network.','Physics.'
    $n=0
    foreach($raw in ($Text -split "`r?`n")){
        $n++;$line=$raw.Trim()
        if(-not $line -or $line[0] -eq '#' -or $line[0] -eq ';'){continue}
        $sp=$line.IndexOfAny([char[]]" `t")
        if($sp -lt 0){$errors.Add("line ${n}: no value (the DLL silently drops it): $line");continue}
        $key=$line.Substring(0,$sp);$val=$line.Substring($sp).Trim()
        if($key.IndexOf('.') -lt 0 -or $key.Length -gt 128 -or $val.Length -gt 256){$errors.Add("line ${n}: key/value dropped by the DLL reader: $key");continue}
        if($key -like 'TestRange.*'){
            if($testRange -notcontains $key.Substring(10)){$errors.Add("line ${n}: unknown key $key (DLL refuses the run)")}
            elseif($key -eq 'TestRange.Mount' -and $val -notmatch '\s'){$errors.Add("line ${n}: TestRange.Mount needs '<source> <target>'")}
        }elseif($key -like 'Arsenal.*'){
            if('Gun','All','Exclude','NextKey','PrevKey','MinIntervalMs','Start','First' -notcontains $key.Substring(8)){$errors.Add("line ${n}: unknown key $key (DLL ignores it)")}
        }elseif($key -like 'Give.*'){
            $f=$key.Substring(5)
            if($f -match '^Slot[0-8]$'){}
            elseif($f -eq 'Select' -and $val -match '^[0-8]$'){}
            elseif(($f -eq 'Loaded' -or $f -eq 'Reserve') -and $val -match '^-?\d+$' -and [int]$val -ge -1){}
            else{$errors.Add("line ${n}: bad Give key/value: $key $val")}
        }elseif(-not ($groups | Where-Object {$key.StartsWith($_)})){
            $errors.Add("line ${n}: unknown key $key (DLL refuses the run)")
        }
    }
    return ,$errors.ToArray()
}

# Parse a cfg file into the composer part of the form state.
function Import-RangeCfg([string]$Path){
    $d=Get-DefaultState;$s=@{}
    foreach($k in $script:CfgKeys){$s[$k]=$d[$k]}
    $s.start=$false;$s.skipCutscene=$false   # an absent key means off in the DLL
    $mounts=New-Object System.Collections.Generic.List[string]
    $engine=New-Object System.Collections.Generic.List[string]
    $slots=@('','','','','','','','','')
    foreach($raw in (Get-Content -LiteralPath $Path)){
        $line=$raw.Trim()
        if(-not $line -or $line[0] -eq '#' -or $line[0] -eq ';'){continue}
        $sp=$line.IndexOfAny([char[]]" `t");if($sp -lt 0){continue}
        $key=$line.Substring(0,$sp);$val=$line.Substring($sp).Trim()
        $on=($val -eq '1' -or $val -eq 'true')
        switch -Wildcard ($key){
            'Client.Level'{$s.level=$val}
            'TestRange.Start'{$s.start=($val -eq 'newSP')}
            'TestRange.Difficulty'{$s.difficulty=[int]$val}
            'TestRange.WaitFrames'{$s.waitFrames=[int]$val}
            'TestRange.SkipCutscene'{$s.skipCutscene=($val -eq '1')}
            'TestRange.SkipSeconds'{$s.skipSeconds=[int]$val}
            'TestRange.TraceUi'{$s.traceUi=($val -eq '1')}
            'TestRange.RegisterNetIds'{$s.regNet=($val -eq '1')}
            'TestRange.ReequipGuard'{$s.reequip=($val -ne '0')}
            'TestRange.ShaderOrder'{$s.shaderOrder=($val -ne '0')}
            'TestRange.Mount'{$mounts.Add($val)}
            'Give.Slot?'{$slots[[int]$key.Substring(9)]=$val}
            'Give.Select'{$s.select=$val}
            'Give.Loaded'{$s.loaded=$val}
            'Give.Reserve'{$s.reserve=$val}
            'Arsenal.Gun'{$s.arsenalGuns=(@($s.arsenalGuns,$val) | Where-Object {$_}) -join "`r`n"}
            'Arsenal.All'{$s.arsenalAll=($val -eq '1')}
            'Arsenal.Exclude'{$s.arsenalExclude=$val}
            'Arsenal.NextKey'{$s.arsenalNext=$val}
            'Arsenal.PrevKey'{$s.arsenalPrev=$val}
            'Arsenal.MinIntervalMs'{$s.arsenalInterval=$val}
            default{
                $hit=$false
                foreach($k in $script:EngineToggles.Keys){if($script:EngineToggles[$k] -eq $key){$s[$k]=$on;$hit=$true}}
                if(-not $hit){$engine.Add("$key $val")}
            }
        }
    }
    $s.slots=$slots
    $s.extraEngine=($engine -join "`r`n")
    # Recognise presets: every source of a preset present with the level's own target.
    $target=Get-LevelTarget $s.level
    $pool=New-Object System.Collections.Generic.List[string]
    foreach($m in $mounts){$pool.Add($m)}
    $presets=@()
    foreach($name in $script:MountPresets.Keys){
        $want=@($script:MountPresets[$name] | ForEach-Object {"$target $_"})
        if(@($want | Where-Object {$pool -contains $_}).Count -eq $want.Count){$presets+=$name;foreach($w in $want){[void]$pool.Remove($w)}}
    }
    $s.presets=$presets
    $s.extraMounts=($pool -join "`r`n")
    return $s
}

# -------------------------------------------------------------- launch plan

function Get-EnvMap($s){
    $map=[ordered]@{}
    foreach($k in $s.env.Keys){$map[$k]=[string]$s.env[$k]}
    foreach($line in ($s.extraEnv -split "`r?`n")){
        $t=$line.Trim();if(-not $t -or $t.StartsWith('#')){continue}
        if($t -notmatch '^([A-Za-z_][A-Za-z0-9_]*)=(.*)$'){throw "Bad extra env line (want NAME=VALUE): $t"}
        $map[$Matches[1]]=$Matches[2]
    }
    return $map
}

# Pure: describes what a launch would run. Writes nothing, starts nothing.
function Get-LaunchPlan($s){
    $cfgRel='ranges\_launcher.cfg'
    $cfgPath=Join-Path $s.stage $cfgRel
    $a=@('-NoProfile','-ExecutionPolicy','Bypass','-File')
    if($s.mode -eq 'TestRange'){
        $a+=(Join-Path $Root 'tools\testrange\Start-BC2TestRange.ps1')
        $a+=@('-GamePath',$s.gamePath,'-Bin',$s.stage,'-InjectDelayMs',"$([int]$s.injectDelay)",'-Seconds',"$([int]$s.seconds)",'-KeepRunning')
        if($s.useCfg){$a+=@('-Config',$cfgPath)}
    }else{
        $a+=(Join-Path $Root 'Play-BC2VR.ps1')
        if($s.mode -eq 'Flat'){$a+='-Flat'}
        if($s.mirror){$a+='-Mirror'}
        if(-not $s.steady){$a+='-NoSteadyWindow'}
        # 0 x 0: the headset runtime's recommended eye size (VR) or 1920x1080 (flat).
        if([int]$s.width -gt 0 -and [int]$s.height -gt 0){$a+=@('-RenderWidth',"$([int]$s.width)",'-RenderHeight',"$([int]$s.height)")}
        if($s.openXr){$a+=@('-OpenXrLoader',$s.openXr)}
        # Play-BC2VR joins -Config onto the stage, so it must stay relative.
        $a+=@('-Stage',$s.stage)
        if($s.useCfg){$a+=@('-Config',$cfgRel)}
        if($s.gamePath){$a+=@('-GamePath',$s.gamePath)}
    }
    [pscustomobject]@{
        File=$script:HostExe; Args=$a; Env=(Get-EnvMap $s)
        CfgPath=$cfgPath; CfgText=(New-RangeCfg $s)
        CommandLine=((@($script:HostExe)+$a | ForEach-Object {Quote-Arg $_}) -join ' ')
    }
}

# --------------------------------------------------------------- task: build

function Invoke-BuildTestRange([string]$StageDir){
    $PSNativeCommandUseErrorActionPreference=$false
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(-not $vs){throw 'Visual Studio C++ tools are required.'}
    & (Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch x86 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $b=Join-Path $Root 'build\testrange-x86'
    $src=Join-Path $Root 'tools\testrange'
    if(-not (Test-Path (Join-Path $b 'CMakeCache.txt'))){
        'Configuring build\testrange-x86 (first build)...'
        cmake -S $src -B $b -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1 | ForEach-Object {"$_"}
        if($LASTEXITCODE){throw "configure failed ($LASTEXITCODE)"}
    }
    $started=Get-Date
    cmake --build $b --config Release --target BC2TestRange --parallel 2 2>&1 | ForEach-Object {"$_"}
    if($LASTEXITCODE){throw "build failed ($LASTEXITCODE)"}
    # Visual Studio generators drop the DLL in Release\, Ninja in the build root.
    $dll=@('Release\BC2TestRange.dll','BC2TestRange.dll') | ForEach-Object {Join-Path $b $_} |
        Where-Object {Test-Path $_} | Sort-Object {(Get-Item $_).LastWriteTime} -Descending | Select-Object -First 1
    if(-not $dll){throw 'BC2TestRange.dll not found after the build.'}
    if((Get-Item $dll).LastWriteTime -lt $started.AddSeconds(-2)){"Note: $dll was not rewritten (already up to date)."}
    if(Get-Process BFBC2Game -ErrorAction SilentlyContinue){throw 'BC2 is running and holds the staged DLL; close it, then build again.'}
    New-Item -ItemType Directory -Path $StageDir -Force | Out-Null
    $target=Join-Path $StageDir 'BC2TestRange.dll'
    if(Test-Path $target){Copy-Item $target (Join-Path $StageDir 'BC2TestRange-prev.dll') -Force;'Backed up the staged DLL as BC2TestRange-prev.dll.'}
    Copy-Item $dll $target -Force
    "Staged $target ($((Get-FileHash $target).Hash.Substring(0,12)))"
}

# ----------------------------------------------------------------------- GUI

function Initialize-Gui {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [System.Windows.Forms.Application]::EnableVisualStyles()
    if(-not ('LauncherJob' -as [type])){
        Add-Type -TypeDefinition @"
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text;
using System.Threading;
// Runs a child with redirected output. Lines queue up for the UI timer, so the GUI never blocks.
// The game inherits the pipe handles and can outlive the script, so exit is reported after a short
// grace period instead of waiting for end-of-stream.
public sealed class LauncherJob {
    public readonly string Tag;
    public readonly ConcurrentQueue<string> Lines = new ConcurrentQueue<string>();
    public volatile bool Finished;
    public int ExitCode;
    public Process Proc;
    public LauncherJob(string tag) { Tag = tag; }
    public void Start(ProcessStartInfo psi) {
        psi.UseShellExecute = false;
        psi.RedirectStandardOutput = true;
        psi.RedirectStandardError = true;
        psi.CreateNoWindow = true;
        psi.StandardOutputEncoding = Encoding.UTF8;
        psi.StandardErrorEncoding = Encoding.UTF8;
        Proc = new Process();
        Proc.StartInfo = psi;
        Proc.EnableRaisingEvents = true;
        Proc.OutputDataReceived += (s, e) => { if (e.Data != null) Lines.Enqueue(e.Data); };
        Proc.ErrorDataReceived += (s, e) => { if (e.Data != null) Lines.Enqueue(e.Data); };
        Proc.Exited += (s, e) => {
            ThreadPool.QueueUserWorkItem(o => {
                Thread.Sleep(400);
                try { ExitCode = Proc.ExitCode; } catch { }
                Lines.Enqueue("[exit code " + ExitCode + "]");
                Finished = true;
            });
        };
        Proc.Start();
        Proc.BeginOutputReadLine();
        Proc.BeginErrorReadLine();
    }
}
"@
    }
}

function Load-WeaponCatalog {
    $script:GiveCat=@{}
    $script:GiveNames=New-Object System.Collections.Generic.List[string]
    $csv=Join-Path $Root 'profiles\weapon-inventory-20261010.csv'
    if(-not (Test-Path $csv)){return}
    foreach($r in (Import-Csv -LiteralPath $csv -Encoding UTF8)){
        if($r.give -and -not $script:GiveCat.ContainsKey($r.give)){
            $script:GiveCat[$r.give]="$($r.category) | level data: $($r.source)"
            $script:GiveNames.Add($r.give)
        }
    }
    $script:GiveNames.Sort([StringComparer]::OrdinalIgnoreCase)
}

function Add-Ctl([string]$Type,$Parent,[int]$X,[int]$Y,[int]$W,[int]$H,[string]$Text=''){
    $c=New-Object "System.Windows.Forms.$Type"
    $c.Location=New-Object System.Drawing.Point($X,$Y)
    if($W -gt 0){$c.Size=New-Object System.Drawing.Size($W,$H)}else{$c.AutoSize=$true}
    if($Text -ne ''){$c.Text=$Text}
    $Parent.Controls.Add($c)
    return $c
}
function Add-Label($Parent,[int]$X,[int]$Y,[string]$Text,[int]$W=0){Add-Ctl 'Label' $Parent $X ($Y+3) $W 18 $Text}

function New-LauncherForm {
    Initialize-Gui
    Load-WeaponCatalog
    $C=@{};$script:C=$C
    $form=New-Object System.Windows.Forms.Form
    $form.Text='Bad Company 2 VR Launcher';$form.Size=New-Object System.Drawing.Size(1080,900)
    $form.StartPosition='CenterScreen';$form.Font=New-Object System.Drawing.Font('Segoe UI',9)
    $script:Form=$form

    $split=New-Object System.Windows.Forms.SplitContainer
    $split.Dock='Fill';$split.Orientation='Horizontal';$split.SplitterDistance=640
    $form.Controls.Add($split)
    $tabs=New-Object System.Windows.Forms.TabControl;$tabs.Dock='Fill'
    $split.Panel1.Controls.Add($tabs)
    $log=New-Object System.Windows.Forms.TextBox
    $log.Multiline=$true;$log.ReadOnly=$true;$log.ScrollBars='Both';$log.WordWrap=$false;$log.Dock='Fill'
    $log.Font=New-Object System.Drawing.Font('Consolas',9);$log.MaxLength=0
    $split.Panel2.Controls.Add($log)
    $script:Log=$log
    $status=New-Object System.Windows.Forms.Label;$status.Dock='Bottom';$status.Height=20;$status.Text='Ready.'
    $form.Controls.Add($status);$script:Status=$status

    function New-Page([string]$Title){
        $p=New-Object System.Windows.Forms.TabPage;$p.Text=$Title;$p.AutoScroll=$true;$tabs.TabPages.Add($p);return $p
    }

    Add-PlaytestPanel $tabs

    # ---- Launch tab
    $t=New-Page 'Developer launch'
    $g=Add-Ctl 'GroupBox' $t 12 8 640 52 'Mode'
    $C.modeVR=Add-Ctl 'RadioButton' $g 14 20 190 22 'VR (Play-BC2VR.ps1)'
    $C.modeFlat=Add-Ctl 'RadioButton' $g 214 20 190 22 'Flat (Play-BC2VR.ps1 -Flat)'
    $C.modeTR=Add-Ctl 'RadioButton' $g 414 20 210 22 'Test range only'
    Add-Label $t 12 72 'Render width' | Out-Null
    $C.width=Add-Ctl 'NumericUpDown' $t 100 72 70 22;$C.width.Minimum=0;$C.width.Maximum=8192
    Add-Label $t 190 72 'height (0 = headset)' | Out-Null
    $C.height=Add-Ctl 'NumericUpDown' $t 240 72 70 22;$C.height.Minimum=0;$C.height.Maximum=8192
    $C.mirror=Add-Ctl 'CheckBox' $t 330 72 200 22 'Mirror window (-Mirror)'
    $C.steady=Add-Ctl 'CheckBox' $t 530 72 220 22 'Steady game window'
    Add-Label $t 12 102 'Game path' | Out-Null
    $C.gamePath=Add-Ctl 'TextBox' $t 100 102 640 22;$C.gamePath.Anchor='Top,Left,Right'
    Add-Label $t 12 130 'Stage dir' | Out-Null
    $C.stage=Add-Ctl 'TextBox' $t 100 130 640 22;$C.stage.Anchor='Top,Left,Right'
    Add-Label $t 12 158 'OpenXR loader' | Out-Null
    $C.openXr=Add-Ctl 'TextBox' $t 100 158 640 22;$C.openXr.Anchor='Top,Left,Right'
    Add-Label $t 12 186 'Inject delay ms' | Out-Null
    $C.injectDelay=Add-Ctl 'NumericUpDown' $t 100 186 80 22;$C.injectDelay.Minimum=0;$C.injectDelay.Maximum=60000
    Add-Label $t 200 186 'Wait seconds' | Out-Null
    $C.seconds=Add-Ctl 'NumericUpDown' $t 285 186 70 22;$C.seconds.Minimum=10;$C.seconds.Maximum=600
    Add-Label $t 372 186 '(test range only; the game is kept running)' | Out-Null
    $C.useCfg=Add-Ctl 'CheckBox' $t 12 216 700 22 'Use the cfg composed on the Test range cfg tab (written to <stage>\ranges\_launcher.cfg)'
    $bLaunch=Add-Ctl 'Button' $t 12 250 110 30 'Launch'
    $bKill=Add-Ctl 'Button' $t 130 250 110 30 'Kill game'
    $bKillVr=Add-Ctl 'Button' $t 248 250 140 30 'Kill VR helpers'
    $bLog=Add-Ctl 'Button' $t 396 250 110 30 'Open log'
    $bStage=Add-Ctl 'Button' $t 514 250 130 30 'Open stage folder'
    $bCfg=Add-Ctl 'Button' $t 652 250 120 30 'Open launcher cfg'
    # Step the running test range's arsenal through its named events, like the
    # headset chord (left trigger + right-stick flick) and F6/F7 do.
    $bPrevGun=Add-Ctl 'Button' $t 780 250 100 30 '< Prev gun'
    $bNextGun=Add-Ctl 'Button' $t 888 250 100 30 'Next gun >'
    $bPrevGun.Add_Click({Send-ArsenalStep 'ArsenalPrev'})
    $bNextGun.Add_Click({Send-ArsenalStep 'ArsenalNext'})
    Add-Label $t 12 292 'Command that Launch will run:' | Out-Null
    $cmd=Add-Ctl 'TextBox' $t 12 314 1000 100;$cmd.Multiline=$true;$cmd.ReadOnly=$true;$cmd.ScrollBars='Vertical';$cmd.Anchor='Top,Left,Right'
    $script:CmdBox=$cmd

    # ---- Test range cfg tab
    $t=New-Page 'Test range cfg'
    $sp=New-Object System.Windows.Forms.SplitContainer;$sp.Dock='Fill';$sp.SplitterDistance=640
    $t.AutoScroll=$false;$t.Controls.Add($sp)
    $L=$sp.Panel1;$L.AutoScroll=$true
    Add-Label $L 8 8 'Start template' | Out-Null
    $C.template=Add-Ctl 'ComboBox' $L 100 8 380 22;$C.template.DropDownStyle='DropDownList'
    $bTplRefresh=Add-Ctl 'Button' $L 490 6 70 24 'Reload'
    Add-Label $L 8 38 'Level' | Out-Null
    $C.level=Add-Ctl 'ComboBox' $L 100 38 200 22;$C.level.DropDownStyle='DropDown'
    foreach($lv in $script:Levels){[void]$C.level.Items.Add($lv)}
    Add-Label $L 310 38 '(only SP_02 is proven to load)' | Out-Null
    $C.start=Add-Ctl 'CheckBox' $L 8 66 110 22 'newSP start'
    Add-Label $L 130 66 'Difficulty' | Out-Null
    $C.difficulty=Add-Ctl 'NumericUpDown' $L 195 66 50 22;$C.difficulty.Minimum=0;$C.difficulty.Maximum=9
    Add-Label $L 260 66 'Wait frames' | Out-Null
    $C.waitFrames=Add-Ctl 'NumericUpDown' $L 340 66 70 22;$C.waitFrames.Minimum=0;$C.waitFrames.Maximum=100000
    $C.skipCutscene=Add-Ctl 'CheckBox' $L 8 94 130 22 'Skip cutscene'
    Add-Label $L 140 94 'for up to' | Out-Null
    $C.skipSeconds=Add-Ctl 'NumericUpDown' $L 200 94 60 22;$C.skipSeconds.Minimum=1;$C.skipSeconds.Maximum=600
    Add-Label $L 266 94 'seconds' | Out-Null
    $g=Add-Ctl 'GroupBox' $L 8 124 600 100 'Mounts (TestRange.Mount <level bundle> <mounted bundle>)'
    $px=12
    foreach($name in $script:MountPresets.Keys){
        $cb=Add-Ctl 'CheckBox' $g $px 20 170 22 $name;$C["preset:$name"]=$cb;$px+=185
    }
    Add-Label $g 12 48 'Extra lines: "<level bundle> <mounted bundle>" (or just the mounted bundle)' | Out-Null
    $C.extraMounts=Add-Ctl 'TextBox' $g 12 68 576 28;$C.extraMounts.Multiline=$true;$C.extraMounts.ScrollBars='Vertical'
    $g.Height=110;$C.extraMounts.Height=36
    $C.regNet=Add-Ctl 'CheckBox' $L 8 242 230 22 'RegisterNetIds (auto-on with a mount)'
    $C.traceUi=Add-Ctl 'CheckBox' $L 250 242 100 22 'TraceUi'
    $C.shaderOrder=Add-Ctl 'CheckBox' $L 356 242 120 22 'ShaderOrder'
    $C.reequip=Add-Ctl 'CheckBox' $L 480 242 120 22 'ReequipGuard'
    $g=Add-Ctl 'GroupBox' $L 8 272 600 50 'Dev engine settings'
    $C.devMenu=Add-Ctl 'CheckBox' $g 10 20 130 22 'IsMenuDevOpts'
    $C.lThumb=Add-Ctl 'CheckBox' $g 145 20 130 22 'DebugMenuOnLThumb'
    $C.unlimitedAmmo=Add-Ctl 'CheckBox' $g 290 20 150 22 'HasUnlimitedAmmo'
    $C.restTool=Add-Ctl 'CheckBox' $g 450 20 140 22 'EnableRestTool'
    Add-Label $L 8 332 'Other engine lines ("Client./Server./Game./Network./Physics.Field value"):' | Out-Null
    $C.extraEngine=Add-Ctl 'TextBox' $L 8 354 600 40;$C.extraEngine.Multiline=$true;$C.extraEngine.ScrollBars='Vertical'
    $g=Add-Ctl 'GroupBox' $L 8 402 600 332 'Loadout (Give.*)'
    $y=20
    for($i=0;$i -lt 9;$i++){
        Add-Label $g 10 $y $script:SlotNames[$i] 100 | Out-Null
        $cb=Add-Ctl 'ComboBox' $g 112 $y 230 22;$cb.DropDownStyle='DropDown'
        [void]$cb.Items.Add('none')
        foreach($n in $script:GiveNames){[void]$cb.Items.Add($n)}
        try{$cb.AutoCompleteMode='SuggestAppend';$cb.AutoCompleteSource='ListItems'}catch{}
        $cat=Add-Ctl 'Label' $g 348 ($y+3) 245 18;$cat.ForeColor=[System.Drawing.Color]::DimGray
        $cb.Tag=$cat
        $cb.Add_TextChanged({
            $lab=$this.Tag;$k=$this.Text.Trim()
            if($script:GiveCat.ContainsKey($k)){$lab.Text=$script:GiveCat[$k]}elseif($k -eq 'none'){$lab.Text='removes the slot'}else{$lab.Text=''}
        })
        $C["slot$i"]=$cb;$y+=26
    }
    Add-Label $g 10 $y 'Select slot' | Out-Null
    $C.select=Add-Ctl 'ComboBox' $g 112 $y 50 22;$C.select.DropDownStyle='DropDown'
    foreach($i in 0..8){[void]$C.select.Items.Add("$i")}
    Add-Label $g 180 $y 'Loaded' | Out-Null
    $C.loaded=Add-Ctl 'TextBox' $g 228 $y 50 22
    Add-Label $g 296 $y 'Reserve' | Out-Null
    $C.reserve=Add-Ctl 'TextBox' $g 350 $y 50 22
    Add-Label $g 410 $y '(blank = not set; -1 = native/infinite)' | Out-Null
    $y+=30
    Add-Label $g 10 $y 'Find weapon' | Out-Null
    $find=Add-Ctl 'TextBox' $g 112 $y 230 22
    Add-Label $g 350 $y 'matches name or category; double-click to put it in:' | Out-Null
    $y+=26
    $hits=Add-Ctl 'ListBox' $g 112 $y 230 60
    $tgt=Add-Ctl 'ComboBox' $g 350 $y 120 22;$tgt.DropDownStyle='DropDownList'
    foreach($sn in $script:SlotNames){[void]$tgt.Items.Add($sn)};$tgt.SelectedIndex=0
    $find.Add_TextChanged({
        $hits.Items.Clear();$q=$find.Text.Trim();if($q.Length -lt 2){return}
        foreach($n in $script:GiveNames){
            if($n -like "*$q*" -or $script:GiveCat[$n] -like "*$q*"){[void]$hits.Items.Add($n);if($hits.Items.Count -ge 80){break}}
        }
    }.GetNewClosure())
    $hits.Add_DoubleClick({
        if($hits.SelectedItem){$C["slot$($tgt.SelectedIndex)"].Text=[string]$hits.SelectedItem}
    }.GetNewClosure())
    $g=Add-Ctl 'GroupBox' $L 8 744 600 140 'Arsenal (cycle guns through Primary1/2 in game; needs a rebuilt test range DLL)'
    $C.arsenalAll=Add-Ctl 'CheckBox' $g 10 20 220 22 'Arsenal.All (every resident gun)'
    Add-Label $g 240 20 'Exclude prefixes' | Out-Null
    $C.arsenalExclude=Add-Ctl 'TextBox' $g 335 20 120 22
    Add-Label $g 10 48 'Next key' | Out-Null
    $C.arsenalNext=Add-Ctl 'TextBox' $g 70 48 50 22
    Add-Label $g 130 48 'Prev key' | Out-Null
    $C.arsenalPrev=Add-Ctl 'TextBox' $g 190 48 50 22
    Add-Label $g 250 48 '(blank = F7 / F6)' | Out-Null
    Add-Label $g 370 48 'Min ms between swaps' | Out-Null
    $C.arsenalInterval=Add-Ctl 'TextBox' $g 500 48 60 22
    Add-Label $g 10 76 'Arsenal.Gun, one name per line' | Out-Null
    $C.arsenalGuns=Add-Ctl 'TextBox' $g 190 74 400 56;$C.arsenalGuns.Multiline=$true;$C.arsenalGuns.ScrollBars='Vertical'
    $pv=$sp.Panel2
    Add-Label $pv 8 4 'Generated cfg (read-only)' | Out-Null
    $prev=Add-Ctl 'TextBox' $pv 8 26 380 400;$prev.Multiline=$true;$prev.ReadOnly=$true;$prev.ScrollBars='Both';$prev.WordWrap=$false
    $prev.Font=New-Object System.Drawing.Font('Consolas',9);$prev.Anchor='Top,Bottom,Left,Right'
    $prev.Size=New-Object System.Drawing.Size(($pv.Width-16),($pv.Height-80))
    $warn=Add-Ctl 'Label' $pv 8 ($pv.Height-50) ($pv.Width-16) 44;$warn.Anchor='Bottom,Left,Right';$warn.ForeColor=[System.Drawing.Color]::Firebrick
    $script:Preview=$prev;$script:Warn=$warn

    # ---- Mod env tab
    $t=New-Page 'Developer switches'
    $hdr=Add-Ctl 'Label' $t 8 6 900 34 'Tick a variable to export it to the launched process only (never machine-wide). The value box holds what is exported; kill switches default on in the mod, so tick with 0 to disable.'
    $y=44
    foreach($d in $script:EnvDefs){
        $cb=Add-Ctl 'CheckBox' $t 8 $y 330 22 $d.Name;$cb.Font=New-Object System.Drawing.Font('Consolas',9)
        $C["envon:$($d.Name)"]=$cb
        $vb=Add-Ctl 'ComboBox' $t 344 $y 130 22
        foreach($ch in $d.Choices){[void]$vb.Items.Add($ch)}
        $vb.Text=$d.Value;$C["env:$($d.Name)"]=$vb
        $h=Add-Ctl 'Label' $t 484 ($y+3) 520 18 $d.Hint;$h.ForeColor=[System.Drawing.Color]::DimGray
        $y+=26
    }
    Add-Label $t 8 $y 'Extra environment (NAME=VALUE per line, e.g. FVR_BC2_PROBE_* test hooks):' | Out-Null
    $C.extraEnv=Add-Ctl 'TextBox' $t 8 ($y+24) 780 60;$C.extraEnv.Multiline=$true;$C.extraEnv.ScrollBars='Vertical'

    # ---- Build tab
    $t=New-Page 'Build'
    Add-Ctl 'Label' $t 12 10 900 40 'Incremental builds (two jobs, no tests; see AGENTS.md). Output goes to the log pane. One build at a time.' | Out-Null
    $bB1=Add-Ctl 'Button' $t 12 56 260 34 'Native DLL (x86)  Build.ps1 -Architecture x86'
    $bB2=Add-Ctl 'Button' $t 12 100 260 34 'XR host (x64)  Build.ps1 -Architecture x64'
    $bB3=Add-Ctl 'Button' $t 12 144 260 34 'Test range DLL, then stage it'
    Add-Ctl 'Label' $t 290 144 720 36 'Builds build\testrange-x86, backs up the staged BC2TestRange.dll as BC2TestRange-prev.dll, then copies the new one into the stage dir. Refuses while BC2 is running.' | Out-Null

    # ---- Profiles tab
    $t=New-Page 'Profiles'
    Add-Ctl 'Label' $t 12 10 900 22 "Saved to $script:ProfilePath (gitignored). The last used profile loads on start." | Out-Null
    Add-Label $t 12 44 'Profile' | Out-Null
    $pn=Add-Ctl 'ComboBox' $t 70 44 280 22;$script:ProfileBox=$pn
    $bPSave=Add-Ctl 'Button' $t 12 80 100 30 'Save'
    $bPLoad=Add-Ctl 'Button' $t 120 80 100 30 'Load'
    $bPDel=Add-Ctl 'Button' $t 228 80 100 30 'Delete'

    $script:Tabs=$tabs
    $script:Buttons=@{launch=$bLaunch;kill=$bKill;killvr=$bKillVr;b1=$bB1;b2=$bB2;b3=$bB3}

    # ---- wiring
    foreach($k in @($C.Keys)){
        $ctl=$C[$k]
        $onChange={if(-not $script:Loading){Update-Preview}}
        if($ctl -is [System.Windows.Forms.CheckBox] -or $ctl -is [System.Windows.Forms.RadioButton]){$ctl.Add_CheckedChanged($onChange)}
        elseif($ctl -is [System.Windows.Forms.NumericUpDown]){$ctl.Add_ValueChanged($onChange)}
        else{$ctl.Add_TextChanged($onChange)}
    }
    foreach($name in $script:MountPresets.Keys){
        $C["preset:$name"].Add_CheckedChanged({
            if($script:Loading){return}
            foreach($n in $script:MountPresets.Keys){if($script:C["preset:$n"].Checked){$script:C.regNet.Checked=$true}}
        })
    }
    $C.template.Add_SelectedIndexChanged({
        if($script:Loading -or -not $script:C.template.SelectedItem){return}
        $path=$script:TemplatePaths[[string]$script:C.template.SelectedItem]
        try{Set-FormState (Import-RangeCfg $path);Write-Log "Loaded template $path"}catch{Write-Log "Template failed: $_"}
    })
    $bTplRefresh.Add_Click({Update-Templates})
    $bLaunch.Add_Click({Start-Launch})
    $bKill.Add_Click({Stop-Game @('BFBC2Game')})
    $bKillVr.Add_Click({Stop-Game @('BC2XrHost','BC2NativeTrace','BC2Observe')})
    $bLog.Add_Click({Open-Path (Join-Path (Get-FormState).stage 'BC2TestRange.log')})
    $bStage.Add_Click({Open-Path (Get-FormState).stage})
    $bCfg.Add_Click({Open-Path (Join-Path (Get-FormState).stage 'ranges\_launcher.cfg')})
    $bB1.Add_Click({Start-Build 'Native DLL x86' @('-Architecture','x86','-Jobs','2') 'Build.ps1'})
    $bB2.Add_Click({Start-Build 'XR host x64' @('-Architecture','x64','-Jobs','2') 'Build.ps1'})
    $bB3.Add_Click({Start-Build 'Test range DLL' @('-Task','BuildTestRange','-Stage',(Get-FormState).stage) 'DevLauncher.ps1'})
    $bPSave.Add_Click({Save-Profile $script:ProfileBox.Text})
    $bPLoad.Add_Click({Load-Profile $script:ProfileBox.Text})
    $bPDel.Add_Click({Remove-Profile $script:ProfileBox.Text})

    $timer=New-Object System.Windows.Forms.Timer;$timer.Interval=100
    $timer.Add_Tick({Pump-Jobs})
    $timer.Start();$script:Timer=$timer
    $form.Add_FormClosing({
        if(-not $SelfTest){try{Save-PlaytestSettings}catch{Write-Log "Could not save settings: $_"}}
        if($script:Jobs.Count -and (Show-Box "A job is still running. Closing the launcher breaks its output pipe. Close anyway?" 'YesNo') -ne 'Yes'){$_.Cancel=$true}
    })

    $script:Jobs=@{}
    Set-FormState (Get-DefaultState)
    Update-Templates
    Update-Profiles
    if(Test-Path (Join-Path $Root 'package-manifest.json')){
        foreach($tab in @($tabs.TabPages)){if($tab.Text -notin @('Playtest','Test options','Profiles')){$tabs.TabPages.Remove($tab)}}
    }
    return $form
}

# --------------------------------------------------------- form <-> state

function Get-FormState {
    $C=$script:C;$s=Get-DefaultState
    foreach($k in @($s.Keys)){
        if(-not $C.ContainsKey($k)){continue}
        $ctl=$C[$k]
        if($ctl -is [System.Windows.Forms.CheckBox]){$s[$k]=[bool]$ctl.Checked}
        elseif($ctl -is [System.Windows.Forms.NumericUpDown]){$s[$k]=[int]$ctl.Value}
        else{$s[$k]=[string]$ctl.Text}
    }
    $s.mode=if($C.modeTR.Checked){'TestRange'}elseif($C.modeFlat.Checked){'Flat'}else{'VR'}
    $s.presets=@($script:MountPresets.Keys | Where-Object {$C["preset:$_"].Checked})
    $s.slots=@(0..8 | ForEach-Object {$C["slot$_"].Text.Trim()})
    $env=@{}
    foreach($d in $script:EnvDefs){if($C["envon:$($d.Name)"].Checked){$env[$d.Name]=$C["env:$($d.Name)"].Text.Trim()}}
    $s.env=$env
    if($script:Playtest){$s.playtest=Get-PlaytestState}
    return $s
}

function Set-FormState($s){
    $C=$script:C;$script:Loading=$true
    try{
        foreach($k in @($s.Keys)){
            if(-not $C.ContainsKey($k)){continue}
            $ctl=$C[$k];$v=$s[$k]
            if($ctl -is [System.Windows.Forms.CheckBox]){$ctl.Checked=[bool]$v}
            elseif($ctl -is [System.Windows.Forms.NumericUpDown]){$ctl.Value=[Math]::Max($ctl.Minimum,[Math]::Min($ctl.Maximum,[decimal]$v))}
            else{$ctl.Text=[string]$v}
        }
        if($s.ContainsKey('mode')){$C.modeVR.Checked=($s.mode -eq 'VR');$C.modeFlat.Checked=($s.mode -eq 'Flat');$C.modeTR.Checked=($s.mode -eq 'TestRange')}
        if($s.ContainsKey('presets')){foreach($n in $script:MountPresets.Keys){$C["preset:$n"].Checked=(@($s.presets) -contains $n)}}
        if($s.ContainsKey('slots')){$sl=@($s.slots);for($i=0;$i -lt 9;$i++){$C["slot$i"].Text=$(if($i -lt $sl.Count){[string]$sl[$i]}else{''})}}
        if($s.ContainsKey('env')){
            foreach($d in $script:EnvDefs){
                $on=$s.env.ContainsKey($d.Name);$C["envon:$($d.Name)"].Checked=$on
                $C["env:$($d.Name)"].Text=$(if($on){[string]$s.env[$d.Name]}else{$d.Value})
            }
        }
    }finally{$script:Loading=$false}
    if($s.ContainsKey('playtest')){Set-PlaytestState $s.playtest}
    Update-Preview
}

function Update-Preview {
    $s=Get-FormState
    $text=New-RangeCfg $s
    $script:Preview.Text=$text
    $err=Test-RangeCfg $text
    $script:Warn.Text=$(if($err.Count){($err -join "`r`n")}else{''})
    try{$script:CmdBox.Text=(Get-LaunchPlan $s).CommandLine}catch{$script:CmdBox.Text="(cannot plan: $_)"}
}

function Update-Templates {
    $script:TemplatePaths=[ordered]@{}
    $add={param($label,$dir,$filter)
        if($dir -and (Test-Path $dir)){Get-ChildItem -LiteralPath $dir -Filter $filter | Where-Object {$_.Name -ne '_launcher.cfg'} | ForEach-Object {$script:TemplatePaths["$label $($_.Name)"]=$_.FullName}}}
    & $add 'repo:' (Join-Path $Root 'tools\testrange\ranges') '*.cfg'
    & $add 'repo:' (Join-Path $Root 'tools\testrange') '*.example.cfg'
    & $add 'stage:' (Join-Path $script:C.stage.Text 'ranges') '*.cfg'
    $script:Loading=$true
    try{
        $script:C.template.Items.Clear()
        foreach($k in $script:TemplatePaths.Keys){[void]$script:C.template.Items.Add($k)}
    }finally{$script:Loading=$false}
}

# ------------------------------------------------------------------ profiles

function Read-ProfileStore {
    if(Test-Path -LiteralPath $script:ProfilePath){
        try{$h=ConvertTo-Hashtable (Get-Content -LiteralPath $script:ProfilePath -Raw | ConvertFrom-Json);if($h.profiles){return $h}}catch{Write-Log "Profile file unreadable: $_"}
    }
    return @{last='';profiles=@{}}
}
function Write-ProfileStore($store){
    New-Item -ItemType Directory -Path (Split-Path $script:ProfilePath) -Force | Out-Null
    [IO.File]::WriteAllText($script:ProfilePath,($store | ConvertTo-Json -Depth 8),(New-Object System.Text.UTF8Encoding($false)))
}
function Update-Profiles {
    $store=Read-ProfileStore
    $script:ProfileBox.Items.Clear()
    foreach($n in ($store.profiles.Keys | Sort-Object)){[void]$script:ProfileBox.Items.Add($n)}
    $script:ProfileBox.Text=[string]$store.last
}
function Save-Profile([string]$Name){
    $Name=$Name.Trim();if(-not $Name){Show-Box 'Type a profile name first.' | Out-Null;return}
    $store=Read-ProfileStore;$store.profiles[$Name]=Get-FormState;$store.last=$Name
    Write-ProfileStore $store;Update-Profiles;Write-Log "Saved profile '$Name'."
}
function Load-Profile([string]$Name){
    $Name=$Name.Trim();$store=Read-ProfileStore
    if(-not $store.profiles.ContainsKey($Name)){Write-Log "No profile named '$Name'.";return}
    $s=Get-DefaultState;$p=$store.profiles[$Name]
    foreach($k in $p.Keys){$s[$k]=$p[$k]}
    if($s.env -isnot [hashtable]){$s.env=@{}}
    Set-FormState $s;$store.last=$Name;Write-ProfileStore $store;Update-Profiles;Write-Log "Loaded profile '$Name'."
}
function Remove-Profile([string]$Name){
    $Name=$Name.Trim();$store=Read-ProfileStore
    if(-not $store.profiles.ContainsKey($Name)){return}
    if((Show-Box "Delete profile '$Name'?" 'YesNo') -ne 'Yes'){return}
    $store.profiles.Remove($Name);if($store.last -eq $Name){$store.last=''}
    Write-ProfileStore $store;Update-Profiles;Write-Log "Deleted profile '$Name'."
}

# ----------------------------------------------------- jobs, launch, build

function Write-Log([string]$Text){
    if($script:Log){
        if($script:Log.TextLength -gt 400000){$script:Log.Clear()}
        $script:Log.AppendText("$Text`r`n")
    }else{Write-Host $Text}
}
function Show-Box([string]$Text,[string]$Buttons='OK'){
    [System.Windows.Forms.MessageBox]::Show($Text,'BC2 VR Dev Launcher',$Buttons) | ForEach-Object {"$_"}
}
function Open-Path([string]$Path){
    if(Test-Path -LiteralPath $Path){Start-Process -FilePath $(if((Get-Item -LiteralPath $Path).PSIsContainer){'explorer.exe'}else{'notepad.exe'}) -ArgumentList (Quote-Arg $Path)}
    else{Write-Log "Not found: $Path"}
}
function Stop-Game([string[]]$Names){
    $procs=@(Get-Process -Name $Names -ErrorAction SilentlyContinue)
    if(-not $procs.Count){Write-Log "Not running: $($Names -join ', ')";return}
    foreach($p in $procs){try{Stop-Process -Id $p.Id -Force;Write-Log "Stopped $($p.ProcessName) ($($p.Id))."}catch{Write-Log "Could not stop $($p.ProcessName): $_"}}
}

function Start-Job2([string]$Tag,[string]$File,[string[]]$ArgList,$EnvMap,[string]$Lease=''){
    if($Lease){
        $jobDir=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'BC2VR/jobs'
        [void][IO.Directory]::CreateDirectory($jobDir)
        $jobFile=Join-Path $jobDir (([guid]::NewGuid().ToString())+'.json')
        [IO.File]::WriteAllText($jobFile,(@{file=$File;arguments=$ArgList;lease=$Lease}|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
        $File=$script:HostExe;$ArgList=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $Root 'tools/Start-BC2LauncherTask.ps1'),'-JobFile',$jobFile)
    }
    $psi=New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName=$File;$psi.Arguments=(($ArgList | ForEach-Object {Quote-Arg $_}) -join ' ');$psi.WorkingDirectory=$Root
    foreach($k in $EnvMap.Keys){$psi.EnvironmentVariables[$k]=[string]$EnvMap[$k]}
    $job=New-Object LauncherJob $Tag
    Write-Log "[$Tag] > $File $($psi.Arguments)"
    foreach($k in $EnvMap.Keys){Write-Log "[$Tag]   env $k=$($EnvMap[$k])"}
    $job.Start($psi)
    $script:Jobs[$Tag]=$job
    Set-ButtonState
}
function Set-ButtonState {
    $script:Buttons.launch.Enabled=-not $script:Jobs.ContainsKey('launch')
    foreach($button in $script:PlaytestActions){$button.Enabled=-not $script:Jobs.ContainsKey('launch')}
    foreach($b in 'b1','b2','b3'){$script:Buttons[$b].Enabled=-not $script:Jobs.ContainsKey('build')}
    $script:Status.Text=$(if($script:Jobs.Count){"Running: $($script:Jobs.Keys -join ', ')"}else{'Ready.'})
}
function Pump-Jobs {
    foreach($tag in @($script:Jobs.Keys)){
        $job=$script:Jobs[$tag];$sb=New-Object System.Text.StringBuilder;$line=$null;$n=0
        while($n -lt 500 -and $job.Lines.TryDequeue([ref]$line)){[void]$sb.Append("[$tag] $line`r`n");$n++}
        if($sb.Length){$script:Log.AppendText($sb.ToString())}
        if($job.Finished -and $job.Lines.IsEmpty){$script:Jobs.Remove($tag);Set-ButtonState}
    }
}

function Confirm-NoGame([string]$Mode){
    $names=@('BFBC2Game');if($Mode -ne 'TestRange'){$names+=@('BC2XrHost','BC2NativeTrace')}
    $procs=@(Get-Process -Name $names -ErrorAction SilentlyContinue)
    if(-not $procs.Count){return $true}
    $list=($procs | ForEach-Object {"$($_.ProcessName) ($($_.Id))"}) -join ', '
    if((Show-Box "Already running: $list`r`n`r`nKill it and continue?" 'YesNo') -ne 'Yes'){return $false}
    foreach($p in $procs){Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue}
    Start-Sleep -Milliseconds 1200
    return -not (Get-Process -Name $names -ErrorAction SilentlyContinue)
}

function Start-Launch {
    $s=Get-FormState
    try{
        if(-not $s.gamePath -or -not (Test-Path (Join-Path $s.gamePath 'BFBC2Game.exe'))){throw "BFBC2Game.exe not found in game path '$($s.gamePath)'."}
        $full=[IO.Path]::GetFullPath($s.stage).TrimEnd('\')
        $gameFull=[IO.Path]::GetFullPath($s.gamePath).TrimEnd('\')
        if($full -eq $gameFull -or $full.StartsWith($gameFull+'\')){throw 'The stage dir must not be inside the game folder.'}
        $plan=Get-LaunchPlan $s
        if($s.useCfg){
            $err=Test-RangeCfg $plan.CfgText
            if($err.Count){throw "The cfg has keys the DLL would refuse:`r`n$($err -join "`r`n")"}
        }
        if($s.useCfg){
            New-Item -ItemType Directory -Path (Split-Path $plan.CfgPath) -Force | Out-Null
            [IO.File]::WriteAllText($plan.CfgPath,$plan.CfgText,(New-Object System.Text.UTF8Encoding($false)))   # no BOM: the DLL reads the first key raw
            Write-Log "Wrote $($plan.CfgPath)"
        }
        Start-Job2 'launch' $plan.File $plan.Args $plan.Env 'game'
    }catch{Write-Log "Launch refused: $_";Show-Box "Launch refused:`r`n$_" | Out-Null}
}
function Start-Build([string]$Name,[string[]]$ScriptArgs,[string]$ScriptName){
    if($ScriptName -eq 'DevLauncher.ps1' -and (Get-Process BFBC2Game -ErrorAction SilentlyContinue)){
        Write-Log 'Close BC2 before building the test range DLL (it holds the staged copy).';return}
    $a=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $Root $ScriptName))+$ScriptArgs
    Start-Job2 'build' $script:HostExe $a @{} 'build'
}

# ------------------------------------------------------------------ self test

function Invoke-SelfTest {
    if(Test-Path (Join-Path $Root 'package-manifest.json')){
        if((Get-PlaytestState).package -ne $Root){throw 'Portable launcher did not select its own folder.'}
        [void](Get-PlaytestArguments (Get-PlaytestState) 'Verify')
        'Portable launcher form ready.';return
    }
    $bad=0
    $show={param($title,$s)
        $text=New-RangeCfg $s;$err=Test-RangeCfg $text
        "=== $title ==="
        $text.TrimEnd()
        if($err.Count){$script:bad++;"!! REJECTED: $($err -join '; ')"}else{"-- all keys accepted by the DLL parser mirror"}
    }
    $default=Get-FormState
    & $show 'default profile' $default
    $tpl=Join-Path $Root 'tools\testrange\ranges\sp02-m9-pistol.cfg'
    Set-FormState (Import-RangeCfg $tpl)
    $m9=Get-FormState
    & $show 'template sp02-m9-pistol.cfg' $m9
    "-- presets recognised: $($m9.presets -join ', '); extra mounts: '$($m9.extraMounts)'; slots: $($m9.slots -join ',')"
    # every repo template must round-trip to a cfg the DLL accepts
    foreach($f in Get-ChildItem (Join-Path $Root 'tools\testrange\ranges\*.cfg')){
        Set-FormState (Import-RangeCfg $f.FullName);$e=Test-RangeCfg (New-RangeCfg (Get-FormState))
        "-- roundtrip $($f.Name): $(if($e.Count){'REJECTED '+($e -join '; ')}else{'ok'})";if($e.Count){$bad++}
    }
    Set-FormState (Get-DefaultState)
    foreach($m in 'VR','Flat','TestRange'){
        $s=Get-DefaultState;$s.mode=$m;$s.env=@{FVR_BC2_TEST_INPUT_LOG='1'}
        $p=Get-LaunchPlan $s
        "-- plan $m : $($p.CommandLine)"
        "   env: $(($p.Env.Keys | ForEach-Object {"$_=$($p.Env[$_])"}) -join ' ')"
    }
    "-- weapons in catalog: $($script:GiveNames.Count); env defs: $($script:EnvDefs.Count); templates: $($script:TemplatePaths.Count)"
    if($bad){"SELFTEST FAILED ($bad)";exit 1}else{'SELFTEST OK'}
}

# ---------------------------------------------------------------------- main

if($Task -eq 'BuildTestRange'){
    if(-not $Stage){$Stage=Get-DefaultStage}
    Invoke-BuildTestRange $Stage
    return
}
if([Threading.Thread]::CurrentThread.GetApartmentState() -ne 'STA'){
    # WinForms autocomplete needs an STA thread (Windows PowerShell 5.1 defaults to MTA).
    $again=@('-NoProfile','-ExecutionPolicy','Bypass','-STA','-File',$PSCommandPath)
    if($SelfTest){$again+='-SelfTest'}
    if($PackageRoot){$again+=@('-PackageRoot',$PackageRoot)}
    & $script:HostExe @again
    exit $LASTEXITCODE
}
$form=New-LauncherForm
if($SelfTest){Invoke-SelfTest;return}
$store=Read-ProfileStore
if($store.last -and $store.profiles.ContainsKey($store.last)){Load-Profile $store.last}
Write-Log "Repo: $Root"
[System.Windows.Forms.Application]::Run($form)
