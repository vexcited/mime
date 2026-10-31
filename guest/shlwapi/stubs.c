/*
 * unimplemented exports: zones, shell context menus and folders, property stores and the browser. each logs once
 * and fails in a way callers handle, with windows' x86 stack sizes.
 */
#include "shlwapi_private.h"

/* zones grant everything since there is no internet explorer to configure them. */
SHLWAPI_STUB(HRESULT, ZoneCheckUrlA, 4, S_OK)
SHLWAPI_STUB(HRESULT, ZoneCheckUrlExA, 8, S_OK)
SHLWAPI_STUB(HRESULT, ZoneCheckUrlExCacheA, 9, S_OK)
SHLWAPI_STUB(HRESULT, ZoneCheckUrlExCacheW, 9, S_OK)
SHLWAPI_STUB(HRESULT, ZoneCheckHostEx, 7, S_OK)
SHLWAPI_STUB(int, ZoneComputePaneSize, 1, 0)
SHLWAPI_STUB(HRESULT, ZoneConfigureW, 2, E_NOTIMPL)

SHLWAPI_STUB(HRESULT, SHSimulateDrop, 5, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHLoadFromPropertyBag, 2, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, IUnknown_HandleIRestrict, 5, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHIsExpandableFolder, 2, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, MayQSForward, 6, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, MayExecForward, 7, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, IsQSForward, 3, E_NOTIMPL)



SHLWAPI_STUB(DWORD, SHRestrictionLookup, 6, 0)
SHLWAPI_STUB(HRESULT, SHRegGetCLSIDKey, 6, E_NOTIMPL)
SHLWAPI_STUB(HKEY, SHGetShellKey, 3, nullptr)
SHLWAPI_STUB(HRESULT, SHGetShellKeyEx, 4, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SKGetValueW, 6, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SKSetValueW, 6, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SKDeleteValueW, 3, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SKAllocValueW, 6, E_NOTIMPL)

SHLWAPI_STUB(HRESULT, IConnectionPoint_InvokeWithCancel, 5, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, IConnectionPoint_SimpleInvoke, 3, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, IConnectionPoint_OnChanged, 2, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, IUnknown_CPContainerOnChanged, 2, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, IUnknown_CPContainerInvokeIndirect, 3, E_NOTIMPL)

HRESULT WINAPIV IUnknown_CPContainerInvokeParam(IUnknown *container, REFIID riid, LONG id, VARIANTARG *args,
                                                UINT count, ...);
HRESULT WINAPIV IUnknown_CPContainerInvokeParam(IUnknown *container, REFIID riid, LONG id, VARIANTARG *args,
                                                UINT count, ...)
{
    MIME_STUB("shlwapi");
    return E_NOTIMPL;
}

SHLWAPI_STUB(HRESULT, IShellFolder_GetDisplayNameOf, 5, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, IShellFolder_ParseDisplayName, 7, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHInvokeDefaultCommand, 3, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHInvokeCommand, 4, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHInvokeCommandOnContextMenu, 5, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHInvokeCommandsOnContextMenu, 6, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHInvokeCommandOnContextMenuEx, 7, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHInvokeCommandsOnContextMenuEx, 8, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHInvokeCommandWithFlagsAndSite, 7, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHForwardContextMenuMsg, 6, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, IUnknown_DoContextMenuPopup, 5, E_NOTIMPL)

SHLWAPI_STUB(HRESULT, AssocCopyVerbs, 2, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, AssocMakeProgid, 4, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, AssocMakeShell, 4, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, AssocMakeApplicationByKeyW, 3, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, AssocMakeApplicationByKeyA, 3, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHRunIndirectRegClientCommand, 2, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, RunIndirectRegCommand, 4, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, RunRegCommand, 3, E_NOTIMPL)

SHLWAPI_STUB(HRESULT, SHCreatePropertyBagOnRegKey, 5, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHCreatePropertyBagOnProfileSection, 5, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHCreatePropertyBagOnCachedProfileSection, 5, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHCreatePropertyBagOnMemory, 3, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHCreatePropertyStoreOnXML, 5, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHClearViewStatePropertyBag, 3, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHPropertyBag_ReadPropertyKey, 3, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHPropertyBag_WritePropertyKey, 3, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, IUnknown_QueryServicePropertyBag, 4, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, QuerySourceCreateFromKey, 5, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, QuerySourceCreateFromKeyEx, 6, E_NOTIMPL)

SHLWAPI_STUB(HRESULT, IUnknown_QueryServiceForWebBrowserApp, 3, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, IUnknown_ShowBrowserBar, 3, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, IUnknown_TranslateAcceleratorGlobal, 2, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, IWebBrowser_Navigate2, 6, E_NOTIMPL)

SHLWAPI_STUB(BOOL, ConvertDIBSECTIONToThumbnail, 8, FALSE)
SHLWAPI_STUB_VOID(FactorAspectRatio, 12)
SHLWAPI_STUB(HBITMAP, CreateSizedDIBSECTION, 7, nullptr)

SHLWAPI_STUB(HRESULT, SHManagedCreateFile, 7, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHManagedCreateStreamOnFile, 7, E_NOTIMPL)
SHLWAPI_STUB(HRESULT, SHRegisterValidateTemplate, 2, S_OK)

/* no about box text to give. */
SHLWAPI_STUB(BOOL, SHAboutInfoA, 2, FALSE)

HRESULT WINAPI SHGetViewStatePropertyBag(const struct _ITEMIDLIST *pidl, LPCWSTR bag, DWORD flags, REFIID riid,
                                         void **out)
{
    MIME_STUB("shlwapi");
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

HRESULT WINAPI SHGetInverseCMAP(BYTE *map, ULONG size)
{
    MIME_STUB("shlwapi");
    return E_NOTIMPL;
}

/* there are no junctions to skip. */
BOOL WINAPI SHSkipJunction(IBindCtx *context, const CLSID *clsid)
{
    return FALSE;
}
