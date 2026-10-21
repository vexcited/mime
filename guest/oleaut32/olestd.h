/*
 * what the standard font and picture objects have in common: errors of controls that reach a caller of IDispatch as
 * exceptions, the lookup of member names, the type information of stdole and a rounded division.
 */
#ifndef MIME_OLEAUT32_OLESTD_H
#define MIME_OLEAUT32_OLESTD_H

#include <mime/win32/winuser.h>
#include "oleaut32_private.h"

/* a member of a dispatch interface: its name and identifier. */
typedef struct OLESTD_MEMBER {
    const WCHAR *Name;
    DISPID Id;
} OLESTD_MEMBER;

/* the code of a control error: the facility and code bits of CTL_E_* values, 0xA0000 once masked with this. */
#define OLESTD_CONTROL_MASK 0x1FFF0000
#define OLESTD_CONTROL_ERROR 0x000A0000

HRESULT OleStdIdsOfNames(const OLESTD_MEMBER *members, int count, REFIID riid, LPOLESTR *names, UINT name_count,
                         DISPID *ids);
HRESULT OleStdException(HRESULT hr, EXCEPINFO *info);
HRESULT OleStdTypeInfo(UINT index, REFGUID id, ITypeInfo **info);
LONGLONG OleStdRoundDiv(LONGLONG numerator, LONGLONG denominator);

#endif
