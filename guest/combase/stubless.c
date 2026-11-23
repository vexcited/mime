#include "combase_private.h"
#include <mime/win32/rpcproxy.h>

#ifdef __i386__
/*
 * the methods of a proxy that has no code of its own, ObjectStublessClient3 to 32, which the 32-bit combase of windows
 * exports and oleaut32 imports by ordinal: guest/combase/x86/stubless.S. the proxy's vtable is preceded by its
 * stubless information, which names the procedure's format string, and rpcrt4's NdrClientCall2 makes the call with
 * the method's arguments as they are on the stack.
 */
typedef struct CB_STUBLESS_CALL {
    PMIDL_STUB_DESC Descriptor;
    PFORMAT_STRING Format;
    ULONG ArgumentBytes; /* what the stdcall method pops: the procedure's stack less the slot of its return value. */
    PVOID ClientCall;
} CB_STUBLESS_CALL;

#define OI_HAS_RPCFLAGS 0x08
#define OIF_HAS_RETURN 0x04

void CbStublessPrepare(ULONG method, PUCHAR arguments, CB_STUBLESS_CALL *call)
{
    static PVOID client_call;
    if (!client_call)
        client_call = (PVOID)GetProcAddress(LoadLibraryW(L"rpcrt4.dll"), "NdrClientCall2");
    void *This = *(void **)arguments;
    const void *const *vtbl = *(const void *const **)This;
    const MIDL_STUBLESS_PROXY_INFO *info = ((const MIDL_STUBLESS_PROXY_INFO *const *)vtbl)[-2];
    PFORMAT_STRING format = info->ProcFormatString + info->FormatStringOffset[method];
    /* the header of an Oi procedure: handle type, flags, rpc flags if the flags say so, number, stack size. */
    ULONG at = 2 + ((format[1] & OI_HAS_RPCFLAGS) ? 4 : 0) + 2;
    ULONG stack = *(const USHORT *)(format + at);
    at += 2;
    /* an explicit handle has its description next, a primitive one four bytes and the others six. */
    if (format[0] == 0)
        at += format[at] == 0x32 /* FC_BIND_PRIMITIVE */ ? 4 : 6;
    /* the client and server buffer sizes, then the flags of the procedure. */
    UCHAR oi2 = format[at + 4];
    call->Descriptor = info->pStubDesc;
    call->Format = format;
    call->ArgumentBytes = stack - ((oi2 & OIF_HAS_RETURN) ? sizeof(void *) : 0);
    call->ClientCall = client_call;
}
#endif
