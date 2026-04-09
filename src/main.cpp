#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <strsafe.h>
#include <stdio.h>

#include "ymodem.h"

#define APP_TITLE L"充电器升级工具"
#define BAUDRATE 115200

#define IDC_PORT_COMBO 1001
#define IDC_CONNECT_BUTTON 1002
#define IDC_DOWNLOAD_BUTTON 1003
#define IDC_PROGRESS 1004
#define IDC_STATUS 1005
#define IDC_LOG 1006
#define IDC_INFO 1007

#define WM_APP_LOG (WM_APP + 1)
#define WM_APP_STATUS (WM_APP + 2)
#define WM_APP_PROGRESS (WM_APP + 3)
#define WM_APP_DONE (WM_APP + 4)
#define WM_APP_ERROR (WM_APP + 5)

static HINSTANCE g_instance = NULL;
static HWND g_main = NULL;
static HWND g_port_combo = NULL;
static HWND g_connect_button = NULL;
static HWND g_download_button = NULL;
static HWND g_progress = NULL;
static HWND g_status = NULL;
static HWND g_log = NULL;
static HWND g_info = NULL;

static HANDLE g_serial = INVALID_HANDLE_VALUE;
static HANDLE g_worker = NULL;
static volatile LONG g_stop_worker = 0;

struct WorkerContext {
    HANDLE serial;
    WCHAR firmware_path[MAX_PATH];
};

static void set_control_font(HWND hwnd) {
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

static WCHAR *heap_copy_text(const WCHAR *text) {
    size_t len;
    WCHAR *copy;

    if (!text) {
        text = L"";
    }
    len = wcslen(text) + 1;
    copy = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (copy) {
        StringCchCopyW(copy, len, text);
    }
    return copy;
}

static void post_text(UINT message, const WCHAR *text) {
    WCHAR *copy = heap_copy_text(text);
    if (copy) {
        PostMessageW(g_main, message, 0, (LPARAM)copy);
    }
}

static void post_logf(const WCHAR *format, ...) {
    WCHAR buffer[1400];
    va_list args;

    va_start(args, format);
    StringCchVPrintfW(buffer, ARRAYSIZE(buffer), format, args);
    va_end(args);
    post_text(WM_APP_LOG, buffer);
}

static void post_status(const WCHAR *text) {
    post_text(WM_APP_STATUS, text);
}

static void append_log(const WCHAR *text) {
    SYSTEMTIME st;
    WCHAR line[1600];
    int len;

    GetLocalTime(&st);
    StringCchPrintfW(line, ARRAYSIZE(line), L"[%02u:%02u:%02u] %s\r\n",
                     st.wHour, st.wMinute, st.wSecond, text ? text : L"");
    len = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, len, len);
    SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)line);
    SendMessageW(g_log, EM_SCROLLCARET, 0, 0);
}

static void format_win_error(DWORD error_code, WCHAR *buffer, size_t count) {
    DWORD written = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error_code,
        0,
        buffer,
        (DWORD)count,
        NULL);
    if (written == 0) {
        StringCchPrintfW(buffer, count, L"Windows 错误码 %lu", error_code);
    }
}

static void set_error(WCHAR *error, size_t count, const WCHAR *format, ...) {
    va_list args;

    va_start(args, format);
    StringCchVPrintfW(error, count, format, args);
    va_end(args);
}

static void fill_ports(void) {
    WCHAR name[16];
    WCHAR target[512];
    int found = 0;
    int i;

    SendMessageW(g_port_combo, CB_RESETCONTENT, 0, 0);
    for (i = 1; i <= 64; ++i) {
        StringCchPrintfW(name, ARRAYSIZE(name), L"COM%d", i);
        if (QueryDosDeviceW(name, target, ARRAYSIZE(target)) != 0) {
            SendMessageW(g_port_combo, CB_ADDSTRING, 0, (LPARAM)name);
            ++found;
        }
    }
    if (found == 0) {
        SendMessageW(g_port_combo, CB_ADDSTRING, 0, (LPARAM)L"COM1");
        SetWindowTextW(g_status, L"未自动发现串口，可检查设备管理器中的 COM 号");
    }
    SendMessageW(g_port_combo, CB_SETCURSEL, 0, 0);
}

static BOOL get_selected_port(WCHAR *port, size_t count) {
    int index = (int)SendMessageW(g_port_combo, CB_GETCURSEL, 0, 0);

    port[0] = L'\0';
    if (index == CB_ERR) {
        GetWindowTextW(g_port_combo, port, (int)count);
        return port[0] != L'\0';
    }
    SendMessageW(g_port_combo, CB_GETLBTEXT, index, (LPARAM)port);
    return port[0] != L'\0';
}

static BOOL open_serial_port(const WCHAR *port, HANDLE *serial_out, WCHAR *error, size_t error_count) {
    WCHAR device[64];
    HANDLE handle;
    DCB dcb;
    COMMTIMEOUTS timeouts;

    if (wcsncmp(port, L"\\\\.\\", 4) == 0) {
        StringCchCopyW(device, ARRAYSIZE(device), port);
    } else {
        StringCchPrintfW(device, ARRAYSIZE(device), L"\\\\.\\%s", port);
    }

    handle = CreateFileW(device, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        WCHAR win_error[256];
        format_win_error(GetLastError(), win_error, ARRAYSIZE(win_error));
        set_error(error, error_count, L"打开 %s 失败：%s", port, win_error);
        return FALSE;
    }

    SetupComm(handle, 8192, 8192);
    SecureZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb)) {
        format_win_error(GetLastError(), error, error_count);
        CloseHandle(handle);
        return FALSE;
    }

    dcb.BaudRate = BAUDRATE;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fAbortOnError = FALSE;

    if (!SetCommState(handle, &dcb)) {
        WCHAR win_error[256];
        format_win_error(GetLastError(), win_error, ARRAYSIZE(win_error));
        set_error(error, error_count, L"设置串口参数失败：%s", win_error);
        CloseHandle(handle);
        return FALSE;
    }

    SecureZeroMemory(&timeouts, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.ReadTotalTimeoutConstant = 200;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 3000;
    SetCommTimeouts(handle, &timeouts);
    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

    *serial_out = handle;
    return TRUE;
}

static void close_serial_port(void) {
    if (g_serial != INVALID_HANDLE_VALUE) {
        CloseHandle(g_serial);
        g_serial = INVALID_HANDLE_VALUE;
    }
}

static BOOL write_all(HANDLE handle, const BYTE *data, DWORD length, WCHAR *error, size_t error_count) {
    DWORD offset = 0;

    while (offset < length) {
        DWORD written = 0;
        if (!WriteFile(handle, data + offset, length - offset, &written, NULL)) {
            WCHAR win_error[256];
            format_win_error(GetLastError(), win_error, ARRAYSIZE(win_error));
            set_error(error, error_count, L"串口写入失败：%s", win_error);
            return FALSE;
        }
        if (written == 0) {
            set_error(error, error_count, L"串口写入超时");
            return FALSE;
        }
        offset += written;
    }
    return TRUE;
}

static void worker_log(void *user, const WCHAR *message) {
    (void)user;
    post_text(WM_APP_LOG, message);
}

static void worker_progress(void *user, DWORD percent) {
    (void)user;
    PostMessageW(g_main, WM_APP_PROGRESS, (WPARAM)percent, 0);
}

static void decode_and_log_boot_output(const BYTE *data, DWORD length) {
    int chars;
    WCHAR *text;

    if (length == 0) {
        return;
    }

    chars = MultiByteToWideChar(936, 0, (const char *)data, (int)length, NULL, 0);
    if (chars <= 0) {
        chars = MultiByteToWideChar(CP_ACP, 0, (const char *)data, (int)length, NULL, 0);
    }
    if (chars <= 0) {
        post_logf(L"已检测到充电器串口输出");
        return;
    }

    text = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (chars + 1) * sizeof(WCHAR));
    if (!text) {
        return;
    }
    if (MultiByteToWideChar(936, 0, (const char *)data, (int)length, text, chars) <= 0) {
        MultiByteToWideChar(CP_ACP, 0, (const char *)data, (int)length, text, chars);
    }
    post_logf(L"充电器输出: %s", text);
    HeapFree(GetProcessHeap(), 0, text);
}

static BOOL wait_for_boot_output(HANDLE serial, WCHAR *error, size_t error_count) {
    BYTE buffer[4096];
    DWORD total = 0;

    while (InterlockedCompareExchange(&g_stop_worker, 0, 0) == 0) {
        BYTE chunk[256];
        DWORD read_count = 0;

        if (!ReadFile(serial, chunk, sizeof(chunk), &read_count, NULL)) {
            WCHAR win_error[256];
            format_win_error(GetLastError(), win_error, ARRAYSIZE(win_error));
            set_error(error, error_count, L"串口读取失败：%s", win_error);
            return FALSE;
        }
        if (read_count == 0) {
            continue;
        }

        if (read_count > sizeof(buffer)) {
            read_count = sizeof(buffer);
        }
        CopyMemory(buffer, chunk, read_count);
        total = read_count;

        Sleep(200);
        while (total < sizeof(buffer)) {
            DWORD more = 0;
            if (!ReadFile(serial, buffer + total, sizeof(buffer) - total, &more, NULL)) {
                break;
            }
            if (more == 0) {
                break;
            }
            total += more;
        }

        decode_and_log_boot_output(buffer, total);
        return TRUE;
    }

    set_error(error, error_count, L"下载任务已取消");
    return FALSE;
}

static DWORD WINAPI upgrade_thread_proc(LPVOID param) {
    WorkerContext *context = (WorkerContext *)param;
    WCHAR error[512];
    const BYTE update_command[] = {'u', 'p', 'd', 'a', 't', 'e', '\r', '\n'};
    BOOL ok;

    error[0] = L'\0';
    PurgeComm(context->serial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    post_status(L"等待充电器上电...");
    post_logf(L"等待充电器上电串口输出");
    ok = wait_for_boot_output(context->serial, error, ARRAYSIZE(error));

    if (ok) {
        post_status(L"已检测到充电器，发送 update...");
        post_logf(L"发送命令: update");
        ok = write_all(context->serial, update_command, sizeof(update_command), error, ARRAYSIZE(error));
    }

    if (ok) {
        ok = ymodem_send_file(context->serial, context->firmware_path, &g_stop_worker,
                              worker_log, worker_progress, NULL, error, ARRAYSIZE(error));
    }

    if (ok) {
        PostMessageW(g_main, WM_APP_DONE, 1, 0);
    } else {
        WCHAR *copy = heap_copy_text(error[0] ? error : L"升级失败");
        PostMessageW(g_main, WM_APP_ERROR, 0, (LPARAM)copy);
    }

    HeapFree(GetProcessHeap(), 0, context);
    return 0;
}

static void set_connected_ui(BOOL connected) {
    SetWindowTextW(g_connect_button, connected ? L"断开" : L"连接");
    EnableWindow(g_download_button, connected);
    EnableWindow(g_port_combo, !connected);
}

static void connect_or_disconnect(void) {
    if (g_serial == INVALID_HANDLE_VALUE) {
        WCHAR port[64];
        WCHAR error[512];

        if (!get_selected_port(port, ARRAYSIZE(port))) {
            MessageBoxW(g_main, L"请先选择 COM 口", APP_TITLE, MB_ICONWARNING);
            return;
        }
        if (!open_serial_port(port, &g_serial, error, ARRAYSIZE(error))) {
            MessageBoxW(g_main, error, L"连接失败", MB_ICONERROR);
            return;
        }
        set_connected_ui(TRUE);
        SendMessageW(g_progress, PBM_SETPOS, 0, 0);
        SetWindowTextW(g_status, L"已连接");
        append_log(L"串口已连接，参数 115200 / 8N1");
    } else {
        close_serial_port();
        set_connected_ui(FALSE);
        SendMessageW(g_progress, PBM_SETPOS, 0, 0);
        SetWindowTextW(g_status, L"未连接");
        append_log(L"串口已断开");
    }
}

static void start_download(void) {
    OPENFILENAMEW ofn;
    WCHAR file_path[MAX_PATH];
    WorkerContext *context;
    DWORD thread_id;

    if (g_serial == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_main, L"请先连接串口", APP_TITLE, MB_ICONWARNING);
        return;
    }
    if (g_worker != NULL) {
        MessageBoxW(g_main, L"当前已有下载任务在执行", APP_TITLE, MB_ICONINFORMATION);
        return;
    }

    ZeroMemory(file_path, sizeof(file_path));
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = L"固件文件 (*.bin)\0*.bin\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file_path;
    ofn.nMaxFile = ARRAYSIZE(file_path);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle = L"选择充电器固件";
    if (!GetOpenFileNameW(&ofn)) {
        return;
    }

    context = (WorkerContext *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WorkerContext));
    if (!context) {
        MessageBoxW(g_main, L"内存不足", APP_TITLE, MB_ICONERROR);
        return;
    }
    context->serial = g_serial;
    StringCchCopyW(context->firmware_path, ARRAYSIZE(context->firmware_path), file_path);

    InterlockedExchange(&g_stop_worker, 0);
    g_worker = CreateThread(NULL, 0, upgrade_thread_proc, context, 0, &thread_id);
    if (!g_worker) {
        HeapFree(GetProcessHeap(), 0, context);
        MessageBoxW(g_main, L"创建下载线程失败", APP_TITLE, MB_ICONERROR);
        return;
    }

    EnableWindow(g_download_button, FALSE);
    EnableWindow(g_connect_button, FALSE);
    SendMessageW(g_progress, PBM_SETPOS, 0, 0);
    SetWindowTextW(g_status, L"等待充电器上电...");
    append_log(L"已选择固件，等待充电器上电");
}

static void layout_controls(HWND hwnd) {
    RECT rc;
    int width;
    int height;
    int margin = 14;
    int top = 14;
    int combo_w = 150;
    int button_w = 86;
    int row_h = 26;

    GetClientRect(hwnd, &rc);
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;

    MoveWindow(g_port_combo, margin, top, combo_w, 300, TRUE);
    MoveWindow(g_connect_button, margin + combo_w + 10, top, button_w, row_h, TRUE);
    MoveWindow(g_download_button, margin + combo_w + 10 + button_w + 8, top, button_w, row_h, TRUE);
    MoveWindow(g_info, width - 210, top + 4, 196, row_h, TRUE);
    MoveWindow(g_progress, margin, 54, width - margin * 2, 20, TRUE);
    MoveWindow(g_status, margin, 82, width - margin * 2, 22, TRUE);
    MoveWindow(g_log, margin, 112, width - margin * 2, height - 126, TRUE);
}

static void create_controls(HWND hwnd) {
    DWORD edit_style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                       ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY;

    g_port_combo = CreateWindowExW(0, WC_COMBOBOXW, NULL,
                                   WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL,
                                   0, 0, 0, 0, hwnd, (HMENU)IDC_PORT_COMBO, g_instance, NULL);
    g_connect_button = CreateWindowExW(0, WC_BUTTONW, L"连接",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       0, 0, 0, 0, hwnd, (HMENU)IDC_CONNECT_BUTTON, g_instance, NULL);
    g_download_button = CreateWindowExW(0, WC_BUTTONW, L"下载",
                                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
                                        0, 0, 0, 0, hwnd, (HMENU)IDC_DOWNLOAD_BUTTON, g_instance, NULL);
    g_progress = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
                                 WS_CHILD | WS_VISIBLE,
                                 0, 0, 0, 0, hwnd, (HMENU)IDC_PROGRESS, g_instance, NULL);
    g_status = CreateWindowExW(0, WC_STATICW, L"未连接",
                               WS_CHILD | WS_VISIBLE,
                               0, 0, 0, 0, hwnd, (HMENU)IDC_STATUS, g_instance, NULL);
    g_info = CreateWindowExW(0, WC_STATICW, L"115200 / 8N1 / GB2312",
                             WS_CHILD | WS_VISIBLE | SS_RIGHT,
                             0, 0, 0, 0, hwnd, (HMENU)IDC_INFO, g_instance, NULL);
    g_log = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, NULL,
                            edit_style,
                            0, 0, 0, 0, hwnd, (HMENU)IDC_LOG, g_instance, NULL);

    set_control_font(g_port_combo);
    set_control_font(g_connect_button);
    set_control_font(g_download_button);
    set_control_font(g_status);
    set_control_font(g_info);
    set_control_font(g_log);

    SendMessageW(g_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    fill_ports();
    append_log(L"请选择串口后点击连接");
}

static void stop_worker_if_needed(void) {
    if (g_worker) {
        InterlockedExchange(&g_stop_worker, 1);
        WaitForSingleObject(g_worker, INFINITE);
        CloseHandle(g_worker);
        g_worker = NULL;
    }
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        create_controls(hwnd);
        return 0;

    case WM_SIZE:
        layout_controls(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_CONNECT_BUTTON:
            connect_or_disconnect();
            return 0;
        case IDC_DOWNLOAD_BUTTON:
            start_download();
            return 0;
        default:
            break;
        }
        break;

    case WM_APP_LOG:
        append_log((const WCHAR *)lparam);
        HeapFree(GetProcessHeap(), 0, (void *)lparam);
        return 0;

    case WM_APP_STATUS:
        SetWindowTextW(g_status, (const WCHAR *)lparam);
        HeapFree(GetProcessHeap(), 0, (void *)lparam);
        return 0;

    case WM_APP_PROGRESS:
        SendMessageW(g_progress, PBM_SETPOS, (int)wparam, 0);
        return 0;

    case WM_APP_DONE:
        if (g_worker) {
            CloseHandle(g_worker);
            g_worker = NULL;
        }
        EnableWindow(g_download_button, TRUE);
        EnableWindow(g_connect_button, TRUE);
        SetWindowTextW(g_status, L"升级完成");
        append_log(L"升级完成");
        return 0;

    case WM_APP_ERROR:
        if (g_worker) {
            CloseHandle(g_worker);
            g_worker = NULL;
        }
        EnableWindow(g_download_button, TRUE);
        EnableWindow(g_connect_button, TRUE);
        SetWindowTextW(g_status, L"升级失败");
        append_log((const WCHAR *)lparam);
        MessageBoxW(hwnd, (const WCHAR *)lparam, L"升级失败", MB_ICONERROR);
        HeapFree(GetProcessHeap(), 0, (void *)lparam);
        return 0;

    case WM_CLOSE:
        stop_worker_if_needed();
        close_serial_port();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show_command) {
    INITCOMMONCONTROLSEX icc;
    WNDCLASSW wc;
    MSG msg;

    (void)previous;
    (void)command_line;
    g_instance = instance;

    ZeroMemory(&icc, sizeof(icc));
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ChargerUpdaterWindow";
    if (!RegisterClassW(&wc)) {
        return 1;
    }

    g_main = CreateWindowExW(0, wc.lpszClassName, APP_TITLE,
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 720, 460,
                             NULL, NULL, instance, NULL);
    if (!g_main) {
        return 1;
    }

    ShowWindow(g_main, show_command);
    UpdateWindow(g_main);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
