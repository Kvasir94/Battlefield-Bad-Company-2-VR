. (Join-Path $PSScriptRoot 'BC2PlaytestOptions.ps1')
# Simple front door to the verified package launcher. Developer tools remain separate.
function Get-PlaytestArguments($State,[string]$Action='Play') {
    if($State.runtime -notin @('Auto','VDXR','SteamVR') -or $State.weapon -notin @((Get-PlaytestWeaponRanges).Keys) -or
       $State.hands -notin @('ik','floating') -or $State.graphics -notin @('HighCompatible','Preserve')){throw 'Invalid playtest setting.'}
    $entry=Join-Path $State.package 'Start-BC2-Playtest.ps1'
    if(-not (Test-Path -LiteralPath $entry -PathType Leaf) -and -not (Test-Path -LiteralPath (Join-Path $State.package 'package-manifest.json'))){throw 'Choose a prepared playtest package folder.'}
    $a=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $Root 'tools/Start-BC2Playtest.ps1'),'-PackageRoot',$State.package,'-Runtime',$State.runtime,'-Weapon',$State.weapon,'-HandsMode',$State.hands,'-GraphicsProfile',$State.graphics)
    if($State.game){$a+=@('-GamePath',$State.game)}
    if($State.runtimePath){$a+=@('-RuntimePath',$State.runtimePath)}
    if($State.optionsPath){$a+=@('-OptionsPath',$State.optionsPath)}
    if($State.custom){
        if($State.width -lt 128 -or $State.width -gt 8192 -or $State.height -lt 128 -or $State.height -gt 8192){throw 'Custom eye dimensions must be 128-8192.'}
        $a+=@('-RenderWidth',[string]$State.width,'-RenderHeight',[string]$State.height)
    }
    if(-not $State.depth){$a+='-NoDepthSubmit'}
    if($State.single){$a+='-SingleFramePipeline'}
    if($null -ne $State.refresh){if([int]$State.refresh -lt 0 -or [int]$State.refresh -gt 240){throw 'Headset refresh must be 0-240 Hz.'};$a+=@('-RefreshHz',[string][int]$State.refresh)}
    if($State.pace -eq $false){$a+='-NoPaceToContent'}
    switch($Action){'Verify'{$a+='-VerifyOnly'} 'Headset'{$a+='-Probe'} 'Play'{} default{throw 'Invalid action.'}}
    return $a
}
function Get-PlaytestState {
    $p=$script:Playtest
    return @{game=$p.game.Text.Trim();runtimePath=$p.runtimePath.Text.Trim();package=$p.package.Text.Trim();runtime=[string]$p.runtime.SelectedItem;weapon=[string]$p.weapon.SelectedItem;
        hands=$(if($p.hands.SelectedIndex -eq 0){'ik'}else{'floating'});
        graphics=$(if($p.graphics.SelectedIndex -eq 0){'HighCompatible'}else{'Preserve'});
        custom=$p.custom.Checked;width=[int]$p.width.Value;height=[int]$p.height.Value;depth=$p.depth.Checked;single=$p.single.Checked;
        refresh=[int]$p.refresh.Value;pace=$p.pace.Checked;options=(Get-PlaytestGridOptions)}
}
function Save-PlaytestSettings {
    $state=Get-PlaytestState
    $dir=Split-Path $script:PlaytestSettings
    [void][IO.Directory]::CreateDirectory($dir)
    [IO.File]::WriteAllText($script:PlaytestSettings,($state|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
}
function Start-PlaytestAction([string]$Action){
    try {
        if($script:Jobs.ContainsKey('launch')){return}
        $state=Get-PlaytestState
        $options=Convert-PlaytestOptions $state.package $state.options
        $optionDir=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'BC2VR/launches'
        [void][IO.Directory]::CreateDirectory($optionDir)
        $state.optionsPath=Join-Path $optionDir (([guid]::NewGuid().ToString())+'.json')
        [IO.File]::WriteAllText($state.optionsPath,($options|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
        $arguments=Get-PlaytestArguments $state $Action
        Save-PlaytestSettings
        Start-Job2 'launch' $script:HostExe $arguments @{}
    }catch{Write-Log "Playtest: $_";Show-Box "$_"|Out-Null}
}
function Add-PlaytestPanel($Tabs){
    $page=[Windows.Forms.TabPage]::new();$page.Text='Playtest';$page.AutoScroll=$true;$Tabs.TabPages.Add($page)
    $p=@{};$script:Playtest=$p
    $script:PlaytestSettings=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'BC2VR\playtest-settings.json'
    [void](Add-Ctl 'Label' $page 20 18 940 28 'Bad Company 2 VR - ready to play')
    [void](Add-Label $page 20 55 'Build folder' 130)
    $p.package=Add-Ctl 'TextBox' $page 160 55 650 24
    $browse=Add-Ctl 'Button' $page 825 53 110 28 'Browse...'
    $browse.Add_Click({
        $dialog=[Windows.Forms.FolderBrowserDialog]::new();$dialog.Description='Choose the prepared BC2 VR package';$dialog.SelectedPath=$script:Playtest.package.Text
        if($dialog.ShowDialog() -eq 'OK'){$script:Playtest.package.Text=$dialog.SelectedPath;Update-PlaytestOptions;$script:OptionPackage=$dialog.SelectedPath};$dialog.Dispose()
    })
    $y=100
    foreach($row in @(@('runtime','VR runtime',@('Auto','VDXR','SteamVR')),@('weapon','Test range',@((Get-PlaytestWeaponRanges).Keys)),
        @('hands','Hands',@('Native skinned hands (recommended)','Legacy floating hands')),
        @('graphics','Graphics',@('High compatible (recommended)','Keep game settings')))){
        [void](Add-Label $page 20 $y $row[1] 130)
        $ctl=Add-Ctl 'ComboBox' $page 160 $y 350 26;$ctl.DropDownStyle='DropDownList';$ctl.Items.AddRange([object[]]$row[2]);$ctl.SelectedIndex=0;$p[$row[0]]=$ctl;$y+=40
    }
    $p.custom=Add-Ctl 'CheckBox' $page 20 270 370 25 'Custom resolution per eye (otherwise automatic)'
    $p.width=Add-Ctl 'NumericUpDown' $page 410 270 100 25;$p.width.Minimum=128;$p.width.Maximum=8192;$p.width.Value=1920
    [void](Add-Label $page 520 270 'x')
    $p.height=Add-Ctl 'NumericUpDown' $page 545 270 100 25;$p.height.Minimum=128;$p.height.Maximum=8192;$p.height.Value=1080
    $p.custom.Add_CheckedChanged({$script:Playtest.width.Enabled=$script:Playtest.custom.Checked;$script:Playtest.height.Enabled=$script:Playtest.custom.Checked})
    $p.depth=Add-Ctl 'CheckBox' $page 20 310 390 25 'Submit depth to the headset runtime (recommended)';$p.depth.Checked=$true
    $p.single=Add-Ctl 'CheckBox' $page 20 342 500 25 'Single-frame pipeline (troubleshooting; normal is four frames)'
    # Headset delivery (docs/HEADSET-FRAME-DELIVERY-20261011.md): refresh request and pace-to-content.
    [void](Add-Label $page 20 376 'Headset refresh (Hz)' 150)
    $p.refresh=Add-Ctl 'NumericUpDown' $page 175 374 70 25;$p.refresh.Minimum=0;$p.refresh.Maximum=240;$p.refresh.Value=72
    [void](Add-Label $page 255 376 '0 = keep the runtime setting; SteamVR: set it in SteamVR Video settings' 520)
    $p.pace=Add-Ctl 'CheckBox' $page 20 406 620 25 'Pace headset to game frames (lets runtime motion smoothing fill in; recommended)';$p.pace.Checked=$true
    $save=Add-Ctl 'Button' $page 20 446 130 36 'Save settings'
    $verify=Add-Ctl 'Button' $page 165 446 130 36 'Verify build'
    $headset=Add-Ctl 'Button' $page 310 446 145 36 'Check headset'
    $play=Add-Ctl 'Button' $page 470 446 145 36 'Play in VR'
    $save.Add_Click({try{Save-PlaytestSettings;Write-Log 'Playtest settings saved.'}catch{Show-Box "$_"|Out-Null}})
    $verify.Add_Click({Start-PlaytestAction 'Verify'});$headset.Add_Click({Start-PlaytestAction 'Headset'});$play.Add_Click({Start-PlaytestAction 'Play'})
    $script:PlaytestActions=@($verify,$headset,$play)
    [void](Add-Ctl 'Label' $page 20 498 940 42 'Connect your headset and controllers, then press Play in VR. Settings are remembered. Build verification and headset checks appear below. Use Test options for additional settings.')
    # Add the two installation choices without a separate setup wizard.
    foreach($control in $page.Controls){if($control.Top -ge 100){$control.Top+=70}}
    [void](Add-Label $page 20 100 'Game folder' 130)
    $p.game=Add-Ctl 'TextBox' $page 160 100 650 24
    $gameBrowse=Add-Ctl 'Button' $page 825 98 110 28 'Browse...'
    $gameBrowse.Add_Click({$dialog=[Windows.Forms.OpenFileDialog]::new();$dialog.Title='Select BFBC2Game.exe';$dialog.Filter='Bad Company 2|BFBC2Game.exe';if($dialog.ShowDialog() -eq 'OK'){$script:Playtest.game.Text=Split-Path $dialog.FileName};$dialog.Dispose()})
    [void](Add-Label $page 20 135 'Runtime JSON' 130)
    $p.runtimePath=Add-Ctl 'TextBox' $page 160 135 650 24
    $runtimeBrowse=Add-Ctl 'Button' $page 825 133 110 28 'Optional...'
    $runtimeBrowse.Add_Click({$dialog=[Windows.Forms.OpenFileDialog]::new();$dialog.Title='Optional OpenXR runtime override';$dialog.Filter='Runtime manifest|*.json';if($dialog.ShowDialog() -eq 'OK'){$script:Playtest.runtimePath.Text=$dialog.FileName};$dialog.Dispose()})
    $p.package.Text=$Root
    if(Test-Path -LiteralPath $script:PlaytestSettings){
        try{
            $s=Get-Content -LiteralPath $script:PlaytestSettings -Raw|ConvertFrom-Json
            if($s.game){$p.game.Text=$s.game};if($s.runtimePath){$p.runtimePath.Text=$s.runtimePath}
            if($s.game){$p.game.Text=$s.game};$p.runtimePath.Text=[string]$s.runtimePath
    if($s.package){$p.package.Text=$s.package}
    if(Test-Path (Join-Path $Root 'package-manifest.json')){$p.package.Text=$Root}
            foreach($key in 'runtime','weapon'){if($p[$key].Items.Contains([string]$s.$key)){$p[$key].SelectedItem=[string]$s.$key}}
            $p.hands.SelectedIndex=$(if($s.hands -eq 'floating'){1}else{0});$p.graphics.SelectedIndex=$(if($s.graphics -eq 'Preserve'){1}else{0})
            foreach($key in 'custom','depth','single','pace'){if($null -ne $s.$key){$p[$key].Checked=[bool]$s.$key}}
            foreach($key in 'width','height'){if($s.$key -ge 128 -and $s.$key -le 8192){$p[$key].Value=$s.$key}}
            if($null -ne $s.refresh -and [int]$s.refresh -ge 0 -and [int]$s.refresh -le 240){$p.refresh.Value=[int]$s.refresh}
        }catch{Write-Log "Could not load playtest settings: $_"}
    }
    if(Test-Path (Join-Path $Root 'package-manifest.json')){$p.package.Text=$Root}
    if(-not $p.game.Text -and (Test-Path (Join-Path $p.package.Text 'config/local.json'))){$p.game.Text=(Get-Content (Join-Path $p.package.Text 'config/local.json') -Raw|ConvertFrom-Json).game_path}
    if($PackageRoot){$p.package.Text=$PackageRoot}
    Add-PlaytestOptionsTab $Tabs
    if($s -and $s.options){foreach($row in $script:OptionGrid.Rows){$name=[string]$row.Tag;if($s.options.PSObject.Properties.Name -contains $name){$row.Cells[0].Value=$true;$row.Cells[2].Value=[string]$s.options.$name}}}
    $p.width.Enabled=$p.custom.Checked;$p.height.Enabled=$p.custom.Checked
}

function Get-PlaytestGridOptions {
    $result=@{}
    if($script:OptionGrid){
        [void]$script:OptionGrid.EndEdit()
        foreach($row in $script:OptionGrid.Rows){if($row.Cells[0].Value){$result[[string]$row.Tag]=$row.Cells[2].Value}}
    }
    return $result
}
function Update-PlaytestOptions {
    $grid=$script:OptionGrid;$grid.Rows.Clear()
    try {
        foreach($d in @(Get-PlaytestOptionDefinitions $script:Playtest.package.Text)){
            $i=$grid.Rows.Add($false,$d.Label,[string]$d.Default,'');$row=$grid.Rows[$i];$row.Tag=$d.Name
            $choices=if($d.Type -eq [Management.Automation.SwitchParameter]){@('True','False')}else{$d.Choices}
            if($choices.Count){$cell=[Windows.Forms.DataGridViewComboBoxCell]::new();$cell.Items.AddRange([object[]]$choices);$row.Cells[2]=$cell;$cell.Value=[string]$d.Default}
            $hint='Default: '+[string]$d.Default
            if($null -ne $d.Minimum){$hint+=' | Range: '+$d.Minimum+' to '+$d.Maximum}
            $row.Cells[3].Value=$hint
        }
    }catch{Write-Log "Test options: $_"}
}
function Add-PlaytestOptionsTab($Tabs){
    $page=[Windows.Forms.TabPage]::new();$page.Text='Test options';$Tabs.TabPages.Add($page)
    $label=Add-Ctl 'Label' $page 16 15 960 42 'Optional controls, reload, rendering and diagnostic settings for Play in VR. Check Override to change a value; unchecked rows use build defaults. Save settings on the Playtest tab.'
    $grid=Add-Ctl 'DataGridView' $page 16 62 985 415;$grid.Anchor='Top,Bottom,Left,Right';$grid.AllowUserToAddRows=$false;$grid.AllowUserToDeleteRows=$false;$grid.RowHeadersVisible=$false;$grid.AutoSizeColumnsMode='Fill'
    $enabled=[Windows.Forms.DataGridViewCheckBoxColumn]::new();$enabled.HeaderText='Override';$enabled.FillWeight=13;[void]$grid.Columns.Add($enabled)
    foreach($title in 'Setting','Value','Default / range'){$col=[Windows.Forms.DataGridViewTextBoxColumn]::new();$col.HeaderText=$title;$col.ReadOnly=$title -ne 'Value';[void]$grid.Columns.Add($col)}
    $grid.Add_CurrentCellDirtyStateChanged({if($script:OptionGrid.IsCurrentCellDirty){[void]$script:OptionGrid.CommitEdit([Windows.Forms.DataGridViewDataErrorContexts]::Commit)}})
    $script:OptionGrid=$grid
    Update-PlaytestOptions
    $script:Playtest.package.Add_Leave({if($script:OptionPackage -ne $script:Playtest.package.Text){Update-PlaytestOptions;$script:OptionPackage=$script:Playtest.package.Text}})
    $script:OptionPackage=$script:Playtest.package.Text
}

function Set-PlaytestState($s){
    $p=$script:Playtest
    if($s.game){$p.game.Text=$s.game};$p.runtimePath.Text=[string]$s.runtimePath
    if($s.package){$p.package.Text=$s.package}
    if(Test-Path (Join-Path $Root 'package-manifest.json')){$p.package.Text=$Root}
    foreach($key in 'runtime','weapon'){if($p[$key].Items.Contains([string]$s.$key)){$p[$key].SelectedItem=[string]$s.$key}}
    $p.hands.SelectedIndex=$(if($s.hands -eq 'floating'){1}else{0});$p.graphics.SelectedIndex=$(if($s.graphics -eq 'Preserve'){1}else{0})
    foreach($key in 'custom','depth','single','pace'){if($null -ne $s.$key){$p[$key].Checked=[bool]$s.$key}}
    foreach($key in 'width','height'){if($s.$key -ge 128 -and $s.$key -le 8192){$p[$key].Value=$s.$key}}
    if($null -ne $s.refresh -and [int]$s.refresh -ge 0 -and [int]$s.refresh -le 240){$p.refresh.Value=[int]$s.refresh}
    Update-PlaytestOptions;$script:OptionPackage=$p.package.Text
    if($s.options){
        $values=@{};if($s.options -is [Collections.IDictionary]){$values=$s.options}else{foreach($prop in $s.options.PSObject.Properties){$values[$prop.Name]=$prop.Value}}
        foreach($row in $script:OptionGrid.Rows){$name=[string]$row.Tag;if($values.ContainsKey($name)){$row.Cells[0].Value=$true;$row.Cells[2].Value=[string]$values[$name]}}
    }
}
