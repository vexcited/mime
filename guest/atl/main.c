/*
 * atl.dll: the runtime of the Active Template Library of the version that Windows ships. what programs of the Windows
 * shells import is the smart pointer helpers, which are here in full. the rest of the interface is not implemented and
 * answers that it is not.
 */
#include <mime/win32/objbase.h>

#define E_NOTIMPL_ ((HRESULT)0x80004001)
#define E_NOINTERFACE_ ((HRESULT)0x80004002)

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

typedef ULONG_PTR ARG;

#define NOT_IMPLEMENTED_0(name) HRESULT WINAPI name(void) { return E_NOTIMPL_; }
#define NOT_IMPLEMENTED_2(name) HRESULT WINAPI name(ARG a, ARG b) { return E_NOTIMPL_; }
#define NOT_IMPLEMENTED_3(name) HRESULT WINAPI name(ARG a, ARG b, ARG c) { return E_NOTIMPL_; }
#define NOT_IMPLEMENTED_4(name) HRESULT WINAPI name(ARG a, ARG b, ARG c, ARG d) { return E_NOTIMPL_; }
#define NOT_IMPLEMENTED_5(name) HRESULT WINAPI name(ARG a, ARG b, ARG c, ARG d, ARG e) { return E_NOTIMPL_; }

/* the pointer is set to the new one, which is counted, and the old one is let go. */
IUnknown *WINAPI AtlComPtrAssign(IUnknown **pointer, IUnknown *value)
{
    if (value)
        value->lpVtbl->AddRef(value);
    if (*pointer)
        (*pointer)->lpVtbl->Release(*pointer);
    *pointer = value;
    return value;
}

/* the same for a pointer to an interface that is asked of the object. */
IUnknown *WINAPI AtlComQIPtrAssign(IUnknown **pointer, IUnknown *value, REFIID riid)
{
    IUnknown *asked = nullptr;
    if (value)
        value->lpVtbl->QueryInterface(value, riid, (void **)&asked);
    if (*pointer)
        (*pointer)->lpVtbl->Release(*pointer);
    *pointer = asked;
    return asked;
}

/* one line of the table of interfaces of a class: the identifier, where its pointer is in the object, and how to get it. */
typedef struct ATL_INTMAP_ENTRY {
    const IID *Iid;
    ULONG_PTR Offset;
    ULONG_PTR Function;
} ATL_INTMAP_ENTRY;

MIME_DEFINE_GUID(IID_IUnknown_, 0x00000000, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

HRESULT WINAPI AtlInternalQueryInterface(void *object, const ATL_INTMAP_ENTRY *entries, REFIID riid, void **result)
{
    if (!entries || !result)
        return E_NOTIMPL_;
    *result = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown_)) {
        /* the first entry that is a plain offset stands for the object. */
        for (const ATL_INTMAP_ENTRY *e = entries; e->Iid; e++) {
            if (e->Function == 1) {
                IUnknown *unknown = (IUnknown *)((uint8_t *)object + e->Offset);
                unknown->lpVtbl->AddRef(unknown);
                *result = unknown;
                return S_OK;
            }
        }
        return E_NOINTERFACE_;
    }
    for (const ATL_INTMAP_ENTRY *e = entries; e->Iid; e++) {
        if (!IsEqualIID(e->Iid, riid))
            continue;
        if (e->Function == 1) {
            IUnknown *unknown = (IUnknown *)((uint8_t *)object + e->Offset);
            unknown->lpVtbl->AddRef(unknown);
            *result = unknown;
            return S_OK;
        }
        if (e->Function > 1) {
            /* a function gets the object, the identifier and the result, and gives the pointer. */
            typedef HRESULT(WINAPI * GET)(void *, REFIID, void **, ULONG_PTR);
            return ((GET)e->Function)(object, riid, result, e->Offset);
        }
    }
    return E_NOINTERFACE_;
}

DWORD WINAPI AtlGetVersion(void *module)
{
    return 0x0300;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_OK;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **result)
{
    if (result)
        *result = nullptr;
    return 0x80040111; /* CLASS_E_CLASSNOTAVAILABLE */
}

NOT_IMPLEMENTED_0(DllRegisterServer)
NOT_IMPLEMENTED_0(DllUnregisterServer)
NOT_IMPLEMENTED_4(AtlAdvise)
NOT_IMPLEMENTED_2(AtlUnadvise)
NOT_IMPLEMENTED_2(AtlFreeMarshalStream)
NOT_IMPLEMENTED_4(AtlMarshalPtrInProc)
NOT_IMPLEMENTED_4(AtlUnmarshalPtr)
NOT_IMPLEMENTED_5(AtlModuleGetClassObject)
NOT_IMPLEMENTED_3(AtlModuleInit)
NOT_IMPLEMENTED_4(AtlModuleRegisterClassObjects)
NOT_IMPLEMENTED_4(AtlModuleRegisterServer)
NOT_IMPLEMENTED_4(AtlModuleRegisterTypeLib)
NOT_IMPLEMENTED_2(AtlModuleRevokeClassObjects)
NOT_IMPLEMENTED_2(AtlModuleTerm)
NOT_IMPLEMENTED_4(AtlModuleUnregisterServer)
NOT_IMPLEMENTED_5(AtlModuleUpdateRegistryFromResourceD)
NOT_IMPLEMENTED_5(AtlWaitWithMessageLoop)
NOT_IMPLEMENTED_5(AtlSetErrorInfo)
NOT_IMPLEMENTED_5(AtlCreateTargetDC)
NOT_IMPLEMENTED_3(AtlHiMetricToPixel)
NOT_IMPLEMENTED_3(AtlPixelToHiMetric)
NOT_IMPLEMENTED_3(AtlDevModeW2A)
NOT_IMPLEMENTED_5(AtlAxDialogBoxW)
NOT_IMPLEMENTED_5(AtlAxDialogBoxA)
NOT_IMPLEMENTED_5(AtlAxCreateDialogW)
NOT_IMPLEMENTED_5(AtlAxCreateDialogA)
NOT_IMPLEMENTED_5(AtlAxCreateControl)
NOT_IMPLEMENTED_5(AtlAxCreateControlEx)
NOT_IMPLEMENTED_3(AtlAxAttachControl)
NOT_IMPLEMENTED_0(AtlAxWinInit)
NOT_IMPLEMENTED_3(AtlModuleAddCreateWndData)
NOT_IMPLEMENTED_2(AtlModuleExtractCreateWndData)
NOT_IMPLEMENTED_3(AtlModuleRegisterWndClassInfoW)
NOT_IMPLEMENTED_3(AtlModuleRegisterWndClassInfoA)
NOT_IMPLEMENTED_3(AtlAxGetControl)
NOT_IMPLEMENTED_3(AtlAxGetHost)
NOT_IMPLEMENTED_5(AtlRegisterClassCategoriesHelper)
NOT_IMPLEMENTED_5(AtlIPersistStreamInit_Load)
NOT_IMPLEMENTED_5(AtlIPersistStreamInit_Save)
NOT_IMPLEMENTED_5(AtlIPersistPropertyBag_Load)
NOT_IMPLEMENTED_5(AtlIPersistPropertyBag_Save)
NOT_IMPLEMENTED_3(AtlGetObjectSourceInterface)
NOT_IMPLEMENTED_4(AtlModuleUnRegisterTypeLib)
NOT_IMPLEMENTED_5(AtlModuleLoadTypeLib)
NOT_IMPLEMENTED_5(AtlModuleUnregisterServerEx)
NOT_IMPLEMENTED_4(AtlModuleAddTermFunc)
NOT_IMPLEMENTED_5(AtlSetErrorInfo2)
NOT_IMPLEMENTED_5(AtlIPersistStreamInit_GetSizeMax)
