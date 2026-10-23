/* small oleaut32 exports: the build version, the conversion locale setting and alternate month names. */
#include "oleaut32_private.h"

/* LOCALE_NOUSEROVERRIDE here makes conversions ignore the user's locale overrides. */
ULONG g_process_nls_flags;

WINOLEAUTAPI ULONG WINAPI OaBuildVersion(void)
{
    return 0x00321396;
}

/* the type library registry is machine wide here, so this setting changes nothing. */
static volatile LONG g_per_user_type_libraries;

WINOLEAUTAPI void WINAPI OaEnablePerUserTLibRegistration(void)
{
    InterlockedExchange(&g_per_user_type_libraries, 1);
}

/* no per-thread state to release. */
WINOLEAUTAPI HRESULT WINAPI OACleanup(void)
{
    return S_OK;
}

/* 0 and 1 honor the user's locale overrides, 2 ignores them, and 1 also caches the user's locale. */
static volatile LONG g_conversion_setting;

WINOLEAUTAPI HRESULT WINAPI SetVarConversionLocaleSetting(DWORD setting)
{
    if (setting > 2)
        return E_INVALIDARG;
    g_process_nls_flags = setting == 2 ? LOCALE_NOUSEROVERRIDE : 0;
    InterlockedExchange(&g_conversion_setting, (LONG)setting);
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI GetVarConversionLocaleSetting(DWORD *setting)
{
    if (!setting)
        return E_INVALIDARG;
    *setting = (DWORD)g_conversion_setting;
    return S_OK;
}

/* month names in the form some languages use after a day number. */
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
