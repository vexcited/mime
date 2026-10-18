/* propsys.dll's own declarations. */
#pragma once

#define PSSTDAPI HRESULT WINAPI
#define PROPSYSAPI
#include <mime/win32/propsys.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>

/* the elements a property value holds: 1 for a scalar, the count of a vector or a one dimensional array. */
ULONG PsElementCount(const PROPVARIANT *value);
/* whether vt names a type a PROPVARIANT may carry, as propsys checks it. */
BOOL PsValidType(VARTYPE vt);
BOOL PsValidValue(const PROPVARIANT *value);
/* the size of one element of a vector of the given base type, 0 for none. */
UINT PsElementSize(VARTYPE base);

HRESULT PsAllocString(LPCWSTR source, LPWSTR *out);
HRESULT PsAllocStringLen(LPCWSTR source, SIZE_T length, LPWSTR *out);
void *PsAllocZero(SIZE_T size);
HRESULT PsAnsiToWideAlloc(LPCSTR source, LPWSTR *out);
HRESULT PsWideToAnsiAlloc(LPCWSTR source, LPSTR *out);
HRESULT PsCopyString(LPWSTR buffer, UINT length, LPCWSTR source);

BOOL PsGuidFromString(LPCWSTR string, GUID *guid);
int PsStrCmpLogical(LPCWSTR a, LPCWSTR b);
int PsStrCmp(LPCWSTR a, LPCWSTR b, BOOL ignore_case);
HRESULT PsDoubleToString(double value, LPWSTR *out);
BOOL PsParseDouble(LPCWSTR string, double *value);

/* one element of a vector, array or by reference value, or the scalar itself. */
HRESULT PsElementPointer(const PROPVARIANT *value, ULONG index, const void **element);
