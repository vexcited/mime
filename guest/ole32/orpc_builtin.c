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

/*
 * The interfaces of automation and of the connection points are marshaled by the classes oleaut32 has for them, the way
 * windows registers them: the proxy and stub class of an interface of a type library is registered with it, and these are
 * the ones a system registers by itself. The classes are found in oleaut32 when nothing registers a server for them.
 */
#define OA_CLASS(n) {0x00020420 + (n), 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}

static const struct {
    IID Interface;
    CLSID Class;
} g_automation[] = {
    {{0x00020400, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, OA_CLASS(0)}, /* IDispatch */
    {{0x00020404, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, OA_CLASS(1)}, /* IEnumVARIANT */
    {{0x00020401, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, OA_CLASS(2)}, /* ITypeInfo */
    {{0x00020412, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, OA_CLASS(2)}, /* ITypeInfo2 */
    {{0x00020402, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, OA_CLASS(3)}, /* ITypeLib */
    {{0x00020411, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, OA_CLASS(3)}, /* ITypeLib2 */
    {{0x00020403, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, OA_CLASS(5)}, /* ITypeComp */
    {{0x1CF2B120, 0x547D, 0x101B, {0x8E, 0x65, 0x08, 0x00, 0x2B, 0x2B, 0xD1, 0x19}}, OA_CLASS(0)}, /* IErrorInfo */
    {{0x22F03340, 0x547D, 0x101B, {0x8E, 0x65, 0x08, 0x00, 0x2B, 0x2B, 0xD1, 0x19}}, OA_CLASS(0)}, /* ICreateErrorInfo */
    {{0xDF0B3D60, 0x548F, 0x101B, {0x8E, 0x65, 0x08, 0x00, 0x2B, 0x2B, 0xD1, 0x19}}, OA_CLASS(0)}, /* ISupportErrorInfo */
    {{0xB196B284, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}}, OA_CLASS(0)}, /* IConnectionPointContainer */
    {{0xB196B285, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}}, OA_CLASS(0)}, /* IEnumConnectionPoints */
    {{0xB196B286, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}}, OA_CLASS(0)}, /* IConnectionPoint */
    {{0xB196B287, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}}, OA_CLASS(0)}, /* IEnumConnections */
};

BOOL OrpcAutomationClass(REFIID iid, CLSID *clsid)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_automation); i++) {
        if (IsEqualGUID(iid, &g_automation[i].Interface)) {
            *clsid = g_automation[i].Class;
            return TRUE;
        }
    }
    return FALSE;
}

/* the dll of a class that windows registers and this system does not need to: the proxies of automation. */
BOOL OrpcBuiltinServer(REFCLSID clsid, const WCHAR **dll)
{
    if (clsid->Data1 >= 0x00020420 && clsid->Data1 <= 0x00020426 && !clsid->Data2 && !clsid->Data3 &&
        clsid->Data4[0] == 0xC0 && clsid->Data4[7] == 0x46) {
        *dll = L"oleaut32.dll";
        return TRUE;
    }
    return FALSE;
}

/* the class that marshals an interface of this file, for CoGetPSClsid. */
BOOL OrpcBuiltinClass(REFIID iid, CLSID *clsid)
{
    if (!builtin(iid))
        return FALSE;
    *clsid = orpcbuiltin_ProxyClsid;
    return TRUE;
}
