#include "wts_private.h"
#include <mime/win32/winuser.h>

BOOL WINAPI WTSQuerySessionInformationW(HANDLE server, DWORD session, int klass, LPWSTR *buffer, DWORD *size);
void WINAPI WTSFreeMemory(PVOID memory);
WINUSERAPI int WINAPI MessageBoxTimeoutW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT style, WORD language, DWORD timeout);

typedef struct {
    HMODULE Module;
    PWSTR Title, Text;
    DWORD Style, Timeout;
} SESSION_MESSAGE;

static DWORD WINAPI send_message(PVOID parameter)
{
    SESSION_MESSAGE *message = parameter;
    HMODULE module = message->Module;
    MessageBoxTimeoutW(NULL, message->Text, message->Title, message->Style, 0, message->Timeout);
    LocalFree(message);
    FreeLibraryAndExitThread(module, 0);
}

DECLSPEC_EXPORT BOOL WINAPI WTSSendMessageW(HANDLE server, DWORD session, LPWSTR title, DWORD title_bytes,
                                           LPWSTR text, DWORD text_bytes, DWORD style, DWORD timeout,
                                           DWORD *response, BOOL wait)
{
    if (!response || (title_bytes && !title) || (text_bytes && !text) ||
        (title_bytes & 1) || (text_bytes & 1) ||
        (ULONGLONG)title_bytes + text_bytes + sizeof(SESSION_MESSAGE) + 4 > INT32_MAX) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (timeout > (UINT32_MAX - 1) / 1000) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    LPWSTR state;
    DWORD size;
    if (!WTSQuerySessionInformationW(server, session, 8, &state, &size))
        return FALSE;
    DWORD connection = size == sizeof(DWORD) ? *(DWORD *)state : UINT32_MAX;
    WTSFreeMemory(state);
    if (connection != 0) {
        SetLastError(7022);
        return FALSE;
    }
    SIZE_T bytes = sizeof(SESSION_MESSAGE) + (SIZE_T)title_bytes + text_bytes + 4;
    SESSION_MESSAGE *message = LocalAlloc(LPTR, bytes);
    if (!message)
        return FALSE;
    message->Title = (PWSTR)(message + 1);
    message->Text = message->Title + title_bytes / sizeof(WCHAR) + 1;
    if (title_bytes)
        memcpy(message->Title, title, title_bytes);
    if (text_bytes)
        memcpy(message->Text, text, text_bytes);
    message->Style = style;
    message->Timeout = timeout ? timeout * 1000 : UINT32_MAX;
    if (wait) {
        int result = MessageBoxTimeoutW(NULL, message->Text, message->Title, style, 0, message->Timeout);
        DWORD error = result ? ERROR_SUCCESS : GetLastError();
        LocalFree(message);
        if (!result) {
            SetLastError(error);
            return FALSE;
        }
        *response = (DWORD)result;
        return TRUE;
    }
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)send_message, &message->Module)) {
        LocalFree(message);
        return FALSE;
    }
    HANDLE thread = CreateThread(NULL, 0, send_message, message, 0, NULL);
    if (!thread) {
        DWORD error = GetLastError();
        FreeLibrary(message->Module);
        LocalFree(message);
        SetLastError(error);
        return FALSE;
    }
    CloseHandle(thread);
    *response = 32001;
    return TRUE;
}
