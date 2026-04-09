#ifndef CHARGER_UPDATER_YMODEM_H
#define CHARGER_UPDATER_YMODEM_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

typedef void (*YmodemLogFn)(void *user, const WCHAR *message);
typedef void (*YmodemProgressFn)(void *user, DWORD percent);

BOOL ymodem_send_file(HANDLE serial,
                      const WCHAR *firmware_path,
                      volatile LONG *stop_flag,
                      YmodemLogFn log_fn,
                      YmodemProgressFn progress_fn,
                      void *user,
                      WCHAR *error,
                      size_t error_count);

#endif
