#ifndef MIME_OLEAUT32_PRIVATE_H
#define MIME_OLEAUT32_PRIVATE_H

#define WINOLEAUTAPI DECLSPEC_EXPORT
#include <mime/win32/olectl.h>
#include <rtl.h>

/* the process heap is ole32's task allocator, so this memory can be freed with CoTaskMemFree. */
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

/* numtext.c. */
BOOL AutDecimalValid(const DECIMAL *d);
double AutDecimalToDouble(const DECIMAL *d);
BOOL AutDecimalToInteger(const DECIMAL *d, BOOL *negative, ULONGLONG *magnitude);
int AutFormatInteger(LONGLONG v, BOOL is_unsigned, WCHAR *out);
HRESULT AutBstrFromInteger(LONGLONG v, BOOL is_unsigned, LCID lcid, ULONG flags, BSTR *out);
HRESULT AutBstrFromDouble(double v, int ndigits, LCID lcid, ULONG flags, BSTR *out);
HRESULT AutFormatDecimal(BOOL negative, ULONG high, ULONGLONG low, int scale, LCID lcid, ULONG flags, BSTR *out);

/* a numeric value of any kind. */
typedef enum { NK_INT, NK_UINT, NK_REAL, NK_CY, NK_DEC } NUM_KIND;
typedef struct {
    NUM_KIND Kind;
    /* significant digits when converting to a decimal: 7 for a float, 15 for a double. */
    int Precision;
    union {
        LONGLONG I;
        ULONGLONG U;
        double R;
        DECIMAL D;
    };
} AUT_NUM;

BOOL AutNumFromVariant(const VARIANT *v, AUT_NUM *n);
HRESULT AutNumStore(const AUT_NUM *n, VARTYPE vt, VARIANT *dest, LCID lcid, ULONG flags);
HRESULT AutExtractValueProperty(IDispatch *object, LCID lcid, VARIANT *result);
BOOL AutRoundHalfEven(double x, double *rounded);

/* fp_kind 0 is no float result, 1 a float, 2 a double, returned in *fp_value: x86 pops it off the x87 stack, x64
 * reads xmm0. */
ULONGLONG AutCallEx(void *function, const ULONG_PTR *slots, ULONG count, ULONG fp_kind, ULONGLONG *fp_value);

/* set by SetVarConversionLocaleSetting. */
extern ULONG g_process_nls_flags;

void AutErrorAttach(void);
void AutErrorDetach(void);

#endif
