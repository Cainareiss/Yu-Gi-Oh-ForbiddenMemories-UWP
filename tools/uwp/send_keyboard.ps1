param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Up','Down','Left','Right','Enter','A','B','X','Y','Z','C','V','P','Backspace')]
    [string]$Key,
    [ValidateRange(1, 50)][int]$Count = 1,
    [ValidateRange(50, 2000)][int]$GapMs = 500
)

$ErrorActionPreference = 'Stop'
if (-not ('YfmKeyboard' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class YfmKeyboard {
    public delegate bool Callback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Callback callback, IntPtr parameter);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    public static IntPtr FindGame() {
        IntPtr result = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr parameter) {
            var title = new StringBuilder(512);
            GetWindowText(window, title, title.Capacity);
            if (title.ToString() != "Yu-Gi-Oh! Forbidden Memories") return true;
            result = window;
            return false;
        }, IntPtr.Zero);
        return result;
    }
}
'@
}
$window = [YfmKeyboard]::FindGame()
if ($window -eq [IntPtr]::Zero) { throw 'The installed game window is not open.' }
[YfmKeyboard]::SetForegroundWindow($window) | Out-Null
Start-Sleep -Milliseconds 150
$keys = @{ Up=0x26; Down=0x28; Left=0x25; Right=0x27; Enter=0x0d; Backspace=0x08 }
$virtualKey = if ($keys.ContainsKey($Key)) { [byte]$keys[$Key] } else { [byte][char]$Key }
for ($press = 0; $press -lt $Count; $press++) {
    if ([YfmKeyboard]::GetForegroundWindow() -ne $window) {
        throw 'Stopped: keyboard focus left the game window.'
    }
    try {
        [YfmKeyboard]::keybd_event($virtualKey, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 80
    }
    finally { [YfmKeyboard]::keybd_event($virtualKey, 0, 2, [UIntPtr]::Zero) }
    if ($press + 1 -lt $Count) { Start-Sleep -Milliseconds $GapMs }
}
Write-Output "Sent $Count $Key press(es) to the foreground game."
