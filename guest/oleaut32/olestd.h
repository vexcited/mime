/* helpers shared by the standard font and picture objects. */
#ifndef MIME_OLEAUT32_OLESTD_H
#define MIME_OLEAUT32_OLESTD_H

#include <mime/win32/winuser.h>
#include "oleaut32_private.h"

typedef struct OLESTD_MEMBER {
    const WCHAR *Name;
    DISPID Id;
} OLESTD_MEMBER;

/* CTL_E_* values give 0xA0000 once masked with this. */
#define OLESTD_CONTROL_MASK 0x1FFF0000
#define OLESTD_CONTROL_ERROR 0x000A0000

HRESULT OleStdIdsOfNames(const OLESTD_MEMBER *members, int count, REFIID riid, LPOLESTR *names, UINT name_count,
                         DISPID *ids);
HRESULT OleStdException(HRESULT hr, EXCEPINFO *info);
HRESULT OleStdTypeInfo(UINT index, REFGUID id, ITypeInfo **info);
LONGLONG OleStdRoundDiv(LONGLONG numerator, LONGLONG denominator);

#endif
