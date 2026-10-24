#ifndef MIME_COMBASE_PARAMETERIZED_H
#define MIME_COMBASE_PARAMETERIZED_H

#include <mime/win32/roapi.h>

typedef struct MIME_RO_BUILDER MIME_RO_BUILDER;
typedef struct MIME_RO_LOCATOR MIME_RO_LOCATOR;

typedef struct MIME_RO_BUILDER_VTBL {
    HRESULT(WINAPI *SetWinRtInterface)(MIME_RO_BUILDER *, GUID);
    HRESULT(WINAPI *SetDelegate)(MIME_RO_BUILDER *, GUID);
    HRESULT(WINAPI *SetInterfaceGroupSimpleDefault)(MIME_RO_BUILDER *, PCWSTR, PCWSTR, const GUID *);
    HRESULT(WINAPI *SetInterfaceGroupParameterizedDefault)(MIME_RO_BUILDER *, PCWSTR, UINT32, PCWSTR const *);
    HRESULT(WINAPI *SetRuntimeClassSimpleDefault)(MIME_RO_BUILDER *, PCWSTR, PCWSTR, const GUID *);
    HRESULT(WINAPI *SetRuntimeClassParameterizedDefault)(MIME_RO_BUILDER *, PCWSTR, UINT32, PCWSTR const *);
    HRESULT(WINAPI *SetStruct)(MIME_RO_BUILDER *, PCWSTR, UINT32, PCWSTR const *);
    HRESULT(WINAPI *SetEnum)(MIME_RO_BUILDER *, PCWSTR, PCWSTR);
    HRESULT(WINAPI *SetParameterizedInterface)(MIME_RO_BUILDER *, GUID, UINT32);
    HRESULT(WINAPI *SetParameterizedDelegate)(MIME_RO_BUILDER *, GUID, UINT32);
} MIME_RO_BUILDER_VTBL;

struct MIME_RO_BUILDER {
    const MIME_RO_BUILDER_VTBL *lpVtbl;
};

typedef struct MIME_RO_LOCATOR_VTBL {
    HRESULT(WINAPI *Locate)(const MIME_RO_LOCATOR *, PCWSTR, MIME_RO_BUILDER *);
} MIME_RO_LOCATOR_VTBL;

struct MIME_RO_LOCATOR {
    const MIME_RO_LOCATOR_VTBL *lpVtbl;
};

ROAPI HRESULT WINAPI RoGetParameterizedTypeInstanceIID(UINT32 count, PCWSTR const *names,
                                                       const MIME_RO_LOCATOR *locator, GUID *iid, HANDLE *extra);
ROAPI void WINAPI RoFreeParameterizedTypeExtra(HANDLE extra);
ROAPI PCSTR WINAPI RoParameterizedTypeExtraGetTypeSignature(HANDLE extra);

#endif
