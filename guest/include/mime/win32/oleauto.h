/*
 * ole automation: strings, variants and safe arrays of oleaut32.dll.
 */
#ifndef MIME_WIN32_OLEAUTO_H
#define MIME_WIN32_OLEAUTO_H

#include <mime/win32/objbase.h>

#ifndef WINOLEAUTAPI
#define WINOLEAUTAPI DECLSPEC_IMPORT
#endif

#ifndef DISP_E_BADVARTYPE
#define DISP_E_BADVARTYPE ((HRESULT)0x80020008L)
#endif
#ifndef DISP_E_OVERFLOW
#define DISP_E_OVERFLOW ((HRESULT)0x8002000AL)
#endif
#ifndef DISP_E_BADINDEX
#define DISP_E_BADINDEX ((HRESULT)0x8002000BL)
#endif
#ifndef DISP_E_ARRAYISLOCKED
#define DISP_E_ARRAYISLOCKED ((HRESULT)0x8002000DL)
#endif

/* ------------------------------------------------------------------ basic types */

typedef WCHAR *BSTR;
typedef const WCHAR *LPCBSTR;
typedef USHORT VARTYPE;
typedef short VARIANT_BOOL;
typedef double DATE;

#define VARIANT_TRUE ((VARIANT_BOOL)-1)
#define VARIANT_FALSE ((VARIANT_BOOL)0)

typedef union tagCY {
    struct {
        ULONG Lo;
        LONG Hi;
    };
    LONGLONG int64;
} CY;

typedef struct tagDEC {
    USHORT wReserved;
    BYTE scale;
    BYTE sign;
    ULONG Hi32;
    ULONGLONG Lo64;
} DECIMAL;

enum VARENUM {
    VT_EMPTY = 0,
    VT_NULL = 1,
    VT_I2 = 2,
    VT_I4 = 3,
    VT_R4 = 4,
    VT_R8 = 5,
    VT_CY = 6,
    VT_DATE = 7,
    VT_BSTR = 8,
    VT_DISPATCH = 9,
    VT_ERROR = 10,
    VT_BOOL = 11,
    VT_VARIANT = 12,
    VT_UNKNOWN = 13,
    VT_DECIMAL = 14,
    VT_I1 = 16,
    VT_UI1 = 17,
    VT_UI2 = 18,
    VT_UI4 = 19,
    VT_I8 = 20,
    VT_UI8 = 21,
    VT_INT = 22,
    VT_UINT = 23,
    VT_VOID = 24,
    VT_HRESULT = 25,
    VT_PTR = 26,
    VT_SAFEARRAY = 27,
    VT_CARRAY = 28,
    VT_USERDEFINED = 29,
    VT_LPSTR = 30,
    VT_LPWSTR = 31,
    VT_RECORD = 36,
    VT_INT_PTR = 37,
    VT_UINT_PTR = 38,
    VT_FILETIME = 64,
    VT_BLOB = 65,
    VT_CLSID = 72,
    VT_VECTOR = 0x1000,
    VT_ARRAY = 0x2000,
    VT_BYREF = 0x4000,
    VT_RESERVED = 0x8000,
    VT_ILLEGAL = 0xFFFF,
    VT_TYPEMASK = 0x0FFF,
};

typedef struct IDispatch IDispatch;
typedef struct IRecordInfo IRecordInfo;

/* ------------------------------------------------------------------ safe arrays */

typedef struct tagSAFEARRAYBOUND {
    ULONG cElements;
    LONG lLbound;
} SAFEARRAYBOUND, *LPSAFEARRAYBOUND;

typedef struct tagSAFEARRAY {
    USHORT cDims;
    USHORT fFeatures;
    ULONG cbElements;
    ULONG cLocks;
    PVOID pvData;
    SAFEARRAYBOUND rgsabound[1];
} SAFEARRAY, *LPSAFEARRAY;
MIME_FIELD_AT(SAFEARRAY, pvData, sizeof(void *) == 4 ? 12 : 16);
MIME_FIELD_AT(SAFEARRAY, rgsabound, sizeof(void *) == 4 ? 16 : 24);

#define FADF_AUTO 0x0001
#define FADF_STATIC 0x0002
#define FADF_EMBEDDED 0x0004
#define FADF_FIXEDSIZE 0x0010
#define FADF_RECORD 0x0020
#define FADF_HAVEIID 0x0040
#define FADF_HAVEVARTYPE 0x0080
#define FADF_BSTR 0x0100
#define FADF_UNKNOWN 0x0200
#define FADF_DISPATCH 0x0400
#define FADF_VARIANT 0x0800
#define FADF_RESERVED 0xF008

/* ------------------------------------------------------------------ variants */

typedef struct tagVARIANT VARIANT, *LPVARIANT, VARIANTARG, *LPVARIANTARG;
struct tagVARIANT {
    union {
        struct {
            VARTYPE vt;
            WORD wReserved1;
            WORD wReserved2;
            WORD wReserved3;
            union {
                LONGLONG llVal;
                LONG lVal;
                BYTE bVal;
                SHORT iVal;
                FLOAT fltVal;
                DOUBLE dblVal;
                VARIANT_BOOL boolVal;
                SCODE scode;
                CY cyVal;
                DATE date;
                BSTR bstrVal;
                IUnknown *punkVal;
                IDispatch *pdispVal;
                SAFEARRAY *parray;
                CHAR cVal;
                USHORT uiVal;
                ULONG ulVal;
                ULONGLONG ullVal;
                INT intVal;
                UINT uintVal;
                PVOID byref;
                VARIANT *pvarVal;
                BSTR *pbstrVal;
                IUnknown **ppunkVal;
                SAFEARRAY **pparray;
                struct {
                    PVOID pvRecord;
                    IRecordInfo *pRecInfo;
                };
            };
        };
        DECIMAL decVal;
    };
};
MIME_STATIC_ASSERT(sizeof(VARIANT) == (sizeof(void *) == 4 ? 16 : 24), "VARIANT size");

#define V_VT(v) ((v)->vt)
#define V_BSTR(v) ((v)->bstrVal)
#define V_I4(v) ((v)->lVal)
#define V_UNKNOWN(v) ((v)->punkVal)
#define V_DISPATCH(v) ((v)->pdispVal)
#define V_ARRAY(v) ((v)->parray)
#define V_BOOL(v) ((v)->boolVal)

/* ------------------------------------------------------------------ oleaut32 functions */

WINOLEAUTAPI BSTR WINAPI SysAllocString(const OLECHAR *string);
WINOLEAUTAPI BSTR WINAPI SysAllocStringLen(const OLECHAR *string, UINT length);
WINOLEAUTAPI BSTR WINAPI SysAllocStringByteLen(LPCSTR string, UINT length);
WINOLEAUTAPI INT WINAPI SysReAllocString(BSTR *bstr, const OLECHAR *string);
WINOLEAUTAPI INT WINAPI SysReAllocStringLen(BSTR *bstr, const OLECHAR *string, UINT length);
WINOLEAUTAPI void WINAPI SysFreeString(BSTR bstr);
WINOLEAUTAPI UINT WINAPI SysStringLen(BSTR bstr);
WINOLEAUTAPI UINT WINAPI SysStringByteLen(BSTR bstr);

WINOLEAUTAPI void WINAPI VariantInit(VARIANTARG *variant);
WINOLEAUTAPI HRESULT WINAPI VariantClear(VARIANTARG *variant);
WINOLEAUTAPI HRESULT WINAPI VariantCopy(VARIANTARG *dst, const VARIANTARG *src);
WINOLEAUTAPI HRESULT WINAPI VariantCopyInd(VARIANT *dst, const VARIANTARG *src);

WINOLEAUTAPI SAFEARRAY *WINAPI SafeArrayCreate(VARTYPE vt, UINT dims, SAFEARRAYBOUND *bounds);
WINOLEAUTAPI SAFEARRAY *WINAPI SafeArrayCreateVector(VARTYPE vt, LONG lower, ULONG count);
WINOLEAUTAPI HRESULT WINAPI SafeArrayAllocDescriptor(UINT dims, SAFEARRAY **out);
WINOLEAUTAPI HRESULT WINAPI SafeArrayAllocDescriptorEx(VARTYPE vt, UINT dims, SAFEARRAY **out);
WINOLEAUTAPI HRESULT WINAPI SafeArrayAllocData(SAFEARRAY *array);
WINOLEAUTAPI HRESULT WINAPI SafeArrayDestroy(SAFEARRAY *array);
WINOLEAUTAPI HRESULT WINAPI SafeArrayDestroyData(SAFEARRAY *array);
WINOLEAUTAPI HRESULT WINAPI SafeArrayDestroyDescriptor(SAFEARRAY *array);
WINOLEAUTAPI HRESULT WINAPI SafeArrayLock(SAFEARRAY *array);
WINOLEAUTAPI HRESULT WINAPI SafeArrayUnlock(SAFEARRAY *array);
WINOLEAUTAPI HRESULT WINAPI SafeArrayAccessData(SAFEARRAY *array, void **data);
WINOLEAUTAPI HRESULT WINAPI SafeArrayUnaccessData(SAFEARRAY *array);
WINOLEAUTAPI UINT WINAPI SafeArrayGetDim(SAFEARRAY *array);
WINOLEAUTAPI UINT WINAPI SafeArrayGetElemsize(SAFEARRAY *array);
WINOLEAUTAPI HRESULT WINAPI SafeArrayGetLBound(SAFEARRAY *array, UINT dim, LONG *bound);
WINOLEAUTAPI HRESULT WINAPI SafeArrayGetUBound(SAFEARRAY *array, UINT dim, LONG *bound);
WINOLEAUTAPI HRESULT WINAPI SafeArrayGetVartype(SAFEARRAY *array, VARTYPE *vt);
WINOLEAUTAPI HRESULT WINAPI SafeArrayPtrOfIndex(SAFEARRAY *array, LONG *indices, void **element);
WINOLEAUTAPI HRESULT WINAPI SafeArrayPutElement(SAFEARRAY *array, LONG *indices, void *value);
WINOLEAUTAPI HRESULT WINAPI SafeArrayGetElement(SAFEARRAY *array, LONG *indices, void *value);
WINOLEAUTAPI HRESULT WINAPI SafeArrayCopy(SAFEARRAY *array, SAFEARRAY **out);
WINOLEAUTAPI HRESULT WINAPI SafeArrayCopyData(SAFEARRAY *source, SAFEARRAY *target);
WINOLEAUTAPI HRESULT WINAPI SafeArrayRedim(SAFEARRAY *array, SAFEARRAYBOUND *bound);

#define VAR_TIMEVALUEONLY 0x00000001
#define VAR_DATEVALUEONLY 0x00000002
WINOLEAUTAPI INT WINAPI SystemTimeToVariantTime(LPSYSTEMTIME time, double *date);
WINOLEAUTAPI INT WINAPI VariantTimeToSystemTime(DATE date, LPSYSTEMTIME time);
WINOLEAUTAPI HRESULT WINAPI VarDateFromStr(LPCOLESTR string, LCID lcid, ULONG flags, DATE *out);
WINOLEAUTAPI HRESULT WINAPI VarCyFromStr(LPCOLESTR string, LCID lcid, ULONG flags, CY *out);
WINOLEAUTAPI HRESULT WINAPI VarDecFromStr(LPCOLESTR string, LCID lcid, ULONG flags, DECIMAL *out);
WINOLEAUTAPI HRESULT WINAPI VarUI4FromStr(LPCOLESTR string, LCID lcid, ULONG flags, ULONG *out);
WINOLEAUTAPI HRESULT WINAPI VarBstrFromDate(DATE date, LCID lcid, ULONG flags, BSTR *out);
WINOLEAUTAPI HRESULT WINAPI VarBstrFromCy(CY value, LCID lcid, ULONG flags, BSTR *out);
WINOLEAUTAPI HRESULT WINAPI VarBstrFromDec(const DECIMAL *value, LCID lcid, ULONG flags, BSTR *out);

#endif
