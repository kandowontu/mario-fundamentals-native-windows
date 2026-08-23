#include "launcher.hpp"

#include "resource_ids.h"

#include <stdexcept>

namespace mf {
namespace {

constexpr int kChooseMac = 100;
constexpr int kChooseDos = 101;

class Launcher {
public:
    Launcher(HINSTANCE instance, int showCommand) : instance_(instance) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = procedure;
        windowClass.hInstance = instance_;
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP));
        windowClass.hIconSm = windowClass.hIcon;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = L"MarioNativeEditionLauncher";
        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("edition launcher class registration failed");
        }

        RECT bounds{0, 0, 620, 300};
        AdjustWindowRectEx(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
        window_ = CreateWindowExW(
            0, windowClass.lpszClassName, L"Mario's Game Gallery / FUNdamentals",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            x, y, width, height, nullptr, nullptr, instance_, this);
        if (!window_) throw std::runtime_error("edition launcher window creation failed");

        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        macButton_ = CreateWindowExW(
            0, L"BUTTON", L"Mario's FUNdamentals\r\nMacintosh version 1.1",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_MULTILINE | BS_PUSHBUTTON,
            45, 117, 245, 82, window_, reinterpret_cast<HMENU>(kChooseMac), instance_, nullptr);
        dosButton_ = CreateWindowExW(
            0, L"BUTTON", L"Mario's Game Gallery\r\nDOS version 1.0",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_MULTILINE | BS_PUSHBUTTON,
            330, 117, 245, 82, window_, reinterpret_cast<HMENU>(kChooseDos), instance_, nullptr);
        if (!macButton_ || !dosButton_) throw std::runtime_error("edition buttons could not be created");
        SendMessageW(macButton_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(dosButton_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        ShowWindow(window_, showCommand);
        UpdateWindow(window_);
        SetFocus(macButton_);
    }

    GameEdition run() {
        MSG message{};
        while (result_ == GameEdition::Cancel && IsWindow(window_)) {
            const BOOL status = GetMessageW(&message, nullptr, 0, 0);
            if (status <= 0) break;
            if (!IsDialogMessageW(window_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return result_;
    }

private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        Launcher* self = reinterpret_cast<Launcher*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            self = static_cast<Launcher*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wParam, lParam);
        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            HBRUSH background = CreateSolidBrush(RGB(0, 52, 15));
            FillRect(dc, &client, background);
            DeleteObject(background);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 230, 30));
            HFONT titleFont = CreateFontW(
                -28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            HGDIOBJ previous = SelectObject(dc, titleFont);
            RECT title{20, 24, 600, 66};
            DrawTextW(dc, L"Choose a game edition", -1, &title,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, previous);
            DeleteObject(titleFont);
            SetTextColor(dc, RGB(235, 235, 235));
            RECT subtitle{30, 70, 590, 104};
            DrawTextW(dc, L"Both editions run natively and are fully self-contained.", -1,
                      &subtitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == kChooseMac || LOWORD(wParam) == kChooseDos) {
                self->result_ = LOWORD(wParam) == kChooseMac
                                    ? GameEdition::Macintosh : GameEdition::Dos;
                DestroyWindow(window);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    HINSTANCE instance_{};
    HWND window_{};
    HWND macButton_{};
    HWND dosButton_{};
    GameEdition result_{GameEdition::Cancel};
};

}  // namespace

GameEdition chooseGameEdition(HINSTANCE instance, int showCommand) {
    Launcher launcher(instance, showCommand);
    return launcher.run();
}

}  // namespace mf
