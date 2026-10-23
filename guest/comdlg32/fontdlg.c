/*
 * ChooseFont. fonts come from gdi32's enumeration, one entry per family with the scripts it supports, the four
 * styles every family can show, simulated ones included unless CF_NOSIMULATIONS, and its sizes: the usual ones
 * for scalable fonts, the real ones for raster fonts.
 */
#include "dlgbase.h"
#include "dlgids.h"

#define MAX_STYLES 4

enum { STYLE_REGULAR, STYLE_ITALIC, STYLE_BOLD, STYLE_BOLD_ITALIC };

static const LPCWSTR g_style_names[MAX_STYLES] = {L"Regular", L"Italic", L"Bold", L"Bold Italic"};

static const struct {
    LPCWSTR Name;
    BYTE Charset;
} g_scripts[] = {
    {L"Western", ANSI_CHARSET},          {L"Greek", GREEK_CHARSET},          {L"Turkish", TURKISH_CHARSET},
    {L"Hebrew", HEBREW_CHARSET},         {L"Arabic", ARABIC_CHARSET},        {L"Baltic", BALTIC_CHARSET},
    {L"Cyrillic", RUSSIAN_CHARSET},      {L"Central European", EASTEUROPE_CHARSET},
    {L"Vietnamese", VIETNAMESE_CHARSET}, {L"Thai", THAI_CHARSET},            {L"Symbol", SYMBOL_CHARSET},
    {L"OEM/DOS", OEM_CHARSET},           {L"Japanese", SHIFTJIS_CHARSET},    {L"Korean", HANGEUL_CHARSET},
    {L"Chinese Simplified", GB2312_CHARSET}, {L"Chinese Traditional", CHINESEBIG5_CHARSET},
};

static const struct {
    LPCWSTR Name;
    COLORREF Color;
} g_colors[] = {
    {L"Black", RGB(0, 0, 0)},       {L"Maroon", RGB(128, 0, 0)},   {L"Green", RGB(0, 128, 0)},
    {L"Olive", RGB(128, 128, 0)},   {L"Navy", RGB(0, 0, 128)},     {L"Purple", RGB(128, 0, 128)},
    {L"Teal", RGB(0, 128, 128)},    {L"Gray", RGB(128, 128, 128)}, {L"Silver", RGB(192, 192, 192)},
    {L"Red", RGB(255, 0, 0)},       {L"Lime", RGB(0, 255, 0)},     {L"Yellow", RGB(255, 255, 0)},
    {L"Blue", RGB(0, 0, 255)},      {L"Fuchsia", RGB(255, 0, 255)}, {L"Aqua", RGB(0, 255, 255)},
    {L"White", RGB(255, 255, 255)},
};

static const int g_standard_sizes[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72};

typedef struct _FACE {
    LOGFONTW Lf;
    WCHAR Script[LF_FACESIZE];
    DWORD Type;
    int Height, InternalLeading;
    BYTE Pitch;
} FACE;

typedef struct _FAMILY {
    WCHAR Name[LF_FACESIZE];
    DWORD Type;
    BYTE PitchAndFamily;
    BYTE Charset;
    /* loaded when the family is first chosen. */
    FACE *Faces;
    int FaceCount, FaceCapacity;
    BOOL Loaded;
} FAMILY;

typedef struct _STYLE {
    int Kind;
    BOOL Simulated;
} STYLE;

typedef struct _FONTDLG {
    CHOOSEFONTW *Cf;
    CHOOSEFONTA *Ansi;
    HWND Dialog;
    HDC Dc;
    int Dpi;
    FAMILY *Families;
    int FamilyCount, FamilyCapacity;
    STYLE Styles[MAX_STYLES];
    DWORD Flags;
    LOGFONTW *Lf;
    HFONT Sample;
    DLGPROC Hook;
    BOOL Filling;
    BOOL FaceValid, StyleValid;
    UINT ErrorCode;
} FONTDLG;

static DWORD g_font_hook_tls = TLS_OUT_OF_INDEXES;
static LONG g_font_once;
static BOOL g_font_cancel;
static UINT g_msg_help;

/* ------------------------------------------------------------------ the fonts */

typedef struct _ENUMRUN {
    FONTDLG *D;
    DWORD Screen;
    FAMILY *Into;
} ENUMRUN;

static BOOL face_accepted(const FONTDLG *d, const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type)
{
    DWORD f = d->Flags;
    if ((f & CF_TTONLY) && !(type & TRUETYPE_FONTTYPE))
        return FALSE;
    if ((f & CF_FIXEDPITCHONLY) && (tm->tmPitchAndFamily & TMPF_FIXED_PITCH))
        return FALSE;
    if ((f & CF_SCALABLEONLY) && (type & RASTER_FONTTYPE))
        return FALSE;
    if ((f & CF_NOVECTORFONTS) && lf->lfCharSet == OEM_CHARSET)
        return FALSE;
    if ((f & CF_NOVERTFONTS) && lf->lfFaceName[0] == L'@')
        return FALSE;
    if ((f & CF_ANSIONLY) && (lf->lfCharSet == OEM_CHARSET || lf->lfCharSet == SYMBOL_CHARSET))
        return FALSE;
    if (!lf->lfFaceName[0])
        return FALSE;
    return TRUE;
}

static FAMILY *family_named(const FONTDLG *d, LPCWSTR name)
{
    for (int i = 0; i < d->FamilyCount; i++)
        if (!_wcsicmp(d->Families[i].Name, name))
            return &d->Families[i];
    return nullptr;
}

static int CALLBACK family_proc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM param)
{
    ENUMRUN *run = (ENUMRUN *)param;
    FONTDLG *d = run->D;
    if (!face_accepted(d, lf, tm, type))
        return 1;
    if (family_named(d, lf->lfFaceName))
        return 1;
    if (d->FamilyCount == d->FamilyCapacity) {
        int cap = d->FamilyCapacity ? d->FamilyCapacity * 2 : 128;
        FAMILY *grown = d->Families ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, d->Families, cap * sizeof(FAMILY))
                                    : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap * sizeof(FAMILY));
        if (!grown)
            return 0;
        d->Families = grown;
        d->FamilyCapacity = cap;
    }
    FAMILY *f = &d->Families[d->FamilyCount++];
    memset(f, 0, sizeof(*f));
    lstrcpynW(f->Name, lf->lfFaceName, LF_FACESIZE);
    f->Type = type | run->Screen;
    f->PitchAndFamily = lf->lfPitchAndFamily;
    f->Charset = lf->lfCharSet;
    return 1;
}

static int CALLBACK face_proc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM param)
{
    ENUMRUN *run = (ENUMRUN *)param;
    FAMILY *fam = run->Into;
    if (!face_accepted(run->D, lf, tm, type))
        return 1;
    if (fam->FaceCount == fam->FaceCapacity) {
        int cap = fam->FaceCapacity ? fam->FaceCapacity * 2 : 16;
        FACE *grown = fam->Faces ? HeapReAlloc(GetProcessHeap(), 0, fam->Faces, cap * sizeof(FACE))
                                 : HeapAlloc(GetProcessHeap(), 0, cap * sizeof(FACE));
        if (!grown)
            return 0;
        fam->Faces = grown;
        fam->FaceCapacity = cap;
    }
    FACE *f = &fam->Faces[fam->FaceCount++];
    memset(f, 0, sizeof(*f));
    f->Lf = *lf;
    f->Type = type | run->Screen;
    f->Height = tm->tmHeight;
    f->InternalLeading = tm->tmInternalLeading;
    f->Pitch = tm->tmPitchAndFamily;
    const ENUMLOGFONTEXW *ex = (const ENUMLOGFONTEXW *)lf;
    lstrcpynW(f->Script, ex->elfScript, LF_FACESIZE);
    return 1;
}

static int compare_families(const FAMILY *a, const FAMILY *b)
{
    return _wcsicmp(a->Name, b->Name);
}

static void sort_families(FAMILY *list, int n)
{
    /* a shell sort. */
    FAMILY temp;
    for (int gap = n / 2; gap > 0; gap /= 2) {
        for (int i = gap; i < n; i++) {
            temp = list[i];
            int j = i;
            while (j >= gap && compare_families(&list[j - gap], &temp) > 0) {
                list[j] = list[j - gap];
                j -= gap;
            }
            list[j] = temp;
        }
    }
}

static void enumerate(FONTDLG *d, HDC dc, DWORD screen)
{
    LOGFONTW lf;
    ENUMRUN run = {d, screen, nullptr};
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &lf, family_proc, (LPARAM)&run, 0);
    sort_families(d->Families, d->FamilyCount);
}

static void load_faces(FONTDLG *d, FAMILY *fam)
{
    if (fam->Loaded)
        return;
    fam->Loaded = TRUE;
    LOGFONTW lf;
    ENUMRUN run = {d, fam->Type & (SCREEN_FONTTYPE | PRINTER_FONTTYPE), fam};
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, fam->Name, LF_FACESIZE);
    EnumFontFamiliesExW(d->Dc, &lf, face_proc, (LPARAM)&run, 0);
    if (!fam->FaceCount) {
        /* the family exists, so it has at least the face that listed it. */
        FACE *f = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FACE));
        if (f) {
            lstrcpynW(f->Lf.lfFaceName, fam->Name, LF_FACESIZE);
            f->Lf.lfWeight = FW_NORMAL;
            f->Lf.lfCharSet = fam->Charset;
            f->Lf.lfPitchAndFamily = fam->PitchAndFamily;
            f->Type = fam->Type;
            fam->Faces = f;
            fam->FaceCount = fam->FaceCapacity = 1;
        }
    }
}

/* ------------------------------------------------------------------ the controls */

static HWND item(const FONTDLG *d, int id)
{
    return GetDlgItem(d->Dialog, id);
}

static int combo_count(const FONTDLG *d, int id)
{
    return (int)SendDlgItemMessageW(d->Dialog, id, CB_GETCOUNT, 0, 0);
}

static void combo_text(const FONTDLG *d, int id, WCHAR *text, int chars)
{
    text[0] = 0;
    GetDlgItemTextW(d->Dialog, id, text, chars);
}

static LRESULT combo_find(const FONTDLG *d, int id, LPCWSTR text)
{
    LRESULT index = SendDlgItemMessageW(d->Dialog, id, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)text);
    return index;
}

static FAMILY *selected_family(FONTDLG *d)
{
    WCHAR name[LF_FACESIZE];
    combo_text(d, FONT_FAMILY, name, LF_FACESIZE);
    FAMILY *fam = family_named(d, name);
    if (fam)
        load_faces(d, fam);
    return fam;
}

static BYTE charset_of_script(const FONTDLG *d)
{
    LRESULT sel = SendDlgItemMessageW(d->Dialog, FONT_SCRIPT, CB_GETCURSEL, 0, 0);
    if (sel < 0)
        return DEFAULT_CHARSET;
    return (BYTE)SendDlgItemMessageW(d->Dialog, FONT_SCRIPT, CB_GETITEMDATA, (WPARAM)sel, 0);
}

static LPCWSTR script_name(BYTE charset)
{
    for (int i = 0; i < (int)(sizeof(g_scripts) / sizeof(g_scripts[0])); i++)
        if (g_scripts[i].Charset == charset)
            return g_scripts[i].Name;
    return nullptr;
}

static BOOL scripts_has(const FONTDLG *d, int id, BYTE charset)
{
    int n = combo_count(d, id);
    for (int i = 0; i < n; i++)
        if ((BYTE)SendDlgItemMessageW(d->Dialog, id, CB_GETITEMDATA, (WPARAM)i, 0) == charset)
            return TRUE;
    return FALSE;
}

static void fill_scripts(FONTDLG *d, FAMILY *fam, BYTE want)
{
    HWND combo = item(d, FONT_SCRIPT);
    if (!combo)
        return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    if (!fam)
        return;
    for (int i = 0; i < fam->FaceCount; i++) {
        BYTE cs = fam->Faces[i].Lf.lfCharSet;
        if (scripts_has(d, FONT_SCRIPT, cs))
            continue;
        LPCWSTR name = fam->Faces[i].Script[0] ? fam->Faces[i].Script : script_name(cs);
        WCHAR other[16];
        if (!name) {
            wsprintfW(other, L"Script %u", cs);
            name = other;
        }
        LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)name);
        if (index >= 0)
            SendMessageW(combo, CB_SETITEMDATA, (WPARAM)index, cs);
    }
    int n = combo_count(d, FONT_SCRIPT), pick = 0;
    for (int i = 0; i < n; i++) {
        if ((BYTE)SendMessageW(combo, CB_GETITEMDATA, (WPARAM)i, 0) == want) {
            pick = i;
            break;
        }
        if ((BYTE)SendMessageW(combo, CB_GETITEMDATA, (WPARAM)i, 0) == ANSI_CHARSET && want == DEFAULT_CHARSET)
            pick = i;
    }
    SendMessageW(combo, CB_SETCURSEL, (WPARAM)pick, 0);
    EnableWindow(combo, n > 1 && !(d->Flags & CF_NOSCRIPTSEL));
}

static BOOL is_bold(const LOGFONTW *lf)
{
    return lf->lfWeight >= 600;
}

static int style_kind(BOOL bold, BOOL italic)
{
    return bold ? (italic ? STYLE_BOLD_ITALIC : STYLE_BOLD) : (italic ? STYLE_ITALIC : STYLE_REGULAR);
}

static void fill_styles(FONTDLG *d, FAMILY *fam, BYTE charset, int want)
{
    HWND combo = item(d, FONT_STYLE);
    if (!combo)
        return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    if (!fam)
        return;
    BOOL have[MAX_STYLES] = {FALSE, FALSE, FALSE, FALSE};
    BOOL scalable = FALSE;
    for (int i = 0; i < fam->FaceCount; i++) {
        const FACE *f = &fam->Faces[i];
        if (charset != DEFAULT_CHARSET && f->Lf.lfCharSet != charset && !(f->Type & TRUETYPE_FONTTYPE))
            continue;
        have[style_kind(is_bold(&f->Lf), f->Lf.lfItalic)] = TRUE;
        if (!(f->Type & RASTER_FONTTYPE))
            scalable = TRUE;
    }
    int added = 0, pick = -1;
    for (int kind = 0; kind < MAX_STYLES; kind++) {
        BOOL simulated = !have[kind];
        /* only scalable fonts can be made bold or italic. */
        if (simulated && ((d->Flags & CF_NOSIMULATIONS) || !scalable))
            continue;
        d->Styles[kind].Kind = kind;
        d->Styles[kind].Simulated = simulated;
        LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)g_style_names[kind]);
        if (index >= 0) {
            SendMessageW(combo, CB_SETITEMDATA, (WPARAM)index, (LPARAM)&d->Styles[kind]);
            if (kind == want)
                pick = (int)index;
            added++;
        }
    }
    if (pick < 0 && added)
        pick = 0;
    if (pick >= 0)
        SendMessageW(combo, CB_SETCURSEL, (WPARAM)pick, 0);
}

static void add_size(HWND combo, int points)
{
    WCHAR text[16];
    wsprintfW(text, L"%d", points);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text);
}

static void fill_sizes(FONTDLG *d, FAMILY *fam, BYTE charset)
{
    HWND combo = item(d, FONT_SIZE);
    if (!combo)
        return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    if (!fam)
        return;
    int low = (d->Flags & CF_LIMITSIZE) ? d->Cf->nSizeMin : 0;
    int high = (d->Flags & CF_LIMITSIZE) ? d->Cf->nSizeMax : 0x7FFFFFFF;
    BOOL scalable = FALSE;
    for (int i = 0; i < fam->FaceCount; i++)
        if (!(fam->Faces[i].Type & RASTER_FONTTYPE))
            scalable = TRUE;
    if (scalable) {
        for (int i = 0; i < (int)(sizeof(g_standard_sizes) / sizeof(g_standard_sizes[0])); i++)
            if (g_standard_sizes[i] >= low && g_standard_sizes[i] <= high)
                add_size(combo, g_standard_sizes[i]);
        return;
    }
    int last = -1;
    for (int i = 0; i < fam->FaceCount; i++) {
        const FACE *f = &fam->Faces[i];
        if (charset != DEFAULT_CHARSET && f->Lf.lfCharSet != charset)
            continue;
        int points = MulDiv(f->Height - f->InternalLeading, 72, d->Dpi);
        if (points == last || points < low || points > high)
            continue;
        last = points;
        add_size(combo, points);
    }
}

/* ------------------------------------------------------------------ the selection */

/* the typed size in tenths of a point, or -1 when it is not a number. */
static int size_tenths(const FONTDLG *d)
{
    WCHAR text[32];
    combo_text(d, FONT_SIZE, text, 32);
    const WCHAR *p = text;
    while (*p == L' ')
        p++;
    if (*p < L'0' || *p > L'9')
        return -1;
    int whole = 0;
    while (*p >= L'0' && *p <= L'9' && whole < 100000)
        whole = whole * 10 + (*p++ - L'0');
    int tenth = 0;
    if (*p == L'.' || *p == L',') {
        p++;
        if (*p >= L'0' && *p <= L'9')
            tenth = *p++ - L'0';
        while (*p >= L'0' && *p <= L'9')
            p++;
    }
    while (*p == L' ')
        p++;
    if (*p)
        return -1;
    return whole * 10 + tenth;
}

static int selected_style(const FONTDLG *d, BOOL *valid)
{
    WCHAR text[LF_FACESIZE];
    combo_text(d, FONT_STYLE, text, LF_FACESIZE);
    *valid = FALSE;
    LRESULT index = combo_find(d, FONT_STYLE, text);
    if (index < 0)
        return STYLE_REGULAR;
    const STYLE *s = (const STYLE *)SendDlgItemMessageW(d->Dialog, FONT_STYLE, CB_GETITEMDATA, (WPARAM)index, 0);
    if (!s || (LRESULT)s == CB_ERR)
        return STYLE_REGULAR;
    *valid = TRUE;
    return s->Kind;
}

/* the font the controls show, its size in tenths of a point. */
static void collect(FONTDLG *d, LOGFONTW *lf, int *tenths, DWORD *type)
{
    WCHAR name[LF_FACESIZE];
    combo_text(d, FONT_FAMILY, name, LF_FACESIZE);
    FAMILY *fam = family_named(d, name);
    if (fam)
        load_faces(d, fam);
    d->FaceValid = fam != nullptr;
    BOOL style_ok;
    int kind = selected_style(d, &style_ok);
    d->StyleValid = style_ok;
    int tn = size_tenths(d);
    if (tn < 0)
        tn = 0;
    memset(lf, 0, sizeof(*lf));
    lstrcpynW(lf->lfFaceName, fam ? fam->Name : name, LF_FACESIZE);
    lf->lfHeight = -MulDiv(tn, d->Dpi, 720);
    lf->lfWeight = (kind == STYLE_BOLD || kind == STYLE_BOLD_ITALIC) ? FW_BOLD : FW_NORMAL;
    lf->lfItalic = (kind == STYLE_ITALIC || kind == STYLE_BOLD_ITALIC);
    lf->lfCharSet = fam ? charset_of_script(d) : DEFAULT_CHARSET;
    if (lf->lfCharSet == DEFAULT_CHARSET && fam)
        lf->lfCharSet = fam->Charset;
    lf->lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf->lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf->lfQuality = DEFAULT_QUALITY;
    if (fam)
        lf->lfPitchAndFamily = fam->PitchAndFamily;
    if (d->Flags & CF_EFFECTS) {
        lf->lfUnderline = (BYTE)(IsDlgButtonChecked(d->Dialog, FONT_UNDERLINE) == BST_CHECKED);
        lf->lfStrikeOut = (BYTE)(IsDlgButtonChecked(d->Dialog, FONT_STRIKEOUT) == BST_CHECKED);
    }
    *tenths = tn;
    DWORD t = fam ? (fam->Type & 0xF) : 0;
    if (d->Flags & CF_SCREENFONTS)
        t |= SCREEN_FONTTYPE;
    if (d->Flags & CF_PRINTERFONTS)
        t |= PRINTER_FONTTYPE;
    t |= (DWORD[]){REGULAR_FONTTYPE, ITALIC_FONTTYPE, BOLD_FONTTYPE, BOLD_FONTTYPE | ITALIC_FONTTYPE}[kind];
    if (style_ok) {
        LRESULT index = combo_find(d, FONT_STYLE, g_style_names[kind]);
        const STYLE *s = index >= 0 ? (const STYLE *)SendDlgItemMessageW(d->Dialog, FONT_STYLE, CB_GETITEMDATA, (WPARAM)index, 0) : nullptr;
        if (s && s->Simulated)
            t |= SIMULATED_FONTTYPE;
    }
    *type = t;
}

static COLORREF selected_color(const FONTDLG *d)
{
    LRESULT sel = SendDlgItemMessageW(d->Dialog, FONT_COLOR, CB_GETCURSEL, 0, 0);
    if (sel < 0)
        return 0;
    return (COLORREF)SendDlgItemMessageW(d->Dialog, FONT_COLOR, CB_GETITEMDATA, (WPARAM)sel, 0);
}

static void ansi_logfont(const LOGFONTW *w, LOGFONTA *a)
{
    memcpy(a, w, offsetof(LOGFONTA, lfFaceName));
    DlgToAnsi(w->lfFaceName, a->lfFaceName, LF_FACESIZE);
}

static void wide_logfont(const LOGFONTA *a, LOGFONTW *w)
{
    memcpy(w, a, offsetof(LOGFONTA, lfFaceName));
    DlgToWide(a->lfFaceName, w->lfFaceName, LF_FACESIZE);
}

/* copies the controls into the program's structure, for OK and Apply. */
static void store(FONTDLG *d)
{
    LOGFONTW lf;
    int tenths;
    DWORD type;
    collect(d, &lf, &tenths, &type);
    d->Cf->iPointSize = tenths;
    d->Cf->nFontType = (WORD)type;
    if (d->Flags & CF_EFFECTS)
        d->Cf->rgbColors = selected_color(d);
    if (d->Ansi) {
        ansi_logfont(&lf, (LOGFONTA *)d->Cf->lpLogFont);
        d->Ansi->iPointSize = tenths;
        d->Ansi->nFontType = (WORD)type;
        d->Ansi->rgbColors = d->Cf->rgbColors;
    } else {
        *d->Cf->lpLogFont = lf;
    }
    if (d->Flags & CF_USESTYLE) {
        BOOL ok;
        int kind = selected_style(d, &ok);
        if (d->Cf->lpszStyle) {
            if (d->Ansi)
                DlgToAnsi(g_style_names[kind], (LPSTR)d->Cf->lpszStyle, LF_FACESIZE);
            else
                lstrcpynW(d->Cf->lpszStyle, g_style_names[kind], LF_FACESIZE);
        }
    }
}

/* ------------------------------------------------------------------ the sample */

static void update_sample(FONTDLG *d)
{
    HWND sample = item(d, FONT_SAMPLE);
    LOGFONTW lf;
    int tenths;
    DWORD type;
    collect(d, &lf, &tenths, &type);
    /* the sample is drawn at screen size, no taller than its box. */
    RECT box;
    int max = 100;
    if (sample) {
        GetClientRect(sample, &box);
        max = box.bottom > 8 ? box.bottom : 8;
    }
    int px = -lf.lfHeight;
    if (px > max)
        px = max;
    if (px < 1)
        px = 1;
    lf.lfHeight = -px;
    HFONT font = CreateFontIndirectW(&lf);
    if (sample) {
        SendMessageW(sample, WM_SETFONT, (WPARAM)font, TRUE);
        InvalidateRect(sample, nullptr, TRUE);
    }
    if (d->Sample)
        DeleteObject(d->Sample);
    d->Sample = font;
    HWND info = item(d, FONT_INFO);
    if (info) {
        LPCWSTR text = L"";
        if (type & SIMULATED_FONTTYPE)
            text = L"This font style is imitated for the display. The closest matching style will be used for printing.";
        else if ((type & TRUETYPE_FONTTYPE) && (d->Flags & CF_BOTH) == CF_BOTH)
            text = L"This is a TrueType font. This same font will be used on both your printer and your screen.";
        else if (!(type & TRUETYPE_FONTTYPE) && (d->Flags & CF_BOTH) == CF_BOTH && (d->Flags & CF_WYSIWYG))
            text = L"This is a screen font. The closest matching printer font will be used for printing.";
        SetWindowTextW(info, text);
    }
}

static void refresh_lists(FONTDLG *d, int style_want, LPCWSTR size_text)
{
    FAMILY *fam = selected_family(d);
    BYTE charset = charset_of_script(d);
    fill_styles(d, fam, charset, style_want);
    fill_sizes(d, fam, charset);
    if (size_text) {
        LRESULT index = combo_find(d, FONT_SIZE, size_text);
        if (index >= 0)
            SendDlgItemMessageW(d->Dialog, FONT_SIZE, CB_SETCURSEL, (WPARAM)index, 0);
        else
            SetDlgItemTextW(d->Dialog, FONT_SIZE, size_text);
    }
    update_sample(d);
}

/* a family was picked or typed: refill the script, style and size lists. */
static void family_changed(FONTDLG *d)
{
    WCHAR size[32];
    BOOL ok;
    FAMILY *fam = selected_family(d);
    int kind = selected_style(d, &ok);
    combo_text(d, FONT_SIZE, size, 32);
    BYTE want = charset_of_script(d);
    d->Filling = TRUE;
    fill_scripts(d, fam, want);
    refresh_lists(d, kind, size[0] ? size : nullptr);
    d->Filling = FALSE;
}

/* ------------------------------------------------------------------ choosing from a LOGFONT */

static void apply_logfont(FONTDLG *d, const LOGFONTW *lf, int tenths, LPCWSTR style_name)
{
    d->Filling = TRUE;
    FAMILY *fam = lf->lfFaceName[0] ? family_named(d, lf->lfFaceName) : nullptr;
    if (fam) {
        load_faces(d, fam);
        LRESULT index = combo_find(d, FONT_FAMILY, fam->Name);
        if (index >= 0)
            SendDlgItemMessageW(d->Dialog, FONT_FAMILY, CB_SETCURSEL, (WPARAM)index, 0);
    } else {
        SetDlgItemTextW(d->Dialog, FONT_FAMILY, lf->lfFaceName);
    }
    fill_scripts(d, fam, lf->lfCharSet);
    int kind = style_kind(is_bold(lf), lf->lfItalic);
    if (style_name) {
        for (int i = 0; i < MAX_STYLES; i++)
            if (!_wcsicmp(style_name, g_style_names[i]))
                kind = i;
    }
    WCHAR size[32];
    if (tenths % 10)
        wsprintfW(size, L"%d.%d", tenths / 10, tenths % 10);
    else
        wsprintfW(size, L"%d", tenths / 10);
    refresh_lists(d, kind, size);
    if (d->Flags & CF_EFFECTS) {
        CheckDlgButton(d->Dialog, FONT_UNDERLINE, lf->lfUnderline ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(d->Dialog, FONT_STRIKEOUT, lf->lfStrikeOut ? BST_CHECKED : BST_UNCHECKED);
    }
    d->Filling = FALSE;
    update_sample(d);
}

static int tenths_of_height(FONTDLG *d, const LOGFONTW *lf)
{
    int h = lf->lfHeight;
    if (h < 0)
        return MulDiv(-h, 72, d->Dpi) * 10;
    if (h == 0)
        return 100;
    /* a positive height is the cell height, internal leading included. */
    HDC dc = GetDC(nullptr);
    LOGFONTW copy = *lf;
    HFONT font = CreateFontIndirectW(&copy);
    HGDIOBJ old = SelectObject(dc, font);
    TEXTMETRICW tm;
    int cell = h;
    if (GetTextMetricsW(dc, &tm))
        cell = h - tm.tmInternalLeading;
    SelectObject(dc, old);
    DeleteObject(font);
    ReleaseDC(nullptr, dc);
    return MulDiv(cell > 0 ? cell : 1, 72, d->Dpi) * 10;
}

static void default_logfont(LOGFONTW *lf)
{
    memset(lf, 0, sizeof(*lf));
    GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(*lf), lf);
}

/* ------------------------------------------------------------------ the dialog */

static void fill_family_combo(FONTDLG *d)
{
    HWND combo = item(d, FONT_FAMILY);
    if (!combo)
        return;
    for (int i = 0; i < d->FamilyCount; i++) {
        LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)d->Families[i].Name);
        if (index >= 0)
            SendMessageW(combo, CB_SETITEMDATA, (WPARAM)index, (LPARAM)&d->Families[i]);
    }
}

static void fill_color_combo(FONTDLG *d)
{
    HWND combo = item(d, FONT_COLOR);
    if (!combo)
        return;
    for (int i = 0; i < 16; i++) {
        LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)g_colors[i].Name);
        if (index >= 0)
            SendMessageW(combo, CB_SETITEMDATA, (WPARAM)index, (LPARAM)g_colors[i].Color);
    }
    int pick = 0;
    for (int i = 0; i < 16; i++)
        if (g_colors[i].Color == d->Cf->rgbColors)
            pick = i;
    SendMessageW(combo, CB_SETCURSEL, (WPARAM)pick, 0);
}

static void hide(const FONTDLG *d, int id)
{
    HWND w = item(d, id);
    if (w) {
        EnableWindow(w, FALSE);
        ShowWindow(w, SW_HIDE);
    }
}


static INT_PTR init_dialog(HWND hwnd, FONTDLG *d)
{
    d->Dialog = hwnd;
    SetPropW(hwnd, L"MimeFontDlg", d);
    if (d->Flags & CF_SCREENFONTS) {
        HDC screen = GetDC(nullptr);
        enumerate(d, screen, SCREEN_FONTTYPE);
        ReleaseDC(nullptr, screen);
    }
    if ((d->Flags & CF_PRINTERFONTS) && d->Cf->hDC) {
        if (d->Flags & CF_SCREENFONTS) {
            /* with both, show the printer faces the screen also has. */
        } else {
            enumerate(d, d->Cf->hDC, PRINTER_FONTTYPE);
        }
    }
    fill_family_combo(d);
    fill_color_combo(d);
    if (!(d->Flags & CF_EFFECTS)) {
        hide(d, 1072);
        hide(d, FONT_STRIKEOUT);
        hide(d, FONT_UNDERLINE);
        hide(d, 1091);
        hide(d, FONT_COLOR);
    }
    if (!(d->Flags & CF_APPLY))
        hide(d, FONT_APPLY);
    if (!(d->Flags & CF_SHOWHELP))
        hide(d, pshHelp);
    HWND combo = item(d, FONT_SIZE);
    if (combo)
        SendMessageW(combo, CB_LIMITTEXT, 8, 0);
    LOGFONTW lf;
    if (d->Flags & CF_INITTOLOGFONTSTRUCT) {
        if (d->Ansi)
            wide_logfont((const LOGFONTA *)d->Cf->lpLogFont, &lf);
        else
            lf = *d->Cf->lpLogFont;
    } else {
        default_logfont(&lf);
    }
    int tenths = tenths_of_height(d, &lf);
    WCHAR style[LF_FACESIZE];
    LPCWSTR style_name = nullptr;
    if ((d->Flags & CF_USESTYLE) && d->Cf->lpszStyle) {
        if (d->Ansi)
            DlgToWide((LPCSTR)d->Cf->lpszStyle, style, LF_FACESIZE);
        else
            lstrcpynW(style, d->Cf->lpszStyle, LF_FACESIZE);
        style_name = style;
    }
    apply_logfont(d, &lf, tenths, style_name);
    if (d->Flags & CF_NOFACESEL) {
        SendDlgItemMessageW(hwnd, FONT_FAMILY, CB_SETCURSEL, (WPARAM)-1, 0);
        SetDlgItemTextW(hwnd, FONT_FAMILY, L"");
    }
    if (d->Flags & CF_NOSIZESEL) {
        SendDlgItemMessageW(hwnd, FONT_SIZE, CB_SETCURSEL, (WPARAM)-1, 0);
        SetDlgItemTextW(hwnd, FONT_SIZE, L"");
    }
    if (d->Flags & CF_NOSTYLESEL) {
        SendDlgItemMessageW(hwnd, FONT_STYLE, CB_SETCURSEL, (WPARAM)-1, 0);
        SetDlgItemTextW(hwnd, FONT_STYLE, L"");
    }
    if (d->Flags & CF_NOSCRIPTSEL)
        EnableWindow(item(d, FONT_SCRIPT), FALSE);
    update_sample(d);
    if (d->Hook)
        return (INT_PTR)d->Hook(hwnd, WM_INITDIALOG, 0, d->Ansi ? (LPARAM)d->Ansi : (LPARAM)d->Cf);
    return TRUE;
}

static void message_box(FONTDLG *d, LPCWSTR text, int focus)
{
    WCHAR title[64];
    GetWindowTextW(d->Dialog, title, 64);
    MessageBoxW(d->Dialog, text, title, MB_OK | MB_ICONINFORMATION);
    HWND w = item(d, focus);
    if (w)
        SetFocus(w);
}

static BOOL accept(FONTDLG *d)
{
    int tenths = size_tenths(d);
    if (tenths < 0) {
        message_box(d, L"Size must be a number.", FONT_SIZE);
        return FALSE;
    }
    if (d->Flags & CF_LIMITSIZE) {
        int low = d->Cf->nSizeMin, high = d->Cf->nSizeMax;
        if (tenths < low * 10 || tenths > high * 10) {
            WCHAR text[96];
            wsprintfW(text, L"Size must be between %d and %d points.", low, high);
            message_box(d, text, FONT_SIZE);
            return FALSE;
        }
    }
    LOGFONTW lf;
    DWORD type;
    collect(d, &lf, &tenths, &type);
    if (d->Flags & CF_FORCEFONTEXIST) {
        if (!d->FaceValid) {
            message_box(d, L"There is no font with that name.\nChoose a font from the list of fonts.", FONT_FAMILY);
            return FALSE;
        }
        if (!d->StyleValid) {
            message_box(d, L"This font is not available in that style.\nChoose a style from the list of styles.", FONT_STYLE);
            return FALSE;
        }
    }
    store(d);
    return TRUE;
}

static void end_dialog(FONTDLG *d, INT_PTR result)
{
    EndDialog(d->Dialog, result);
}

static BOOL command(FONTDLG *d, WORD id, WORD code)
{
    switch (id) {
    case IDOK:
        if (accept(d))
            end_dialog(d, 1);
        return TRUE;
    case IDCANCEL:
        g_font_cancel = TRUE;
        end_dialog(d, 0);
        return TRUE;
    case FONT_APPLY:
        store(d);
        return TRUE;
    case pshHelp:
        if (d->Cf->hwndOwner)
            SendMessageW(d->Cf->hwndOwner, DlgMessage(&g_msg_help, HELPMSGSTRINGW), (WPARAM)d->Dialog,
                         d->Ansi ? (LPARAM)d->Ansi : (LPARAM)d->Cf);
        return TRUE;
    case FONT_FAMILY:
        if (d->Filling)
            return TRUE;
        if (code == CBN_SELCHANGE) {
            /* the edit does not show the new selection yet. */
            LRESULT sel = SendDlgItemMessageW(d->Dialog, FONT_FAMILY, CB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                WCHAR name[LF_FACESIZE];
                SendDlgItemMessageW(d->Dialog, FONT_FAMILY, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)name);
                SetDlgItemTextW(d->Dialog, FONT_FAMILY, name);
            }
            family_changed(d);
        } else if (code == CBN_EDITCHANGE) {
            WCHAR name[LF_FACESIZE];
            combo_text(d, FONT_FAMILY, name, LF_FACESIZE);
            if (family_named(d, name))
                family_changed(d);
            else
                update_sample(d);
        }
        return TRUE;
    case FONT_STYLE:
    case FONT_SIZE:
    case FONT_COLOR:
    case FONT_SCRIPT:
        if (d->Filling)
            return TRUE;
        if (code == CBN_SELCHANGE && id != FONT_SCRIPT) {
            LRESULT sel = SendDlgItemMessageW(d->Dialog, id, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && id != FONT_COLOR) {
                WCHAR text[LF_FACESIZE];
                SendDlgItemMessageW(d->Dialog, id, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)text);
                SetDlgItemTextW(d->Dialog, id, text);
            }
            update_sample(d);
        } else if (code == CBN_SELCHANGE && id == FONT_SCRIPT) {
            WCHAR size[32];
            BOOL ok;
            int kind = selected_style(d, &ok);
            combo_text(d, FONT_SIZE, size, 32);
            d->Filling = TRUE;
            refresh_lists(d, kind, size);
            d->Filling = FALSE;
        } else if (code == CBN_EDITCHANGE) {
            update_sample(d);
        }
        return TRUE;
    case FONT_STRIKEOUT:
    case FONT_UNDERLINE:
        update_sample(d);
        return TRUE;
    }
    return FALSE;
}

static void draw_item(FONTDLG *d, const DRAWITEMSTRUCT *di)
{
    if (di->itemID == (UINT)-1)
        return;
    BOOL selected = (di->itemState & ODS_SELECTED) != 0;
    FillRect(di->hDC, &di->rcItem, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    WCHAR text[LF_FACESIZE];
    text[0] = 0;
    SendMessageW(di->hwndItem, CB_GETLBTEXT, di->itemID, (LPARAM)text);
    RECT r = di->rcItem;
    r.left += 4;
    if (di->CtlID == FONT_COLOR) {
        COLORREF color = (COLORREF)di->itemData;
        RECT swatch = {r.left, r.top + 2, r.left + 22, r.bottom - 2};
        FrameRect(di->hDC, &swatch, (HBRUSH)GetStockObject(BLACK_BRUSH));
        RECT inner = {swatch.left + 1, swatch.top + 1, swatch.right - 1, swatch.bottom - 1};
        DlgFillRect(di->hDC, &inner, color);
        r.left = swatch.right + 6;
    } else if (di->CtlID == FONT_FAMILY) {
        const FAMILY *fam = (const FAMILY *)di->itemData;
        if (fam && (fam->Type & TRUETYPE_FONTTYPE)) {
            SetTextColor(di->hDC, selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : RGB(96, 96, 96));
            DrawTextW(di->hDC, L"TT", 2, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SetTextColor(di->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        }
        r.left += 20;
    }
    DrawTextW(di->hDC, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (di->itemState & ODS_FOCUS)
        DrawFocusRect(di->hDC, &di->rcItem);
}

static void measure_item(HWND hwnd, MEASUREITEMSTRUCT *mi)
{
    HDC dc = GetDC(hwnd);
    HFONT font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    if (old)
        SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    mi->itemHeight = (UINT)(tm.tmHeight + (mi->CtlID == FONT_COLOR ? 4 : 2));
}

static INT_PTR CALLBACK font_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    FONTDLG *d = (FONTDLG *)GetPropW(hwnd, L"MimeFontDlg");
    /* the combo boxes ask for item heights while the controls are created. */
    if (message == WM_MEASUREITEM && (!d || !d->Hook)) {
        measure_item(hwnd, (MEASUREITEMSTRUCT *)lparam);
        return TRUE;
    }
    if (!d) {
        if (message == WM_INITDIALOG)
            return init_dialog(hwnd, (FONTDLG *)lparam);
        DLGPROC early = g_font_hook_tls != TLS_OUT_OF_INDEXES ? (DLGPROC)TlsGetValue(g_font_hook_tls) : nullptr;
        return early ? early(hwnd, message, wparam, lparam) : 0;
    }
    INT_PTR result = d->Hook ? d->Hook(hwnd, message, wparam, lparam) : 0;
    if (result) {
        if (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)
            g_font_cancel = TRUE;
        return result;
    }
    switch (message) {
    case WM_COMMAND:
        return command(d, LOWORD(wparam), HIWORD(wparam));
    case WM_DRAWITEM:
        draw_item(d, (const DRAWITEMSTRUCT *)lparam);
        return TRUE;
    case WM_MEASUREITEM:
        measure_item(hwnd, (MEASUREITEMSTRUCT *)lparam);
        return TRUE;
    case WM_CTLCOLORSTATIC:
        if ((HWND)lparam == item(d, FONT_SAMPLE) && (d->Flags & CF_EFFECTS)) {
            SetTextColor((HDC)wparam, selected_color(d));
            SetBkMode((HDC)wparam, TRANSPARENT);
            return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
        }
        return FALSE;
    case WM_CHOOSEFONT_GETLOGFONT: {
        LOGFONTW lf;
        int tenths;
        DWORD type;
        collect(d, &lf, &tenths, &type);
        if (d->Ansi)
            ansi_logfont(&lf, (LOGFONTA *)lparam);
        else
            *(LOGFONTW *)lparam = lf;
        return TRUE;
    }
    case WM_CHOOSEFONT_SETLOGFONT: {
        LOGFONTW lf;
        if (d->Ansi)
            wide_logfont((const LOGFONTA *)lparam, &lf);
        else
            lf = *(const LOGFONTW *)lparam;
        apply_logfont(d, &lf, tenths_of_height(d, &lf), nullptr);
        return TRUE;
    }
    case WM_CHOOSEFONT_SETFLAGS:
        d->Cf->Flags = (DWORD)lparam;
        d->Flags = (DWORD)lparam;
        return TRUE;
    case WM_DESTROY:
        if (d->Sample) {
            HWND sample = item(d, FONT_SAMPLE);
            if (sample)
                SendMessageW(sample, WM_SETFONT, 0, FALSE);
            DeleteObject(d->Sample);
            d->Sample = nullptr;
        }
        RemovePropW(hwnd, L"MimeFontDlg");
        return FALSE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ the entry points */

static LPDLGTEMPLATEW build_template(void)
{
    static const DWORD tab = WS_VISIBLE | WS_TABSTOP;
    static const CTL controls[] = {
        {CTL_STATIC, L"&Font:", stc1, WS_VISIBLE | SS_LEFT, 7, 7, 60, 9},
        {CTL_COMBOBOX, L"", cmb1, tab | WS_VSCROLL | CBS_SIMPLE | CBS_OWNERDRAWFIXED | CBS_AUTOHSCROLL | CBS_SORT | CBS_HASSTRINGS | CBS_DISABLENOSCROLL, 7, 17, 98, 76},
        {CTL_STATIC, L"Font st&yle:", stc2, WS_VISIBLE | SS_LEFT, 110, 7, 60, 9},
        {CTL_COMBOBOX, L"", cmb2, tab | WS_VSCROLL | CBS_SIMPLE | CBS_AUTOHSCROLL | CBS_HASSTRINGS | CBS_DISABLENOSCROLL, 110, 17, 74, 76},
        {CTL_STATIC, L"&Size:", stc3, WS_VISIBLE | SS_LEFT, 189, 7, 30, 9},
        {CTL_COMBOBOX, L"", cmb3, tab | WS_VSCROLL | CBS_SIMPLE | CBS_AUTOHSCROLL | CBS_HASSTRINGS | CBS_DISABLENOSCROLL, 189, 17, 40, 76},
        {CTL_BUTTON, L"Effects", 1072, WS_VISIBLE | BS_GROUPBOX, 7, 98, 98, 72},
        {CTL_BUTTON, L"Stri&keout", chx1, tab | BS_AUTOCHECKBOX, 13, 111, 80, 10},
        {CTL_BUTTON, L"&Underline", chx2, tab | BS_AUTOCHECKBOX, 13, 124, 80, 10},
        {CTL_STATIC, L"&Color:", stc4, WS_VISIBLE | SS_LEFT, 13, 137, 40, 9},
        {CTL_COMBOBOX, L"", cmb4, tab | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, 13, 147, 82, 100},
        {CTL_BUTTON, L"Sample", 1073, WS_VISIBLE | BS_GROUPBOX, 110, 98, 119, 44},
        {CTL_STATIC, L"AaBbYyZz", stc5, WS_VISIBLE | SS_CENTER | SS_NOPREFIX, 118, 111, 103, 27},
        {CTL_STATIC, L"", stc6, WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 7, 174, 222, 20},
        {CTL_STATIC, L"Sc&ript:", stc7, WS_VISIBLE | SS_LEFT, 110, 148, 40, 9},
        {CTL_COMBOBOX, L"", cmb5, tab | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 110, 158, 119, 100},
        {CTL_BUTTON, L"OK", IDOK, tab | BS_DEFPUSHBUTTON, 235, 17, 45, 14},
        {CTL_BUTTON, L"Cancel", IDCANCEL, tab | BS_PUSHBUTTON, 235, 34, 45, 14},
        {CTL_BUTTON, L"&Apply", psh3, tab | BS_PUSHBUTTON, 235, 51, 45, 14},
        {CTL_BUTTON, L"&Help", pshHelp, tab | BS_PUSHBUTTON, 235, 68, 45, 14},
    };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | WS_VISIBLE;
    return DlgBuild(L"Font", style, 287, 198, controls, sizeof(controls) / sizeof(controls[0]));
}

static void release(FONTDLG *d)
{
    for (int i = 0; i < d->FamilyCount; i++)
        if (d->Families[i].Faces)
            HeapFree(GetProcessHeap(), 0, d->Families[i].Faces);
    if (d->Families)
        HeapFree(GetProcessHeap(), 0, d->Families);
}

static BOOL choose_font(CHOOSEFONTW *cf, CHOOSEFONTA *ansi)
{
    DlgSetError(0);
    g_font_cancel = FALSE;
    if (!cf) {
        DlgSetError(CDERR_INITIALIZATION);
        return FALSE;
    }
    if (cf->lStructSize != sizeof(CHOOSEFONTW)) {
        DlgSetError(CDERR_STRUCTSIZE);
        return FALSE;
    }
    DWORD flags = cf->Flags, error = 0;
    if (!DlgHookValid(flags, CF_ENABLEHOOK, cf->lpfnHook, &error)) {
        DlgSetError(error);
        return FALSE;
    }
    if (!(flags & CF_ENABLEHOOK))
        cf->lpfnHook = nullptr;
    if (!(flags & CF_BOTH)) {
        DlgSetError(CFERR_NOFONTS);
        return FALSE;
    }
    if ((flags & CF_LIMITSIZE) && cf->nSizeMax < cf->nSizeMin) {
        DlgSetError(CFERR_MAXLESSTHANMIN);
        return FALSE;
    }
    if ((flags & CF_PRINTERFONTS) && !cf->hDC) {
        DlgSetError(CDERR_INITIALIZATION);
        return FALSE;
    }
    DLGSOURCE source = {flags, CF_ENABLETEMPLATE, CF_ENABLETEMPLATEHANDLE, cf->hInstance, cf->lpTemplateName,
                        ansi != nullptr};
    const void *given = DlgFindTemplate(&source, &error);
    if (error) {
        DlgSetError(error);
        return FALSE;
    }
    LPDLGTEMPLATEW built = given ? nullptr : build_template();
    FONTDLG *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    LOGFONTW own;
    if (!d || (!given && !built)) {
        if (d)
            HeapFree(GetProcessHeap(), 0, d);
        if (built)
            HeapFree(GetProcessHeap(), 0, built);
        DlgSetError(CDERR_MEMALLOCFAILURE);
        return FALSE;
    }
    d->Cf = cf;
    d->Ansi = ansi;
    d->Flags = flags;
    d->Hook = (DLGPROC)cf->lpfnHook;
    /* a program that gave no LOGFONT gets a private one while the dialog runs. */
    LOGFONTW *given_lf = cf->lpLogFont;
    if (!given_lf) {
        memset(&own, 0, sizeof(own));
        cf->lpLogFont = &own;
    }
    HDC screen = GetDC(nullptr);
    d->Dc = (flags & CF_PRINTERFONTS) && cf->hDC ? cf->hDC : screen;
    d->Dpi = GetDeviceCaps(d->Dc, LOGPIXELSY);
    if (d->Dpi <= 0)
        d->Dpi = 96;
    ReleaseDC(nullptr, screen);
    if (g_font_hook_tls == TLS_OUT_OF_INDEXES && !g_font_once) {
        g_font_once = 1;
        g_font_hook_tls = TlsAlloc();
    }
    if (g_font_hook_tls != TLS_OUT_OF_INDEXES)
        TlsSetValue(g_font_hook_tls, (void *)d->Hook);
    INT_PTR answer = DialogBoxIndirectParamW(g_comdlg32, given ? (LPCDLGTEMPLATEW)given : built, cf->hwndOwner,
                                             font_proc, (LPARAM)d);
    if (g_font_hook_tls != TLS_OUT_OF_INDEXES)
        TlsSetValue(g_font_hook_tls, nullptr);
    if (!given_lf)
        cf->lpLogFont = nullptr;
    release(d);
    HeapFree(GetProcessHeap(), 0, d);
    if (built)
        HeapFree(GetProcessHeap(), 0, built);
    if (answer == -1 || (answer == 0 && !g_font_cancel && !CommDlgExtendedError()))
        DlgSetError(CDERR_DIALOGFAILURE);
    return answer == 1;
}

CDAPI BOOL WINAPI ChooseFontW(LPCHOOSEFONTW cf)
{
    return choose_font(cf, nullptr);
}

/* the ANSI structure is read as the wide one, keeping its pointers; the dialog converts at the edges. */
CDAPI BOOL WINAPI ChooseFontA(LPCHOOSEFONTA cf)
{
    if (!cf)
        return choose_font(nullptr, nullptr);
    CHOOSEFONTW wide;
    memcpy(&wide, cf, sizeof(wide));
    BOOL ok = choose_font(&wide, cf);
    cf->Flags = wide.Flags;
    cf->lpfnHook = wide.lpfnHook;
    return ok;
}
