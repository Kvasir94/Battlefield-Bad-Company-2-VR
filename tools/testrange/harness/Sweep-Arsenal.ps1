param([int]$Steps=400,[int]$DelayMs=1000)
$s="$env:USERPROFILE\fvr-testrange-stage"
Get-Process BFBC2Game -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep 3
& "$s\Start-BC2TestRange.ps1" -GamePath 'C:\Program Files (x86)\Electronic Arts\Battlefield Bad Company 2' -Bin $s -Config "$s\ranges\sp02-arsenal.cfg" -InjectDelayMs 3000 -Seconds 75 -KeepRunning | Select-String "arsenal: \d+ weapons|give: done|elapsed"
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class EV{[DllImport("kernel32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr OpenEventW(uint a,bool i,string n);[DllImport("kernel32.dll")]public static extern bool SetEvent(IntPtr h);}
'@
$h=[EV]::OpenEventW(2,$false,'Local\BC2TestRange.ArsenalNext'); "event $h"
for($i=1;$i -le $Steps;$i++){
  if(-not (Get-Process BFBC2Game -ErrorAction SilentlyContinue)){"GAME EXITED before step $i";break}
  [EV]::SetEvent($h)|Out-Null; Start-Sleep -Milliseconds $DelayMs
}
"alive at end: $([bool](Get-Process BFBC2Game -ErrorAction SilentlyContinue))"
Get-Content "$s\BC2TestRange.log" | Select-String "arsenal: \d+/|fault:" | Select-Object -Last 4
