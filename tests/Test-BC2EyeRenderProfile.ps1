$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/BC2EyeRenderProfile.ps1')
function Assert([bool]$value,[string]$message){if(-not $value){throw $message}}
$ready=[pscustomobject]@{state='runtime_ready';eye_width=1832;eye_height=1920}
$p=Resolve-BC2EyeRenderProfile $ready
Assert ($p.Width -eq 1832 -and $p.Height -eq 1920 -and -not $p.DesktopFit -and $p.Source -eq 'runtime_recommended') 'The exact runtime recommendation must reach both native and XR paths without desktop fitting.'
$p=Resolve-BC2EyeRenderProfile ([pscustomobject]@{state='runtime_ready';eye_width=2015;eye_height=2201})
Assert ($p.Width -eq 2015 -and $p.Height -eq 2201) 'Recommendation must be queried, not pinned to the last headset or rounded.'
$p=Resolve-BC2EyeRenderProfile $ready -Width 1920 -Height 1080
Assert ($p.Source -eq 'explicit_override' -and $p.RecommendedWidth -eq 1832) 'Explicit paired rollback dimensions must remain distinct from the recommendation.'
foreach($case in @(@{Readiness=$ready;Width=1920},@{Readiness=$ready;Width=127;Height=1080},@{Readiness=$ready;Width=8193;Height=1080},@{Readiness=[pscustomobject]@{state='runtime_unavailable';eye_width=1832;eye_height=1920}},@{Readiness=[pscustomobject]@{state='runtime_ready';eye_width=0;eye_height=1920}},@{Readiness=[pscustomobject]@{state='runtime_ready';eye_width=9000;eye_height=1920}})){
 $rejected=$false;try{Resolve-BC2EyeRenderProfile @case | Out-Null}catch{$rejected=$true};Assert $rejected 'Invalid or unavailable eye profile must fail before game creation.'
}
'Eye profile: exact recommendations, no desktop fit, paired override and six rejection cases passed.'
