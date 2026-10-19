param([Parameter(Mandatory=$true)][string]$PackageRoot,[string]$SnapshotPath,[switch]$VerifyBuild,[string]$LauncherRoot=(Split-Path $PSScriptRoot))
$ErrorActionPreference='Stop'
. (Join-Path $LauncherRoot 'DevLauncher.ps1') -SelfTest -PackageRoot $PackageRoot | Out-Null
try {
    $s=Get-PlaytestState
    $s.runtime='VDXR';$s.weapon='XM8';$s.hands='ik';$s.graphics='HighCompatible';$s.custom=$false;$s.depth=$true;$s.single=$false
    $a=@(Get-PlaytestArguments $s 'Play')
    if($a -contains '-Probe' -or $a -contains '-VerifyOnly' -or $a -contains '-RenderWidth'){throw 'Default Play arguments are incorrect.'}
    if($a[3] -ne '-File' -or $a[4] -ne (Join-Path $Root 'tools/Start-BC2Playtest.ps1') -or $a[6] -ne $PackageRoot){throw 'Wrong package entrypoint.'}
    foreach($action in 'Verify','Headset'){
        $args2=@(Get-PlaytestArguments $s $action)
        $flag=if($action -eq 'Verify'){'-VerifyOnly'}else{'-Probe'}
        if($args2 -notcontains $flag){throw "Missing action flag: $action"}
    }
    foreach($weapon in 'XM8 + SPAS','SPAS-12'){
        $s.weapon=$weapon
        if(@(Get-PlaytestArguments $s 'Play') -notcontains $weapon){throw "Missing reload playtest loadout: $weapon"}
    }
    $s.runtime='SteamVR';$s.weapon='M9';$s.hands='floating';$s.graphics='Preserve';$s.custom=$true;$s.width=2048;$s.height=2304;$s.depth=$false;$s.single=$true
    $a=@(Get-PlaytestArguments $s)
    foreach($value in 'SteamVR','M9','floating','Preserve','2048','2304','-NoDepthSubmit','-SingleFramePipeline'){if($a -notcontains $value){throw "Missing setting: $value"}}
    $s.width=0;$rejected=$false
    try{Get-PlaytestArguments $s|Out-Null}catch{$rejected=$true}
    if(-not $rejected){throw 'Bad resolution accepted.'}
    $converted=Convert-PlaytestOptions $PackageRoot @{TurnMode='snap';SmoothTurnDegPerSec='120';NoDroppedMags='True';InteractZoneScale='1.25'}
    if($converted.TurnMode -ne 'snap' -or $converted.SmoothTurnDegPerSec -ne 120 -or -not $converted.NoDroppedMags -or $converted.InteractZoneScale -ne 1.25){throw 'Option conversion failed.'}
    foreach($bad in @(@{Stage='elsewhere'},@{SmoothTurnDegPerSec='1'},@{TurnMode='wrong'},@{NoDroppedMags='yes'})){
        $rejected=$false;try{Convert-PlaytestOptions $PackageRoot $bad|Out-Null}catch{$rejected=$true}
        if(-not $rejected){throw 'Invalid advanced option accepted.'}
    }
    $s=Get-PlaytestState;$s.options=@{TurnMode='snap';NoDroppedMags=$true}
    Set-PlaytestState $s
    $roundtrip=Get-PlaytestState
    if($roundtrip.options.TurnMode -ne 'snap' -or $roundtrip.options.NoDroppedMags -ne 'True'){throw 'Profile options did not roundtrip.'}
    $saved=Get-FormState;Set-FormState $saved
    if((Get-PlaytestState).options.TurnMode -ne 'snap'){throw 'Named profile lost playtest options.'}
    $s.options=@{};Set-PlaytestState $s
    $script:Form.Size=[Drawing.Size]::new(1080,900)
    $script:Form.ShowInTaskbar=$false;$script:Form.StartPosition='Manual';$script:Form.Location=[Drawing.Point]::new(-3000,-3000)
    $script:Form.Show();[Windows.Forms.Application]::DoEvents();$script:Form.PerformLayout()
    if($SnapshotPath){
        $bmp=[Drawing.Bitmap]::new($script:Form.Width,$script:Form.Height)
        $script:Form.DrawToBitmap($bmp,[Drawing.Rectangle]::new(0,0,$bmp.Width,$bmp.Height))
        $bmp.Save($SnapshotPath,[Drawing.Imaging.ImageFormat]::Png);$bmp.Dispose()
    }
    if($VerifyBuild){
        Start-PlaytestAction 'Verify'
        $job=$script:Jobs['launch'];if(-not $job){throw 'GUI did not start the verification worker.'}
        $deadline=[DateTime]::UtcNow.AddSeconds(25)
        while($script:Jobs.Count -and [DateTime]::UtcNow -lt $deadline){Pump-Jobs;Start-Sleep -Milliseconds 50}
        if($script:Jobs.Count -or $job.ExitCode -ne 0 -or $script:Log.Text -notmatch 'package_verified'){throw 'GUI verification worker failed.'}
    }
    'Playtest GUI checks passed; no game or headset session started.'
} finally {$script:Timer.Stop();$script:Timer.Dispose();$script:Form.Dispose()}
