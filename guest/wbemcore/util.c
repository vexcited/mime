/* memory, growing strings and the small helpers of the engine. */
#include "wbemcore.h"

LONG g_wbem_locks;

void *wb_alloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void *wb_realloc(void *memory, SIZE_T size)
{
    if (!memory)
        return wb_alloc(size);
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, memory, size ? size : 1);
}

void wb_free(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

WCHAR *wb_wcsndup(const WCHAR *text, SIZE_T length)
{
    WCHAR *copy = wb_alloc((length + 1) * sizeof(WCHAR));
    if (copy && text)
        memcpy(copy, text, length * sizeof(WCHAR));
    return copy;
}

WCHAR *wb_wcsdup(const WCHAR *text)
{
    return text ? wb_wcsndup(text, wcslen(text)) : nullptr;
}

void wb_buf_addn(WBuf *buf, const WCHAR *text, SIZE_T length)
{
    if (buf->length + length + 1 > buf->capacity) {
        SIZE_T capacity = buf->capacity ? buf->capacity : 64;
        while (buf->length + length + 1 > capacity)
            capacity *= 2;
        buf->text = wb_realloc(buf->text, capacity * sizeof(WCHAR));
        buf->capacity = capacity;
    }
    memcpy(buf->text + buf->length, text, length * sizeof(WCHAR));
    buf->length += length;
    buf->text[buf->length] = 0;
}

void wb_buf_add(WBuf *buf, const WCHAR *text)
{
    wb_buf_addn(buf, text, wcslen(text));
}

void wb_buf_addc(WBuf *buf, WCHAR c)
{
    wb_buf_addn(buf, &c, 1);
}

void wb_buf_addf(WBuf *buf, const WCHAR *format, ...)
{
    WCHAR stack[512];
    va_list ap;
    va_start(ap, format);
    int n = _vsnwprintf(stack, RTL_NUMBER_OF(stack) - 1, format, ap);
    va_end(ap);
    if (n >= 0) {
        wb_buf_addn(buf, stack, (SIZE_T)n);
        return;
    }
    SIZE_T size = 4096;
    for (;;) {
        WCHAR *big = wb_alloc(size * sizeof(WCHAR));
        va_start(ap, format);
        n = _vsnwprintf(big, size - 1, format, ap);
        va_end(ap);
        if (n >= 0) {
            wb_buf_addn(buf, big, (SIZE_T)n);
            wb_free(big);
            return;
        }
        wb_free(big);
        size *= 4;
    }
}

WCHAR *wb_buf_take(WBuf *buf)
{
    WCHAR *text = buf->text ? buf->text : wb_alloc(sizeof(WCHAR));
    buf->text = nullptr;
    buf->length = buf->capacity = 0;
    return text;
}

WCHAR *wb_format(const WCHAR *format, ...)
{
    WCHAR stack[512];
    va_list ap;
    va_start(ap, format);
    int n = _vsnwprintf(stack, RTL_NUMBER_OF(stack) - 1, format, ap);
    va_end(ap);
    if (n >= 0)
        return wb_wcsndup(stack, (SIZE_T)n);
    SIZE_T size = 4096;
    for (;;) {
        WCHAR *big = wb_alloc(size * sizeof(WCHAR));
        va_start(ap, format);
        n = _vsnwprintf(big, size - 1, format, ap);
        va_end(ap);
        if (n >= 0)
            return big;
        wb_free(big);
        size *= 4;
    }
}

/* a query for an interface the object does not have, written to the debug output when MIME_WBEM_TRACE is set. */
HRESULT wb_no_interface(const char *who, REFIID riid, void **out)
{
    WCHAR text[160];
    if (!GetEnvironmentVariableW(L"MIME_WBEM_TRACE", text, 2)) {
        if (out)
            *out = nullptr;
        return E_NOINTERFACE;
    }
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"wbem: %S has no interface {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\n",
               who, (unsigned long)riid->Data1, riid->Data2, riid->Data3, riid->Data4[0], riid->Data4[1], riid->Data4[2],
               riid->Data4[3], riid->Data4[4], riid->Data4[5], riid->Data4[6], riid->Data4[7]);
    text[RTL_NUMBER_OF(text) - 1] = 0;
    OutputDebugStringW(text);
    if (out)
        *out = nullptr;
    return E_NOINTERFACE;
}
