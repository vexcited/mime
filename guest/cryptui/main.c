/*
 * cryptui: the certificate manager and viewer as plain message boxes over crypt32's stores and names, after the
 * same argument checks as windows' dialogs. the wizards and other dialogs are missing.
 */
#define CRYPTUIAPI
#include <mime/win32/cryptuiapi.h>
#include <mime/win32/winuser.h>


BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

static struct {
    HCERTSTORE (WINAPI *open)(LPCSTR, DWORD, HCRYPTPROV_LEGACY, DWORD, const void *);
    PCCERT_CONTEXT (WINAPI *next)(HCERTSTORE, PCCERT_CONTEXT);
    BOOL (WINAPI *close)(HCERTSTORE, DWORD);
    DWORD (WINAPI *name)(PCCERT_CONTEXT, DWORD, DWORD, void *, LPWSTR, DWORD);
} g_crypt32;

static BOOL load_crypt32(void)
{
    HMODULE module = LoadLibraryW(L"crypt32.dll");
    if (!module)
        return FALSE;
    g_crypt32.open = (void *)GetProcAddress(module, "CertOpenStore");
    g_crypt32.next = (void *)GetProcAddress(module, "CertEnumCertificatesInStore");
    g_crypt32.close = (void *)GetProcAddress(module, "CertCloseStore");
    g_crypt32.name = (void *)GetProcAddress(module, "CertGetNameStringW");
    return g_crypt32.open && g_crypt32.next && g_crypt32.close && g_crypt32.name;
}

/* appends without overflowing, truncating what does not fit. */
static void append(WCHAR *text, SIZE_T size, LPCWSTR more)
{
    SIZE_T length = wcslen(text);
    while (*more && length + 1 < size)
        text[length++] = *more++;
    text[length] = 0;
}

static void append_time(WCHAR *text, SIZE_T size, const FILETIME *time)
{
    SYSTEMTIME st;
    WCHAR date[64];
    if (FileTimeToSystemTime(time, &st) && GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, date, 64))
        append(text, size, date);
}

BOOL WINAPI CryptUIDlgViewCertificateW(PCCRYPTUI_VIEWCERTIFICATE_STRUCTW info, BOOL *changed)
{
    if (!info || info->dwSize != sizeof(*info)) {
        SetLastError(E_INVALIDARG);
        return FALSE;
    }
    if (changed)
        *changed = FALSE;
    if (!info->pCertContext || !load_crypt32()) {
        SetLastError(E_INVALIDARG);
        return FALSE;
    }
    WCHAR text[2048] = {0}, name[512];
    append(text, 2048, L"Issued to: ");
    g_crypt32.name(info->pCertContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 512);
    append(text, 2048, name);
    append(text, 2048, L"\nIssued by: ");
    g_crypt32.name(info->pCertContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG, nullptr, name, 512);
    append(text, 2048, name);
    if (info->pCertContext->pCertInfo) {
        append(text, 2048, L"\nValid from ");
        append_time(text, 2048, &info->pCertContext->pCertInfo->NotBefore);
        append(text, 2048, L" to ");
        append_time(text, 2048, &info->pCertContext->pCertInfo->NotAfter);
    }
    MessageBoxW(info->hwndParent, text, info->szTitle ? info->szTitle : L"Certificate", MB_OK | MB_ICONINFORMATION);
    return TRUE;
}

BOOL WINAPI CryptUIDlgCertMgr(PCCRYPTUI_CERT_MGR_STRUCT info)
{
    if (!info || info->dwSize != sizeof(*info) || (info->dwFlags & 0xF) > 4) {
        SetLastError(E_INVALIDARG);
        return FALSE;
    }
    if (!load_crypt32()) {
        SetLastError(ERROR_MOD_NOT_FOUND);
        return FALSE;
    }
    WCHAR text[4096] = {0}, name[512];
    HCERTSTORE store = g_crypt32.open(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                      CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG,
                                      L"MY");
    UINT count = 0;
    if (store) {
        for (PCCERT_CONTEXT cert = g_crypt32.next(store, nullptr); cert; cert = g_crypt32.next(store, cert)) {
            g_crypt32.name(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 512);
            append(text, 4096, name);
            append(text, 4096, L"\n");
            count++;
        }
        g_crypt32.close(store, 0);
    }
    if (!count)
        append(text, 4096, L"There are no personal certificates.");
    HWND parent = info->hwndParent ? info->hwndParent : GetDesktopWindow();
    MessageBoxW(parent, text, info->pwszTitle ? info->pwszTitle : L"Certificates", MB_OK | MB_ICONINFORMATION);
    return TRUE;
}
