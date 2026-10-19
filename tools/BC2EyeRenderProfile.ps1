# The host --probe performs the same primary-stereo view query used at session creation.
# Its eye dimensions are the maximum recommendation across the two runtime views.
function Resolve-BC2EyeRenderProfile {
    [CmdletBinding()]
    param([Parameter(Mandatory)]$Readiness, [int]$Width=0, [int]$Height=0)
    if($Readiness.state -ne 'runtime_ready'){throw 'Runtime readiness is required before choosing native eye dimensions.'}
    if(($Width -eq 0) -ne ($Height -eq 0)){throw 'Both explicit eye dimensions must be supplied together.'}
    $recommendedWidth=[long]$Readiness.eye_width
    $recommendedHeight=[long]$Readiness.eye_height
    if($recommendedWidth -lt 128 -or $recommendedHeight -lt 128 -or $recommendedWidth -gt 8192 -or $recommendedHeight -gt 8192){throw 'Runtime recommendation exceeds the supported native bridge dimensions.'}
    $source='runtime_recommended'
    if($Width -or $Height){
        if($Width -lt 128 -or $Height -lt 128 -or $Width -gt 8192 -or $Height -gt 8192){throw 'Explicit eye dimensions exceed the supported native bridge dimensions.'}
        $source='explicit_override'
    }else{$Width=[int]$recommendedWidth;$Height=[int]$recommendedHeight}
    [pscustomobject]@{Width=$Width;Height=$Height;Source=$source;RecommendedWidth=$recommendedWidth;RecommendedHeight=$recommendedHeight;Scale=1.0;DesktopFit=$false}
}
