/*
 * msxml6.dll: msxml 6.0 dom documents and the sax reader, with secure defaults: a document type is refused and selections use xpath.
 * the engine is in guest/lib/xmldom.
 */
#include <xmldom/xmldom.h>

#define BASE_FLAGS (XDF_ASYNC | XDF_VALIDATE | XDF_FORCED_RESYNC | XDF_PROHIBIT_DTD | XDF_XPATH | XDF_V6)

static const XClass classes[] = {
    {&CLSID_DOMDocument60, BASE_FLAGS, L"Msxml2.DOMDocument.6.0", nullptr, L"XML DOM Document 6.0"},
    {&CLSID_FreeThreadedDOMDocument60, BASE_FLAGS | XDF_FREE, L"Msxml2.FreeThreadedDOMDocument.6.0", nullptr,
     L"Free Threaded XML DOM Document 6.0"},
    {&CLSID_XSLTemplate60, XDF_V6, L"Msxml2.XSLTemplate.6.0", nullptr, L"XSL Template 6.0", XK_TEMPLATE},
    {&CLSID_SAXXMLReader60, XDF_V6, L"Msxml2.SAXXMLReader.6.0", nullptr, L"SAX XML Reader 6.0", XK_SAXREADER},
};

static HMODULE g_module;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        xd_init();
    }
    return TRUE;
}

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    return xd_get_class_object(classes, sizeof(classes) / sizeof(classes[0]), clsid, riid, object);
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return xd_can_unload();
}

DECLSPEC_EXPORT HRESULT WINAPI DllRegisterServer(void)
{
    return xd_register_server(classes, sizeof(classes) / sizeof(classes[0]), g_module, FALSE);
}

DECLSPEC_EXPORT HRESULT WINAPI DllUnregisterServer(void)
{
    return xd_register_server(classes, sizeof(classes) / sizeof(classes[0]), g_module, TRUE);
}
