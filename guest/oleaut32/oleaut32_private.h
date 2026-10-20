/*
 * oleaut32 internal interface shared by its source files.
 */
#ifndef MIME_OLEAUT32_PRIVATE_H
#define MIME_OLEAUT32_PRIVATE_H

#define WINOLEAUTAPI DECLSPEC_EXPORT
#include <mime/win32/olectl.h>
#include <rtl.h>

/*
 * oleaut32 allocates from the task allocator, which is the process heap in
 * mime's ole32, so memory stays interchangeable with CoTaskMemFree.
 */
FORCEINLINE void *AutAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

FORCEINLINE void *AutAllocZero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

FORCEINLINE void AutFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

FORCEINLINE void AutRelease(void *object)
{
    if (object)
        ((IUnknown *)object)->lpVtbl->Release((IUnknown *)object);
}

FORCEINLINE void AutAddRef(void *object)
{
    if (object)
        ((IUnknown *)object)->lpVtbl->AddRef((IUnknown *)object);
}

WINOLEAUTAPI HRESULT WINAPI LoadRegTypeLib(REFGUID id, WORD major, WORD minor, LCID locale, void **result);

/* olefont.c and olepic.c */
LONGLONG AutRoundDiv64(LONGLONG numerator, LONGLONG denominator);
HRESULT AutStdOleTypeInfo(UINT index, REFGUID id, ITypeInfo **info);
HRESULT AutNamesToIds(const WCHAR *const *table, const DISPID *ids, int count, REFIID riid, LPOLESTR *names, UINT cnames,
                      DISPID *result);

void AutErrorAttach(void);
void AutErrorDetach(void);

#endif
