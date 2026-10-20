/*
 * the small exports of oleaut32 that are settings and tables: the version, the setting of how conversions read the
 * locale, and the names of months that some languages have in a second form.
 */
#include "oleaut32_private.h"

/* the user's overrides of the locale are not read by the conversions when this holds LOCALE_NOUSEROVERRIDE. */
ULONG g_process_nls_flags;
static BOOL g_per_user_cache;

WINOLEAUTAPI ULONG WINAPI OaBuildVersion(void)
{
    /* windows works it out from a constant; the answer is the same one. */
    int i = 10;
    while (i < 15014)
        i *= 10;
    return (USHORT)(15014 - i / 10) | 0x320000;
}

/* the registry of type libraries is the machine's here, so there is nothing for the setting to change. */
static volatile LONG g_per_user_type_libraries;

WINOLEAUTAPI void WINAPI OaEnablePerUserTLibRegistration(void)
{
    InterlockedExchange(&g_per_user_type_libraries, 1);
}

/* nothing is kept for a thread that has to be given back. */
WINOLEAUTAPI HRESULT WINAPI OACleanup(void)
{
    return S_OK;
}

/*
 * 0 reads the locale as the user has set it, 1 does and keeps what it read for the user, 2 reads the locale as it comes
 * with the system. A number is kept for the process.
 */
WINOLEAUTAPI HRESULT WINAPI SetVarConversionLocaleSetting(DWORD setting)
{
    switch (setting) {
    case 0:
        g_process_nls_flags = 0;
        g_per_user_cache = FALSE;
        return S_OK;
    case 1:
        g_process_nls_flags = 0;
        g_per_user_cache = TRUE;
        return S_OK;
    case 2:
        g_process_nls_flags = LOCALE_NOUSEROVERRIDE;
        g_per_user_cache = FALSE;
        return S_OK;
    }
    return E_INVALIDARG;
}

WINOLEAUTAPI HRESULT WINAPI GetVarConversionLocaleSetting(DWORD *setting)
{
    if (!setting)
        return E_INVALIDARG;
    *setting = g_per_user_cache ? 1 : (g_process_nls_flags >> 30) & 2;
    return S_OK;
}

/* the names of the months in the form some languages use when a day goes before them. */
static LPOLESTR g_hijri[13] = {L"محرم",
                               L"صفر",
                               L"ربيع الاول",
                               L"ربيع الثاني",
                               L"جمادى الاولى",
                               L"جمادى الثانية",
                               L"رجب",
                               L"شعبان",
                               L"رمضان",
                               L"شوال",
                               L"ذو القعدة",
                               L"ذو الحجة",
                               nullptr};
static LPOLESTR g_polish[13] = {L"stycznia", L"lutego",   L"marca",     L"kwietnia",   L"maja",    L"czerwca", L"lipca",
                                L"sierpnia", L"września", L"października", L"listopada", L"grudnia", nullptr};
static LPOLESTR g_russian[13] = {L"января",
                                 L"февраля",
                                 L"марта",
                                 L"апреля",
                                 L"мая",
                                 L"июня",
                                 L"июля",
                                 L"августа",
                                 L"сентября",
                                 L"октября",
                                 L"ноября",
                                 L"декабря",
                                 nullptr};

WINOLEAUTAPI HRESULT WINAPI GetAltMonthNames(LCID lcid, LPOLESTR **names)
{
    switch (lcid & 0x3FF) {
    case 0x01: *names = g_hijri; break;
    case 0x15: *names = g_polish; break;
    case 0x19: *names = g_russian; break;
    default: *names = nullptr; break;
    }
    return S_OK;
}
