/*
 * the classes of the standard objects, which programs make with CoCreateInstance: the font, its older form and the picture, each made as
 * OleCreateFontIndirect and OleCreatePictureIndirect make them without a description.
 */
#include "oleaut32_private.h"

static const GUID g_clsid_font = {0x0BE35203, 0x8F91, 0x11CE, {0x9D, 0xE3, 0x00, 0xAA, 0x00, 0x4B, 0xB8, 0x51}};
static const GUID g_clsid_picture = {0x0BE35204, 0x8F91, 0x11CE, {0x9D, 0xE3, 0x00, 0xAA, 0x00, 0x4B, 0xB8, 0x51}};
static const GUID g_clsid_old_font = {0x46763EE0, 0xCAB2, 0x11CE, {0x8C, 0x20, 0x00, 0xAA, 0x00, 0x51, 0xE5, 0xD4}};

typedef struct {
    const IClassFactoryVtbl *lpVtbl;
    BOOL picture;
} STD_FACTORY;

static HRESULT STDMETHODCALLTYPE factory_query(IClassFactory *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IClassFactory)) {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    *object = This;
    return S_OK;
}

/* the factories are static and live as long as the library. */
static ULONG STDMETHODCALLTYPE factory_add_ref(IClassFactory *This) { return 2; }
static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This) { return 1; }

static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (((STD_FACTORY *)This)->picture)
        return OleCreatePictureIndirect(nullptr, riid, TRUE, object);
    return OleCreateFontIndirect(nullptr, riid, object);
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *This, BOOL lock) { return S_OK; }

static const IClassFactoryVtbl g_factory_vtbl = {factory_query, factory_add_ref, factory_release, factory_create, factory_lock};
static STD_FACTORY g_font_factory = {&g_factory_vtbl, FALSE};
static STD_FACTORY g_picture_factory = {&g_factory_vtbl, TRUE};

/* DllGetClassObject asks here for the classes that are not the proxies. */
HRESULT AutStandardFactory(REFCLSID clsid, REFIID iid, void **object)
{
    STD_FACTORY *factory = nullptr;
    if (IsEqualGUID(clsid, &g_clsid_font) || IsEqualGUID(clsid, &g_clsid_old_font))
        factory = &g_font_factory;
    else if (IsEqualGUID(clsid, &g_clsid_picture))
        factory = &g_picture_factory;
    if (!factory)
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory_query((IClassFactory *)factory, iid, object);
}
