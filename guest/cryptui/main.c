/* cryptui: the certificate manager. its viewers, choosers, wizards and helpers are in the other files. */
#include "cryptui_private.h"

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

/* the certificates of the user in a list; the tabs and the buttons of the manager are not here. */
BOOL WINAPI CryptUIDlgCertMgr(PCCRYPTUI_CERT_MGR_STRUCT info)
{
    if (!info || info->dwSize != sizeof(*info) || (info->dwFlags & 0xF) > 4) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    if (!CuiCrypt32()) {
        SetLastError(ERROR_MOD_NOT_FOUND);
        return FALSE;
    }
    WCHAR *text = LocalAlloc(0x40, 4096 * sizeof(WCHAR));
    if (!text)
        return FALSE;
    HCERTSTORE store = CUI_PROC(CertOpenStore)(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                               CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG, L"MY");
    UINT count = 0;
    if (store) {
        for (PCCERT_CONTEXT cert = CUI_PROC(CertEnumCertificatesInStore)(store, nullptr); cert;
             cert = CUI_PROC(CertEnumCertificatesInStore)(store, cert)) {
            WCHAR *name = CuiCertName(cert, FALSE);
            CuiAppend(text, 4096, name);
            CuiAppend(text, 4096, L"\r\n");
            LocalFree(name);
            count++;
        }
        CUI_PROC(CertCloseStore)(store, 0);
    }
    if (!count)
        CuiAppend(text, 4096, L"There are no personal certificates.");
    CuiShowText(info->hwndParent, info->pwszTitle ? info->pwszTitle : L"Certificates", text);
    LocalFree(text);
    return TRUE;
}
