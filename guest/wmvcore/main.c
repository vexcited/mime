/* wmvcore.dll: the Windows Media Format SDK. there is no reader, writer or editor behind it, so none can be created. */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

#define E_INVALIDARG_ ((HRESULT)0x80070057)
#define E_NOTIMPL_ ((HRESULT)0x80004001)

static HRESULT create(void **object)
{
    if (!object)
        return E_INVALIDARG_;
    *object = nullptr;
    return E_NOTIMPL_;
}

HRESULT WINAPI WMCreateReader(void *reserved, DWORD rights, void **reader)
{
    return create(reader);
}

HRESULT WINAPI WMCreateSyncReader(void *reserved, DWORD rights, void **reader)
{
    return create(reader);
}

HRESULT WINAPI WMCreateWriter(void *reserved, void **writer)
{
    return create(writer);
}

HRESULT WINAPI WMCreateEditor(void **editor)
{
    return create(editor);
}
