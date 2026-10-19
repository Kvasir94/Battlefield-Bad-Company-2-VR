# Save the BC2 window's client area with PrintWindow (no focus change, no input).
param([string]$Out,[int]$AfterSeconds=0)
Add-Type -AssemblyName System.Drawing
if (-not ('PWC' -as [type])) {
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class PWC{[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint f);[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out R r);public struct R{public int L,T,Ri,B;}}
'@
}
$deadline=(Get-Date).AddSeconds($AfterSeconds)
while((Get-Date) -lt $deadline){ [Threading.Thread]::Sleep(500) }
$p=@(Get-Process BFBC2Game)[0]; $r=New-Object PWC+R; [PWC]::GetClientRect($p.MainWindowHandle,[ref]$r)|Out-Null
$bmp=New-Object Drawing.Bitmap $r.Ri,$r.B; $g=[Drawing.Graphics]::FromImage($bmp); $dc=$g.GetHdc()
$ok=[PWC]::PrintWindow($p.MainWindowHandle,$dc,3); $g.ReleaseHdc($dc); $bmp.Save($Out)
"captured=$ok ws=$([int]($p.WorkingSet64/1MB))MB"
