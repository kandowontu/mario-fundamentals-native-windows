param(
    [Parameter(Mandatory = $true)] [string] $Executable
)

$ErrorActionPreference = "Stop"

Add-Type @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class FullscreenTestNative {
    public delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct MonitorInfo {
        public uint Size;
        public Rect Monitor;
        public Rect Work;
        public uint Flags;
    }

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    public static extern IntPtr GetWindowLongPtrW(IntPtr window, int index);
    [DllImport("user32.dll")]
    public static extern IntPtr GetMenu(IntPtr window);
    [DllImport("user32.dll")]
    public static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool GetMonitorInfoW(IntPtr monitor, ref MonitorInfo info);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessageW(IntPtr window, uint message,
                                              IntPtr wParam, IntPtr lParam);

    public static Rect ReadWindowRect(IntPtr window) {
        Rect rect;
        if (!GetWindowRect(window, out rect)) throw new Win32Exception();
        return rect;
    }

    public static MonitorInfo ReadMonitorInfo(IntPtr window) {
        MonitorInfo info = new MonitorInfo();
        info.Size = (uint)Marshal.SizeOf<MonitorInfo>();
        IntPtr monitor = MonitorFromWindow(window, 2);
        if (monitor == IntPtr.Zero || !GetMonitorInfoW(monitor, ref info))
            throw new Win32Exception();
        return info;
    }

    public static IntPtr FindLargestWindow(uint processId) {
        IntPtr largest = IntPtr.Zero;
        long largestArea = 0;
        EnumWindows(delegate(IntPtr window, IntPtr parameter) {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner != processId) return true;
            Rect rect;
            if (!GetWindowRect(window, out rect)) return true;
            long area = Math.Max(0, rect.Right - rect.Left) *
                        (long)Math.Max(0, rect.Bottom - rect.Top);
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

function Test-RectEqual($Actual, $Expected) {
    return $Actual.Left -eq $Expected.Left -and $Actual.Top -eq $Expected.Top -and
        $Actual.Right -eq $Expected.Right -and $Actual.Bottom -eq $Expected.Bottom
}

$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
$cases = @(
    @{ Name = "Macintosh"; Arguments = "--edition=mac --qa-menu"; HasMenu = $true },
    @{ Name = "DOS"; Arguments = "--edition=dos --qa-dos-menu"; HasMenu = $false }
)

foreach ($case in $cases) {
    $process = Start-Process -FilePath $resolvedExecutable -ArgumentList $case.Arguments `
        -WindowStyle Hidden -PassThru
    $window = [IntPtr]::Zero
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 50
            $process.Refresh()
            $window = [FullscreenTestNative]::FindLargestWindow([uint32]$process.Id)
        } while ($window -eq [IntPtr]::Zero -and !$process.HasExited -and
                 [DateTime]::UtcNow -lt $deadline)

        if ($process.HasExited) {
            throw "$($case.Name) edition exited before opening its hidden test window."
        }
        if ($window -eq [IntPtr]::Zero) {
            throw "$($case.Name) edition did not create its hidden test window."
        }

        $windowedRect = [FullscreenTestNative]::ReadWindowRect($window)
        $windowedStyle = [FullscreenTestNative]::GetWindowLongPtrW($window, -16).ToInt64()
        $windowedMenu = [FullscreenTestNative]::GetMenu($window)
        if (($windowedMenu -ne [IntPtr]::Zero) -ne $case.HasMenu) {
            throw "$($case.Name) edition began with an unexpected menu-bar state."
        }

        # WM_SYSKEYDOWN, VK_RETURN, with the Alt-context bit set.
        [FullscreenTestNative]::SendMessageW(
            $window, 0x0104, [IntPtr]13, [IntPtr](1L -shl 29)) | Out-Null
        $fullscreenRect = [FullscreenTestNative]::ReadWindowRect($window)
        $monitor = [FullscreenTestNative]::ReadMonitorInfo($window)
        $fullscreenStyle = [FullscreenTestNative]::GetWindowLongPtrW($window, -16).ToInt64()
        if (($fullscreenStyle -band 0x00cf0000L) -ne 0) {
            throw "$($case.Name) Alt+Enter left overlapped-window chrome enabled."
        }
        if (!(Test-RectEqual $fullscreenRect $monitor.Monitor)) {
            throw "$($case.Name) Alt+Enter did not cover its current monitor."
        }
        if ([FullscreenTestNative]::GetMenu($window) -ne [IntPtr]::Zero) {
            throw "$($case.Name) fullscreen mode retained a menu bar."
        }

        [FullscreenTestNative]::SendMessageW(
            $window, 0x0105, [IntPtr]13, [IntPtr](1L -shl 29)) | Out-Null
        [FullscreenTestNative]::SendMessageW(
            $window, 0x0104, [IntPtr]13, [IntPtr](1L -shl 29)) | Out-Null
        $restoredRect = [FullscreenTestNative]::ReadWindowRect($window)
        $restoredStyle = [FullscreenTestNative]::GetWindowLongPtrW($window, -16).ToInt64()
        if ($restoredStyle -ne $windowedStyle -or
            !(Test-RectEqual $restoredRect $windowedRect) -or
            [FullscreenTestNative]::GetMenu($window) -ne $windowedMenu) {
            throw "$($case.Name) Alt+Enter did not restore its exact windowed state " +
                "(style $windowedStyle->$restoredStyle, " +
                "rect $($windowedRect.Left),$($windowedRect.Top),$($windowedRect.Right),$($windowedRect.Bottom)" +
                "->$($restoredRect.Left),$($restoredRect.Top),$($restoredRect.Right),$($restoredRect.Bottom), " +
                "menu $windowedMenu->$([FullscreenTestNative]::GetMenu($window)))."
        }

        Write-Output "PASS $($case.Name.ToLowerInvariant())_alt_enter=fullscreen_restore"
    }
    finally {
        if (!$process.HasExited) {
            [FullscreenTestNative]::SendMessageW(
                $window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
            if (!$process.WaitForExit(2000)) { Stop-Process -Id $process.Id }
        }
    }
}
