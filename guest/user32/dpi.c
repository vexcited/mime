/*
 * the process dpi awareness a program's manifest asks for, read when
 * user32 starts as windows' InitProcessDpiAwareness does.
 */
#include "user32_private.h"

/* what a manifest's dpi elements say, in windows' order of precedence. */
enum {
    DPI_MANIFEST_NONE,
    DPI_MANIFEST_INVALID,
    DPI_MANIFEST_UNAWARE,
    DPI_MANIFEST_SYSTEM,
    DPI_MANIFEST_PER_MONITOR,
    DPI_MANIFEST_PER_MONITOR_V2,
    DPI_MANIFEST_UNIFORM,
};

ULONG UserDpiAwareness = 1;
BOOL UserDpiAwarenessLocked;
BOOL UserGdiScaling;

static BOOL is_blank(WCHAR c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* dpiAwareness of the 2016 namespace: a comma separated list, the first known value wins. */
static ULONG parse_dpi_awareness(void)
{
    static const struct {
        PCWSTR Name;
        ULONG Value;
    } values[] = {{L"unaware", DPI_MANIFEST_UNAWARE},
                  {L"system", DPI_MANIFEST_SYSTEM},
                  {L"permonitor", DPI_MANIFEST_PER_MONITOR},
                  {L"permonitorv2", DPI_MANIFEST_PER_MONITOR_V2},
                  {L"uniform", DPI_MANIFEST_UNIFORM}};
    SIZE_T needed = 0;
    QueryActCtxSettingsW(0, nullptr, L"http://schemas.microsoft.com/SMI/2016/WindowsSettings", L"dpiAwareness",
                         nullptr, 0, &needed);
    if (!needed)
        return DPI_MANIFEST_NONE;
    PWSTR text = UserAlloc(needed * sizeof(WCHAR));
    if (!text)
        return DPI_MANIFEST_NONE;
    ULONG result = DPI_MANIFEST_NONE;
    if (QueryActCtxSettingsW(0, nullptr, L"http://schemas.microsoft.com/SMI/2016/WindowsSettings", L"dpiAwareness",
                             text, needed, nullptr)) {
        result = DPI_MANIFEST_INVALID;
        for (PCWSTR p = text; *p && result == DPI_MANIFEST_INVALID;) {
            while (is_blank(*p))
                p++;
            PCWSTR start = p;
            while (*p && *p != ',')
                p++;
            PCWSTR end = p;
            while (end > start && is_blank(end[-1]))
                end--;
            for (ULONG i = 0; i < RTL_NUMBER_OF(values) && end > start; i++) {
                /* uniform is known but never taken. */
                if (CompareStringOrdinal(values[i].Name, -1, start, (int)(end - start), TRUE) == CSTR_EQUAL &&
                    values[i].Value != DPI_MANIFEST_UNIFORM) {
                    result = values[i].Value;
                    break;
                }
            }
            if (*p == ',')
                p++;
        }
    }
    UserFree(text);
    return result;
}

/* dpiAware of the 2005 namespace: true, true/pm or per monitor; anything else means unaware. */
static ULONG parse_dpi_aware(void)
{
    WCHAR value[12];
    if (!QueryActCtxSettingsW(0, nullptr, nullptr, L"dpiAware", value, RTL_NUMBER_OF(value), nullptr))
        return DPI_MANIFEST_NONE;
    if (CompareStringOrdinal(L"true", -1, value, -1, TRUE) == CSTR_EQUAL)
        return DPI_MANIFEST_SYSTEM;
    if (CompareStringOrdinal(L"true/pm", -1, value, -1, TRUE) == CSTR_EQUAL ||
        CompareStringOrdinal(L"per monitor", -1, value, -1, TRUE) == CSTR_EQUAL)
        return DPI_MANIFEST_PER_MONITOR;
    return DPI_MANIFEST_UNAWARE;
}

void UserInitializeDpiAwareness(void)
{
    WCHAR scaling[6];
    UserGdiScaling = QueryActCtxSettingsW(0, nullptr, L"http://schemas.microsoft.com/SMI/2017/WindowsSettings",
                                          L"gdiScaling", scaling, RTL_NUMBER_OF(scaling), nullptr) &&
        !lstrcmpiW(scaling, L"true");
    ULONG manifest = parse_dpi_awareness();
    if (manifest == DPI_MANIFEST_NONE)
        manifest = parse_dpi_aware();
    /*
     * the process awareness as win32k keeps it: 0 unaware, 1 system aware,
     * 2 per monitor, 3 per monitor v2. a program without a dpi setting stays
     * system aware, mime's default while it has no dpi virtualization.
     */
    ULONG awareness;
    switch (manifest) {
    case DPI_MANIFEST_UNAWARE:
        awareness = 0;
        break;
    case DPI_MANIFEST_SYSTEM:
        awareness = 1;
        break;
    case DPI_MANIFEST_PER_MONITOR:
        awareness = 2;
        break;
    case DPI_MANIFEST_PER_MONITOR_V2:
        awareness = 3;
        break;
    default:
        UserDpiAwareness = (ULONG)NtUserCallOneParam(0, MIME_ONEPARAM_GETPROCESSDPIAWARENESS);
        return;
    }
    UserDpiAwareness = awareness;
    UserDpiAwarenessLocked = TRUE;
    NtUserCallOneParam(awareness, MIME_ONEPARAM_SETPROCESSDPIAWARENESS);
}

DPI_AWARENESS_CONTEXT UserDpiAwarenessContext(void)
{
    static const LONG_PTR contexts[] = {-1, -2, -3, -4};
    return (DPI_AWARENESS_CONTEXT)contexts[UserDpiAwareness < 4 ? UserDpiAwareness : 1];
}
