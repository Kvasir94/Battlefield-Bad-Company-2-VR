# Bring the BC2 window to the foreground without synthetic input: attach to the
# current foreground thread's input queue so SetForegroundWindow is permitted.
param([int]$GamePid=0)
if (-not ('FG' -as [type])) {
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class FG{
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
 [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a,uint b,bool attach);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int cmd);
 public static bool Focus(IntPtr target){
  uint p; var fg=GetForegroundWindow(); uint fgThread=GetWindowThreadProcessId(fg,out p); uint me=GetCurrentThreadId();
  bool attached=fgThread!=me && AttachThreadInput(me,fgThread,true);
  ShowWindow(target,9); BringWindowToTop(target); bool ok=SetForegroundWindow(target);
  if(attached) AttachThreadInput(me,fgThread,false);
  return ok && GetForegroundWindow()==target;
 }
}
'@
}
$p = if ($GamePid) { Get-Process -Id $GamePid } else { @(Get-Process BFBC2Game)[0] }
[FG]::Focus($p.MainWindowHandle)
