/* functions of other dlls that shlwapi forwards or does not link, called by the name they have in windows. */
#ifndef MIME_SHLWAPI_DYNAMIC_H
#define MIME_SHLWAPI_DYNAMIC_H

#include "shlwapi_private.h"

#define SHL_DYNAMIC(ret, name, dll, params, call)                                                                      \
    static inline ret Shl_##name params                                                                                \
    {                                                                                                                  \
        ret(WINAPI * function) params = (ret(WINAPI *) params)ShlProc(dll, #name);                                     \
        return function ? function call : (ret)0;                                                                      \
    }

SHL_DYNAMIC(LSTATUS, SHGetValueW, L"shcore.dll",
            (HKEY key, LPCWSTR sub, LPCWSTR value, DWORD *type, void *data, DWORD *size),
            (key, sub, value, type, data, size))
SHL_DYNAMIC(LSTATUS, SHSetValueW, L"shcore.dll",
            (HKEY key, LPCWSTR sub, LPCWSTR value, DWORD type, LPCVOID data, DWORD size),
            (key, sub, value, type, data, size))
SHL_DYNAMIC(LSTATUS, SHDeleteValueW, L"shcore.dll", (HKEY key, LPCWSTR sub, LPCWSTR value), (key, sub, value))
SHL_DYNAMIC(LSTATUS, SHDeleteKeyW, L"shcore.dll", (HKEY key, LPCWSTR sub), (key, sub))
SHL_DYNAMIC(LSTATUS, SHCopyKeyW, L"shcore.dll", (HKEY key, LPCWSTR sub, HKEY target, DWORD reserved),
            (key, sub, target, reserved))
SHL_DYNAMIC(LSTATUS, SHRegGetValueW, L"shcore.dll",
            (HKEY key, LPCWSTR sub, LPCWSTR value, DWORD flags, DWORD *type, void *data, DWORD *size),
            (key, sub, value, flags, type, data, size))
SHL_DYNAMIC(BOOL, OpenThreadToken, L"advapi32.dll", (HANDLE thread, DWORD access, BOOL as_self, PHANDLE token),
            (thread, access, as_self, token))
SHL_DYNAMIC(HRESULT, VariantClear, L"oleaut32.dll", (VARIANT * variant), (variant))
SHL_DYNAMIC(HRESULT, VariantChangeType, L"oleaut32.dll",
            (VARIANT * to, const VARIANT *from, USHORT flags, VARTYPE type), (to, from, flags, type))
SHL_DYNAMIC(BSTR, SysAllocString, L"oleaut32.dll", (const OLECHAR *text), (text))
SHL_DYNAMIC(BSTR, SysAllocStringByteLen, L"oleaut32.dll", (LPCSTR text, UINT bytes), (text, bytes))

static inline void Shl_SysFreeString(BSTR text)
{
    void(WINAPI * function)(BSTR) = (void(WINAPI *)(BSTR))ShlProc(L"oleaut32.dll", "SysFreeString");
    if (function)
        function(text);
}

#define SHGetValueW Shl_SHGetValueW
#define SHSetValueW Shl_SHSetValueW
#define SHDeleteValueW Shl_SHDeleteValueW
#define SHDeleteKeyW Shl_SHDeleteKeyW
#define SHCopyKeyW Shl_SHCopyKeyW
#define SHRegGetValueW Shl_SHRegGetValueW
#define OpenThreadToken Shl_OpenThreadToken
#define VariantClear Shl_VariantClear
#define VariantChangeType Shl_VariantChangeType
#define SysAllocString Shl_SysAllocString
#define SysAllocStringByteLen Shl_SysAllocStringByteLen
#define SysFreeString Shl_SysFreeString
#define VariantInit(variant) memset(variant, 0, sizeof(VARIANT))

#endif
