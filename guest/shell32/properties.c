#include "shell32_private.h"

static SRWLOCK app_id_lock;
static WCHAR *app_id;

SHSTDAPI HRESULT WINAPI SetCurrentProcessExplicitAppUserModelID(LPCWSTR id)
{
    if (!id || !*id || wcslen(id) > 128)
        return E_INVALIDARG;
    WCHAR *copy = ShDuplicate(id);
    if (!copy)
        return E_OUTOFMEMORY;
    AcquireSRWLockExclusive(&app_id_lock);
    WCHAR *old = app_id;
    app_id = copy;
    ReleaseSRWLockExclusive(&app_id_lock);
    ShFree(old);
    return S_OK;
}

SHSTDAPI HRESULT WINAPI GetCurrentProcessExplicitAppUserModelID(LPWSTR *out)
{
    if (!out)
        return E_POINTER;
    AcquireSRWLockShared(&app_id_lock);
    *out = app_id ? ShDuplicate(app_id) : NULL;
    HRESULT hr = app_id ? (*out ? S_OK : E_OUTOFMEMORY) : E_FAIL;
    ReleaseSRWLockShared(&app_id_lock);
    return hr;
}

SHSTDAPI HRESULT WINAPI SHGetPropertyStoreForWindow(HWND window, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!IsWindow(window))
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    return E_NOINTERFACE;
}
