/*
 * the small calls of cryptui: the comparison of certificates for the lists that sort them, the text of dates, errors and
 * usages, the chain added to a store, and the help links.
 */
#include "cryptui_private.h"

HMODULE CuiCrypt32(void)
{
    static HMODULE module;
    if (!module)
        module = LoadLibraryW(L"crypt32.dll");
    return module;
}

WCHAR *CuiCopy(const WCHAR *text)
{
    if (!text)
        return nullptr;
    WCHAR *copy = LocalAlloc(0x40, (lstrlenW(text) + 1) * sizeof(WCHAR));
    if (copy)
        lstrcpyW(copy, text);
    return copy;
}

WCHAR *CuiAnsiToWide(const char *text)
{
    if (!text)
        return nullptr;
    int count = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    WCHAR *wide = count ? LocalAlloc(0x40, count * sizeof(WCHAR)) : nullptr;
    if (wide)
        MultiByteToWideChar(CP_ACP, 0, text, -1, wide, count);
    return wide;
}

void CuiAppend(WCHAR *text, SIZE_T size, const WCHAR *more)
{
    SIZE_T length = lstrlenW(text);
    while (*more && length + 1 < size)
        text[length++] = *more++;
    text[length] = 0;
}

/* ------------------------------------------------------------------ names, usages and dates */

WCHAR *CuiCertName(PCCERT_CONTEXT cert, BOOL issuer)
{
    DWORD flags = issuer ? CERT_NAME_ISSUER_FLAG : 0;
    DWORD count = CUI_PROC(CertGetNameStringW)(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, flags, nullptr, nullptr, 0);
    WCHAR *name = count ? LocalAlloc(0x40, count * sizeof(WCHAR)) : nullptr;
    if (name) {
        CUI_PROC(CertGetNameStringW)(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, flags, nullptr, name, count);
        return name;
    }
    /* "<None>" is the text of a certificate that has no such name. */
    return CuiCopy(L"<None>");
}

/* the friendly name of an usage, or the identifier itself. */
static BOOL usage_name(WCHAR *out, DWORD size, char *oid)
{
    PCCRYPT_OID_INFO info = CUI_PROC(CryptFindOIDInfo)(1, oid, 0);
    if (!info)
        return MultiByteToWideChar(CP_ACP, 0, oid, -1, out, size) != 0;
    DWORD length = info->pwszName ? lstrlenW(info->pwszName) : 0;
    if (length + 1 > size)
        return FALSE;
    if (info->pwszName)
        lstrcpyW(out, info->pwszName);
    else
        out[0] = 0;
    return TRUE;
}

BOOL CuiUsageString(WCHAR **out, PCCERT_CONTEXT cert, BOOL property_only)
{
    DWORD size = 0;
    DWORD flags = property_only ? 4 : 0;
    if (!CUI_PROC(CertGetEnhancedKeyUsage)(cert, flags, nullptr, &size))
        return FALSE;
    CTL_USAGE *usage = LocalAlloc(0x40, size);
    if (!usage)
        return FALSE;
    if (!CUI_PROC(CertGetEnhancedKeyUsage)(cert, flags, usage, &size)) {
        LocalFree(usage);
        return FALSE;
    }
    if (!usage->cUsageIdentifier) {
        DWORD error = GetLastError();
        LocalFree(usage);
        *out = CuiCopy(error != CUI_CRYPT_E_NOT_FOUND ? L"<None>" : L"<All>");
        return *out != nullptr;
    }
    SIZE_T total = 1;
    WCHAR name[0x300];
    for (DWORD i = 0; i < usage->cUsageIdentifier; i++) {
        if (!usage_name(name, 0x300, usage->rgpszUsageIdentifier[i])) {
            LocalFree(usage);
            return FALSE;
        }
        total += lstrlenW(name) + (i ? 2 : 0);
    }
    WCHAR *text = LocalAlloc(0x40, total * sizeof(WCHAR));
    if (text) {
        text[0] = 0;
        for (DWORD i = 0; i < usage->cUsageIdentifier; i++) {
            usage_name(name, 0x300, usage->rgpszUsageIdentifier[i]);
            if (i)
                CuiAppend(text, total, L", ");
            CuiAppend(text, total, name);
        }
    }
    LocalFree(usage);
    *out = text;
    return text != nullptr;
}

BOOL CuiDateString(WCHAR **out, FILETIME time, BOOL with_time)
{
    FILETIME local;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(&time, &local) || (!FileTimeToSystemTime(&local, &st) && !FileTimeToSystemTime(&time, &st)))
        return FALSE;
    /* DATE_AUTOLAYOUT puts the marks of a language written from the right in. */
    int date = GetDateFormatEx(nullptr, 0x40, &st, nullptr, nullptr, 0, nullptr);
    int count = date + GetTimeFormatEx(nullptr, 0, &st, nullptr, nullptr, 0) + 5;
    WCHAR *text = LocalAlloc(0x40, (count + 5) * sizeof(WCHAR));
    if (!text)
        return FALSE;
    int written = GetDateFormatEx(nullptr, 0x40, &st, nullptr, text, count, nullptr);
    if (with_time) {
        text[written - 1] = ' ';
        GetTimeFormatEx(nullptr, 0, &st, nullptr, text + written, count - written);
    }
    *out = text;
    return TRUE;
}

BOOL WINAPI FormatDateStringAutoLayout(WCHAR **out, FILETIME time, BOOL with_time)
{
    return CuiDateString(out, time, with_time);
}

BOOL WINAPI MyFormatEnhancedKeyUsageString(WCHAR **out, PCCERT_CONTEXT cert, BOOL property_only, DWORD reserved)
{
    WCHAR *text = nullptr;
    if (!CuiUsageString(&text, cert, property_only))
        return FALSE;
    *out = CuiCopy(text);
    LocalFree(text);
    return *out != nullptr;
}

BOOL WINAPI GetUnknownErrorString(WCHAR **out, DWORD error)
{
    WCHAR *message = nullptr;
    /* FORMAT_MESSAGE_ALLOCATE_BUFFER and FORMAT_MESSAGE_FROM_SYSTEM. */
    if (FormatMessageW(0x1100, nullptr, error, 0x400, (LPWSTR)&message, 0, nullptr)) {
        *out = CuiCopy(message);
        LocalFree(message);
    } else {
        *out = CuiCopy(L"An unknown error has occurred.");
    }
    return TRUE;
}

/* ------------------------------------------------------------------ comparison */

static WCHAR *location_name(PCCERT_CONTEXT cert)
{
    DWORD size = 0;
    if (CUI_PROC(CertGetStoreProperty)(cert->hCertStore, 0x1000, nullptr, &size)) {
        WCHAR *name = LocalAlloc(0x40, size);
        if (name && CUI_PROC(CertGetStoreProperty)(cert->hCertStore, 0x1000, name, &size))
            return name;
        LocalFree(name);
        return nullptr;
    }
    return CuiCopy(L"Not available");
}

static WCHAR *friendly_name(PCCERT_CONTEXT cert)
{
    DWORD size = 0;
    if (CUI_PROC(CertGetCertificateContextProperty)(cert, 0xB, nullptr, &size) && size) {
        WCHAR *name = LocalAlloc(0x40, size);
        if (name && CUI_PROC(CertGetCertificateContextProperty)(cert, 0xB, name, &size))
            return name;
        LocalFree(name);
    }
    return CuiCopy(L"<None>");
}

/* the modes are the columns of the list: 1 issuer, 2 subject, 4 expiration, 8 purpose, 0x10 name, 0x20 location. */
int WINAPI CompareCertificate(PCCERT_CONTEXT a, PCCERT_CONTEXT b, DWORD mode)
{
    if (!a || !b)
        return 0;
    WCHAR *first = nullptr, *second = nullptr;
    int result = 0;
    switch (mode & 0xFFFF) {
    case 1: first = CuiCertName(a, TRUE); second = CuiCertName(b, TRUE); break;
    case 2: first = CuiCertName(a, FALSE); second = CuiCertName(b, FALSE); break;
    case 8:
        CuiUsageString(&first, a, FALSE);
        CuiUsageString(&second, b, FALSE);
        break;
    case 0x10: first = friendly_name(a); second = friendly_name(b); break;
    case 0x20:
        first = location_name(a);
        second = first ? location_name(b) : nullptr;
        break;
    case 4:
        result = CompareFileTime(&a->pCertInfo->NotAfter, &b->pCertInfo->NotAfter);
        goto reverse;
    }
    if (!first || !second)
        goto done;
    result = CompareStringW(0x400, 1, first, -1, second, -1) - 2;
reverse:
    if (mode & 0x20000)
        result = -result;
done:
    LocalFree(first);
    LocalFree(second);
    return result;
}

/* ------------------------------------------------------------------ chains */

/* the certificates of the chain of a certificate, the ones of the chain that carry no mark as already trusted aside. */
BOOL WINAPI AddChainToStore(HCERTSTORE store, PCCERT_CONTEXT cert, DWORD count, HCERTSTORE *additional, BOOL skip_marked,
                            CERT_TRUST_STATUS *status)
{
    HCERTCHAINENGINE engine = nullptr;
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    BOOL ok = TRUE;
    CERT_CHAIN_PARA para = {};
    para.cbSize = CUI_SIZE(32, 16);
    CERT_CHAIN_ENGINE_CONFIG config = {};
    config.cbSize = sizeof(config);
    config.cAdditionalStore = count;
    config.rghAdditionalStore = additional;
    if (!CUI_PROC(CertCreateCertificateChainEngine)(&config, &engine) ||
        !CUI_PROC(CertGetCertificateChain)(engine, cert, nullptr, nullptr, &para, 0, nullptr, &chain) ||
        !chain->cChain) {
        ok = FALSE;
    } else {
        PCERT_SIMPLE_CHAIN simple = chain->rgpChain[0];
        for (DWORD i = 0; i < simple->cElement; i++) {
            if (skip_marked && (simple->rgpElement[i]->TrustStatus.dwInfoStatus & 8))
                continue;
            PCCERT_CONTEXT added = nullptr;
            CUI_PROC(CertAddCertificateContextToStore)(store, simple->rgpElement[i]->pCertContext, 3, &added);
            if (added) {
                CUI_PROC(CertSetCertificateContextProperty)(added, 2, 0, nullptr);
                CUI_PROC(CertFreeCertificateContext)(added);
            }
        }
        if (status) {
            status->dwErrorStatus = chain->TrustStatus.dwErrorStatus;
            status->dwInfoStatus = chain->TrustStatus.dwInfoStatus;
        }
    }
    if (chain)
        CUI_PROC(CertFreeCertificateChain)(chain);
    if (engine)
        CUI_PROC(CertFreeCertificateChainEngine)(engine);
    return ok;
}

/* ------------------------------------------------------------------ help */

typedef HWND(WINAPI *HTML_HELP)(HWND, LPCWSTR, UINT, DWORD_PTR);

void WINAPI DisplayHtmlHelp(HWND caller, LPCWSTR parameters)
{
    HMODULE module = LoadLibraryW(L"hhctrl.ocx");
    HTML_HELP help = module ? (HTML_HELP)GetProcAddress(module, "HtmlHelpW") : nullptr;
    if (help)
        help(caller, parameters, 0, 0);
}

/* asks the page that a link was clicked on for the topic it points to, which it writes at the end of the block. */
void WINAPI InvokeHelpLink(HWND window)
{
    if (!window)
        return;
    BYTE query[0x10C0] = {};
    *(LPARAM *)query = 9;
    if (SendMessageA(window, 0x703, 0, (LPARAM)query))
        DisplayHtmlHelp(window, (WCHAR *)(query + 0x70));
}

BOOL WINAPI CommonInit(void)
{
    return TRUE;
}

HRESULT WINAPI DllRegisterServer(void)
{
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return S_OK;
}
