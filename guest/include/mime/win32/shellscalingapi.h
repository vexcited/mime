/* shcore.dll: the scaling api of shellscalingapi.h plus the stream and registry helpers. */
#ifndef MIME_WIN32_SHELLSCALINGAPI_H
#define MIME_WIN32_SHELLSCALINGAPI_H

#include <mime/win32/objidl.h>
#include <mime/win32/winuser.h>

#ifndef SHCOREAPI
#define SHCOREAPI DECLSPEC_IMPORT
#endif

typedef enum PROCESS_DPI_AWARENESS {
    PROCESS_DPI_UNAWARE = 0,
    PROCESS_SYSTEM_DPI_AWARE = 1,
    PROCESS_PER_MONITOR_DPI_AWARE = 2,
} PROCESS_DPI_AWARENESS;

typedef enum MONITOR_DPI_TYPE {
    MDT_EFFECTIVE_DPI = 0,
    MDT_ANGULAR_DPI = 1,
    MDT_RAW_DPI = 2,
    MDT_DEFAULT = MDT_EFFECTIVE_DPI,
} MONITOR_DPI_TYPE;

typedef enum DEVICE_SCALE_FACTOR {
    DEVICE_SCALE_FACTOR_INVALID = 0,
    SCALE_100_PERCENT = 100,
    SCALE_120_PERCENT = 120,
    SCALE_125_PERCENT = 125,
    SCALE_140_PERCENT = 140,
    SCALE_150_PERCENT = 150,
    SCALE_160_PERCENT = 160,
    SCALE_175_PERCENT = 175,
    SCALE_180_PERCENT = 180,
    SCALE_200_PERCENT = 200,
    SCALE_225_PERCENT = 225,
    SCALE_250_PERCENT = 250,
    SCALE_300_PERCENT = 300,
    SCALE_350_PERCENT = 350,
    SCALE_400_PERCENT = 400,
    SCALE_450_PERCENT = 450,
    SCALE_500_PERCENT = 500,
} DEVICE_SCALE_FACTOR;

typedef enum DISPLAY_DEVICE_TYPE {
    DEVICE_PRIMARY = 0,
    DEVICE_IMMERSIVE = 1,
} DISPLAY_DEVICE_TYPE;

typedef enum SHELL_UI_COMPONENT {
    SHELL_UI_COMPONENT_TASKBARS = 0,
    SHELL_UI_COMPONENT_NOTIFICATIONAREA = 1,
    SHELL_UI_COMPONENT_DESKBAND = 2,
} SHELL_UI_COMPONENT;

SHCOREAPI HRESULT WINAPI GetDpiForMonitor(HMONITOR monitor, MONITOR_DPI_TYPE type, UINT *x, UINT *y);
SHCOREAPI HRESULT WINAPI GetScaleFactorForMonitor(HMONITOR monitor, DEVICE_SCALE_FACTOR *scale);
SHCOREAPI DEVICE_SCALE_FACTOR WINAPI GetScaleFactorForDevice(DISPLAY_DEVICE_TYPE type);
SHCOREAPI HRESULT WINAPI SetProcessDpiAwareness(PROCESS_DPI_AWARENESS value);
SHCOREAPI HRESULT WINAPI GetProcessDpiAwareness(HANDLE process, PROCESS_DPI_AWARENESS *value);
SHCOREAPI UINT WINAPI GetDpiForShellUIComponent(SHELL_UI_COMPONENT component);
SHCOREAPI HRESULT WINAPI RegisterScaleChangeNotifications(DISPLAY_DEVICE_TYPE type, HWND window, UINT message,
                                                          DWORD *cookie);
SHCOREAPI HRESULT WINAPI RevokeScaleChangeNotifications(DISPLAY_DEVICE_TYPE type, DWORD cookie);
SHCOREAPI HRESULT WINAPI RegisterScaleChangeEvent(HANDLE event, DWORD_PTR *cookie);
SHCOREAPI HRESULT WINAPI UnregisterScaleChangeEvent(DWORD_PTR cookie);

SHCOREAPI IStream *WINAPI SHCreateMemStream(const BYTE *data, UINT size);
SHCOREAPI HRESULT WINAPI SHCreateStreamOnFileEx(LPCWSTR file, DWORD mode, DWORD attributes, BOOL create,
                                                IStream *template_stream, IStream **stream);
SHCOREAPI HRESULT WINAPI SHCreateStreamOnFileW(LPCWSTR file, DWORD mode, IStream **stream);
SHCOREAPI HRESULT WINAPI IStream_Read(IStream *stream, void *data, ULONG size);
SHCOREAPI HRESULT WINAPI IStream_Write(IStream *stream, const void *data, ULONG size);
SHCOREAPI HRESULT WINAPI IStream_Reset(IStream *stream);
SHCOREAPI HRESULT WINAPI IStream_Size(IStream *stream, ULARGE_INTEGER *size);
SHCOREAPI HRESULT WINAPI IStream_ReadStr(IStream *stream, LPWSTR *string);
SHCOREAPI HRESULT WINAPI IStream_WriteStr(IStream *stream, LPCWSTR string);
SHCOREAPI HRESULT WINAPI IStream_Copy(IStream *from, IStream *to, DWORD size);
SHCOREAPI HRESULT WINAPI SHStrDupW(LPCWSTR string, LPWSTR *copy);
SHCOREAPI LONG WINAPI SHGetValueW(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD *type, void *data, DWORD *size);
SHCOREAPI LONG WINAPI SHSetValueW(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD type, const void *data, DWORD size);
SHCOREAPI LONG WINAPI SHDeleteKeyW(HKEY key, LPCWSTR subkey);
SHCOREAPI LONG WINAPI SHDeleteEmptyKeyW(HKEY key, LPCWSTR subkey);
SHCOREAPI HRESULT WINAPI SetCurrentProcessExplicitAppUserModelID(LPCWSTR id);
SHCOREAPI HRESULT WINAPI GetCurrentProcessExplicitAppUserModelID(LPWSTR *id);
SHCOREAPI BOOL WINAPI IsOS(DWORD os);
/* a Windows.Storage.Streams.IRandomAccessStream on the file, read only or read and write by the FileAccessMode. */
SHCOREAPI HRESULT WINAPI CreateRandomAccessStreamOnFile(LPCWSTR path, DWORD access, REFIID riid, void **result);

#endif
