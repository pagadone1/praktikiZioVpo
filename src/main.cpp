#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>
#include "../resources/resource.h"

#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif
#ifndef NIN_KEYSELECT
#define NIN_KEYSELECT (WM_USER + 1)
#endif
#ifndef NIN_CONTEXT
#define NIN_CONTEXT (WM_USER + 3)
#endif

namespace {

    constexpr wchar_t kMainWindowClass[] = L"ziovptrayui_main";
    constexpr wchar_t kTrayWindowClass[] = L"ziovptrayui_tray";
    constexpr wchar_t kAppTitle[] = L"\u0422\u0440\u0435\u0439";
    constexpr wchar_t kMutexName[] = L"Local\\ziovptrayui_SingleInstance";

    constexpr UINT kTrayIconId = 1;
    constexpr UINT kWmTrayIcon = WM_APP + 1;
    constexpr UINT kMenuOpen = 1001;
    constexpr UINT kMenuExit = 1002;
    constexpr UINT kMenuFileExit = 2001;

    HINSTANCE g_instance = nullptr;
    HWND g_main_window = nullptr;
    HWND g_tray_window = nullptr;
    HANDLE g_single_instance_mutex = nullptr;
    HICON g_tray_icon = nullptr;
    UINT g_taskbar_created_message = 0;
    bool g_tray_icon_added = false;
    bool g_is_exiting = false;

    HICON LoadTrayIcon() {
        HICON icon = static_cast<HICON>(LoadImageW(
            g_instance,
            MAKEINTRESOURCE(IDI_TRAY_ICON),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR
        ));
        if (icon) {
            return icon;
        }

        icon = static_cast<HICON>(LoadImageW(
            g_instance,
            MAKEINTRESOURCE(IDI_TRAY_ICON),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXICON),
            GetSystemMetrics(SM_CYICON),
            LR_DEFAULTCOLOR
        ));
        if (icon) {
            return icon;
        }

        return static_cast<HICON>(LoadImageW(
            nullptr,
            IDI_APPLICATION,
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_SHARED
        ));
    }

    void FreeTrayIconHandle() {
        if (g_tray_icon) {
            DestroyIcon(g_tray_icon);
            g_tray_icon = nullptr;
        }
    }

    bool IsHiddenLaunch(int argc, wchar_t** argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--hidden") == 0 ||
                _wcsicmp(argv[i], L"/hidden") == 0 ||
                _wcsicmp(argv[i], L"--no-window") == 0 ||
                _wcsicmp(argv[i], L"/no-window") == 0) {
                return true;
            }
        }
        return false;
    }

    void ShowMainWindow() {
        if (!g_main_window) {
            return;
        }

        if (IsIconic(g_main_window)) {
            ShowWindow(g_main_window, SW_RESTORE);
        }

        ShowWindow(g_main_window, SW_SHOW);
        ShowWindow(g_main_window, SW_SHOWNORMAL);

        const HWND foreground = GetForegroundWindow();
        const DWORD foreground_thread = GetWindowThreadProcessId(foreground, nullptr);
        const DWORD current_thread = GetCurrentThreadId();

        if (foreground_thread && foreground_thread != current_thread) {
            AttachThreadInput(foreground_thread, current_thread, TRUE);
        }

        SetForegroundWindow(g_main_window);
        BringWindowToTop(g_main_window);
        SetActiveWindow(g_main_window);
        SetFocus(g_main_window);

        if (foreground_thread && foreground_thread != current_thread) {
            AttachThreadInput(foreground_thread, current_thread, FALSE);
        }
    }

    bool IsTrayOpenMessage(UINT msg) {
        return msg == WM_LBUTTONUP ||
            msg == WM_LBUTTONDOWN ||
            msg == WM_LBUTTONDBLCLK ||
            msg == NIN_SELECT ||
            msg == NIN_KEYSELECT;
    }

    bool IsTrayMenuMessage(UINT msg) {
        return msg == WM_RBUTTONUP ||
            msg == WM_RBUTTONDOWN ||
            msg == WM_CONTEXTMENU ||
            msg == NIN_CONTEXT;
    }

    void RemoveTrayIcon() {
        if (!g_tray_icon_added || !g_tray_window) {
            return;
        }

        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = g_tray_window;
        nid.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        g_tray_icon_added = false;
    }

    bool AddTrayIcon() {
        if (!g_tray_window) {
            return false;
        }

        if (!g_tray_icon) {
            g_tray_icon = LoadTrayIcon();
        }
        if (!g_tray_icon) {
            return false;
        }

        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = g_tray_window;
        nid.uID = kTrayIconId;
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        nid.uCallbackMessage = kWmTrayIcon;
        nid.hIcon = g_tray_icon;
        StringCchCopyW(nid.szTip, ARRAYSIZE(nid.szTip), kAppTitle);

        if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
            return false;
        }

        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        g_tray_icon_added = true;
        return true;
    }

    void RecreateTrayIcon() {
        RemoveTrayIcon();
        AddTrayIcon();
    }

    void ExitApplication() {
        if (g_is_exiting) {
            return;
        }
        g_is_exiting = true;

        RemoveTrayIcon();

        if (g_main_window) {
            DestroyWindow(g_main_window);
            g_main_window = nullptr;
        }

        if (g_tray_window) {
            DestroyWindow(g_tray_window);
            g_tray_window = nullptr;
        }

        PostQuitMessage(0);
    }

    void ShowTrayMenu(HWND tray_hwnd) {
        HMENU menu = CreatePopupMenu();
        if (!menu) {
            return;
        }

        AppendMenuW(menu, MF_STRING, kMenuOpen, L"\u041E\u0442\u043A\u0440\u044B\u0442\u044C");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuExit, L"\u0412\u044B\u0445\u043E\u0434");

        POINT cursor{};
        GetCursorPos(&cursor);

        SetForegroundWindow(tray_hwnd);

        const UINT selected = TrackPopupMenu(
            menu,
            TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RETURNCMD,
            cursor.x,
            cursor.y,
            0,
            tray_hwnd,
            nullptr
        );

        DestroyMenu(menu);

        if (selected != 0) {
            SendMessageW(tray_hwnd, WM_COMMAND, MAKEWPARAM(selected, 0), 0);
        }

        PostMessageW(tray_hwnd, WM_NULL, 0, 0);
    }

    void HandleTrayNotification(LPARAM l_param) {
        const UINT tray_msg = static_cast<UINT>(l_param);
        const UINT tray_lo = LOWORD(l_param);

        if (IsTrayOpenMessage(tray_msg) || IsTrayOpenMessage(tray_lo)) {
            ShowMainWindow();
            return;
        }

        if (IsTrayMenuMessage(tray_msg) || IsTrayMenuMessage(tray_lo)) {
            ShowTrayMenu(g_tray_window);
        }
    }

    HMENU CreateMainMenu() {
        HMENU menu_bar = CreateMenu();
        HMENU file_menu = CreatePopupMenu();

        AppendMenuW(file_menu, MF_STRING, kMenuFileExit, L"\u0412\u044B\u0445\u043E\u0434");
        AppendMenuW(menu_bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file_menu), L"\u0424\u0430\u0439\u043B");

        return menu_bar;
    }

    void FillRoundedRect(HDC hdc, const RECT& rect, COLORREF color) {
        HBRUSH brush = CreateSolidBrush(color);
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HGDIOBJ old_brush = SelectObject(hdc, brush);
        HGDIOBJ old_pen = SelectObject(hdc, pen);
        RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 18, 18);
        SelectObject(hdc, old_brush);
        SelectObject(hdc, old_pen);
        DeleteObject(brush);
        DeleteObject(pen);
    }

    void PaintMainWindow(HWND hwnd) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rect{};
        GetClientRect(hwnd, &rect);

        HBRUSH background = CreateSolidBrush(RGB(14, 38, 22));
        FillRect(hdc, &rect, background);
        DeleteObject(background);

        RECT card = { 80, 60, rect.right - 80, rect.bottom - 100 };
        FillRoundedRect(hdc, card, RGB(28, 88, 48));

        const int cx = (card.left + card.right) / 2;
        const int cy = (card.top + card.bottom) / 2;

        HBRUSH leaf_brush = CreateSolidBrush(RGB(72, 210, 96));
        HPEN leaf_pen = CreatePen(PS_SOLID, 3, RGB(120, 255, 140));
        HGDIOBJ old_brush = SelectObject(hdc, leaf_brush);
        HGDIOBJ old_pen = SelectObject(hdc, leaf_pen);

        POINT leaf[] = {
            { cx, card.top + 36 },
            { cx - 70, cy + 10 },
            { cx - 20, cy + 55 },
            { cx, cy + 18 },
            { cx + 20, cy + 55 },
            { cx + 70, cy + 10 },
        };
        Polygon(hdc, leaf, static_cast<int>(sizeof(leaf) / sizeof(leaf[0])));

        SelectObject(hdc, old_brush);
        SelectObject(hdc, old_pen);
        DeleteObject(leaf_brush);
        DeleteObject(leaf_pen);

        HPEN stem_pen = CreatePen(PS_SOLID, 5, RGB(40, 140, 60));
        old_pen = SelectObject(hdc, stem_pen);
        MoveToEx(hdc, cx, cy + 20, nullptr);
        LineTo(hdc, cx, card.bottom - 28);
        SelectObject(hdc, old_pen);
        DeleteObject(stem_pen);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(210, 255, 220));

        RECT title_rect = rect;
        title_rect.top = card.bottom + 18;
        DrawTextW(hdc, kAppTitle, -1, &title_rect, DT_CENTER | DT_SINGLELINE);

        RECT hint_rect = rect;
        hint_rect.top = title_rect.top + 28;
        SetTextColor(hdc, RGB(120, 200, 130));
        DrawTextW(
            hdc,
            L"\u041F\u0440\u0438\u043B\u043E\u0436\u0435\u043D\u0438\u0435 \u0440\u0430\u0431\u043E\u0442\u0430\u0435\u0442 \u0432 \u0442\u0440\u0435\u0435",
            -1,
            &hint_rect,
            DT_CENTER | DT_SINGLELINE
        );

        EndPaint(hwnd, &ps);
    }

    LRESULT CALLBACK TrayWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        if (message == g_taskbar_created_message) {
            RecreateTrayIcon();
            return 0;
        }

        switch (message) {
        case WM_COMMAND: {
            const UINT command = LOWORD(w_param);
            if (command == kMenuOpen) {
                ShowMainWindow();
                return 0;
            }
            if (command == kMenuExit) {
                ExitApplication();
                return 0;
            }
            break;
        }

        case kWmTrayIcon:
            HandleTrayNotification(l_param);
            return 0;

        case WM_DESTROY:
            RemoveTrayIcon();
            g_tray_window = nullptr;
            if (!g_is_exiting) {
                PostQuitMessage(0);
            }
            return 0;

        default:
            break;
        }

        return DefWindowProcW(hwnd, message, w_param, l_param);
    }

    LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        switch (message) {
        case WM_COMMAND:
            if (LOWORD(w_param) == kMenuFileExit) {
                ExitApplication();
                return 0;
            }
            break;

        case WM_PAINT:
            PaintMainWindow(hwnd);
            return 0;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            g_main_window = nullptr;
            return 0;

        default:
            break;
        }

        return DefWindowProcW(hwnd, message, w_param, l_param);
    }

    bool RegisterWindowClasses() {
        if (!g_tray_icon) {
            g_tray_icon = LoadTrayIcon();
        }

        WNDCLASSEXW tray_class{};
        tray_class.cbSize = sizeof(tray_class);
        tray_class.lpfnWndProc = TrayWindowProc;
        tray_class.hInstance = g_instance;
        tray_class.lpszClassName = kTrayWindowClass;
        if (!RegisterClassExW(&tray_class)) {
            return false;
        }

        WNDCLASSEXW main_class{};
        main_class.cbSize = sizeof(main_class);
        main_class.lpfnWndProc = MainWindowProc;
        main_class.hInstance = g_instance;
        main_class.lpszClassName = kMainWindowClass;
        main_class.hIcon = g_tray_icon;
        main_class.hIconSm = g_tray_icon;
        main_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        main_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

        return RegisterClassExW(&main_class) != 0;
    }

    bool CreateTrayHostWindow() {
        g_tray_window = CreateWindowExW(
            0,
            kTrayWindowClass,
            kAppTitle,
            WS_OVERLAPPED,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            nullptr,
            g_instance,
            nullptr
        );

        return g_tray_window != nullptr;
    }

    bool CreateMainWindow() {
        g_main_window = CreateWindowExW(
            0,
            kMainWindowClass,
            kAppTitle,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            680,
            440,
            nullptr,
            CreateMainMenu(),
            g_instance,
            nullptr
        );

        return g_main_window != nullptr;
    }

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    g_instance = instance;

    g_single_instance_mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!g_single_instance_mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_single_instance_mutex) {
            CloseHandle(g_single_instance_mutex);
        }
        return 0;
    }

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool hidden_launch = argv ? IsHiddenLaunch(argc, argv) : false;
    if (argv) {
        LocalFree(argv);
    }

    g_taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");

    if (!RegisterWindowClasses()) {
        FreeTrayIconHandle();
        CloseHandle(g_single_instance_mutex);
        return 1;
    }

    if (!CreateTrayHostWindow()) {
        FreeTrayIconHandle();
        CloseHandle(g_single_instance_mutex);
        return 1;
    }

    if (!AddTrayIcon()) {
        DestroyWindow(g_tray_window);
        g_tray_window = nullptr;
        FreeTrayIconHandle();
        CloseHandle(g_single_instance_mutex);
        MessageBoxW(
            nullptr,
            L"\u041D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C \u0434\u043E\u0431\u0430\u0432\u0438\u0442\u044C \u0438\u043A\u043E\u043D\u043A\u0443 \u0432 \u0442\u0440\u0435\u0439.",
            kAppTitle,
            MB_ICONERROR | MB_OK
        );
        return 1;
    }

    if (!CreateMainWindow()) {
        ExitApplication();
        CloseHandle(g_single_instance_mutex);
        return 1;
    }

    if (!hidden_launch) {
        ShowWindow(g_main_window, show_command);
        UpdateWindow(g_main_window);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    FreeTrayIconHandle();

    if (g_single_instance_mutex) {
        ReleaseMutex(g_single_instance_mutex);
        CloseHandle(g_single_instance_mutex);
        g_single_instance_mutex = nullptr;
    }

    return static_cast<int>(msg.wParam);
}
