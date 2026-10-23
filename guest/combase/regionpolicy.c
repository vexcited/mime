/*
 * Windows.Internal.System.Profile.RegionPolicyEvaluator: region dependent services, such as the EEA integrated
 * services, ask whether a policy is enabled. Windows reads the policy set from a json file next to its libraries.
 * mime has no such file, which windows treats as an empty set: every policy is not configured.
 */
#include "combase_private.h"
#include <mime/winrt_object.h>

const WCHAR CbRegionPolicyClass[] = L"Windows.Internal.System.Profile.RegionPolicyEvaluator";

MIME_DEFINE_GUID(IID_IRegionPolicyEvaluatorStatics, 0x1CA20398, 0xC1D9, 0x5F12, 0xB4, 0x0A, 0x65, 0x28, 0x20, 0x6E, 0x3B, 0x5E);

typedef struct IRegionPolicyEvaluatorStatics IRegionPolicyEvaluatorStatics;
#define IRegionPolicyEvaluatorStatics_METHODS(T)                                                                       \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *EvaluatePolicyState)(T * This, GUID policy, INT32 * state);                             \
    HRESULT(STDMETHODCALLTYPE *ForceEvaluatePolicyState)(T * This, GUID policy, INT32 * state);
MIME_COM_INTERFACE(IRegionPolicyEvaluatorStatics)

WRT_INSPECTABLE_THUNKS(IRegionPolicyEvaluatorStatics)

typedef struct IRegionPolicyFactory IRegionPolicyFactory;
#define IRegionPolicyFactory_METHODS(T)                                                                                \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *ActivateInstance)(T * This, IInspectable * *instance);
MIME_COM_INTERFACE(IRegionPolicyFactory)

WRT_INSPECTABLE_THUNKS(IRegionPolicyFactory)

/* 0 when the set does not name the policy, 1 when enabled for the region, 2 when not. */
static HRESULT STDMETHODCALLTYPE evaluate(IRegionPolicyEvaluatorStatics *This, GUID policy, INT32 *state)
{
    *state = 0;
    return S_OK;
}

/* same answer after an uncached reread of the set. */
static HRESULT STDMETHODCALLTYPE evaluate_again(IRegionPolicyEvaluatorStatics *This, GUID policy, INT32 *state)
{
    *state = 0;
    return S_OK;
}

/* the class has statics and no instances. */
static HRESULT STDMETHODCALLTYPE activate(IRegionPolicyFactory *This, IInspectable **instance)
{
    *instance = nullptr;
    return E_NOTIMPL;
}

static const IRegionPolicyFactoryVtbl factory_vtbl = {WRT_INSPECTABLE_ENTRIES(IRegionPolicyFactory), activate};
static const IRegionPolicyEvaluatorStaticsVtbl statics_vtbl = {WRT_INSPECTABLE_ENTRIES(IRegionPolicyEvaluatorStatics),
                                                               evaluate, evaluate_again};

static void factory_destroy(WRT_OBJECT *object)
{
    object->Refs = 1;
}

static const WRT_CLASS factory_class = {CbRegionPolicyClass, 2, {&IID_IActivationFactory, &IID_IRegionPolicyEvaluatorStatics}, 0,
                                        factory_destroy};
static WRT_OBJECT g_factory;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const void *vtables[2] = {&factory_vtbl, &statics_vtbl};
    WrtInit(&g_factory, &factory_class, vtables);
    return TRUE;
}

HRESULT CbRegionPolicyFactory(REFIID riid, void **factory)
{
    InitOnceExecuteOnce(&g_once, make, nullptr, nullptr);
    return WrtQuery(&g_factory.Ifaces[0], riid, factory);
}
