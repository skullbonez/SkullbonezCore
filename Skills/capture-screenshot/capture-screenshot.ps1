# Screenshot capture utility for SkullbonezCore debugging
# Captures the SkullbonezCore window (or full screen) after a delay

param(
    [int]$DelaySeconds = 3,
    [string]$OutputPath = "Y:\SkullbonezCore\Debug\screenshot.png"
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Collections.Generic;
public class WinAPI {
    [DllImport("user32.dll")]
    public static extern IntPtr FindWindow(string className, string windowName);
    
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
    
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left, Top, Right, Bottom;
    }

    public static IntPtr FindWindowByPid(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            uint wpid;
            GetWindowThreadProcessId(hWnd, out wpid);
            if (wpid == pid && IsWindowVisible(hWnd)) {
                StringBuilder sb = new StringBuilder(256);
                GetClassName(hWnd, sb, 256);
                if (sb.ToString() == "SkullbonezWindow") {
                    found = hWnd;
                    return false;
                }
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
"@

# Launch the exe
$proc = Start-Process "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe" -WorkingDirectory "Y:\SkullbonezCore" -PassThru

Write-Host "Launched PID $($proc.Id), waiting ${DelaySeconds}s..."
Start-Sleep -Seconds $DelaySeconds

# Check if still alive
if ($proc.HasExited) {
    Write-Host "CRASHED - Process exited with code $($proc.ExitCode) after ${DelaySeconds}s"
    # Capture full screen anyway to see any error dialogs
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bitmap = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
    Write-Host "Full-screen screenshot saved to $OutputPath"
    exit 1
}

# Find the window by process ID (title changes at runtime with FPS data)
$hwnd = [WinAPI]::FindWindowByPid([uint32]$proc.Id)
if ($hwnd -eq [IntPtr]::Zero) {
    Write-Host "Window not found for PID $($proc.Id) - capturing full screen"
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bitmap = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
    Write-Host "Full-screen screenshot saved to $OutputPath"
    exit 1
}

# Capture the window
[WinAPI]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 500

$rect = New-Object WinAPI+RECT
[WinAPI]::GetWindowRect($hwnd, [ref]$rect) | Out-Null

$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top

if ($width -le 0 -or $height -le 0) {
    Write-Host "Invalid window dimensions: ${width}x${height}"
    exit 1
}

$bitmap = New-Object System.Drawing.Bitmap($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, (New-Object System.Drawing.Size($width, $height)))
$bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()

Write-Host "Window screenshot saved to $OutputPath (${width}x${height})"
Write-Host "Process still running: PID $($proc.Id)"
