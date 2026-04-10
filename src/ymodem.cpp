#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#include "ymodem.h"

#include <strsafe.h>
#include <stdio.h>
#include <string.h>

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define CRC_REQUEST 'C'
#define CPMEOF 0x1A

#define PACKET_128 128
#define PACKET_1K 1024

#define INITIAL_RECEIVER_TIMEOUT_MS 20000

static void set_error(WCHAR *error, size_t count, const WCHAR *format, ...) {
    va_list args;
    va_start(args, format);
    StringCchVPrintfW(error, count, format, args);
    va_end(args);
}

static void log_text(YmodemLogFn log_fn, void *user, const WCHAR *text) {
    if (log_fn) {
        log_fn(user, text);
    }
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

static WORD crc16_ccitt(const BYTE *data, DWORD length) {
    WORD crc = 0;
    DWORD i;
    int bit;

    for (i = 0; i < length; ++i) {
        crc ^= (WORD)data[i] << 8;
        for (bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = (WORD)((crc << 1) ^ 0x1021);
            } else {
                crc = (WORD)(crc << 1);
            }
        }
    }
    return crc;
}

static int read_byte_until(HANDLE handle,
                           DWORD timeout_ms,
                           volatile LONG *stop_flag,
                           BYTE *value,
                           WCHAR *error,
                           size_t error_count) {
    DWORD start = GetTickCount();

    while (GetTickCount() - start < timeout_ms) {
        BYTE byte_value = 0;
        DWORD read_count = 0;

        if (stop_flag && InterlockedCompareExchange(stop_flag, 0, 0) != 0) {
            set_error(error, error_count, L"下载任务已取消");
            return -2;
        }
        if (!ReadFile(handle, &byte_value, 1, &read_count, NULL)) {
            WCHAR win_error[256];
            format_win_error(GetLastError(), win_error, ARRAYSIZE(win_error));
            set_error(error, error_count, L"串口读取失败：%s", win_error);
            return -1;
        }
        if (read_count == 1) {
            *value = byte_value;
            return 1;
        }
    }
    return 0;
}

static int wait_control_response(HANDLE handle,
                                 DWORD timeout_ms,
                                 BOOL want_receiver,
                                 volatile LONG *stop_flag,
                                 WCHAR *error,
                                 size_t error_count) {
    DWORD start = GetTickCount();

    while (GetTickCount() - start < timeout_ms) {
        BYTE value = 0;
        int result = read_byte_until(handle, 500, stop_flag, &value, error, error_count);
        if (result < 0) {
            return result;
        }
        if (result == 0) {
            continue;
        }

        if (value == CAN) {
            set_error(error, error_count, L"充电器取消了传输");
            return -1;
        }
        if (want_receiver) {
            if (value == CRC_REQUEST || value == NAK) {
                return value;
            }
        } else {
            if (value == ACK || value == NAK) {
                return value;
            }
        }
    }

    set_error(error, error_count, want_receiver ? L"等待 YMODEM 接收握手超时" : L"等待 ACK/NAK 超时");
    return 0;
}

static BOOL send_packet(HANDLE handle,
                        BYTE block_no,
                        const BYTE *data,
                        DWORD data_len,
                        DWORD packet_size,
                        BYTE pad,
                        WCHAR *error,
                        size_t error_count) {
    BYTE packet[3 + PACKET_1K + 2];
    BYTE *payload = packet + 3;
    WORD crc;

    if (packet_size != PACKET_128 && packet_size != PACKET_1K) {
        set_error(error, error_count, L"内部错误：YMODEM 包长度无效");
        return FALSE;
    }
    if (data_len > packet_size) {
        data_len = packet_size;
    }

    packet[0] = (packet_size == PACKET_128) ? SOH : STX;
    packet[1] = block_no;
    packet[2] = (BYTE)(0xFF - block_no);
    FillMemory(payload, packet_size, pad);
    if (data_len > 0) {
        CopyMemory(payload, data, data_len);
    }
    crc = crc16_ccitt(payload, packet_size);
    packet[3 + packet_size] = (BYTE)((crc >> 8) & 0xFF);
    packet[3 + packet_size + 1] = (BYTE)(crc & 0xFF);

    return write_all(handle, packet, 3 + packet_size + 2, error, error_count);
}

static BOOL send_packet_wait_ack(HANDLE handle,
                                 BYTE block_no,
                                 const BYTE *data,
                                 DWORD data_len,
                                 DWORD packet_size,
                                 BYTE pad,
                                 volatile LONG *stop_flag,
                                 WCHAR *error,
                                 size_t error_count) {
    int attempt;

    for (attempt = 0; attempt < 10; ++attempt) {
        int response;
        if (!send_packet(handle, block_no, data, data_len, packet_size, pad, error, error_count)) {
            return FALSE;
        }
        response = wait_control_response(handle, 10000, FALSE, stop_flag, error, error_count);
        if (response == ACK) {
            return TRUE;
        }
        if (response == NAK) {
            continue;
        }
        return FALSE;
    }

    set_error(error, error_count, L"YMODEM 数据包 %u 重试失败", (unsigned int)block_no);
    return FALSE;
}

static const WCHAR *base_name_of(const WCHAR *path) {
    const WCHAR *last_slash = wcsrchr(path, L'\\');
    const WCHAR *last_forward = wcsrchr(path, L'/');
    const WCHAR *base = path;

    if (last_slash && last_slash + 1 > base) {
        base = last_slash + 1;
    }
    if (last_forward && last_forward + 1 > base) {
        base = last_forward + 1;
    }
    return base;
}

static BOOL build_header_payload(const WCHAR *firmware_path,
                                 DWORD file_size,
                                 BYTE *payload,
                                 DWORD payload_size,
                                 WCHAR *error,
                                 size_t error_count) {
    const WCHAR *name = base_name_of(firmware_path);
    char file_name[96];
    char size_text[32];
    int name_len;
    size_t size_len;

    ZeroMemory(payload, payload_size);
    name_len = WideCharToMultiByte(CP_ACP, 0, name, -1, file_name, sizeof(file_name), NULL, NULL);
    if (name_len <= 1) {
        StringCchCopyA(file_name, ARRAYSIZE(file_name), "firmware.bin");
        name_len = (int)strlen(file_name) + 1;
    }
    file_name[sizeof(file_name) - 1] = '\0';
    StringCchPrintfA(size_text, ARRAYSIZE(size_text), "%lu", (unsigned long)file_size);
    size_len = strlen(size_text) + 1;

    if ((DWORD)name_len + (DWORD)size_len > payload_size) {
        set_error(error, error_count, L"固件文件名过长，无法放入 YMODEM 文件头");
        return FALSE;
    }

    CopyMemory(payload, file_name, name_len);
    CopyMemory(payload + name_len, size_text, size_len);
    return TRUE;
}

BOOL ymodem_send_file(HANDLE serial,
                      const WCHAR *firmware_path,
                      volatile LONG *stop_flag,
                      YmodemLogFn log_fn,
                      YmodemProgressFn progress_fn,
                      void *user,
                      WCHAR *error,
                      size_t error_count) {
    HANDLE file;
    DWORD file_size_high = 0;
    DWORD file_size;
    BYTE header[PACKET_128];
    BYTE data[PACKET_1K];
    DWORD sent = 0;
    BYTE block_no = 1;
    int response;

    file = CreateFileW(firmware_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        WCHAR win_error[256];
        format_win_error(GetLastError(), win_error, ARRAYSIZE(win_error));
        set_error(error, error_count, L"打开固件失败：%s", win_error);
        return FALSE;
    }

    file_size = GetFileSize(file, &file_size_high);
    if (file_size == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
        WCHAR win_error[256];
        format_win_error(GetLastError(), win_error, ARRAYSIZE(win_error));
        set_error(error, error_count, L"读取固件大小失败：%s", win_error);
        CloseHandle(file);
        return FALSE;
    }
    if (file_size_high != 0 || file_size == 0) {
        set_error(error, error_count, L"固件大小不支持：文件为空或超过 4GB");
        CloseHandle(file);
        return FALSE;
    }

    log_text(log_fn, user, L"等待充电器进入 YMODEM 接收状态...");
    response = wait_control_response(serial, INITIAL_RECEIVER_TIMEOUT_MS, TRUE, stop_flag, error, error_count);
    if (response <= 0) {
        if (response == 0) {
            set_error(error, error_count, L"发送 update 后未进入 YMODEM 接收状态，请检查设备是否在 7 秒窗口内收到命令");
        }
        CloseHandle(file);
        return FALSE;
    }

    if (!build_header_payload(firmware_path, file_size, header, sizeof(header), error, error_count)) {
        CloseHandle(file);
        return FALSE;
    }

    log_text(log_fn, user, L"发送 YMODEM 文件头...");
    if (!send_packet_wait_ack(serial, 0, header, sizeof(header), PACKET_128, 0x00,
                              stop_flag, error, error_count)) {
        CloseHandle(file);
        return FALSE;
    }
    response = wait_control_response(serial, 10000, TRUE, stop_flag, error, error_count);
    if (response <= 0) {
        CloseHandle(file);
        return FALSE;
    }

    while (TRUE) {
        DWORD read_count = 0;
        DWORD packet_size;

        if (!ReadFile(file, data, sizeof(data), &read_count, NULL)) {
            WCHAR win_error[256];
            format_win_error(GetLastError(), win_error, ARRAYSIZE(win_error));
            set_error(error, error_count, L"读取固件失败：%s", win_error);
            CloseHandle(file);
            return FALSE;
        }
        if (read_count == 0) {
            break;
        }

        packet_size = (read_count > PACKET_128) ? PACKET_1K : PACKET_128;
        if (!send_packet_wait_ack(serial, block_no, data, read_count, packet_size, CPMEOF,
                                  stop_flag, error, error_count)) {
            CloseHandle(file);
            return FALSE;
        }

        sent += read_count;
        if (progress_fn) {
            progress_fn(user, (sent * 100UL) / file_size);
        }
        block_no = (BYTE)(block_no + 1);
    }

    CloseHandle(file);

    log_text(log_fn, user, L"发送 YMODEM 结束帧...");
    {
        BYTE eot = EOT;
        if (!write_all(serial, &eot, 1, error, error_count)) {
            return FALSE;
        }
        response = wait_control_response(serial, 10000, FALSE, stop_flag, error, error_count);
        if (response == NAK) {
            if (!write_all(serial, &eot, 1, error, error_count)) {
                return FALSE;
            }
            response = wait_control_response(serial, 10000, FALSE, stop_flag, error, error_count);
            if (response != ACK) {
                return FALSE;
            }
        } else if (response != ACK) {
            return FALSE;
        }
    }

    response = wait_control_response(serial, 10000, TRUE, stop_flag, error, error_count);
    if (response <= 0) {
        return FALSE;
    }

    ZeroMemory(header, sizeof(header));
    if (!send_packet_wait_ack(serial, 0, header, sizeof(header), PACKET_128, 0x00,
                              stop_flag, error, error_count)) {
        return FALSE;
    }

    if (progress_fn) {
        progress_fn(user, 100);
    }
    return TRUE;
}
