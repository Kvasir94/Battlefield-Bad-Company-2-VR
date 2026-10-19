# Sweep the whole arsenal; on a fault record the gun, exclude it, resume at the next one.
param([int]$DelayMs=3000,[int]$MaxRestarts=40)
$s="$env:USERPROFILE\fvr-testrange-stage"; $log="$s\BC2TestRange.log"
$out="$PSScriptRoot\crashers.txt"; if(-not (Test-Path $out)){New-Item $out -ItemType File | Out-Null}
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class EV{[DllImport("kernel32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr OpenEventW(uint a,bool i,string n);[DllImport("kernel32.dll")]public static extern bool SetEvent(IntPtr h);[DllImport("kernel32.dll")]public static extern bool CloseHandle(IntPtr h);}
'@
$base=Get-Content "$s\ranges\sp02-arsenal.cfg" | Where-Object {$_ -notmatch '^Arsenal\.(Exclude|Start)'}
$start=''
for($run=0;$run -lt $MaxRestarts;$run++){
  $crashers=@(Get-Content $out | Where-Object {$_})
  $cfg=@($base)+@("Arsenal.Exclude AI_ Binoculars NAM_Handgrenade $($crashers -join ' ')")
  if($start){$cfg+="Arsenal.Start $start"}
  [IO.File]::WriteAllLines("$s\ranges\_crashfind.cfg",$cfg)
  Get-Process BFBC2Game -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep 4
  "run $run start '$start' excluded $($crashers.Count)"
  & "$s\Start-BC2TestRange.ps1" -GamePath 'C:\Program Files (x86)\Electronic Arts\Battlefield Bad Company 2' -Bin $s -Config "$s\ranges\_crashfind.cfg" -InjectDelayMs 3000 -Seconds 80 -KeepRunning | Out-Null
  $list=@(Get-Content $log | Select-String '^\S+ \[\d+\] arsenal: +\d+ (\S+) 0x' | ForEach-Object {$_.Matches[0].Groups[1].Value})
  if(-not $list.Count){"no arsenal list; aborting";break}
  $h=[EV]::OpenEventW(2,$false,'Local\BC2TestRange.ArsenalNext')
  $first=if($start){[array]::IndexOf($list,$start)}else{0}; if($first -lt 0){$first=0}
  $steps=$list.Count-$first; $crashed=$false
  for($i=0;$i -lt $steps;$i++){
    if(-not (Get-Process BFBC2Game -ErrorAction SilentlyContinue)){$crashed=$true;break}
    [EV]::SetEvent($h)|Out-Null; Start-Sleep -Milliseconds $DelayMs
  }
  Start-Sleep 2
  if(-not $crashed -and -not (Get-Process BFBC2Game -ErrorAction SilentlyContinue)){$crashed=$true}
  if(-not $crashed){"sweep complete: last $((Get-Content $log | Select-String 'arsenal: \d+/' | Select-Object -Last 1).Line)";break}
  $last=(Get-Content $log | Select-String 'arsenal: (\d+)/\d+ (\S+) into' | Select-Object -Last 1)
  $gun=$last.Matches[0].Groups[2].Value
  $fault=(Get-Content $log | Select-String 'fault:' | Select-Object -Last 1).Line
  "CRASH on $gun :: $fault"
  Add-Content $out $gun
  # Resume at the next gun that the prefix exclusion does not also remove.
  $idx=[array]::IndexOf($list,$gun); $start=''
  if($idx -ge 0){for($j=$idx+1;$j -lt $list.Count;$j++){if(-not $list[$j].StartsWith($gun,[StringComparison]::OrdinalIgnoreCase)){$start=$list[$j];break}}}
  if(-not $start){"crasher was last; done";break}
}
Get-Process BFBC2Game -ErrorAction SilentlyContinue | Stop-Process -Force
"DONE"
