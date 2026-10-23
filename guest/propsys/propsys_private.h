#pragma once

#define PSSTDAPI HRESULT WINAPI
#define PROPSYSAPI
#include <mime/win32/propsys.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>

/* 1 for a scalar, else the element count of a vector or one dimensional array. */
ULONG PsElementCount(const PROPVARIANT *value);
BOOL PsValidType(VARTYPE vt);
BOOL PsValidValue(const PROPVARIANT *value);
/* 0 when base is not a vector element type. */
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
