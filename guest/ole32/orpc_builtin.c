/*
 * the proxies and stubs of the interfaces that ole32 itself knows how to carry between processes: IClassFactory, which
 * every local server is reached through, and INotificationActivationCallback, which a toast is activated through. They
 * are written at build time by tools/ndrgen.py from builtin_ps.py, as midl writes a proxy file of stubless proxies; the
 * interpreter of rpcrt4 does the rest.
 */
#include "orpc.h"
#include <mime/win32/notificationactivationcallback.h>
#include "ole32/builtin_ps.h"

static BOOL builtin(REFIID iid)
{
    return IsEqualGUID(iid, &IID_IClassFactory) || IsEqualGUID(iid, &IID_INotificationActivationCallback);
}

HRESULT OrpcBuiltinFactory(REFIID iid, IPSFactoryBuffer **factory)
{
    if (!builtin(iid))
        return E_NOINTERFACE;
    return orpcbuiltin_DllGetClassObject(&orpcbuiltin_ProxyClsid, &IID_IPSFactoryBuffer, (void **)factory);
}

/* the class that marshals an interface of this file, for CoGetPSClsid. */
BOOL OrpcBuiltinClass(REFIID iid, CLSID *clsid)
{
    if (!builtin(iid))
        return FALSE;
    *clsid = orpcbuiltin_ProxyClsid;
    return TRUE;
}
