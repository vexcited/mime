/*
 * oleaut32 internal interface shared by its source files.
 */
#ifndef MIME_OLEAUT32_PRIVATE_H
#define MIME_OLEAUT32_PRIVATE_H

#define WINOLEAUTAPI DECLSPEC_EXPORT
#include <mime/win32/oleauto.h>
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

void AutErrorAttach(void);
void AutErrorDetach(void);

#endif
