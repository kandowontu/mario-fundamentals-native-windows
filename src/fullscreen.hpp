#pragma once

#include <windows.h>

namespace mf {

// Windows reports Alt+Enter through WM_SYSKEYDOWN rather than WM_KEYDOWN.
// Keep the shortcut recognition and borderless-window transition shared by
// both native editions so their behavior cannot drift apart.
[[nodiscard]] inline bool isFullscreenShortcut(UINT message, WPARAM wParam,
                                               LPARAM lParam) {
    const bool firstPress = (static_cast<ULONG_PTR>(lParam) & (1ULL << 30U)) == 0;
    if (!firstPress) return false;
    if (message == WM_KEYDOWN && wParam == VK_F11) return true;
    return (message == WM_SYSKEYDOWN || message == WM_KEYDOWN) &&
           wParam == VK_RETURN &&
           (((HIWORD(lParam) & KF_ALTDOWN) != 0) || GetKeyState(VK_MENU) < 0);
}

class FullscreenController {
public:
    FullscreenController() { windowedPlacement_.length = sizeof(WINDOWPLACEMENT); }

    [[nodiscard]] bool active() const { return active_; }

    void toggle(HWND window) {
        if (active_) restore(window);
        else enter(window);
    }

    void restore(HWND window) {
        if (!window || !active_) return;
        SetWindowLongPtrW(window, GWL_STYLE, windowedStyle_);
        SetWindowLongPtrW(window, GWL_EXSTYLE, windowedExtendedStyle_);
        if (windowedMenu_) SetMenu(window, windowedMenu_);
        SetWindowPlacement(window, &windowedPlacement_);
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        if (!windowedVisible_) ShowWindow(window, SW_HIDE);
        DrawMenuBar(window);
        active_ = false;
    }

private:
    void enter(HWND window) {
        if (!window || active_) return;
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(WINDOWPLACEMENT);
        if (!GetWindowPlacement(window, &placement) ||
            !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
            return;
        }

        windowedPlacement_ = placement;
        windowedStyle_ = GetWindowLongPtrW(window, GWL_STYLE);
        windowedExtendedStyle_ = GetWindowLongPtrW(window, GWL_EXSTYLE);
        windowedMenu_ = GetMenu(window);
        windowedVisible_ = IsWindowVisible(window) != FALSE;
        if (windowedMenu_) SetMenu(window, nullptr);
        SetWindowLongPtrW(window, GWL_STYLE,
                          windowedStyle_ & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW));
        if (!SetWindowPos(window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                          monitor.rcMonitor.right - monitor.rcMonitor.left,
                          monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                          SWP_NOOWNERZORDER | SWP_FRAMECHANGED)) {
            SetWindowLongPtrW(window, GWL_STYLE, windowedStyle_);
            SetWindowLongPtrW(window, GWL_EXSTYLE, windowedExtendedStyle_);
            if (windowedMenu_) SetMenu(window, windowedMenu_);
            SetWindowPlacement(window, &windowedPlacement_);
            if (!windowedVisible_) ShowWindow(window, SW_HIDE);
            return;
        }
        active_ = true;
    }

    bool active_{};
    WINDOWPLACEMENT windowedPlacement_{};
    LONG_PTR windowedStyle_{};
    LONG_PTR windowedExtendedStyle_{};
    HMENU windowedMenu_{};
    bool windowedVisible_{};
};

}  // namespace mf
