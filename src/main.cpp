#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <rpc.h>
#include <sddl.h>
#include <dwmapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "../resources/resource.h"
#include "common.h"
#include "Pract2Control_h.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "dwmapi.lib")

handle_t Pract2Control_IfHandle = nullptr;

namespace {

    constexpr wchar_t kWindowClassName[] = L"Pract2ServiceTrayWindowClass";
    constexpr wchar_t kWindowTitle[] = L"\u0422\u0440\u0435\u0439";
    constexpr wchar_t kMutexName[] = L"Local\\Pract2ServiceTrayApp.SingleInstance";

    constexpr COLORREF kColorBackground = RGB(11, 29, 18);
    constexpr COLORREF kColorPanel = RGB(29, 90, 50);
    constexpr COLORREF kColorAccent = RGB(74, 222, 128);

    constexpr UINT kTrayCallbackMessage = WM_APP + 1;
    constexpr UINT_PTR kTrayIconId = 1;

    constexpr UINT kMenuOpen = 1001;
    constexpr UINT kMenuExit = 1002;

    HINSTANCE g_instance = nullptr;
    HWND g_main_window = nullptr;
    HBRUSH g_background_brush = nullptr;
    NOTIFYICONDATAW g_tray_icon{};
    UINT g_taskbar_created_message = 0;
    HANDLE g_single_instance_mutex = nullptr;
    bool g_is_exiting = false;

    std::wstring ToLower(std::wstring text) {
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(towlower(ch));
            });

        return text;
    }

    bool HasArgument(const wchar_t* expected) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

        if (!argv) {
            return false;
        }

        bool found = false;

        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], expected) == 0) {
                found = true;
                break;
            }
        }

        LocalFree(argv);
        return found;
    }

    int RunSecureStopConfirmation() {
        /*
            Secure-stop confirmation helper.

            This helper is started by the service in the active user's session:
                TrayWin32App.exe --secure-stop-confirm

            It creates a separate restricted private desktop, switches the user
            to that desktop, shows a blocking confirmation dialog, then switches
            back to the original input desktop.

            This is not the system Winlogon/UAC secure desktop, because ordinary
            user-mode applications cannot display UI on the real Winlogon desktop.
            For this assignment this is the closest practical implementation:
            a separate private desktop with a restricted DACL.
        */

        HDESK original_desktop = OpenInputDesktop(
            0,
            FALSE,
            DESKTOP_SWITCHDESKTOP |
            DESKTOP_READOBJECTS |
            DESKTOP_WRITEOBJECTS |
            DESKTOP_CREATEWINDOW
        );

        if (!original_desktop) {
            return 1;
        }

        wchar_t desktop_name[128]{};

        wsprintfW(
            desktop_name,
            L"Pract2StopConfirmationDesktop-%lu",
            GetCurrentProcessId()
        );

        PSECURITY_DESCRIPTOR security_descriptor = nullptr;

        /*
            Desktop DACL:
            - SY = LocalSystem full access
            - OW = object owner full access

            The helper process is launched with the active user's token, so the
            object owner is the active user.
        */
        constexpr wchar_t kDesktopSecurityDescriptor[] =
            L"D:P"
            L"(A;;GA;;;SY)"
            L"(A;;GA;;;OW)";

        SECURITY_ATTRIBUTES security_attributes{};
        security_attributes.nLength = sizeof(security_attributes);
        security_attributes.bInheritHandle = FALSE;
        security_attributes.lpSecurityDescriptor = nullptr;

        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kDesktopSecurityDescriptor,
            SDDL_REVISION_1,
            &security_descriptor,
            nullptr
        )) {
            security_attributes.lpSecurityDescriptor = security_descriptor;
        }

        HDESK confirmation_desktop = CreateDesktopW(
            desktop_name,
            nullptr,
            nullptr,
            0,
            GENERIC_ALL,
            security_attributes.lpSecurityDescriptor ? &security_attributes : nullptr
        );

        if (security_descriptor) {
            LocalFree(security_descriptor);
            security_descriptor = nullptr;
        }

        if (!confirmation_desktop) {
            CloseDesktop(original_desktop);
            return 1;
        }

        const BOOL switched_to_confirmation = SwitchDesktop(confirmation_desktop);
        const BOOL thread_desktop_set = SetThreadDesktop(confirmation_desktop);

        int result = IDNO;

        if (switched_to_confirmation && thread_desktop_set) {
            result = MessageBoxW(
                nullptr,
                L"Stop Pract2Service and close all tray applications?",
                L"Pract2Service",
                MB_YESNO |
                MB_ICONWARNING |
                MB_TOPMOST |
                MB_SETFOREGROUND |
                MB_SYSTEMMODAL
            );
        }

        SwitchDesktop(original_desktop);
        SetThreadDesktop(original_desktop);

        CloseDesktop(confirmation_desktop);
        CloseDesktop(original_desktop);

        if (!switched_to_confirmation || !thread_desktop_set) {
            return 1;
        }

        return result == IDYES ? 0 : 1;
    }

    bool HasHiddenStartupArgument() {
        return HasArgument(L"--hidden") ||
            HasArgument(L"--no-window") ||
            HasArgument(L"/hidden");
    }

    void AppendTrayLog(const wchar_t* message) {
        try {
            const std::filesystem::path log_path =
                std::filesystem::path(L"C:\\ProgramData") / pract2::kLogFolderName / L"tray.log";
            std::filesystem::create_directories(log_path.parent_path());
            std::wofstream log(log_path, std::ios::app);
            if (log) {
                log << message << L'\n';
            }
        } catch (...) {
        }
    }

    bool QueryServiceState(DWORD* state) {
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);

        if (!scm) {
            return false;
        }

        SC_HANDLE service = OpenServiceW(
            scm,
            pract2::kServiceName,
            SERVICE_QUERY_STATUS | SERVICE_START
        );

        if (!service) {
            CloseServiceHandle(scm);
            return false;
        }

        SERVICE_STATUS_PROCESS status{};
        DWORD bytes_needed = 0;

        const BOOL ok = QueryServiceStatusEx(
            service,
            SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status),
            sizeof(status),
            &bytes_needed
        );

        if (ok && state) {
            *state = status.dwCurrentState;
        }

        CloseServiceHandle(service);
        CloseServiceHandle(scm);

        return ok != FALSE;
    }

    bool QueryServiceStatusProcess(SERVICE_STATUS_PROCESS* status) {
        if (!status) {
            return false;
        }

        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);

        if (!scm) {
            return false;
        }

        SC_HANDLE service = OpenServiceW(
            scm,
            pract2::kServiceName,
            SERVICE_QUERY_STATUS | SERVICE_START
        );

        if (!service) {
            CloseServiceHandle(scm);
            return false;
        }

        DWORD bytes_needed = 0;
        const BOOL ok = QueryServiceStatusEx(
            service,
            SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(status),
            sizeof(*status),
            &bytes_needed
        );

        CloseServiceHandle(service);
        CloseServiceHandle(scm);

        return ok != FALSE;
    }

    DWORD GetServiceProcessId() {
        SERVICE_STATUS_PROCESS status{};

        if (!QueryServiceStatusProcess(&status)) {
            return 0;
        }

        return status.dwProcessId;
    }

    bool StartServiceAndWaitRunning() {
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);

        if (!scm) {
            return false;
        }

        SC_HANDLE service = OpenServiceW(
            scm,
            pract2::kServiceName,
            SERVICE_QUERY_STATUS | SERVICE_START
        );

        if (!service) {
            CloseServiceHandle(scm);
            return false;
        }

        SERVICE_STATUS_PROCESS status{};
        DWORD bytes_needed = 0;

        if (!QueryServiceStatusEx(
            service,
            SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status),
            sizeof(status),
            &bytes_needed
        )) {
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            return false;
        }

        if (status.dwCurrentState == SERVICE_STOPPED) {
            StartServiceW(service, 0, nullptr);
        }

        bool running = false;

        for (int i = 0; i < 50; ++i) {
            if (!QueryServiceStatusEx(
                service,
                SC_STATUS_PROCESS_INFO,
                reinterpret_cast<LPBYTE>(&status),
                sizeof(status),
                &bytes_needed
            )) {
                break;
            }

            if (status.dwCurrentState == SERVICE_RUNNING) {
                running = true;
                break;
            }

            Sleep(200);
        }

        CloseServiceHandle(service);
        CloseServiceHandle(scm);

        return running;
    }

    bool IsServiceStopped() {
        DWORD state = 0;
        return QueryServiceState(&state) && state == SERVICE_STOPPED;
    }

    bool EnsureServiceStartedIfNeeded() {
        SERVICE_STATUS_PROCESS status{};

        if (!QueryServiceStatusProcess(&status)) {
            return false;
        }

        if (status.dwCurrentState == SERVICE_RUNNING) {
            return true;
        }

        if (status.dwCurrentState == SERVICE_STOPPED ||
            status.dwCurrentState == SERVICE_STOP_PENDING ||
            status.dwCurrentState == SERVICE_START_PENDING) {
            return StartServiceAndWaitRunning();
        }

        return false;
    }

    DWORD GetParentProcessIdOf(DWORD process_id) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        if (snapshot == INVALID_HANDLE_VALUE) {
            return 0;
        }

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);

        DWORD parent_pid = 0;

        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID == process_id) {
                    parent_pid = entry.th32ParentProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return parent_pid;
    }

    DWORD GetParentProcessId() {
        return GetParentProcessIdOf(GetCurrentProcessId());
    }

    std::wstring GetProcessImageBaseName(DWORD pid) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        if (snapshot == INVALID_HANDLE_VALUE) {
            return L"";
        }

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);

        std::wstring name;

        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID == pid) {
                    name = entry.szExeFile;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);

        return name;
    }

    bool IsParentServiceProcess() {
        if (!HasArgument(L"--service-child")) {
            return false;
        }

        const DWORD service_pid = GetServiceProcessId();
        DWORD walk_pid = GetParentProcessId();
        for (int depth = 0; depth < 16 && walk_pid != 0; ++depth) {
            if (service_pid != 0 && walk_pid == service_pid) {
                return true;
            }
            if (ToLower(GetProcessImageBaseName(walk_pid)) == ToLower(pract2::kServiceExeName)) {
                return true;
            }
            walk_pid = GetParentProcessIdOf(walk_pid);
        }

        DWORD state = 0;
        if (QueryServiceState(&state) && state == SERVICE_RUNNING) {
            return true;
        }

        // Launched only by Pract2Service with --service-child.
        return true;
    }

    void RequestServiceStop() {
        RPC_WSTR string_binding = nullptr;

        RPC_STATUS status = RpcStringBindingComposeW(
            nullptr,
            reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(L"ncalrpc")),
            nullptr,
            reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(pract2::kRpcEndpoint)),
            nullptr,
            &string_binding
        );

        if (status != RPC_S_OK) {
            return;
        }

        status = RpcBindingFromStringBindingW(
            string_binding,
            &Pract2Control_IfHandle
        );

        RpcStringFreeW(&string_binding);

        if (status != RPC_S_OK) {
            return;
        }

        RpcTryExcept
        {
            RpcStopService();
        }
            RpcExcept(1)
        {
        }
        RpcEndExcept

            RpcBindingFree(&Pract2Control_IfHandle);
        Pract2Control_IfHandle = nullptr;
    }

    void ShowMainWindow() {
        if (!g_main_window) {
            return;
        }

        ShowWindow(g_main_window, SW_SHOWNORMAL);
        SetForegroundWindow(g_main_window);
    }

    void RemoveTrayIcon() {
        if (g_tray_icon.cbSize != 0) {
            Shell_NotifyIconW(NIM_DELETE, &g_tray_icon);
            g_tray_icon = {};
        }
    }

    void AddTrayIcon(HWND hwnd) {
        g_tray_icon = {};
        g_tray_icon.cbSize = sizeof(g_tray_icon);
        g_tray_icon.hWnd = hwnd;
        g_tray_icon.uID = kTrayIconId;
        g_tray_icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        g_tray_icon.uCallbackMessage = kTrayCallbackMessage;
        g_tray_icon.hIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_TRAY_ICON));
        if (!g_tray_icon.hIcon) {
            g_tray_icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        }

        wcscpy_s(g_tray_icon.szTip, kWindowTitle);

        if (!Shell_NotifyIconW(NIM_ADD, &g_tray_icon)) {
            AppendTrayLog((L"WARN Shell_NotifyIcon ADD err=" + std::to_wstring(GetLastError())).c_str());
        } else {
            AppendTrayLog(L"OK tray icon added");
        }

        g_tray_icon.uVersion = NOTIFYICON_VERSION_4;

        Shell_NotifyIconW(NIM_SETVERSION, &g_tray_icon);
    }

    void ExitApplication() {
        g_is_exiting = true;
        RemoveTrayIcon();
        PostQuitMessage(0);
    }

    void StopServiceAndExit() {
        RequestServiceStop();

        /*
            Do not close the GUI immediately.

            If the user confirms the service stop on the private desktop,
            the service will terminate all launched TrayWin32App.exe processes.

            If the user clicks No or the confirmation cannot be shown,
            the tray app remains running.
        */
    }

    void ShowTrayMenu(HWND hwnd) {
        POINT cursor_position{};
        GetCursorPos(&cursor_position);

        HMENU menu = CreatePopupMenu();

        if (!menu) {
            return;
        }

        AppendMenuW(menu, MF_STRING, kMenuOpen, L"\u041E\u0442\u043A\u0440\u044B\u0442\u044C");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuExit, L"\u0412\u044B\u0445\u043E\u0434");

        SetForegroundWindow(hwnd);

        const UINT command = TrackPopupMenu(
            menu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
            cursor_position.x,
            cursor_position.y,
            0,
            hwnd,
            nullptr
        );

        DestroyMenu(menu);

        switch (command) {
        case kMenuOpen:
            ShowMainWindow();
            break;

        case kMenuExit:
            StopServiceAndExit();
            break;

        default:
            break;
        }
    }

    HMENU CreateMainMenu() {
        HMENU menu_bar = CreateMenu();
        HMENU file_menu = CreatePopupMenu();

        AppendMenuW(file_menu, MF_STRING, kMenuExit, L"\u0412\u044B\u0445\u043E\u0434");
        AppendMenuW(menu_bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file_menu), L"\u0424\u0430\u0439\u043B");

        return menu_bar;
    }

    void FillRoundRect(HDC hdc, const RECT& rect, int radius, COLORREF color) {
        HBRUSH brush = CreateSolidBrush(color);
        HPEN pen = CreatePen(PS_NULL, 0, color);
        HGDIOBJ old_brush = SelectObject(hdc, brush);
        HGDIOBJ old_pen = SelectObject(hdc, pen);
        RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
        SelectObject(hdc, old_brush);
        SelectObject(hdc, old_pen);
        DeleteObject(brush);
        DeleteObject(pen);
    }

    HFONT CreateUiFont(int height, bool bold) {
        return CreateFontW(
            height,
            0,
            0,
            0,
            bold ? FW_BOLD : FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
    }

    void DrawLeafIcon(HDC hdc, int center_x, int center_y) {
        const int half_width = 34;
        const int half_height = 38;

        POINT diamond[4] = {
            {center_x, center_y - half_height},
            {center_x + half_width, center_y},
            {center_x, center_y + half_height},
            {center_x - half_width, center_y},
        };

        HGDIOBJ hollow_brush = GetStockObject(HOLLOW_BRUSH);
        HPEN outline_pen = CreatePen(PS_SOLID, 4, kColorAccent);
        HGDIOBJ old_brush = SelectObject(hdc, hollow_brush);
        HGDIOBJ old_pen = SelectObject(hdc, outline_pen);

        Polygon(hdc, diamond, 4);

        MoveToEx(hdc, center_x, center_y + half_height, nullptr);
        LineTo(hdc, center_x, center_y + half_height + 18);

        SelectObject(hdc, old_brush);
        SelectObject(hdc, old_pen);
        DeleteObject(outline_pen);
    }

    void PaintMainWindow(HWND hwnd) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT client{};
        GetClientRect(hwnd, &client);

        FillRect(hdc, &client, g_background_brush);

        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;

        const int panel_width = client_width - 80;
        const int panel_height = 130;
        const int panel_left = (client_width - panel_width) / 2;
        const int panel_top = (client_height - panel_height) / 2 - 36;

        RECT panel_rect{
            panel_left,
            panel_top,
            panel_left + panel_width,
            panel_top + panel_height,
        };

        FillRoundRect(hdc, panel_rect, 28, kColorPanel);

        const int icon_center_x = (panel_rect.left + panel_rect.right) / 2;
        const int icon_center_y = (panel_rect.top + panel_rect.bottom) / 2;
        DrawLeafIcon(hdc, icon_center_x, icon_center_y);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, kColorAccent);

        HFONT title_font = CreateUiFont(-28, true);
        HFONT subtitle_font = CreateUiFont(-15, false);
        HGDIOBJ old_font = SelectObject(hdc, title_font);

        RECT title_rect{
            0,
            panel_rect.bottom + 18,
            client_width,
            panel_rect.bottom + 58,
        };
        DrawTextW(hdc, kWindowTitle, -1, &title_rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        SelectObject(hdc, subtitle_font);
        RECT subtitle_rect{
            0,
            title_rect.bottom + 4,
            client_width,
            title_rect.bottom + 34,
        };
        DrawTextW(
            hdc,
            L"\u041F\u0440\u0438\u043B\u043E\u0436\u0435\u043D\u0438\u0435 \u0440\u0430\u0431\u043E\u0442\u0430\u0435\u0442 \u0432 \u0442\u0440\u0435\u0435",
            -1,
            &subtitle_rect,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        SelectObject(hdc, old_font);
        DeleteObject(title_font);
        DeleteObject(subtitle_font);

        EndPaint(hwnd, &ps);
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        if (message == g_taskbar_created_message) {
            AddTrayIcon(hwnd);
            return 0;
        }

        switch (message) {
        case WM_CREATE:
            AddTrayIcon(hwnd);
            return 0;

        case kTrayCallbackMessage:
            switch (LOWORD(l_param)) {
            case WM_LBUTTONUP:
            case NIN_SELECT:
            case NIN_KEYSELECT:
                ShowMainWindow();
                return 0;

            case WM_RBUTTONUP:
            case WM_CONTEXTMENU:
                ShowTrayMenu(hwnd);
                return 0;

            default:
                return 0;
            }

        case WM_COMMAND:
            switch (LOWORD(w_param)) {
            case kMenuExit:
                StopServiceAndExit();
                return 0;

            default:
                break;
            }

            break;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            PaintMainWindow(hwnd);
            return 0;

        case WM_CLOSE:
            if (!g_is_exiting) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }

            break;

        case WM_DESTROY:
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }

        return DefWindowProcW(hwnd, message, w_param, l_param);
    }

    bool RegisterMainWindowClass() {
        if (!g_background_brush) {
            g_background_brush = CreateSolidBrush(kColorBackground);
        }

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = WindowProc;
        window_class.hInstance = g_instance;
        window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = g_background_brush;
        window_class.lpszClassName = kWindowClassName;
        window_class.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

        return RegisterClassExW(&window_class) != 0;
    }

    HWND CreateMainWindow() {
        return CreateWindowExW(
            0,
            kWindowClassName,
            kWindowTitle,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            480,
            320,
            nullptr,
            CreateMainMenu(),
            g_instance,
            nullptr
        );
    }

}

extern "C" void* __RPC_USER midl_user_allocate(size_t size) {
    return HeapAlloc(GetProcessHeap(), 0, size);
}

extern "C" void __RPC_USER midl_user_free(void* pointer) {
    HeapFree(GetProcessHeap(), 0, pointer);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    g_instance = instance;

    if (HasArgument(L"--secure-stop-confirm")) {
        return RunSecureStopConfirmation();
    }

    const bool launched_by_service = HasArgument(L"--service-child");

    if (!launched_by_service) {
        if (!EnsureServiceStartedIfNeeded()) {
            AppendTrayLog(L"exit: service not available");
            return 0;
        }

        if (IsServiceStopped()) {
            AppendTrayLog(L"exit: service stopped");
            return 0;
        }

        // Manual start: service is running, but tray must be spawned only by the service.
        AppendTrayLog(L"exit: manual launch (service bootstrap only)");
        return 0;
    }

    if (!IsParentServiceProcess()) {
        AppendTrayLog(L"exit: parent is not service");
        return 0;
    }

    g_single_instance_mutex = CreateMutexW(nullptr, TRUE, kMutexName);

    if (!g_single_instance_mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_single_instance_mutex) {
            CloseHandle(g_single_instance_mutex);
        }

        AppendTrayLog(L"exit: already running for this user");
        return 0;
    }

    AppendTrayLog(L"start: tray app running");

    g_taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");

    if (!RegisterMainWindowClass()) {
        AppendTrayLog((L"exit: RegisterClass err=" + std::to_wstring(GetLastError())).c_str());
        CloseHandle(g_single_instance_mutex);
        return 1;
    }

    g_main_window = CreateMainWindow();

    if (g_main_window) {
        const BOOL use_dark_mode = TRUE;
        DwmSetWindowAttribute(
            g_main_window,
            DWMWA_USE_IMMERSIVE_DARK_MODE,
            &use_dark_mode,
            sizeof(use_dark_mode));
    }

    if (!g_main_window) {
        AppendTrayLog((L"exit: CreateWindow err=" + std::to_wstring(GetLastError())).c_str());
        CloseHandle(g_single_instance_mutex);
        return 1;
    }

    if (!HasHiddenStartupArgument()) {
        ShowWindow(g_main_window, show_command);
        UpdateWindow(g_main_window);
    }

    MSG message{};

    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_single_instance_mutex) {
        ReleaseMutex(g_single_instance_mutex);
        CloseHandle(g_single_instance_mutex);
    }

    return static_cast<int>(message.wParam);
}
