/*
 * A program that asks for a Windows font that the machine does not have asks the feature service for it: the fonts
 * of Microsoft are an optional download. Nothing stands in for them, so a program whose font is still missing after
 * the request is ended. The request is made once for each family in a process, and only for the families that the
 * catalog of features on demand has, so every other font name costs a few comparisons.
 */
#include "gdi32_private.h"
#include <mime/feature.h>
#include <mime/fonts.h>

extern const char *const g_catalog_font_names[];
extern const unsigned g_catalog_font_name_count;

#define ASKED_MAX 64

static volatile LONG g_asked[ASKED_MAX];
static volatile LONG g_asked_count;
/* 0 not looked at, 1 asking is fine, 2 never ask in this process. */
static volatile LONG g_mode;

static const MIME_FONT_SUBSTITUTE g_substitutes[] = {MIME_FONT_SUBSTITUTES};

static WCHAR fold(WCHAR c)
{
    return c >= L'A' && c <= L'Z' ? (WCHAR)(c + 32) : c;
}

static BOOL same_name(const WCHAR *a, const WCHAR *b)
{
    for (; *a && *b; a++, b++)
        if (fold(*a) != fold(*b))
            return FALSE;
    return *a == *b;
}

/* The place of a family in the sorted names of the catalog, as lower case ASCII. */
static BOOL in_catalog(const WCHAR *name)
{
    char key[LF_FACESIZE];
    int n = 0;
    for (; name[n]; n++) {
        if (n + 1 >= LF_FACESIZE || name[n] > 0x7E || name[n] < 0x20)
            return FALSE;
        key[n] = (char)fold(name[n]);
    }
    key[n] = 0;
    unsigned low = 0, high = g_catalog_font_name_count;
    while (low < high) {
        unsigned middle = (low + high) / 2;
        const char *a = key, *b = g_catalog_font_names[middle];
        while (*a && *a == *b) {
            a++;
            b++;
        }
        if (*a == *b)
            return TRUE;
        if ((unsigned char)*a < (unsigned char)*b)
            high = middle;
        else
            low = middle + 1;
    }
    return FALSE;
}

static int CALLBACK found_one(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM param)
{
    *(BOOL *)param = TRUE;
    return 0;
}

static BOOL installed(const WCHAR *family)
{
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return TRUE;
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, family, LF_FACESIZE);
    BOOL found = FALSE;
    EnumFontFamiliesExW(dc, &lf, found_one, (LPARAM)&found, 0);
    DeleteDC(dc);
    return found;
}

/* The service itself and its other names never ask: they would start themselves. */
static BOOL is_the_service(void)
{
    WCHAR path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return FALSE;
    const WCHAR *name = path + n;
    while (name > path && name[-1] != L'\\')
        name--;
    return same_name(name, L"OptionalFeatures.exe") || same_name(name, L"dism.exe") || same_name(name, L"fondue.exe");
}

/* Remembers a family as asked for. FALSE when it already was. */
static BOOL first_time(const WCHAR *family)
{
    LONG hash = 5381;
    for (const WCHAR *p = family; *p; p++)
        hash = hash * 33 + fold(*p);
    hash |= 1;
    for (LONG i = 0; i < g_asked_count && i < ASKED_MAX; i++)
        if (g_asked[i] == hash)
            return FALSE;
    LONG slot = InterlockedIncrement(&g_asked_count) - 1;
    if (slot >= ASKED_MAX)
        return FALSE;
    g_asked[slot] = hash;
    return TRUE;
}

typedef int(WINAPI *MESSAGE_BOX)(HWND, LPCWSTR, LPCWSTR, UINT);

/* features.exe could not tell the user, this does it as well as it can before the end. */
static void tell_missing(const WCHAR *family, DWORD result)
{
    WCHAR text[256];
    _snwprintf(text, RTL_NUMBER_OF(text),
               L"This program cannot run without the font %ls, which is not installed (error %lu). It is closed.", family,
               result);
    text[RTL_NUMBER_OF(text) - 1] = 0;
    OutputDebugStringW(text);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    MESSAGE_BOX box = user32 ? (MESSAGE_BOX)(void *)GetProcAddress(user32, "MessageBoxW") : nullptr;
    if (box)
        box(nullptr, text, L"Mime", 0x10 | 0x1000); /* MB_ICONERROR | MB_SYSTEMMODAL */
}

void GdiAskForFont(const LOGFONTW *lf)
{
    if (g_mode == 2 || !lf->lfFaceName[0])
        return;
    const WCHAR *family = lf->lfFaceName;
    for (unsigned i = 0; i < sizeof(g_substitutes) / sizeof(g_substitutes[0]); i++)
        if (same_name(family, g_substitutes[i].From)) {
            family = g_substitutes[i].To;
            break;
        }
    if (!in_catalog(family))
        return;
    if (!g_mode) {
        /* What OptionalFeatures.exe sets for itself and for what it runs: they must not ask for what they are installing. */
        WCHAR busy[4];
        DWORD last = GetLastError();
        InterlockedExchange(&g_mode, is_the_service() || GetEnvironmentVariableW(MIME_FEATURE_BUSY_VARIABLE, busy, RTL_NUMBER_OF(busy)) ? 2 : 1);
        SetLastError(last);
        if (g_mode == 2)
            return;
    }
    if (!first_time(family) || installed(family))
        return;
    DWORD last = GetLastError();
    DWORD result = MimeFeatureRequestFont(family);
    if (installed(family)) {
        SetLastError(last);
        return;
    }
    /* What the program would show next needs fonts too, none is asked for again. */
    InterlockedExchange(&g_mode, 2);
    if (result != MIME_FEATURE_FONT_MISSING)
        tell_missing(family, result);
    TerminateProcess(GetCurrentProcess(), ERROR_CANCELLED);
}

enum { SCRIPT_THAI = 1, SCRIPT_HANGUL = 2, SCRIPT_KANA = 4, SCRIPT_HAN = 8 };

static volatile LONG g_scripts_seen;

static BOOL any_installed(const WCHAR *const *families)
{
    for (; *families; families++)
        if (installed(*families))
            return TRUE;
    return FALSE;
}

static void ask_family(const WCHAR *family)
{
    LOGFONTW lf = {};
    lstrcpynW(lf.lfFaceName, family, LF_FACESIZE);
    GdiAskForFont(&lf);
}

/*
 * text in a script that no font here has asks once for the font windows links the script to, so the linked fonts of
 * the kernel can draw it.
 */
void GdiAskForText(const WCHAR *text, int count)
{
    if (g_mode == 2 || !text || count <= 0)
        return;
    LONG scripts = 0;
    for (int i = 0; i < count; i++) {
        WCHAR c = text[i];
        if (c < 0x0E00)
            continue;
        if (c <= 0x0E7F)
            scripts |= SCRIPT_THAI;
        else if ((c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F) || (c >= 0xAC00 && c <= 0xD7AF))
            scripts |= SCRIPT_HANGUL;
        else if ((c >= 0x3040 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF) || (c >= 0xFF66 && c <= 0xFF9F))
            scripts |= SCRIPT_KANA;
        else if ((c >= 0x2E80 && c <= 0x2FDF) || (c >= 0x3400 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF))
            scripts |= SCRIPT_HAN;
    }
    scripts &= ~g_scripts_seen;
    if (!scripts)
        return;
    InterlockedOr(&g_scripts_seen, scripts);
    static const WCHAR *const thai[] = {L"Tahoma", L"Leelawadee UI", nullptr};
    static const WCHAR *const hangul[] = {L"Malgun Gothic", L"Gulim", L"Batang", nullptr};
    static const WCHAR *const kana[] = {L"Yu Gothic UI", L"Meiryo UI", L"MS UI Gothic", nullptr};
    static const WCHAR *const han[] = {L"Yu Gothic UI", L"Meiryo UI", L"MS UI Gothic", L"Microsoft JhengHei UI",
                                       L"Microsoft YaHei UI", L"SimSun", L"PMingLiU", nullptr};
    if ((scripts & SCRIPT_THAI) && !any_installed(thai))
        ask_family(L"Leelawadee UI");
    if ((scripts & SCRIPT_HANGUL) && !any_installed(hangul))
        ask_family(L"Malgun Gothic");
    if ((scripts & SCRIPT_KANA) && !any_installed(kana))
        ask_family(L"Yu Gothic UI");
    else if ((scripts & SCRIPT_HAN) && !any_installed(han))
        ask_family(L"Microsoft YaHei UI");
}
