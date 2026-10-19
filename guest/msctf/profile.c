/*
 * the input processor profiles: which text services and keyboard layouts the user has. mime has no text service
 * to install, the host's input method is built in, so what the profiles report is the keyboard layout.
 */
#include "msctf_private.h"
#include <mime/win32/winnls.h>

typedef struct PROFILES {
    ITfInputProcessorProfiles Profiles;
    ITfInputProcessorProfileMgr Mgr;
    ITfSource Source;
    LONG Refs;
    SINKS Sinks;
} PROFILES;

static HRESULT profiles_query(PROFILES *p, REFIID riid, void **object)
{
    void *found = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfInputProcessorProfiles))
        found = &p->Profiles;
    else if (IsEqualIID(riid, &IID_ITfInputProcessorProfileMgr))
        found = &p->Mgr;
    else if (IsEqualIID(riid, &IID_ITfSource))
        found = &p->Source;
    if (!found) {
        CTF_TRACE("msctf: profiles query %08lx refused\n", riid->Data1);
        *object = nullptr;
        return E_NOINTERFACE;
    }
    *object = found;
    InterlockedIncrement(&p->Refs);
    return S_OK;
}

static ULONG profiles_release(PROFILES *p)
{
    LONG refs = InterlockedDecrement(&p->Refs);
    if (!refs) {
        SinksFree(&p->Sinks);
        CtfFree(p);
        CtfObjectDestroyed();
    }
    return refs;
}

#define PROFILE_THUNKS(Iface, field)                                                                                   \
    static HRESULT STDMETHODCALLTYPE field##_query(Iface *This, REFIID riid, void **object)                            \
    {                                                                                                                  \
        return profiles_query(CTF_OBJ(PROFILES, field, This), riid, object);                                           \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE field##_add_ref(Iface *This)                                                        \
    {                                                                                                                  \
        return InterlockedIncrement(&CTF_OBJ(PROFILES, field, This)->Refs);                                            \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE field##_release(Iface *This)                                                        \
    {                                                                                                                  \
        return profiles_release(CTF_OBJ(PROFILES, field, This));                                                       \
    }

PROFILE_THUNKS(ITfInputProcessorProfiles, Profiles)
PROFILE_THUNKS(ITfInputProcessorProfileMgr, Mgr)
PROFILE_THUNKS(ITfSource, Source)

static LANGID current_language(void)
{
    return (LANGID)((ULONG_PTR)GetKeyboardLayout(0) & 0xFFFF);
}

/* ------------------------------------------------------------------ ITfInputProcessorProfiles */

static HRESULT STDMETHODCALLTYPE pr_register(ITfInputProcessorProfiles *This, REFCLSID clsid)
{
    return E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE pr_add_profile(ITfInputProcessorProfiles *This, REFCLSID clsid, LANGID langid,
                                                REFGUID profile, const WCHAR *description, ULONG cch,
                                                const WCHAR *icon, ULONG icon_cch, ULONG icon_index)
{
    return E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE pr_remove_profile(ITfInputProcessorProfiles *This, REFCLSID clsid, LANGID langid,
                                                   REFGUID profile)
{
    return E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE pr_enum_info(ITfInputProcessorProfiles *This, IEnumGUID **out)
{
    if (!out)
        return E_INVALIDARG;
    return EnumGuidsCreate(nullptr, 0, out);
}

static HRESULT STDMETHODCALLTYPE pr_get_default(ITfInputProcessorProfiles *This, LANGID langid, REFGUID catid,
                                                CLSID *clsid, GUID *profile)
{
    if (!catid || !clsid || !profile)
        return E_INVALIDARG;
    memset(clsid, 0, sizeof(*clsid));
    memset(profile, 0, sizeof(*profile));
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE pr_set_default(ITfInputProcessorProfiles *This, LANGID langid, REFCLSID clsid,
                                                REFGUID profile)
{
    return E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE pr_activate(ITfInputProcessorProfiles *This, REFCLSID clsid, LANGID langid,
                                             REFGUID profile)
{
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE pr_get_active(ITfInputProcessorProfiles *This, REFCLSID clsid, LANGID *langid,
                                               GUID *profile)
{
    if (!clsid || !langid || !profile)
        return E_INVALIDARG;
    *langid = current_language();
    memset(profile, 0, sizeof(*profile));
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE pr_get_description(ITfInputProcessorProfiles *This, REFCLSID clsid, LANGID langid,
                                                    REFGUID profile, BSTR *description)
{
    if (!description)
        return E_INVALIDARG;
    *description = nullptr;
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE pr_get_current_language(ITfInputProcessorProfiles *This, LANGID *langid)
{
    if (!langid)
        return E_INVALIDARG;
    *langid = current_language();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pr_change_language(ITfInputProcessorProfiles *This, LANGID langid)
{
    return langid == current_language() ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE pr_get_language_list(ITfInputProcessorProfiles *This, LANGID **list, ULONG *count)
{
    if (!list || !count)
        return E_INVALIDARG;
    *list = CoTaskMemAlloc(sizeof(LANGID));
    if (!*list)
        return E_OUTOFMEMORY;
    (*list)[0] = current_language();
    *count = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pr_enum_profiles(ITfInputProcessorProfiles *This, LANGID langid,
                                                  IEnumTfLanguageProfiles **out)
{
    if (!out)
        return E_INVALIDARG;
    return EnumCreate(&IID_IEnumTfLanguageProfiles, nullptr, 0, (void **)out);
}

static HRESULT STDMETHODCALLTYPE pr_enable(ITfInputProcessorProfiles *This, REFCLSID clsid, LANGID langid,
                                           REFGUID profile, BOOL enable)
{
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE pr_is_enabled(ITfInputProcessorProfiles *This, REFCLSID clsid, LANGID langid,
                                               REFGUID profile, BOOL *enabled)
{
    if (!enabled)
        return E_INVALIDARG;
    *enabled = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pr_enable_by_default(ITfInputProcessorProfiles *This, REFCLSID clsid, LANGID langid,
                                                      REFGUID profile, BOOL enable)
{
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE pr_substitute(ITfInputProcessorProfiles *This, REFCLSID clsid, LANGID langid,
                                               REFGUID profile, HKL layout)
{
    return E_INVALIDARG;
}

static const ITfInputProcessorProfilesVtbl profiles_vtbl = {
    Profiles_query, Profiles_add_ref, Profiles_release, pr_register, pr_register, pr_add_profile, pr_remove_profile,
    pr_enum_info, pr_get_default, pr_set_default, pr_activate, pr_get_active, pr_get_description,
    pr_get_current_language, pr_change_language, pr_get_language_list, pr_enum_profiles, pr_enable, pr_is_enabled,
    pr_enable_by_default, pr_substitute};

/* ------------------------------------------------------------------ ITfInputProcessorProfileMgr */

static void keyboard_profile(TF_INPUTPROCESSORPROFILE *profile)
{
    HKL layout = GetKeyboardLayout(0);
    memset(profile, 0, sizeof(*profile));
    profile->dwProfileType = TF_PROFILETYPE_KEYBOARDLAYOUT;
    profile->langid = (LANGID)((ULONG_PTR)layout & 0xFFFF);
    profile->catid = GUID_TFCAT_TIP_KEYBOARD;
    profile->hkl = layout;
    profile->hklSubstitute = layout;
    profile->dwFlags = TF_IPP_FLAG_ACTIVE | TF_IPP_FLAG_ENABLED;
}

static HRESULT STDMETHODCALLTYPE mg_activate(ITfInputProcessorProfileMgr *This, DWORD type, LANGID langid, REFCLSID clsid,
                                             REFGUID profile, HKL layout, DWORD flags)
{
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE mg_deactivate(ITfInputProcessorProfileMgr *This, DWORD type, LANGID langid,
                                               REFCLSID clsid, REFGUID profile, HKL layout, DWORD flags)
{
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE mg_get_profile(ITfInputProcessorProfileMgr *This, DWORD type, LANGID langid,
                                                REFCLSID clsid, REFGUID profile, HKL layout,
                                                TF_INPUTPROCESSORPROFILE *out)
{
    if (!out)
        return E_INVALIDARG;
    if (type == TF_PROFILETYPE_KEYBOARDLAYOUT && (!layout || layout == GetKeyboardLayout(0))) {
        keyboard_profile(out);
        return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE mg_enum_profiles(ITfInputProcessorProfileMgr *This, LANGID langid,
                                                  IEnumTfInputProcessorProfiles **out)
{
    if (!out)
        return E_INVALIDARG;
    return EnumCreate(&IID_IEnumTfInputProcessorProfiles, nullptr, 0, (void **)out);
}

static HRESULT STDMETHODCALLTYPE mg_release_processor(ITfInputProcessorProfileMgr *This, REFCLSID clsid, DWORD flags)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mg_register(ITfInputProcessorProfileMgr *This, REFCLSID clsid, LANGID langid,
                                             REFGUID profile, const WCHAR *description, ULONG cch, const WCHAR *icon,
                                             ULONG icon_cch, ULONG icon_index, HKL substitute, DWORD layout,
                                             BOOL enabled, DWORD flags)
{
    return E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE mg_unregister(ITfInputProcessorProfileMgr *This, REFCLSID clsid, LANGID langid,
                                               REFGUID profile, DWORD flags)
{
    return E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE mg_get_active(ITfInputProcessorProfileMgr *This, REFGUID catid,
                                               TF_INPUTPROCESSORPROFILE *out)
{
    if (!catid || !out)
        return E_INVALIDARG;
    memset(out, 0, sizeof(*out));
    if (!IsEqualGUID(catid, &GUID_TFCAT_TIP_KEYBOARD))
        return E_INVALIDARG;
    keyboard_profile(out);
    return S_OK;
}

static const ITfInputProcessorProfileMgrVtbl mgr_vtbl = {
    Mgr_query, Mgr_add_ref, Mgr_release, mg_activate, mg_deactivate, mg_get_profile, mg_enum_profiles,
    mg_release_processor, mg_register, mg_unregister, mg_get_active};

/* ------------------------------------------------------------------ ITfSource */

static const IID *const g_profile_sinks[] = {&IID_ITfInputProcessorProfileActivationSink,
                                            &IID_ITfActiveLanguageProfileNotifySink,
                                            &IID_ITfLanguageProfileNotifySink};

static HRESULT STDMETHODCALLTYPE src_advise(ITfSource *This, REFIID riid, IUnknown *sink, DWORD *cookie)
{
    return SinksAdvise(&CTF_OBJ(PROFILES, Source, This)->Sinks, g_profile_sinks, 3, riid, sink, cookie);
}

static HRESULT STDMETHODCALLTYPE src_unadvise(ITfSource *This, DWORD cookie)
{
    return SinksUnadvise(&CTF_OBJ(PROFILES, Source, This)->Sinks, cookie);
}

static const ITfSourceVtbl source_vtbl = {Source_query, Source_add_ref, Source_release, src_advise, src_unadvise};

HRESULT ProfilesCreate(REFIID riid, void **object)
{
    PROFILES *p = CtfAlloc(sizeof(*p));
    if (!p)
        return E_OUTOFMEMORY;
    p->Profiles.lpVtbl = &profiles_vtbl;
    p->Mgr.lpVtbl = &mgr_vtbl;
    p->Source.lpVtbl = &source_vtbl;
    p->Refs = 1;
    CtfObjectCreated();
    HRESULT hr = profiles_query(p, riid, object);
    profiles_release(p);
    return hr;
}
