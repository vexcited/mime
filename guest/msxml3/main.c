/*
 * msxml3.dll: msxml 3.0 dom documents, the sax reader and the version independent classes that resolve to it. the engine is in
 * guest/lib/xmldom.
 */
#include <xmldom/xmldom.h>

#define BASE_FLAGS (XDF_ASYNC | XDF_VALIDATE | XDF_RESOLVE | XDF_FORCED_RESYNC | XDF_ALLOW_DOCFN | XDF_ALLOW_SCRIPT)

static const XClass classes[] = {
    {&CLSID_DOMDocument30, BASE_FLAGS, L"Msxml2.DOMDocument.3.0", L"Msxml2.DOMDocument", L"XML DOM Document 3.0"},
    {&CLSID_FreeThreadedDOMDocument30, BASE_FLAGS | XDF_FREE, L"Msxml2.FreeThreadedDOMDocument.3.0",
     L"Msxml2.FreeThreadedDOMDocument", L"Free Threaded XML DOM Document 3.0"},
    {&CLSID_DOMDocument2, BASE_FLAGS, L"Msxml2.DOMDocument", nullptr, L"XML DOM Document"},
    {&CLSID_FreeThreadedDOMDocument2, BASE_FLAGS | XDF_FREE, L"Msxml2.FreeThreadedDOMDocument", nullptr,
     L"Free Threaded XML DOM Document"},
    {&CLSID_DOMDocumentLegacy, BASE_FLAGS, L"Microsoft.XMLDOM.1.0", L"Microsoft.XMLDOM", L"XML DOM Document"},
    {&CLSID_FreeThreadedDOMDocumentLegacy, BASE_FLAGS | XDF_FREE, L"Microsoft.FreeThreadedXMLDOM.1.0",
     L"Microsoft.FreeThreadedXMLDOM", L"Free Threaded XML DOM Document"},
    {&CLSID_DOMDocument26, BASE_FLAGS, L"Msxml2.DOMDocument.2.6", nullptr, L"XML DOM Document 2.6"},
    {&CLSID_FreeThreadedDOMDocument26, BASE_FLAGS | XDF_FREE, L"Msxml2.FreeThreadedDOMDocument.2.6", nullptr,
     L"Free Threaded XML DOM Document 2.6"},
    {&CLSID_XSLTemplate, 0, L"Msxml2.XSLTemplate", nullptr, L"XSL Template", XK_TEMPLATE},
    {&CLSID_XSLTemplate30, 0, L"Msxml2.XSLTemplate.3.0", L"Msxml2.XSLTemplate", L"XSL Template 3.0", XK_TEMPLATE},
    {&CLSID_SAXXMLReader, 0, L"Msxml2.SAXXMLReader", nullptr, L"SAX XML Reader", XK_SAXREADER},
    {&CLSID_SAXXMLReader30, 0, L"Msxml2.SAXXMLReader.3.0", L"Msxml2.SAXXMLReader", L"SAX XML Reader 3.0", XK_SAXREADER},
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
