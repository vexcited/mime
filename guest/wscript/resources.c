/*
 * the texts of the host and where they go: the string table of the program, the messages formatted from it, and the two
 * ways to show them, lines on the console for cscript and message boxes for wscript.
 */
#include "wsh.h"

PCWSTR WshLastSeparator(PCWSTR path)
{
    PCWSTR last = nullptr;
    for (; path && *path; path++)
        if (*path == L'\\')
            last = path;
    if (last)
        return last;
    for (; path && *path; path++)
        ;
    return nullptr;
}

BOOL WshEqualNoCase(PCWSTR a, PCWSTR b, SIZE_T count)
{
    return _wcsnicmp(a, b, count) == 0;
}

BSTR WshLoadString(UINT id)
{
    WCHAR buffer[2048];
    int length = LoadStringW(g_host.Instance, id, buffer, 2048);
    if (!length)
        return nullptr;
    return SysAllocStringLen(buffer, length);
}

/* the text of an hresult from the system messages, or its number when there is none. */
HRESULT WshFormatHResult(HRESULT code, BSTR *out)
{
    PWSTR text = nullptr;
    *out = nullptr;
    if (!FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM | 0xFF, nullptr, (DWORD)code,
                        0, (LPWSTR)&text, 0, nullptr)) {
        text = LocalAlloc(0, 22);
        if (!text)
            return E_OUTOFMEMORY;
        _snwprintf(text, 11, L"0x%8X", (unsigned)code);
    }
    *out = SysAllocString(text);
    LocalFree(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT format_va(PCWSTR format, BSTR *out, va_list *arguments)
{
    PWSTR text = nullptr;
    *out = nullptr;
    if (!FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_STRING, format, 0, 0, (LPWSTR)&text, 0, arguments))
        return HRESULT_FROM_WIN32(GetLastError());
    *out = SysAllocString(text);
    LocalFree(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}

HRESULT WshFormatMessage(PCWSTR format, BSTR *out, ...)
{
    va_list arguments;
    va_start(arguments, out);
    HRESULT hr = format_va(format, out, &arguments);
    va_end(arguments);
    return hr;
}

#if WSH_CONSOLE

/* a line to a handle: lines end with CR LF, the console takes UTF-16 and a file the OEM code page unless /U asked for UTF-16. */
static HRESULT write_line(HANDLE handle, PCWSTR text)
{
    SIZE_T length = text ? wcslen(text) : 0;
    PWSTR buffer = HeapAlloc(GetProcessHeap(), 0, (length * 2 + 4) * sizeof(WCHAR));
    if (!buffer)
        return E_OUTOFMEMORY;
    SIZE_T used = 0;
    WCHAR before = 0;
    for (SIZE_T i = 0; i < length; i++) {
        if (text[i] == L'\n' && before != L'\r')
            buffer[used++] = L'\r';
        buffer[used++] = text[i];
        before = text[i];
    }
    buffer[used++] = L'\r';
    buffer[used++] = L'\n';
    HRESULT hr = S_OK;
    DWORD mode;
    BOOL console = GetConsoleMode(handle, &mode);
    if (g_host.Unicode || console) {
        PBYTE bytes = (PBYTE)buffer;
        SIZE_T left = used;
        while (left) {
            DWORD part = left > 0x3FFF ? 0x3FFF : (DWORD)left, written = 0;
            if (WriteConsoleW(handle, bytes, part, &written, nullptr)) {
                left -= written;
                bytes += written * 2;
            } else if (GetLastError() == ERROR_INVALID_HANDLE) {
                while (left) {
                    if (!WriteFile(handle, bytes, (DWORD)(left * 2), &written, nullptr)) {
                        hr = HRESULT_FROM_WIN32(GetLastError());
                        break;
                    }
                    left -= written / 2;
                    bytes += written & ~1u;
                }
                break;
            } else {
                hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }
        }
    } else {
        int size = WideCharToMultiByte(CP_OEMCP, 0, buffer, (int)used, nullptr, 0, nullptr, nullptr);
        PSTR bytes = size ? HeapAlloc(GetProcessHeap(), 0, size) : nullptr;
        if (bytes && WideCharToMultiByte(CP_OEMCP, 0, buffer, (int)used, bytes, size, nullptr, nullptr)) {
            PSTR at = bytes;
            while (size > 0) {
                DWORD written = 0;
                if (!WriteFile(handle, at, size, &written, nullptr)) {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    break;
                }
                at += written;
                size -= (int)written;
            }
        } else
            hr = bytes ? HRESULT_FROM_WIN32(GetLastError()) : E_OUTOFMEMORY;
        if (bytes)
            HeapFree(GetProcessHeap(), 0, bytes);
    }
    HeapFree(GetProcessHeap(), 0, buffer);
    return hr;
}

HRESULT WshPrint(PCWSTR text)
{
    if (g_host.Batch)
        return S_OK;
    return write_line(GetStdHandle(STD_OUTPUT_HANDLE), text);
}

/* a message of the string table, with the title of its kind in front when there is one. */
void WshPrintF(UINT prefix, UINT id, ...)
{
    if (g_host.Batch)
        return;
    BSTR format = WshLoadString(id), message = nullptr, title = nullptr;
    if (!format)
        return;
    va_list arguments;
    va_start(arguments, id);
    HRESULT hr = format_va(format, &message, &arguments);
    va_end(arguments);
    if (SUCCEEDED(hr)) {
        if (prefix && (title = WshLoadString(prefix))) {
            SIZE_T size = SysStringLen(title) + SysStringLen(message) + 3;
            PWSTR line = HeapAlloc(GetProcessHeap(), 0, size * sizeof(WCHAR));
            if (line) {
                wcscpy(line, title);
                wcscat(line, L": ");
                wcscat(line, message);
                write_line(GetStdHandle(STD_OUTPUT_HANDLE), line);
                HeapFree(GetProcessHeap(), 0, line);
            }
        } else
            write_line(GetStdHandle(STD_OUTPUT_HANDLE), message);
    }
    SysFreeString(format);
    SysFreeString(message);
    SysFreeString(title);
}

HRESULT WshReportScriptError(ULONG line, LONG column, PCWSTR source, PCWSTR description, PCWSTR text, DWORD code)
{
    if (g_host.Batch)
        return S_OK;
    BSTR format = WshLoadString(2023), message = nullptr;
    if (!format)
        return E_OUTOFMEMORY;
    HRESULT hr = WshFormatMessage(format, &message, g_host.ScriptPath, line, column, source, description);
    if (SUCCEEDED(hr))
        hr = write_line(GetStdHandle(STD_ERROR_HANDLE), message);
    SysFreeString(format);
    SysFreeString(message);
    return FAILED(hr) ? hr : S_OK;
}

void WshShowLogo(void)
{
    if (!g_host.Batch)
        WshPrintF(0, 2100, (unsigned)g_host.VersionMajor, (unsigned)g_host.VersionMinor);
}

void WshShowUsage(void)
{
    WshPrintF(0, 2010);
}

int WshRunWithoutArgument(void)
{
    SETTINGS settings = {.Logo = TRUE};
    HRESULT hr = WshLoadRegistrySettings(HKEY_CURRENT_USER, &settings);
    if (FAILED(hr)) {
        WshPrintF(RES_BASE + 20, 3205, hr);
        return 1;
    }
    g_host.Batch = FALSE;
    if (settings.Logo)
        WshShowLogo();
    WshShowUsage();
    return 0;
}

void WshOnTimeout(void)
{
    WshPrintF(0, 3100, g_host.ScriptPath);
}

#else

/* a message to read: a box with the title of the host, and the icon of an error when the message has a title. */
HRESULT WshPrint(PCWSTR text)
{
    if (g_host.Batch)
        return S_OK;
    BSTR caption = WshLoadString(RES_BASE + 1);
    if (!caption)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    if (!MessageBoxW(nullptr, text, caption, MB_SETFOREGROUND))
        hr = HRESULT_FROM_WIN32(GetLastError());
    SysFreeString(caption);
    return hr;
}

void WshPrintF(UINT prefix, UINT id, ...)
{
    if (g_host.Batch)
        return;
    BSTR format = WshLoadString(id), message = nullptr, caption = nullptr;
    if (!format)
        return;
    va_list arguments;
    va_start(arguments, id);
    HRESULT hr = format_va(format, &message, &arguments);
    va_end(arguments);
    if (SUCCEEDED(hr)) {
        SIZE_T length = SysStringLen(message);
        PWSTR text = HeapAlloc(GetProcessHeap(), 0, (length + 2) * sizeof(WCHAR));
        caption = WshLoadString(RES_BASE + 1);
        if (text && caption) {
            SIZE_T used = 0;
            for (SIZE_T i = 0; i < length; i++)
                if (message[i] != L'\r')
                    text[used++] = message[i];
            text[used++] = L'\n';
            text[used] = 0;
            MessageBoxW(GetActiveWindow(), text, caption, prefix ? MB_TOPMOST | MB_ICONERROR : MB_TOPMOST | MB_ICONINFORMATION);
        }
        if (text)
            HeapFree(GetProcessHeap(), 0, text);
    }
    SysFreeString(format);
    SysFreeString(message);
    SysFreeString(caption);
}

HRESULT WshReportScriptError(ULONG line, LONG column, PCWSTR source, PCWSTR description, PCWSTR text, DWORD code)
{
    if (g_host.Batch)
        return S_OK;
    WshPrintF(2601, 2600, g_host.ScriptPath, line, column, description, code, source);
    return S_OK;
}

void WshShowLogo(void)
{
}

void WshShowUsage(void)
{
    BSTR text = WshLoadString(RES_BASE + 10), caption = WshLoadString(RES_BASE + 1);
    if (text && caption)
        MessageBoxW(GetActiveWindow(), text, caption, MB_ICONINFORMATION);
    SysFreeString(text);
    SysFreeString(caption);
}

int WshRunWithoutArgument(void)
{
    /* the dialog of the settings of the host is not here, the usage is what there is to show. */
    WshShowUsage();
    return 0;
}

/* the windows host says nothing when the time is over, the box of the script is closed and that is all. */
void WshOnTimeout(void)
{
}

#endif

/* a failure that has been told to the user is passed on as the code that says so. */
HRESULT WshReportError(UINT prefix, UINT id, HRESULT code)
{
    if (code == WSH_E_REPORTED || code == (HRESULT)SCRIPT_E_REPORTED)
        return WSH_E_REPORTED;
    BSTR text = WshLoadString(id), reason = nullptr;
    if (text) {
        WshFormatHResult(code, &reason);
#if WSH_CONSOLE
        WshPrintF(prefix, 3501, text, reason);
#else
        WshPrintF(prefix, 3501, text, reason);
#endif
    }
    SysFreeString(text);
    SysFreeString(reason);
    return WSH_E_REPORTED;
}

HRESULT WshReportError2(UINT prefix, UINT id, PCWSTR first, PCWSTR second)
{
    WshPrintF(prefix, id, first, second);
    return WSH_E_REPORTED;
}
