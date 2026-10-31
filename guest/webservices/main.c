/*
 * webservices.dll: the windows web services api. nothing of it works here: a program that links to it, as the database
 * engine does for the calls it makes to web services of its own, loads, and every call that would use a service says the
 * api is not supported. the error and heap objects are the exception, which a caller makes before it knows whether it can
 * reach a service: they are plain memory.
 */
#include <mime/win32/winbase.h>
#include <rtl.h>

#define WS_EXPORT DECLSPEC_EXPORT
#define WS_E_NOT_SUPPORTED ((HRESULT)0x803D0013)

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

typedef struct WS_ERROR_OBJECT {
    DWORD Magic;
} WS_ERROR_OBJECT;

typedef struct WS_HEAP_OBJECT {
    DWORD Magic;
    SIZE_T Maximum;
} WS_HEAP_OBJECT;

#define ERROR_MAGIC 0x52455357
#define HEAP_MAGIC 0x50485357

WS_EXPORT HRESULT WINAPI WsCreateError(const void *properties, ULONG count, void **error)
{
    if (!error)
        return E_INVALIDARG;
    WS_ERROR_OBJECT *object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;
    object->Magic = ERROR_MAGIC;
    *error = object;
    return S_OK;
}

WS_EXPORT HRESULT WINAPI WsResetError(void *error)
{
    return error && ((WS_ERROR_OBJECT *)error)->Magic == ERROR_MAGIC ? S_OK : E_INVALIDARG;
}

/* an error object here never holds a property or a string, so asking for one is asking for what is not there. */
WS_EXPORT HRESULT WINAPI WsGetErrorProperty(void *error, DWORD id, void *value, ULONG size)
{
    return error && ((WS_ERROR_OBJECT *)error)->Magic == ERROR_MAGIC ? WS_E_NOT_SUPPORTED : E_INVALIDARG;
}

WS_EXPORT HRESULT WINAPI WsGetErrorString(void *error, ULONG index, void *text)
{
    return E_INVALIDARG;
}

WS_EXPORT HRESULT WINAPI WsCreateHeap(SIZE_T maximum, SIZE_T trim, const void *properties, ULONG count, void **heap, void *error)
{
    if (!heap)
        return E_INVALIDARG;
    WS_HEAP_OBJECT *object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;
    object->Magic = HEAP_MAGIC;
    object->Maximum = maximum;
    *heap = object;
    return S_OK;
}

WS_EXPORT HRESULT WINAPI WsResetHeap(void *heap, void *error)
{
    return heap && ((WS_HEAP_OBJECT *)heap)->Magic == HEAP_MAGIC ? S_OK : E_INVALIDARG;
}

WS_EXPORT HRESULT WINAPI WsCreateServiceProxy(DWORD type, DWORD binding, const void *security, const void *properties,
                                              ULONG count, const void *channel_properties, ULONG channel_count, void **proxy,
                                              void *error)
{
    if (proxy)
        *proxy = nullptr;
    return WS_E_NOT_SUPPORTED;
}

WS_EXPORT HRESULT WINAPI WsOpenServiceProxy(void *proxy, const void *address, const void *context, void *error)
{
    return WS_E_NOT_SUPPORTED;
}

WS_EXPORT HRESULT WINAPI WsCall(void *proxy, const void *operation, const void **arguments, void *heap, const void *properties,
                                ULONG count, const void *context, void *error)
{
    return WS_E_NOT_SUPPORTED;
}

WS_EXPORT HRESULT WINAPI WsAddCustomHeader(void *message, const void *description, DWORD option, const void *value, ULONG size,
                                           ULONG attributes, void *error)
{
    return WS_E_NOT_SUPPORTED;
}
