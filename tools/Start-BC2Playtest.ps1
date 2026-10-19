[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$PackageRoot,
 [string]$OptionsPath,
 [string]$GamePath,
 [string]$RuntimePath,
 [ValidateSet('Auto','VDXR','SteamVR')][string]$Runtime='Auto',
 [string]$Weapon='XM8',
 [ValidateSet('ik','floating')][string]$HandsMode=$(if($env:FVR_HANDS_MODE -in @('ik','floating')){$env:FVR_HANDS_MODE}else{'ik'}),
 [int]$RenderWidth=0,[int]$RenderHeight=0,
 [ValidateSet('Preserve','HighCompatible')][string]$GraphicsProfile='HighCompatible',
 [switch]$VerifyOnly,[switch]$Probe,[switch]$NoDepthSubmit,[switch]$SingleFramePipeline,
 # Headset refresh request (XR_FB_display_refresh_rate runtimes; 0 = keep the runtime's
 # setting) and pace-to-content (docs/HEADSET-FRAME-DELIVERY-20261011.md).
 [ValidateRange(0,240)][int]$RefreshHz=72,[switch]$NoPaceToContent,
 [switch]$Flat)
$ErrorActionPreference='Stop'
# Load the matching host's full utility manifest. A launcher inherited from
# another PowerShell host can resolve its binary cmdlets without Get-FileHash.
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1') -Force -ErrorAction Stop
$root=[IO.Path]::GetFullPath($PackageRoot)
. (Join-Path $PSScriptRoot 'BC2PlaytestOptions.ps1')
$weaponRanges=Get-PlaytestWeaponRanges
if(-not $weaponRanges.Contains($Weapon)){throw 'Unknown test range weapon.'}
. (Join-Path $root 'tools/BC2EyeRenderProfile.ps1')
. (Join-Path $PSScriptRoot 'BC2Package.ps1')
$receipt=Read-BC2Package $root
$options=@{}
if($OptionsPath){
 $raw=Get-Content -LiteralPath $OptionsPath -Raw|ConvertFrom-Json
 $values=@{};foreach($property in $raw.PSObject.Properties){$values[$property.Name]=$property.Value}
 $options=Convert-PlaytestOptions $root $values
}
if(-not $options.ContainsKey('StereoCapture')){$options.StereoCapture='Off'}
if($VerifyOnly){[ordered]@{state='package_verified';source=$receipt.source_commit;files=$receipt.package_files.Count;installed_selected=[bool]$receipt.selected;game_started=$false} | ConvertTo-Json;return}
[void][IO.Directory]::CreateDirectory((Join-Path $root 'reports'))
$runtimeJson=Resolve-BC2Runtime $Runtime $RuntimePath
if($Runtime -eq 'Auto'){$Runtime=if($runtimeJson -match 'steam'){'SteamVR'}else{'VDXR'}}
if(-not $Probe){
    $configPath=Join-Path $root 'config/local.json'
    if(-not $GamePath -and (Test-Path -LiteralPath $configPath)){$GamePath=(Get-Content -LiteralPath $configPath -Raw|ConvertFrom-Json).game_path}
    if(-not $GamePath -or -not (Test-Path -LiteralPath (Join-Path $GamePath 'BFBC2Game.exe'))){throw 'Choose the folder containing BFBC2Game.exe in the launcher.'}
    if($receipt.game_sha256 -and (Get-FileHash -LiteralPath (Join-Path $GamePath 'BFBC2Game.exe')).Hash -ne $receipt.game_sha256){throw 'This game executable is not supported by this build.'}
    if(Test-Path -LiteralPath (Join-Path $root 'package-manifest.json')){
        [void][IO.Directory]::CreateDirectory((Split-Path $configPath))
        [IO.File]::WriteAllText($configPath,(@{game_path=[IO.Path]::GetFullPath($GamePath)}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    }elseif((Get-Content -LiteralPath $configPath -Raw|ConvertFrom-Json).game_path -ne $GamePath){throw 'This developer candidate has a fixed game path. Use a portable release package to change it.'}
}
$savedRuntime=[Environment]::GetEnvironmentVariable('XR_RUNTIME_JSON','Process')
$savedDepth=[Environment]::GetEnvironmentVariable('FVR_XR_DEPTH_SUBMIT','Process')
$savedPipeline=[Environment]::GetEnvironmentVariable('FVR_XR_PIPELINE_DEPTH','Process')
$savedRefresh=[Environment]::GetEnvironmentVariable('FVR_XR_REFRESH_HZ','Process')
$savedPace=[Environment]::GetEnvironmentVariable('FVR_XR_PACE_TO_CONTENT','Process')
$settingsPath=Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BFBC2/settings.ini'
$settingsBytes=$null
try {
 [Environment]::SetEnvironmentVariable('XR_RUNTIME_JSON',$runtimeJson,'Process')
 [Environment]::SetEnvironmentVariable('FVR_XR_DEPTH_SUBMIT',$(if($NoDepthSubmit){'0'}else{'1'}),'Process')
 # Depth 4 keeps one request queued so every game frame renders stereo (1 = troubleshooting).
 [Environment]::SetEnvironmentVariable('FVR_XR_PIPELINE_DEPTH',$(if($SingleFramePipeline){'1'}else{'4'}),'Process')
 [Environment]::SetEnvironmentVariable('FVR_XR_REFRESH_HZ',[string]$RefreshHz,'Process')
 [Environment]::SetEnvironmentVariable('FVR_XR_PACE_TO_CONTENT',$(if($NoPaceToContent){'0'}else{'1'}),'Process')
 if(-not $Flat){
  $probeText=(& (Join-Path $root 'build/x64/BC2XrHost.exe') --probe) -join "`n"
  $probeCode=$LASTEXITCODE
  $probeText | Set-Content -LiteralPath (Join-Path $root 'reports/latest-readiness.private.json')
  if($Probe){$probeText;return}
  $readiness=$probeText|ConvertFrom-Json
  if($probeCode -ne 0 -or $readiness.state -ne 'runtime_ready'){throw 'Headset unavailable. Connect it through Virtual Desktop/SteamVR with both controllers on, then run this launcher again. No game was started.'}
  $eyeProfile=Resolve-BC2EyeRenderProfile -Readiness $readiness -Width $RenderWidth -Height $RenderHeight
  $RenderWidth=$eyeProfile.Width;$RenderHeight=$eyeProfile.Height
  $eyeProfile | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'reports/latest-eye-profile.private.json')
  Write-Host "Native renderer and XR eyes: ${RenderWidth}x${RenderHeight} ($($eyeProfile.Source)), graphics $GraphicsProfile."
 }
 $fixture=switch($Weapon){'XM8'{'ranges/sp02-xm8-only.cfg'};'XM8 + SPAS'{'ranges/sp02-xm8-spas.cfg'};'M9'{'ranges/sp02-m9-pistol.cfg'};'Arsenal'{'ranges/sp02-arsenal.cfg'}}
 if($weaponRanges[$Weapon]){
  # Reuse the mounted MP range and generate only the selected loadout.
  # The generated configuration is outside the immutable package payload.
  $fixture='../reports/selected-weapon.cfg'
  $range=Get-Content -LiteralPath (Join-Path $root 'stage/ranges/sp02-m9-pistol.cfg')
  $range=@($range|Where-Object {$_ -notmatch '^\s*(#|Give\.)'})
  $range+=@(('Give.Slot0 '+$weaponRanges[$Weapon]),'Give.Slot1 US_hg_M9-3','Give.Select 0')
  [IO.File]::WriteAllLines((Join-Path $root 'reports/selected-weapon.cfg'),[string[]]$range,[Text.UTF8Encoding]::new($false))
 }
 if(Test-Path -LiteralPath $settingsPath){$settingsBytes=[IO.File]::ReadAllBytes($settingsPath)}
 if($Flat){& (Join-Path $root 'Play-BC2VR.ps1') -Flat -Stage (Join-Path $root 'stage') -Config $fixture; $game=Get-Process BFBC2Game -ErrorAction Stop; $null=$game.Handle; $game.WaitForExit();return}
 $wrapper=Join-Path $root 'tools/Start-BC2Game.ps1'
 if(-not (Test-Path -LiteralPath $wrapper)){$wrapper=Join-Path $root 'reports/Start-Installed-BC2VR.ps1'}
 & $wrapper -Runtime $Runtime -RuntimeJson $runtimeJson -Stage (Join-Path $root 'stage') -Config $fixture -HandsMode $HandsMode @options -RenderWidth $RenderWidth -RenderHeight $RenderHeight -GraphicsProfile $GraphicsProfile
} finally {
 if($null -ne $settingsBytes){[IO.File]::WriteAllBytes($settingsPath,$settingsBytes)}
 [Environment]::SetEnvironmentVariable('XR_RUNTIME_JSON',$savedRuntime,'Process')
 [Environment]::SetEnvironmentVariable('FVR_XR_DEPTH_SUBMIT',$savedDepth,'Process')
 [Environment]::SetEnvironmentVariable('FVR_XR_PIPELINE_DEPTH',$savedPipeline,'Process')
 [Environment]::SetEnvironmentVariable('FVR_XR_REFRESH_HZ',$savedRefresh,'Process')
 [Environment]::SetEnvironmentVariable('FVR_XR_PACE_TO_CONTENT',$savedPace,'Process')
}
