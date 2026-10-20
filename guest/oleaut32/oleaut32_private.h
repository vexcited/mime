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

/* a number as it was typed: digits (values 0 to 9), with the exponent that puts the point right. */
typedef struct {
    BOOL Negative;
    BOOL Inexact;
    /* 0 for a decimal number, 3 for octal and 4 for hexadecimal digits, which have no exponent. */
    int Shift;
    int Count;
    int Exponent;
    BYTE Digits[80];
} AUT_NUMBER;

HRESULT AutNumberFromString(LPCOLESTR text, LCID lcid, ULONG locale_flags, ULONG in_flags, AUT_NUMBER *n,
                            ULONG *out_flags, int *used);
BOOL AutNumberToU96(const AUT_NUMBER *n, int extra, ULONG *high, ULONGLONG *low);
double AutNumberToDouble(const AUT_NUMBER *n);
HRESULT AutDecimalFromNumber(const AUT_NUMBER *n, DECIMAL *out);
BOOL AutDecimalValid(const DECIMAL *d);
double AutDecimalToDouble(const DECIMAL *d);
BOOL AutDecimalToInteger(const DECIMAL *d, BOOL *negative, ULONGLONG *magnitude);
int AutFormatInteger(LONGLONG v, BOOL is_unsigned, WCHAR *out);
HRESULT AutBstrFromInteger(LONGLONG v, BOOL is_unsigned, LCID lcid, ULONG flags, BSTR *out);
HRESULT AutBstrFromDouble(double v, int ndigits, LCID lcid, ULONG flags, BSTR *out);
HRESULT AutFormatDecimal(BOOL negative, ULONG high, ULONGLONG low, int scale, LCID lcid, ULONG flags, BSTR *out);

/* the value of a variant or a number, whichever kind of number it is. */
typedef enum { NK_INT, NK_UINT, NK_REAL, NK_CY, NK_DEC } NUM_KIND;
typedef struct {
    NUM_KIND Kind;
    /* digits that matter when the value becomes a decimal: 7 for a float, 15 for a double. */
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

/* calls a function with its arguments in slots. A result in a float or a double register comes back in *fp_value:
 * fp_kind is 0 for none, 1 for a float and 2 for a double (x86 takes it off the x87 stack, x64 reads xmm0). */
ULONGLONG AutCallEx(void *function, const ULONG_PTR *slots, ULONG count, ULONG fp_kind, ULONGLONG *fp_value);

void AutErrorAttach(void);
void AutErrorDetach(void);

#endif
