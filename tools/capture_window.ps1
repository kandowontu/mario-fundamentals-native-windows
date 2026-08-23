param(
    [Parameter(Mandatory = $true)] [string] $Executable,
    [Parameter(Mandatory = $true)] [string] $Output,
    [string] $Arguments = "",
    [string] $SecondOutput = "",
    [int] $FrameDelayMilliseconds = 330,
    [int] $LogicalClickX = -1,
    [int] $LogicalClickY = -1,
    [int] $DragFromLogicalX = -1,
    [int] $DragFromLogicalY = -1,
    [int] $DragToLogicalX = -1,
    [int] $DragToLogicalY = -1,
    [int] $SecondLogicalClickX = -1,
    [int] $SecondLogicalClickY = -1,
    [int] $VirtualKey = -1,
    [int] $CommandId = -1,
    [int] $TimerTicks = 0,
    [string] $Text = "",
    [int] $PostTextLogicalClickX = -1,
    [int] $PostTextLogicalClickY = -1,
    [int] $InitialDelayMilliseconds = 500,
    [int] $PostActionDelayMilliseconds = 500,
    [int] $TimeoutSeconds = 10
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class WindowCaptureNative {
    public delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr window, IntPtr destination, uint flags);
    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")]
    public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessageW(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    // PowerShell 7 can return a zeroed value-type instance when a P/Invoke
    // `out` struct is passed through [ref].  Keep the out parameter inside C#
    // and return the populated struct by value instead.
    public static Rect ReadWindowRect(IntPtr window) {
        Rect rect;
        if (!GetWindowRect(window, out rect)) throw new Win32Exception();
        return rect;
    }
    public static Rect ReadClientRect(IntPtr window) {
        Rect rect;
        if (!GetClientRect(window, out rect)) throw new Win32Exception();
        return rect;
    }
    public static IntPtr FindLargestVisibleWindow(uint processId) {
        IntPtr largest = IntPtr.Zero;
        long largestArea = 0;
        EnumWindows(delegate(IntPtr window, IntPtr parameter) {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner != processId || !IsWindowVisible(window)) return true;
            Rect rect;
            if (!GetWindowRect(window, out rect)) return true;
            long width = Math.Max(0, rect.Right - rect.Left);
            long height = Math.Max(0, rect.Bottom - rect.Top);
            long area = width * height;
            if (area > largestArea) {
                largestArea = area;
                largest = window;
            }
            return true;
        }, IntPtr.Zero);
        return largest;
    }
}
'@

[WindowCaptureNative]::SetProcessDPIAware() | Out-Null

$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
$resolvedOutput = [System.IO.Path]::GetFullPath($Output)
$resolvedSecondOutput = if ($SecondOutput) { [System.IO.Path]::GetFullPath($SecondOutput) } else { "" }
$process = if ($Arguments) {
    Start-Process -FilePath $resolvedExecutable -ArgumentList $Arguments -PassThru
} else {
    Start-Process -FilePath $resolvedExecutable -PassThru
}
try {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        $windowHandle = [WindowCaptureNative]::FindLargestVisibleWindow([uint32]$process.Id)
    } while ($windowHandle -eq [IntPtr]::Zero -and !$process.HasExited -and [DateTime]::UtcNow -lt $deadline)

    if ($process.HasExited) { throw "Application exited before opening a window (exit $($process.ExitCode))." }
    if ($windowHandle -eq [IntPtr]::Zero) { throw "Application did not open a window in time." }

    [WindowCaptureNative]::ShowWindow($windowHandle, 9) | Out-Null
    [WindowCaptureNative]::SetWindowPos($windowHandle, [IntPtr](-1), 0, 0, 0, 0, 0x0053) | Out-Null
    [WindowCaptureNative]::SetForegroundWindow($windowHandle) | Out-Null
    Start-Sleep -Milliseconds $InitialDelayMilliseconds
    if ($LogicalClickX -ge 0 -and $LogicalClickY -ge 0) {
        $client = [WindowCaptureNative]::ReadClientRect($windowHandle)
        $clientWidth = $client.Right - $client.Left
        $clientHeight = $client.Bottom - $client.Top
        $scale = [Math]::Min($clientWidth / 512.0, $clientHeight / 384.0)
        $viewportWidth = [int](512 * $scale)
        $viewportHeight = [int](384 * $scale)
        $x = [int](($clientWidth - $viewportWidth) / 2 + $LogicalClickX * $scale)
        $y = [int](($clientHeight - $viewportHeight) / 2 + $LogicalClickY * $scale)
        $packedPoint = [IntPtr](($y -band 0xffff) -shl 16 -bor ($x -band 0xffff))
        [WindowCaptureNative]::SendMessageW($windowHandle, 0x0201, [IntPtr]1, $packedPoint) | Out-Null
        [WindowCaptureNative]::SendMessageW($windowHandle, 0x0202, [IntPtr]0, $packedPoint) | Out-Null
        Start-Sleep -Milliseconds $PostActionDelayMilliseconds
    }
    if ($DragFromLogicalX -ge 0 -and $DragFromLogicalY -ge 0 -and
        $DragToLogicalX -ge 0 -and $DragToLogicalY -ge 0) {
        $client = [WindowCaptureNative]::ReadClientRect($windowHandle)
        $clientWidth = $client.Right - $client.Left
        $clientHeight = $client.Bottom - $client.Top
        $scale = [Math]::Min($clientWidth / 512.0, $clientHeight / 384.0)
        $viewportWidth = [int](512 * $scale)
        $viewportHeight = [int](384 * $scale)
        $viewportX = [int](($clientWidth - $viewportWidth) / 2)
        $viewportY = [int](($clientHeight - $viewportHeight) / 2)
        $fromX = [int]($viewportX + $DragFromLogicalX * $scale)
        $fromY = [int]($viewportY + $DragFromLogicalY * $scale)
        $toX = [int]($viewportX + $DragToLogicalX * $scale)
        $toY = [int]($viewportY + $DragToLogicalY * $scale)
        $fromPoint = [IntPtr](($fromY -band 0xffff) -shl 16 -bor ($fromX -band 0xffff))
        [WindowCaptureNative]::SendMessageW(
            $windowHandle, 0x0201, [IntPtr]1, $fromPoint) | Out-Null
        for ($step = 1; $step -le 8; ++$step) {
            $x = [int]($fromX + ($toX - $fromX) * $step / 8.0)
            $y = [int]($fromY + ($toY - $fromY) * $step / 8.0)
            $packedPoint = [IntPtr](($y -band 0xffff) -shl 16 -bor ($x -band 0xffff))
            [WindowCaptureNative]::SendMessageW(
                $windowHandle, 0x0200, [IntPtr]1, $packedPoint) | Out-Null
        }
        $toPoint = [IntPtr](($toY -band 0xffff) -shl 16 -bor ($toX -band 0xffff))
        [WindowCaptureNative]::SendMessageW(
            $windowHandle, 0x0202, [IntPtr]0, $toPoint) | Out-Null
        Start-Sleep -Milliseconds $PostActionDelayMilliseconds
    }
    if ($SecondLogicalClickX -ge 0 -and $SecondLogicalClickY -ge 0) {
        $client = [WindowCaptureNative]::ReadClientRect($windowHandle)
        $clientWidth = $client.Right - $client.Left
        $clientHeight = $client.Bottom - $client.Top
        $scale = [Math]::Min($clientWidth / 512.0, $clientHeight / 384.0)
        $viewportWidth = [int](512 * $scale)
        $viewportHeight = [int](384 * $scale)
        $x = [int](($clientWidth - $viewportWidth) / 2 + $SecondLogicalClickX * $scale)
        $y = [int](($clientHeight - $viewportHeight) / 2 + $SecondLogicalClickY * $scale)
        $packedPoint = [IntPtr](($y -band 0xffff) -shl 16 -bor ($x -band 0xffff))
        [WindowCaptureNative]::SendMessageW($windowHandle, 0x0201, [IntPtr]1, $packedPoint) | Out-Null
        [WindowCaptureNative]::SendMessageW($windowHandle, 0x0202, [IntPtr]0, $packedPoint) | Out-Null
        Start-Sleep -Milliseconds $PostActionDelayMilliseconds
    }
    if ($VirtualKey -ge 0) {
        [WindowCaptureNative]::SendMessageW($windowHandle, 0x0100, [IntPtr]$VirtualKey, [IntPtr]0) | Out-Null
        [WindowCaptureNative]::SendMessageW($windowHandle, 0x0101, [IntPtr]$VirtualKey, [IntPtr]0) | Out-Null
        Start-Sleep -Milliseconds $PostActionDelayMilliseconds
    }
    if ($CommandId -ge 0) {
        [WindowCaptureNative]::SendMessageW(
            $windowHandle, 0x0111, [IntPtr]$CommandId, [IntPtr]0) | Out-Null
        Start-Sleep -Milliseconds $PostActionDelayMilliseconds
    }
    for ($tick = 0; $tick -lt $TimerTicks; ++$tick) {
        [WindowCaptureNative]::SendMessageW(
            $windowHandle, 0x0113, [IntPtr]1, [IntPtr]0) | Out-Null
    }
    if ($TimerTicks -gt 0) { Start-Sleep -Milliseconds $PostActionDelayMilliseconds }
    if ($Text) {
        foreach ($character in $Text.ToCharArray()) {
            [WindowCaptureNative]::SendMessageW(
                $windowHandle, 0x0102, [IntPtr][int]$character, [IntPtr]0) | Out-Null
        }
        [WindowCaptureNative]::SendMessageW(
            $windowHandle, 0x0102, [IntPtr]13, [IntPtr]0) | Out-Null
        Start-Sleep -Milliseconds $PostActionDelayMilliseconds
    }
    if ($PostTextLogicalClickX -ge 0 -and $PostTextLogicalClickY -ge 0) {
        $client = [WindowCaptureNative]::ReadClientRect($windowHandle)
        $clientWidth = $client.Right - $client.Left
        $clientHeight = $client.Bottom - $client.Top
        $scale = [Math]::Min($clientWidth / 512.0, $clientHeight / 384.0)
        $viewportWidth = [int](512 * $scale)
        $viewportHeight = [int](384 * $scale)
        $x = [int](($clientWidth - $viewportWidth) / 2 + $PostTextLogicalClickX * $scale)
        $y = [int](($clientHeight - $viewportHeight) / 2 + $PostTextLogicalClickY * $scale)
        $packedPoint = [IntPtr](($y -band 0xffff) -shl 16 -bor ($x -band 0xffff))
        [WindowCaptureNative]::SendMessageW($windowHandle, 0x0201, [IntPtr]1, $packedPoint) | Out-Null
        [WindowCaptureNative]::SendMessageW($windowHandle, 0x0202, [IntPtr]0, $packedPoint) | Out-Null
        Start-Sleep -Milliseconds $PostActionDelayMilliseconds
    }
    $bounds = [WindowCaptureNative]::ReadWindowRect($windowHandle)
    $width = $bounds.Right - $bounds.Left
    $height = $bounds.Bottom - $bounds.Top
    foreach ($capturePath in @($resolvedOutput, $resolvedSecondOutput)) {
        if (!$capturePath) { continue }
        if ($capturePath -eq $resolvedSecondOutput) { Start-Sleep -Milliseconds $FrameDelayMilliseconds }
        $bitmap = New-Object System.Drawing.Bitmap $width, $height
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $deviceContext = $graphics.GetHdc()
            try {
                if (![WindowCaptureNative]::PrintWindow($windowHandle, $deviceContext, 2)) {
                    throw "Could not render the application window."
                }
            } finally {
                $graphics.ReleaseHdc($deviceContext)
            }
            $bitmap.Save($capturePath, [System.Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
} finally {
    if (!$process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (!$process.WaitForExit(2000)) { Stop-Process -Id $process.Id }
    }
}

Get-Item -LiteralPath $resolvedOutput
if ($resolvedSecondOutput) { Get-Item -LiteralPath $resolvedSecondOutput }
