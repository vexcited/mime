/*
 * property sheets and wizards. A sheet is a dialog from a template made in
 * memory, the page dialogs are its children and are created when they are
 * first shown. Everything a page hears comes as WM_NOTIFY with PSN_ codes,
 * the answers travel in DWLP_MSGRESULT. The notes in docs/comctl32.md list
 * the rules this follows.
 */
#include "comctl32_private.h"

#define MAX_PAGES 100
#define RT_DIALOGW MAKEINTRESOURCEW(5)
#define PAGE_MAGIC 0x50414745u

/* the controls of the sheet template. */
#define IDC_TAB 0x3020
#define IDC_APPLYNOW 0x3021
#define IDC_BACK 0x3023
#define IDC_NEXT 0x3024
#define IDC_FINISH 0x3025
#define IDC_BOTTOMLINE 0x3026
#define IDC_TOPLINE 0x3027
/* the command a click on the close box makes. */
#define ID_CLOSEBOX 8

#define WIZARD_MASK (PSH_WIZARD | PSH_WIZARD97_OLD | PSH_WIZARD_LITE | PSH_WIZARD97)
#define WIZARD97_MASK (PSH_WIZARD97 | PSH_WIZARD97_OLD)

/* sheet states. */
#define STATE_CANCELTOCLOSE 0x02
#define STATE_SHELLFONT 0x20

#define PSP_V1_SIZE ((DWORD)sizeof(PROPSHEETPAGEW_V1))
#define PSP_V2_SIZE ((DWORD)sizeof(PROPSHEETPAGEW_V2))
#define PSH_V1_SIZE ((DWORD)sizeof(PROPSHEETHEADERW_V1))
#define PSH_V2_SIZE ((DWORD)sizeof(PROPSHEETHEADERW_V2))

#define SIZEOF_TITLE 178
#define TCM_APPEND 1000

#ifndef WM_QUERYNEWPALETTE
#define WM_QUERYNEWPALETTE 0x030F
#endif
#ifndef WM_PALETTECHANGED
#define WM_PALETTECHANGED 0x0311
#endif
#ifndef HKM_SETHOTKEY
#define HKM_SETHOTKEY (WM_USER + 1)
#endif

/* the second notification the tab control sends when an object is asked for. */
typedef struct NMOBJECTNOTIFY_ {
    NMHDR hdr;
    int iItem;
    const GUID *piid;
    void *pObject;
    HRESULT hResult;
    DWORD dwFlags;
} NMOBJECTNOTIFY_;

/*
 * A page. The handle callers hold is the address of this structure. Psp is
 * the sheet's copy of the page with wide strings and must stay last, as a
 * program may append fields of its own to the page it gives.
 */
typedef struct PAGE {
    DWORD Magic;
    /* the structure as an ansi caller gave it, with ansi strings. */
    BYTE *Shadow;
    HWND Hwnd;
    BOOL Changed;
    /* the size of the dialog in pixels. */
    int Cx, Cy;
    PROPSHEETPAGEW Psp;
} PAGE;

typedef struct SHEET {
    HWND Hwnd;
    PROPSHEETHEADERW_V2 H;
    DWORD Flags;
    DWORD State;
    PAGE *Pages[MAX_PAGES];
    UINT Count;
    HWND Tab;
    HWND Current;
    int CurIndex;
    int DefId;
    int Result;
    int Restart;
    BOOL ApplyAllowed;
    BOOL Ansi;
    BOOL Modeless;
    /* where the pages go, in the client area of the sheet, and the size of the tab control when it was made. */
    RECT PageRect;
    int BaseCx, BaseCy;
    int HeaderHeight, TitleHeight;
    HFONT HeaderFont;
    HBITMAP Watermark, HeaderBitmap;
    HIMAGELIST Images;
    HICON LoadedIcon;
    HANDLE ActCtx;
    LPWSTR Caption, StartPage, IconName, WatermarkName, HeaderName;
    /* the last font a page asked for and the base units it gave. */
    WCHAR FontFace[LF_FACESIZE];
    int FontPoint, FontCharset, FontItalic;
    SIZE FontUnits;
} SHEET;

typedef struct PAGE_INFO {
    int Cx, Cy;
    BOOL Ex, Rtl, HasFont, Italic;
    int Point, Weight;
    BYTE Charset;
    WCHAR Face[LF_FACESIZE];
    WCHAR Title[SIZEOF_TITLE];
    HICON Icon;
    DWORD Style;
} PAGE_INFO;

#define PI_ICON 1
#define PI_FONT 2
#define PI_TITLE 4

static const WCHAR g_prop[] = L"MimePropSheet";

static const int g_normal_buttons[] = {IDOK, IDCANCEL, IDC_APPLYNOW, IDHELP, 0};
static const int g_wizard_buttons[] = {IDC_BACK, IDC_NEXT, IDC_FINISH, IDCANCEL, IDHELP, 0};
/* the buttons that may become the default one of a wizard, best first. */
static const int g_default_buttons[] = {IDC_NEXT, IDC_FINISH, IDC_BACK, IDCANCEL};
/* PSM_PRESSBUTTON indices to control ids. */
static const int g_press_ids[] = {IDC_BACK, IDC_NEXT, IDC_FINISH, IDOK, IDC_APPLYNOW, IDCANCEL, IDHELP};

static INT_PTR CALLBACK sheet_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
static void sheet_ncdestroy(SHEET *s, HWND hwnd);
static LRESULT CALLBACK wizard_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id,
                                         DWORD_PTR data);
static LRESULT CALLBACK button_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data);

static inline BOOL is_wizard(const SHEET *s)
{
    return (s->Flags & WIZARD_MASK) != 0;
}

static inline BOOL is_wizard97(const SHEET *s)
{
    return (s->Flags & WIZARD97_MASK) != 0;
}

static inline SHEET *get_sheet(HWND hwnd)
{
    return (SHEET *)GetPropW(hwnd, g_prop);
}

static inline BOOL is_resource_id(const void *p)
{
    return (UINT_PTR)p < 0x10000;
}

/* ------------------------------------------------------------------ strings */

static void *dup_ansi(const char *s)
{
    SIZE_T n = (SIZE_T)lstrlenA(s) + 1;
    char *d = CcAlloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

/* a copy of text in the character set the sheet keeps; ansi text is only ever copied to ansi. */
static void *dup_text(const void *s, BOOL src_wide, BOOL dst_wide)
{
    if (src_wide == dst_wide)
        return src_wide ? (void *)CcStrDupW(s) : dup_ansi(s);
    return CcStrDupAtoW(s);
}

static BOOL dup_slot(const void **slot, BOOL src_wide, BOOL dst_wide)
{
    if (is_resource_id(*slot))
        return TRUE;
    void *copy = dup_text(*slot, src_wide, dst_wide);
    if (!copy)
        return FALSE;
    *slot = copy;
    return TRUE;
}

static void free_slot(const void *s)
{
    if (!is_resource_id(s))
        CcFree((void *)s);
}

/*
 * Makes the page own the strings it points at and drops the pointers its
 * flags do not announce, as the page keeps them as it was made.
 */
static BOOL copy_page_strings(PROPSHEETPAGEW *p, BOOL src_wide, BOOL dst_wide)
{
    BOOL ok = TRUE;
    if (!(p->dwFlags & PSP_DLGINDIRECT))
        ok = dup_slot((const void **)&p->pszTemplate, src_wide, dst_wide);
    if (p->dwFlags & PSP_USEICONID)
        ok &= dup_slot((const void **)&p->pszIcon, src_wide, dst_wide);
    else if (!(p->dwFlags & PSP_USEHICON))
        p->hIcon = nullptr;
    if (p->dwFlags & PSP_USETITLE)
        ok &= dup_slot((const void **)&p->pszTitle, src_wide, dst_wide);
    else
        p->pszTitle = nullptr;
    if (p->dwFlags & PSP_USEHEADERTITLE)
        ok &= dup_slot((const void **)&p->pszHeaderTitle, src_wide, dst_wide);
    else if (p->dwSize > PSP_V1_SIZE)
        p->pszHeaderTitle = nullptr;
    if (p->dwFlags & PSP_USEHEADERSUBTITLE)
        ok &= dup_slot((const void **)&p->pszHeaderSubTitle, src_wide, dst_wide);
    else if (p->dwSize > PSP_V1_SIZE)
        p->pszHeaderSubTitle = nullptr;
    return ok;
}

static void free_page_strings(const PROPSHEETPAGEW *p)
{
    if (!(p->dwFlags & PSP_DLGINDIRECT))
        free_slot(p->pszTemplate);
    if (p->dwFlags & PSP_USEICONID)
        free_slot(p->pszIcon);
    if (p->dwFlags & PSP_USETITLE)
        free_slot(p->pszTitle);
    if (p->dwFlags & PSP_USEHEADERTITLE)
        free_slot(p->pszHeaderTitle);
    if (p->dwFlags & PSP_USEHEADERSUBTITLE)
        free_slot(p->pszHeaderSubTitle);
}

/* ------------------------------------------------------------------ activation contexts */

static ULONG_PTR activate_context(const SHEET *s, const PAGE *p)
{
    HANDLE ctx = nullptr;
    ULONG_PTR cookie = 0;
    if (p && (p->Psp.dwFlags & PSP_USEFUSIONCONTEXT) && p->Psp.dwSize > PSP_V2_SIZE)
        ctx = p->Psp.hActCtx;
    if (!ctx && s)
        ctx = s->ActCtx;
    if (ctx && !ActivateActCtx(ctx, &cookie))
        cookie = 0;
    return cookie;
}

static void deactivate_context(ULONG_PTR cookie)
{
    if (cookie)
        DeactivateActCtx(0, cookie);
}

/* ------------------------------------------------------------------ page handles */

static UINT call_page_callback(const SHEET *s, PAGE *p, UINT msg)
{
    const PROPSHEETPAGEW *psp = &p->Psp;
    if (!(psp->dwFlags & PSP_USECALLBACK) || !psp->pfnCallback)
        return 1;
    /* a page of the first size can only be asked to create or release. */
    if (psp->dwSize <= PSP_V1_SIZE && msg != PSPCB_RELEASE && msg != PSPCB_CREATE)
        return 1;
    ULONG_PTR cookie = activate_context(s, p);
    PROPSHEETPAGEW *arg = p->Shadow ? (PROPSHEETPAGEW *)p->Shadow : &p->Psp;
    UINT result = psp->pfnCallback(nullptr, msg, arg);
    deactivate_context(cookie);
    return result;
}

static PAGE *alloc_page(DWORD size)
{
    SIZE_T body = size > sizeof(PROPSHEETPAGEW) ? size : sizeof(PROPSHEETPAGEW);
    PAGE *p = CcAlloc(offsetof(PAGE, Psp) + body);
    if (p)
        p->Magic = PAGE_MAGIC;
    return p;
}

static void free_page(PAGE *p)
{
    if (p->Shadow) {
        free_page_strings((PROPSHEETPAGEW *)p->Shadow);
        CcFree(p->Shadow);
    }
    free_page_strings(&p->Psp);
    p->Magic = 0;
    CcFree(p);
}

static BOOL is_page(HPROPSHEETPAGE h)
{
    return h && ((PAGE *)h)->Magic == PAGE_MAGIC;
}

static HPROPSHEETPAGE create_page_handle(const PROPSHEETPAGEW *src, BOOL ansi)
{
    if (!src)
        return nullptr;
    DWORD size = src->dwSize;
    /* the structure is one of the known sizes up to a page of memory, and uses no flag above the last one. */
    if (size < PSP_V1_SIZE || size > 4096 || (src->dwFlags & 0xFFF80000))
        return nullptr;
    PAGE *p = alloc_page(size);
    if (!p)
        return nullptr;
    DWORD copy = ansi && size > sizeof(PROPSHEETPAGEW) ? (DWORD)sizeof(PROPSHEETPAGEW) : size;
    memcpy(&p->Psp, src, copy);
    if (!copy_page_strings(&p->Psp, !ansi, TRUE)) {
        free_page(p);
        return nullptr;
    }
    if (ansi) {
        /* callbacks and the dialog procedure see the structure as the caller made it. */
        SIZE_T body = size > sizeof(PROPSHEETPAGEW) ? size : sizeof(PROPSHEETPAGEW);
        p->Shadow = CcAlloc(body);
        if (!p->Shadow) {
            free_page(p);
            return nullptr;
        }
        memcpy(p->Shadow, src, size);
        if (!copy_page_strings((PROPSHEETPAGEW *)p->Shadow, FALSE, FALSE)) {
            free_page(p);
            return nullptr;
        }
    }
    if ((p->Psp.dwFlags & PSP_USEREFPARENT) && p->Psp.pcRefParent)
        InterlockedIncrement((LONG volatile *)p->Psp.pcRefParent);
    call_page_callback(nullptr, p, PSPCB_ADDREF);
    return (HPROPSHEETPAGE)p;
}

/* a page of an ansi caller keeps its strings in two forms; the wide one follows what the callback did to the ansi one. */
static void rethunk_shadow(PAGE *p)
{
    if (!p->Shadow)
        return;
    const PROPSHEETPAGEW *shadow = (const PROPSHEETPAGEW *)p->Shadow;
    DWORD size = shadow->dwSize > sizeof(PROPSHEETPAGEW) ? (DWORD)sizeof(PROPSHEETPAGEW) : shadow->dwSize;
    free_page_strings(&p->Psp);
    memcpy(&p->Psp, shadow, size);
    copy_page_strings(&p->Psp, FALSE, TRUE);
}

WINCOMMCTRLAPI HPROPSHEETPAGE WINAPI CreatePropertySheetPageA(LPCPROPSHEETPAGEA page)
{
    return create_page_handle((const PROPSHEETPAGEW *)page, TRUE);
}

WINCOMMCTRLAPI HPROPSHEETPAGE WINAPI CreatePropertySheetPageW(LPCPROPSHEETPAGEW page)
{
    return create_page_handle(page, FALSE);
}

WINCOMMCTRLAPI BOOL WINAPI DestroyPropertySheetPage(HPROPSHEETPAGE page)
{
    if (!is_page(page))
        return FALSE;
    PAGE *p = (PAGE *)page;
    call_page_callback(nullptr, p, PSPCB_RELEASE);
    if ((p->Psp.dwFlags & PSP_USEREFPARENT) && p->Psp.pcRefParent)
        InterlockedDecrement((LONG volatile *)p->Psp.pcRefParent);
    free_page(p);
    return TRUE;
}

/* ------------------------------------------------------------------ dialog templates */

typedef struct TEMPLATE_INFO {
    BOOL Ex;
    DWORD Style, ExStyle;
    WORD Items;
    int Cx, Cy;
    const WCHAR *Title;
    BOOL HasFont, Italic;
    int Point, Weight;
    BYTE Charset;
    WCHAR Face[LF_FACESIZE];
    /* the first control, aligned. */
    const BYTE *FirstItem;
} TEMPLATE_INFO;

/* skips a name that is 0xFFFF and an ordinal, a string, or a lone zero. */
static const BYTE *skip_name(const BYTE *p)
{
    const WORD *w = (const WORD *)p;
    if (*w == 0xFFFF)
        return p + 4;
    while (*w)
        w++;
    return (const BYTE *)(w + 1);
}

static BOOL parse_template(const void *tmpl, TEMPLATE_INFO *t)
{
    memset(t, 0, sizeof(*t));
    if (!tmpl)
        return FALSE;
    const WORD *w = tmpl;
    const BYTE *p;
    if (w[1] == 0xFFFF) {
        /* the extended form, version 1. */
        if (w[0] != 1)
            return FALSE;
        t->Ex = TRUE;
        t->ExStyle = *(const DWORD *)(w + 4);
        t->Style = *(const DWORD *)(w + 6);
        t->Items = w[8];
        t->Cx = (short)w[11];
        t->Cy = (short)w[12];
        p = (const BYTE *)(w + 13);
    } else {
        t->Style = *(const DWORD *)w;
        t->ExStyle = *(const DWORD *)(w + 2);
        t->Items = w[4];
        t->Cx = (short)w[7];
        t->Cy = (short)w[8];
        p = (const BYTE *)(w + 9);
    }
    p = skip_name(p);
    p = skip_name(p);
    t->Title = (const WCHAR *)p;
    p = skip_name(p);
    if (t->Style & DS_SETFONT) {
        t->Point = *(const WORD *)p;
        p += 2;
        t->Weight = 400;
        if (t->Ex) {
            t->Weight = *(const WORD *)p;
            t->Italic = p[2] != 0;
            t->Charset = p[3];
            p += 4;
        }
        const WCHAR *face = (const WCHAR *)p;
        for (int i = 0; i < LF_FACESIZE - 1 && face[i]; i++)
            t->Face[i] = face[i];
        t->HasFont = t->Point != 0;
        p = skip_name(p);
    }
    t->FirstItem = (const BYTE *)(((UINT_PTR)p + 3) & ~(UINT_PTR)3);
    return TRUE;
}

/* the size of a template in memory, which a page made with PSP_DLGINDIRECT has no other way to tell. */
static SIZE_T template_size(const void *tmpl)
{
    TEMPLATE_INFO t;
    if (!parse_template(tmpl, &t))
        return 0;
    const BYTE *p = t.FirstItem;
    for (UINT i = 0; i < t.Items; i++) {
        p = (const BYTE *)(((UINT_PTR)p + 3) & ~(UINT_PTR)3);
        p += t.Ex ? 24 : 18;
        p = skip_name(p);
        p = skip_name(p);
        p += 2 + *(const WORD *)p;
    }
    return (SIZE_T)(p - (const BYTE *)tmpl);
}

static const void *find_template(const PAGE *p, SIZE_T *size)
{
    if (p->Psp.dwFlags & PSP_DLGINDIRECT) {
        *size = template_size(p->Psp.pResource);
        return p->Psp.pResource;
    }
    HRSRC res = FindResourceExW(p->Psp.hInstance, RT_DIALOGW, p->Psp.pszTemplate, 0);
    if (!res)
        res = FindResourceW(p->Psp.hInstance, p->Psp.pszTemplate, RT_DIALOGW);
    if (!res)
        return nullptr;
    HGLOBAL g = LoadResource(p->Psp.hInstance, res);
    if (!g)
        return nullptr;
    const void *data = LockResource(g);
    *size = data ? SizeofResource(p->Psp.hInstance, res) : 0;
    return data;
}

static BOOL get_page_info(const SHEET *s, const PAGE *p, PAGE_INFO *info, UINT want)
{
    memset(info, 0, sizeof(*info));
    if (want & PI_ICON) {
        if (p->Psp.dwFlags & PSP_USEHICON)
            info->Icon = p->Psp.hIcon;
        else if (p->Psp.dwFlags & PSP_USEICONID)
            info->Icon = LoadImageW(p->Psp.hInstance, p->Psp.pszIcon, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), 0);
    }
    SIZE_T size = 0;
    const void *tmpl = find_template(p, &size);
    TEMPLATE_INFO t;
    if (!tmpl || !parse_template(tmpl, &t))
        return FALSE;
    info->Ex = t.Ex;
    info->Style = t.Style;
    info->Cx = t.Cx;
    info->Cy = t.Cy;
    info->Rtl = (t.Ex && (t.ExStyle & WS_EX_RTLREADING)) || (p->Psp.dwFlags & PSP_RTLREADING);
    if (want & (PI_FONT | PI_TITLE)) {
        BOOL own = (p->Psp.dwFlags & PSP_USETITLE) != 0;
        if (own) {
            if (!is_resource_id(p->Psp.pszTitle))
                CcCopyText(info->Title, SIZEOF_TITLE, p->Psp.pszTitle);
            else
                LoadStringW(p->Psp.hInstance, (UINT)(UINT_PTR)p->Psp.pszTitle, info->Title, SIZEOF_TITLE);
        } else {
            CcCopyText(info->Title, SIZEOF_TITLE, t.Title);
        }
        if ((want & PI_FONT) && t.HasFont) {
            info->HasFont = TRUE;
            info->Point = t.Point;
            info->Weight = t.Weight;
            info->Italic = t.Italic;
            info->Charset = t.Charset;
            memcpy(info->Face, t.Face, sizeof(info->Face));
            /* a sheet of shell fonts uses the font the shell picks for pages that ask for the old name. */
            if ((s->State & STATE_SHELLFONT) && t.Ex && (t.Style & DS_SHELLFONT) == DS_SHELLFONT &&
                !_wcsicmp(info->Face, L"MS Shell Dlg"))
                CcCopyText(info->Face, LF_FACESIZE, L"MS Shell Dlg 2");
        }
    }
    return TRUE;
}

/* ------------------------------------------------------------------ page sizes */

/* the base units of the font a page asks for; FALSE when it asks for none. */
static BOOL page_font_units(SHEET *s, const PAGE_INFO *info, SIZE *units)
{
    if (!info->HasFont || info->Point <= 0 || !info->Face[0])
        return FALSE;
    if (info->Point == s->FontPoint && info->Charset == s->FontCharset && info->Italic == s->FontItalic &&
        !_wcsicmp(info->Face, s->FontFace)) {
        *units = s->FontUnits;
        return TRUE;
    }
    HDC dc = GetDC(s->Hwnd);
    if (!dc)
        return FALSE;
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(info->Point, GetDeviceCaps(dc, LOGPIXELSY), 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfItalic = (BYTE)info->Italic;
    lf.lfCharSet = info->Charset;
    CcCopyText(lf.lfFaceName, LF_FACESIZE, info->Face);
    HFONT font = CreateFontIndirectW(&lf);
    BOOL ok = FALSE;
    if (font) {
        HGDIOBJ old = SelectObject(dc, font);
        LONG height = 0;
        LONG width = GdiGetCharDimensions(dc, nullptr, &height);
        SelectObject(dc, old);
        DeleteObject(font);
        s->FontUnits.cx = width;
        s->FontUnits.cy = height;
        s->FontPoint = info->Point;
        s->FontCharset = info->Charset;
        s->FontItalic = info->Italic;
        CcCopyText(s->FontFace, LF_FACESIZE, info->Face);
        *units = s->FontUnits;
        ok = TRUE;
    }
    ReleaseDC(s->Hwnd, dc);
    return ok;
}

static void compute_page_size(SHEET *s, PAGE *p, const PAGE_INFO *info)
{
    SIZE units;
    if (page_font_units(s, info, &units)) {
        p->Cx = MulDiv(info->Cx, units.cx, 4);
        p->Cy = MulDiv(info->Cy, units.cy, 8);
        return;
    }
    RECT r = {0, 0, info->Cx, info->Cy};
    MapDialogRect(s->Hwnd, &r);
    p->Cx = r.right;
    p->Cy = r.bottom;
}

/* the size the biggest page needs; the mask says which kind of page counts for the height. */
#define SIZE_HEADER_PAGES 2
#define SIZE_EXTERIOR_PAGES 1

static void ideal_page_size(const SHEET *s, SIZE *size, UINT which)
{
    size->cx = s->BaseCx;
    size->cy = s->BaseCy;
    for (UINT i = 0; i < s->Count; i++) {
        const PAGE *p = s->Pages[i];
        BOOL count_height = TRUE;
        if (is_wizard97(s)) {
            BOOL hidden = (p->Psp.dwFlags & PSP_HIDEHEADER) != 0;
            count_height = hidden ? (which & SIZE_EXTERIOR_PAGES) != 0 : (which & SIZE_HEADER_PAGES) != 0;
        }
        if (count_height && size->cy < p->Cy)
            size->cy = p->Cy;
        if (size->cx < p->Cx)
            size->cx = p->Cx;
    }
}

/* ------------------------------------------------------------------ notifications to pages */

static LRESULT notify_page(const SHEET *s, int index, UINT code, LPARAM lparam)
{
    if (index < 0 || (UINT)index >= s->Count || !s->Pages[index]->Hwnd)
        return 0;
    PSHNOTIFY n = {};
    n.hdr.hwndFrom = s->Hwnd;
    n.hdr.idFrom = (UINT_PTR)GetDlgCtrlID(s->Hwnd);
    n.hdr.code = code;
    n.lParam = lparam;
    return SendMessageW(s->Pages[index]->Hwnd, WM_NOTIFY, n.hdr.idFrom, (LPARAM)&n);
}

static LRESULT notify_current(const SHEET *s, UINT code, LPARAM lparam)
{
    return notify_page(s, s->CurIndex, code, lparam);
}

/* ------------------------------------------------------------------ small helpers */

static HWND get_item_rect(HWND dlg, int id, RECT *r)
{
    HWND item = GetDlgItem(dlg, id);
    if (item) {
        GetWindowRect(item, r);
        MapWindowPoints(nullptr, dlg, (POINT *)r, 2);
    } else {
        SetRectEmpty(r);
    }
    return item;
}

static void move_buttons(HWND dlg, const int *ids, int last, int dx, int dy)
{
    for (; *ids; ids++) {
        HWND item = GetDlgItem(dlg, *ids);
        RECT r = {};
        GetWindowRect(item, &r);
        LONG x = (GetWindowLongW(dlg, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) ? r.right : r.left;
        POINT pt = {x, r.top};
        ScreenToClient(dlg, &pt);
        SetWindowPos(item, nullptr, pt.x + dx, pt.y + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (*ids == last)
            break;
    }
}

/* hides a button and closes the gap it leaves in the row. */
static void remove_button(HWND dlg, int id, const int *ids)
{
    HWND item = GetDlgItem(dlg, id);
    int previous = 0;
    for (const int *p = ids; *p && *p != id; p++)
        previous = *p;
    int gap = 0;
    if (previous) {
        RECT before = {}, button = {};
        GetWindowRect(GetDlgItem(dlg, previous), &before);
        GetWindowRect(item, &button);
        gap = (GetWindowLongW(dlg, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) ? before.right - button.right
                                                                   : button.right - before.right;
    }
    move_buttons(dlg, ids, id, gap, 0);
    if (item)
        ShowWindow(item, SW_HIDE);
}

/* the tab control keeps one item per page, in the order of the pages. */
static HWND page_window(const SHEET *s, int index)
{
    return index >= 0 && (UINT)index < s->Count ? s->Pages[index]->Hwnd : nullptr;
}

static int find_page_by_handle(const SHEET *s, const void *h)
{
    for (int i = (int)s->Count - 1; i >= 0; i--)
        if ((const void *)s->Pages[i] == h)
            return i;
    return -1;
}

static int find_page_by_window(const SHEET *s, HWND hwnd)
{
    if (!hwnd)
        return -1;
    for (UINT i = 0; i < s->Count; i++)
        if (s->Pages[i]->Hwnd == hwnd)
            return (int)i;
    return -1;
}

/* the index of the page whose dialog id is given, or that many pages along when no id is given. */
static int find_page_index(const SHEET *s, int current, UINT_PTR id, int step)
{
    if (id) {
        for (UINT i = 0; i < s->Count; i++)
            if ((UINT_PTR)s->Pages[i]->Psp.pszTemplate == id)
                return (int)i;
        return -1;
    }
    UINT result = (UINT)(step + current);
    return result <= s->Count ? (int)result : -1;
}

/* ------------------------------------------------------------------ title */

static void set_title(SHEET *s)
{
    WCHAR loaded[128], formatted[SIZEOF_TITLE];
    LPCWSTR title = s->Caption;
    if (is_resource_id(title)) {
        loaded[0] = 0;
        LoadStringW(s->H.hInstance, (UINT)(UINT_PTR)title, loaded, 128);
        title = loaded;
    }
    if (s->Flags & PSH_PROPTITLE) {
        static const WCHAR suffix[] = L" Properties";
        int length = lstrlenW(title);
        if (!*title) {
            CcCopyText(formatted, SIZEOF_TITLE, L"Properties");
            title = formatted;
        } else if ((UINT)length + 13 + 2 < SIZEOF_TITLE) {
            CcCopyText(formatted, SIZEOF_TITLE, title);
            CcCopyText(formatted + length, SIZEOF_TITLE - length, suffix);
            title = formatted;
        }
    }
    if (s->Flags & PSH_RTLREADING)
        SetWindowLongW(s->Hwnd, GWL_EXSTYLE, GetWindowLongW(s->Hwnd, GWL_EXSTYLE) | WS_EX_RTLREADING);
    SetWindowTextW(s->Hwnd, title);
}

/* ------------------------------------------------------------------ wizard buttons */

static void reset_wizard_buttons(SHEET *s)
{
    if (!is_wizard(s))
        return;
    for (const int *id = g_wizard_buttons; *id; id++)
        SendDlgItemMessageW(s->Hwnd, *id, BM_SETSTYLE, BS_PUSHBUTTON, TRUE);
}

/* the default button follows the first of the usual ones that can be clicked. */
static BOOL reset_focus_and_default(SHEET *s)
{
    HWND focus = GetFocus();
    BOOL refocus = !focus || (SendMessageW(focus, WM_GETDLGCODE, 0, 0) & (DLGC_DEFPUSHBUTTON | DLGC_UNDEFPUSHBUTTON)) ||
                   !IsWindowVisible(focus) || !IsWindowEnabled(focus);
    HWND button = nullptr;
    int found = -1;
    for (int i = 0; i < 4; i++) {
        HWND item = GetDlgItem(s->Hwnd, g_default_buttons[i]);
        if ((GetWindowLongW(item, GWL_STYLE) & WS_VISIBLE) && IsWindowEnabled(item)) {
            button = item;
            found = i;
            break;
        }
    }
    if (found >= 0)
        s->DefId = g_default_buttons[found];
    if (refocus && (!focus || focus == s->Hwnd || IsChild(s->Hwnd, focus)))
        SetFocus(button ? button : s->Hwnd);
    reset_wizard_buttons(s);
    if (found >= 0)
        SendMessageW(s->Hwnd, DM_SETDEFID, (WPARAM)g_default_buttons[found], 0);
    return refocus;
}

static void set_wizard_buttons(SHEET *s, DWORD flags)
{
    HWND dlg = s->Hwnd;
    int shown = IDC_NEXT, hidden = IDC_FINISH;
    EnableWindow(GetDlgItem(dlg, IDC_BACK), (flags & PSWIZB_BACK) != 0);
    EnableWindow(GetDlgItem(dlg, IDC_NEXT), (flags & PSWIZB_NEXT) != 0);
    if (flags & (PSWIZB_FINISH | PSWIZB_DISABLEDFINISH)) {
        shown = IDC_FINISH;
        hidden = IDC_NEXT;
        EnableWindow(GetDlgItem(dlg, IDC_FINISH), (flags & PSWIZB_FINISH) != 0);
    }
    /* a sheet with its own finish button keeps both on the screen. */
    if (!(s->Flags & PSH_WIZARDHASFINISH)) {
        ShowWindow(GetDlgItem(dlg, hidden), SW_HIDE);
        ShowWindow(GetDlgItem(dlg, shown), SW_SHOW);
    }
    reset_focus_and_default(s);
}

static void set_finish_text(SHEET *s, const void *text, BOOL ansi)
{
    HWND dlg = s->Hwnd, finish = GetDlgItem(dlg, IDC_FINISH);
    HWND focus = GetFocus();
    BOOL had_focus = FALSE;
    if (!(s->Flags & PSH_WIZARDHASFINISH)) {
        HWND next = GetDlgItem(dlg, IDC_NEXT);
        ShowWindow(next, SW_HIDE);
        had_focus = next == focus;
    }
    HWND back = GetDlgItem(dlg, IDC_BACK);
    if (back == focus)
        had_focus = TRUE;
    ShowWindow(back, SW_HIDE);
    if (text) {
        if (ansi)
            SetWindowTextA(finish, text);
        else
            SetWindowTextW(finish, text);
    }
    ShowWindow(finish, SW_SHOW);
    EnableWindow(finish, TRUE);
    reset_wizard_buttons(s);
    SendMessageW(dlg, DM_SETDEFID, IDC_FINISH, 0);
    s->DefId = IDC_FINISH;
    if (had_focus)
        SetFocus(finish);
}

/* ------------------------------------------------------------------ pages */

static void remove_page_data(SHEET *s, int index)
{
    PAGE *p = s->Pages[index];
    ULONG_PTR cookie = activate_context(s, p);
    SendMessageW(s->Tab, TCM_DELETEITEM, (WPARAM)index, 0);
    DestroyPropertySheetPage((HPROPSHEETPAGE)p);
    deactivate_context(cookie);
    memmove(&s->Pages[index], &s->Pages[index + 1], sizeof(PAGE *) * (s->Count - (UINT)index - 1));
    s->Count--;
}

static HWND create_page_dialog(SHEET *s, PAGE *p, DLGTEMPLATE *tmpl)
{
    ULONG_PTR cookie = activate_context(s, p);
    DWORD *style = (DWORD *)tmpl;
    if (HIWORD(tmpl->style) == 0xFFFF)
        style = (DWORD *)((BYTE *)tmpl + 12);
    /* a page is a child that takes part in the dialog keys and keeps only what a child may of its own styles. */
    DWORD keep = *style & (WS_CLIPCHILDREN | DS_SETFONT | DS_LOCALEDIT);
    if ((s->State & STATE_SHELLFONT) || (*style & DS_SHELLFONT) != DS_SHELLFONT)
        keep |= *style & DS_FIXEDSYS;
    *style = keep | WS_CHILD | WS_TABSTOP | DS_CONTROL | DS_3DLOOK;
    PROPSHEETPAGEW *arg = p->Shadow ? (PROPSHEETPAGEW *)p->Shadow : &p->Psp;
    HWND page;
    if (p->Shadow) {
        page = CreateDialogIndirectParamA(p->Psp.hInstance, (LPCDLGTEMPLATEA)tmpl, s->Hwnd, p->Psp.pfnDlgProc,
                                          (LPARAM)arg);
        rethunk_shadow(p);
    } else {
        page = CreateDialogIndirectParamW(p->Psp.hInstance, tmpl, s->Hwnd, p->Psp.pfnDlgProc, (LPARAM)arg);
    }
    if (page && !is_wizard(s))
        EnableThemeDialogTexture(page, ETDT_USETABTEXTURE);
    deactivate_context(cookie);
    return page;
}

static HWND create_page(SHEET *s, PAGE *p)
{
    if (!call_page_callback(s, p, PSPCB_CREATE))
        return nullptr;
    rethunk_shadow(p);
    SIZE_T size = 0;
    const void *tmpl = find_template(p, &size);
    if (!tmpl || !size)
        return nullptr;
    DLGTEMPLATE *copy = CcAlloc(size);
    if (!copy)
        return nullptr;
    memcpy(copy, tmpl, size);
    HWND page = create_page_dialog(s, p, copy);
    CcFree(copy);
    return page;
}

static void store_page_window(SHEET *s, int index)
{
    TCITEMW item = {};
    item.mask = TCIF_PARAM;
    item.lParam = (LPARAM)s->Pages[index]->Hwnd;
    SendMessageW(s->Tab, TCM_SETITEMW, (WPARAM)index, (LPARAM)&item);
}

static void page_position(SHEET *s, int index, HWND page)
{
    HWND dlg = s->Hwnd;
    const PAGE *p = s->Pages[index];
    if (!is_wizard(s)) {
        RECT r = {};
        GetClientRect(s->Tab, &r);
        SendMessageW(s->Tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&r);
        MapWindowPoints(s->Tab, dlg, (POINT *)&r, 2);
        SetWindowPos(page, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, 0);
        return;
    }
    HWND top_line = GetDlgItem(dlg, IDC_TOPLINE);
    if (is_wizard97(s)) {
        RECT client = {}, line = {};
        GetClientRect(dlg, &client);
        HWND bottom = get_item_rect(dlg, IDC_BOTTOMLINE, &line);
        if (bottom)
            SetWindowPos(bottom, nullptr, client.left, line.top, client.right - client.left, line.bottom - line.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        if (p->Psp.dwFlags & PSP_HIDEHEADER) {
            /* an exterior page covers the sheet down to the line over the buttons. */
            ShowWindow(top_line, SW_HIDE);
            ShowWindow(s->Tab, SW_HIDE);
            get_item_rect(dlg, IDC_BOTTOMLINE, &line);
            SetWindowPos(page, nullptr, client.left, client.top, client.right - client.left, line.top - client.top, 0);
            return;
        }
        ShowWindow(top_line, SW_SHOW);
        ShowWindow(s->Tab, SW_SHOW);
    } else {
        ShowWindow(top_line, SW_HIDE);
    }
    SetWindowPos(page, nullptr, s->PageRect.left, s->PageRect.top, s->PageRect.right - s->PageRect.left,
                 s->PageRect.bottom - s->PageRect.top, 0);
}

/* the focus goes to the first control of the page that can have it, or as the page asks. */
static void set_new_default(SHEET *s)
{
    HWND first = GetNextDlgTabItem(s->Current, nullptr, FALSE);
    if (!first)
        return;
    if (SendMessageW(first, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL)
        SendMessageW(first, EM_SETSEL, 0, (LPARAM)-1);
    int id = GetDlgCtrlID(first);
    HWND target = first;
    if ((GetWindowLongW(first, GWL_STYLE) & (WS_VISIBLE | WS_DISABLED | WS_TABSTOP)) == (WS_VISIBLE | WS_TABSTOP)) {
        HWND wanted = (HWND)notify_current(s, PSN_QUERYINITIALFOCUS, (LPARAM)first);
        if (wanted && IsWindow(wanted) && IsChild(s->Current, wanted))
            target = wanted;
    } else if (is_wizard(s)) {
        target = GetDlgItem(s->Hwnd, s->DefId);
    } else {
        target = s->Tab;
    }
    SetFocus(target);
    reset_wizard_buttons(s);
    HWND owner = s->Hwnd;
    WPARAM def = (WPARAM)s->DefId;
    if (SendDlgItemMessageW(s->Current, id, WM_GETDLGCODE, 0, 0) & DLGC_UNDEFPUSHBUTTON) {
        owner = s->Current;
        def = (WPARAM)id;
    }
    SendMessageW(owner, DM_SETDEFID, def, 0);
}

/* shows the page the tab control selected; a page may send the sheet on to another one. */
static void page_change(SHEET *s, int direction)
{
    HWND tab = s->Tab;
    HWND focus = GetFocus();
    for (UINT tries = 1; tries <= s->Count; tries++) {
        int index = (int)SendMessageW(tab, TCM_GETCURSEL, 0, 0);
        if (index < 0 || (UINT)index >= s->Count)
            return;
        PAGE *p = s->Pages[index];
        HWND page = p->Hwnd;
        if (!page) {
            page = create_page(s, p);
            if (!page) {
                remove_page_data(s, index);
                SendMessageW(tab, TCM_SETCURSEL, 0, 0);
                continue;
            }
            p->Hwnd = page;
            store_page_window(s, index);
            if (is_wizard97(s) && (p->Psp.dwFlags & PSP_HIDEHEADER))
                SetWindowSubclass(page, wizard_page_proc, 0, (DWORD_PTR)s);
        }
        page_position(s, index, page);
        LRESULT answer = notify_page(s, index, PSN_SETACTIVE, 0);
        if (answer) {
            /* the page asked for another one, by dialog id or, with -1, the next one in the direction. */
            int other = find_page_index(s, index, answer == -1 ? 0 : (UINT_PTR)answer, direction);
            if (answer == -1 && (other == index || other >= (int)SendMessageW(tab, TCM_GETITEMCOUNT, 0, 0)))
                other = s->CurIndex;
            if (other != -1) {
                SendMessageW(tab, TCM_SETCURSEL, (WPARAM)other, 0);
                ShowWindow(page, SW_HIDE);
                continue;
            }
        }
        if ((s->Flags & PSH_HASHELP))
            EnableWindow(GetDlgItem(s->Hwnd, IDHELP), (p->Psp.dwFlags & PSP_HASHELP) != 0);
        if (is_wizard(s)) {
            /* a wizard takes the title of the page it shows. */
            TCITEMW item = {};
            WCHAR title[SIZEOF_TITLE] = L"";
            item.mask = TCIF_TEXT;
            item.pszText = title;
            item.cchTextMax = SIZEOF_TITLE;
            if (SendMessageW(tab, TCM_GETITEMW, (WPARAM)index, (LPARAM)&item)) {
                BOOL rtl = (s->Flags & PSH_RTLREADING) || (p->Psp.dwFlags & PSP_RTLREADING);
                LONG ex = GetWindowLongW(s->Hwnd, GWL_EXSTYLE);
                SetWindowLongW(s->Hwnd, GWL_EXSTYLE, rtl ? (ex | WS_EX_RTLREADING) : (ex & ~WS_EX_RTLREADING));
                if (title[0])
                    SetWindowTextW(s->Hwnd, title);
            }
        }
        ShowWindow(page, SW_SHOW);
        if (s->Current && s->Current != page)
            ShowWindow(s->Current, SW_HIDE);
        s->Current = page;
        s->CurIndex = index;
        if (focus == tab)
            SendMessageW(s->Hwnd, WM_NEXTDLGCTL, (WPARAM)tab, TRUE);
        else
            set_new_default(s);
        if (is_wizard97(s) && !(p->Psp.dwFlags & PSP_HIDEHEADER))
            InvalidateRect(s->Hwnd, nullptr, TRUE);
        return;
    }
}

/* asks the current page whether it may be left; TRUE when it refuses. */
static BOOL page_changing(SHEET *s)
{
    return s->Current && notify_current(s, PSN_KILLACTIVE, 0) != 0;
}

static BOOL page_set_selection(SHEET *s, int index, const void *handle, int direction)
{
    if (handle) {
        int found = find_page_by_handle(s, handle);
        if (found != -1)
            index = found;
        else if (index == -1)
            return FALSE;
    } else if (index == -1) {
        return FALSE;
    }
    if (index >= MAX_PAGES)
        return FALSE;
    if (page_changing(s))
        return FALSE;
    int old = (int)SendMessageW(s->Tab, TCM_GETCURSEL, 0, 0);
    BOOL ok = TRUE;
    if (SendMessageW(s->Tab, TCM_SETCURSEL, (WPARAM)index, 0) == -1) {
        SendMessageW(s->Tab, TCM_SETCURSEL, (WPARAM)old, 0);
        ok = FALSE;
    }
    page_change(s, direction);
    return ok;
}

static BOOL wizard_next_back(SHEET *s, UINT code)
{
    LRESULT answer = notify_current(s, code, 0);
    if (answer == -1)
        return FALSE;
    int step = code == PSN_WIZNEXT ? 1 : -1;
    int index = find_page_index(s, s->CurIndex, (UINT_PTR)answer, step);
    return index != -1 && page_set_selection(s, index, nullptr, step);
}

/* adds a page to a running sheet; FALSE when it has no usable dialog. */
static BOOL insert_page(SHEET *s, UINT_PTR after, HPROPSHEETPAGE handle)
{
    if (!is_page(handle) || s->Count >= MAX_PAGES)
        return FALSE;
    int at;
    if (after >= 0x10000) {
        int found = find_page_by_handle(s, (void *)after);
        if (found == -1)
            return FALSE;
        at = found + 1;
    } else {
        at = after > s->Count ? (int)s->Count : (int)after;
    }
    PAGE *p = (PAGE *)handle;
    for (int i = (int)s->Count - 1; i >= at; i--)
        s->Pages[i + 1] = s->Pages[i];
    s->Pages[at] = p;
    s->Count++;
    HIMAGELIST images = (HIMAGELIST)SendMessageW(s->Tab, TCM_GETIMAGELIST, 0, 0);
    PAGE_INFO info;
    BOOL ok = get_page_info(s, p, &info, PI_ICON | PI_FONT | PI_TITLE);
    if (ok) {
        compute_page_size(s, p, &info);
        TCITEMW item = {};
        item.mask = info.Rtl ? 15 : 11;
        item.pszText = info.Title;
        item.iImage = -1;
        if (info.Icon) {
            if (images)
                item.iImage = ImageList_ReplaceIcon(images, -1, info.Icon);
            DestroyIcon(info.Icon);
        }
        SendMessageW(s->Tab, TCM_INSERTITEMW, (WPARAM)at, (LPARAM)&item);
        if (p->Psp.dwFlags & PSP_PREMATURE) {
            p->Hwnd = create_page(s, p);
            if (!p->Hwnd) {
                SendMessageW(s->Tab, TCM_DELETEITEM, (WPARAM)at, 0);
                ok = FALSE;
            } else {
                store_page_window(s, at);
            }
        }
    }
    if (!ok) {
        for (int i = at; i + 1 < (int)s->Count; i++)
            s->Pages[i] = s->Pages[i + 1];
        s->Count--;
        return FALSE;
    }
    if (s->CurIndex >= at)
        s->CurIndex++;
    return TRUE;
}

static void remove_page(SHEET *s, UINT index, HPROPSHEETPAGE handle)
{
    int at;
    if (handle && (at = find_page_by_handle(s, handle)) != -1) {
    } else if (index < s->Count) {
        at = (int)index;
    } else {
        return;
    }
    int selected = (int)SendMessageW(s->Tab, TCM_GETCURSEL, 0, 0);
    if (at == selected) {
        page_changing(s);
        int other = selected ? selected - 1 : 1;
        if (SendMessageW(s->Tab, TCM_SETCURSEL, (WPARAM)other, 0) == -1)
            SendMessageW(s->Tab, TCM_SETCURSEL, 0, 0);
        page_change(s, 1);
    }
    /* the page may have moved while its sheet changed pages. */
    HWND window = s->Pages[at]->Hwnd;
    if (window) {
        if (s->Current == window)
            s->Current = nullptr;
        DestroyWindow(window);
    }
    remove_page_data(s, at);
}

static LRESULT query_siblings(SHEET *s, WPARAM wparam, LPARAM lparam)
{
    for (UINT i = 0; i < s->Count; i++) {
        HWND page = s->Pages[i]->Hwnd;
        if (page) {
            LRESULT r = SendMessageW(page, PSM_QUERYSIBLINGS, wparam, lparam);
            if (r)
                return r;
        }
    }
    return 0;
}

static void page_changed(SHEET *s, HWND page)
{
    int index = find_page_by_window(s, page);
    if (index < 0)
        return;
    s->Pages[index]->Changed = TRUE;
    if (s->ApplyAllowed)
        EnableWindow(GetDlgItem(s->Hwnd, IDC_APPLYNOW), TRUE);
}

static void page_unchanged(SHEET *s, HWND page)
{
    int index = find_page_by_window(s, page);
    if (index < 0)
        return;
    s->Pages[index]->Changed = FALSE;
    for (UINT i = 0; i < s->Count; i++)
        if (s->Pages[i]->Changed)
            return;
    EnableWindow(GetDlgItem(s->Hwnd, IDC_APPLYNOW), FALSE);
}

/* ------------------------------------------------------------------ the buttons of the sheet */

static void send_last_chance_apply(SHEET *s)
{
    for (int i = (int)s->Count - 1; i >= 0; i--)
        if (s->Pages[i]->Hwnd)
            notify_page(s, i, PSN_LASTCHANCEAPPLY, TRUE);
}

/*
 * OK, Apply and Cancel. Both of the first two ask the page to give up the
 * focus and then tell every page that was ever shown; the sheet ends when
 * every page agreed.
 */
static BOOL button_pushed(SHEET *s, UINT id)
{
    int result = s->Result;
    int button;
    LPARAM closing;
    UINT code;
    BOOL end, ask_kill;
    switch (id) {
    case IDOK:
        button = PSBTN_OK, closing = TRUE, end = TRUE, ask_kill = TRUE, code = PSN_APPLY;
        break;
    case IDC_APPLYNOW:
        button = PSBTN_APPLYNOW, closing = FALSE, end = FALSE, ask_kill = TRUE, code = PSN_APPLY;
        break;
    case IDCANCEL:
        button = PSBTN_CANCEL, closing = FALSE, end = TRUE, ask_kill = FALSE, code = PSN_RESET;
        break;
    case ID_CLOSEBOX:
        button = PSBTN_FINISH, closing = TRUE, end = TRUE, ask_kill = FALSE, code = PSN_RESET;
        break;
    default:
        return FALSE;
    }
    if (ask_kill) {
        if (notify_current(s, PSN_KILLACTIVE, 0))
            return FALSE;
        result = 1;
    }
    if (s->H.pfnCallback)
        s->H.pfnCallback(s->Hwnd, PSCB_BUTTONPRESSED, button);
    HCURSOR old_cursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    LRESULT refusal = 0;
    for (UINT i = 0; i < s->Count; i++) {
        if (!s->Pages[i]->Hwnd)
            continue;
        refusal = notify_page(s, (int)i, code, closing);
        if (refusal) {
            if (refusal != PSNRET_INVALID_NOCHANGEPAGE)
                SendMessageW(s->Tab, TCM_SETCURSEL, i, 0);
            break;
        }
        if (code == PSN_APPLY && !end)
            end = s->Restart != 0;
        s->Pages[i]->Changed = FALSE;
    }
    BOOL done = refusal == 0;
    if (done) {
        s->Current = nullptr;
        s->Result = result;
        if (end) {
            SetCursor(old_cursor);
            return TRUE;
        }
    }
    if (done) {
        if (!is_wizard(s)) {
            HWND apply = GetDlgItem(s->Hwnd, IDC_APPLYNOW);
            SendMessageW(apply, BM_SETSTYLE, BS_PUSHBUTTON, TRUE);
            EnableWindow(apply, FALSE);
            reset_wizard_buttons(s);
            SendMessageW(s->Hwnd, DM_SETDEFID, IDOK, 0);
            s->DefId = IDOK;
        }
        if (s->State & STATE_CANCELTOCLOSE) {
            s->State &= ~STATE_CANCELTOCLOSE;
            SetDlgItemTextW(s->Hwnd, IDOK, L"OK");
            EnableWindow(GetDlgItem(s->Hwnd, IDCANCEL), TRUE);
        }
    }
    if (refusal != PSNRET_INVALID_NOCHANGEPAGE)
        page_change(s, 1);
    SetCursor(old_cursor);
    return done;
}

static BOOL on_command(SHEET *s, UINT id, HWND control, UINT code)
{
    if (control && GetParent(control) != s->Hwnd) {
        SendMessageW(page_window(s, s->CurIndex), WM_COMMAND, MAKEWPARAM(id, code), (LPARAM)control);
        return TRUE;
    }
    if (!control)
        control = GetDlgItem(s->Hwnd, (int)id);
    switch (id) {
    case IDCANCEL:
    case ID_CLOSEBOX:
        if (!notify_current(s, PSN_QUERYCANCEL, 0))
            button_pushed(s, id);
        return TRUE;
    case IDHELP:
        if (IsWindowEnabled(control))
            notify_current(s, PSN_HELP, 0);
        return TRUE;
    case IDC_BACK:
    case IDC_NEXT:
        s->DefId = (int)id;
        wizard_next_back(s, id == IDC_NEXT ? PSN_WIZNEXT : PSN_WIZBACK);
        return TRUE;
    case IDC_FINISH: {
        EnableWindow(s->Hwnd, FALSE);
        HWND refusal = (HWND)notify_current(s, PSN_WIZFINISH, 0);
        if (refusal) {
            EnableWindow(s->Hwnd, TRUE);
            if (IsWindow(refusal) && IsChild(s->Hwnd, refusal))
                SetFocus(refusal);
        } else {
            s->Current = nullptr;
            s->Result = 1;
        }
        return TRUE;
    }
    case IDOK:
    case IDC_APPLYNOW:
        if (!is_wizard(s) && button_pushed(s, id))
            send_last_chance_apply(s);
        return TRUE;
    default:
        SendMessageW(page_window(s, s->CurIndex), WM_COMMAND, MAKEWPARAM(id, code), (LPARAM)control);
        return TRUE;
    }
}

/* ------------------------------------------------------------------ keyboard */

static BOOL page_is_hidden(const SHEET *s, int index)
{
    (void)s;
    (void)index;
    return FALSE;
}

static BOOL is_dialog_message(SHEET *s, MSG *msg)
{
    if (!msg)
        return FALSE;
    BOOL switching = FALSE, previous = FALSE;
    if (msg->message == WM_KEYDOWN && GetKeyState(VK_CONTROL) < 0) {
        if (msg->wParam == VK_TAB) {
            switching = TRUE;
            previous = GetKeyState(VK_SHIFT) < 0;
        } else if (msg->wParam == VK_PRIOR || msg->wParam == VK_NEXT) {
            switching = TRUE;
            previous = msg->wParam == VK_PRIOR;
        }
    }
    if (!switching) {
        if (msg->message - WM_KEYFIRST > WM_KEYLAST - WM_KEYFIRST || !msg->hwnd ||
            (IsChild(s->Hwnd, msg->hwnd) && !IsChild(s->Current, msg->hwnd)) ||
            notify_current(s, PSN_TRANSLATEACCELERATOR, (LPARAM)msg) != PSNRET_MESSAGEHANDLED)
            return IsDialogMessageW(s->Hwnd, msg);
        return TRUE;
    }
    SendMessageW(s->Hwnd, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS | UISF_HIDEACCEL), 0);
    if (is_wizard(s)) {
        HWND button = GetDlgItem(s->Hwnd, previous ? IDC_BACK : IDC_NEXT);
        if (IsWindowVisible(button) && IsWindowEnabled(button))
            wizard_next_back(s, previous ? PSN_WIZBACK : PSN_WIZNEXT);
    } else {
        int count = (int)s->Count, start = (int)SendMessageW(s->Tab, TCM_GETCURSEL, 0, 0), index = start;
        if (count > 0) {
            do {
                index = previous ? (index + count - 1) % count : (index + 1) % count;
            } while (page_is_hidden(s, index) && index != start);
            page_set_selection(s, index, nullptr, 1);
        }
    }
    return TRUE;
}

static BOOL handle_hotkey(HWND hwnd)
{
    HWND focus = GetFocus();
    WORD key = (WORD)SendMessageW(hwnd, WM_GETHOTKEY, 0, 0);
    WCHAR name[32];
    if (!GetClassNameW(focus, name, 32) || lstrcmpW(name, L"msctls_hotkey32"))
        return FALSE;
    SendMessageW(focus, HKM_SETHOTKEY, key, 0);
    return TRUE;
}

/* ------------------------------------------------------------------ headers of wizard 97 pages */

static BOOL wizard_rect(const SHEET *s, int part, RECT *r)
{
    if (!(s->Flags & PSH_WIZARD97))
        return FALSE;
    RECT client = {};
    GetClientRect(s->Hwnd, &client);
    switch (part) {
    case 0: {
        /* the bitmap in the corner. */
        int left = client.right - client.left - 54;
        SetRect(r, left, 5, left + 49, 54);
        return TRUE;
    }
    case 1:
        SetRect(r, client.left + 22, client.top + 10, client.right - 64, s->HeaderHeight);
        return TRUE;
    case 2:
        SetRect(r, 44, s->TitleHeight, client.right - 64, s->HeaderHeight + client.top);
        return TRUE;
    }
    return FALSE;
}

static int write_header_text(const SHEET *s, HDC dc, RECT *r, const WCHAR *text, BOOL title, UINT format)
{
    WCHAR loaded[1040];
    int height = 0;
    if (is_resource_id(text)) {
        LoadStringW(s->Pages[s->CurIndex]->Psp.hInstance, (UINT)(UINT_PTR)text, loaded, 1040);
        text = loaded;
    }
    HGDIOBJ font = (title && s->HeaderFont) ? s->HeaderFont : (HGDIOBJ)SendMessageW(s->Hwnd, WM_GETFONT, 0, 0);
    HGDIOBJ old = SelectObject(dc, font);
    if (title)
        ExtTextOutW(dc, r->left, r->top, 0, nullptr, text, (UINT)lstrlenW(text), nullptr);
    else
        height = DrawTextW(dc, text, lstrlenW(text), r, format);
    if (old)
        SelectObject(dc, old);
    return height;
}

/* the height of the header: room for the title and the longest subtitle. */
static int compute_header_height(SHEET *s)
{
    int height = 58, leading = 5, title_height = 25;
    HDC dc = GetDC(s->Hwnd);
    HGDIOBJ old = SelectObject(dc, s->HeaderFont ? s->HeaderFont : (HGDIOBJ)SendMessageW(s->Hwnd, WM_GETFONT, 0, 0));
    TEXTMETRICW tm = {};
    if (GetTextMetricsW(dc, &tm)) {
        leading = tm.tmExternalLeading;
        if (tm.tmExternalLeading + tm.tmHeight + 10 > 25)
            title_height = tm.tmExternalLeading + tm.tmHeight + 10;
    }
    s->TitleHeight = title_height;
    if (old)
        SelectObject(dc, old);
    RECT probe;
    if (wizard_rect(s, 2, &probe)) {
        for (UINT i = 0; i < s->Count; i++) {
            const PAGE *p = s->Pages[i];
            RECT r;
            if ((p->Psp.dwFlags & (PSP_USEHEADERSUBTITLE | PSP_HIDEHEADER)) != PSP_USEHEADERSUBTITLE ||
                !wizard_rect(s, 2, &r))
                continue;
            int wanted = write_header_text(s, dc, &r, p->Psp.pszHeaderSubTitle, FALSE,
                                           DT_EDITCONTROL | DT_NOPREFIX | DT_CALCRECT | DT_WORDBREAK) + title_height;
            if (wanted > height)
                height = wanted;
        }
    }
    ReleaseDC(s->Hwnd, dc);
    return height == 58 ? 58 : height + leading;
}

static void fill_bitmap(HDC dc, const RECT *r, HBITMAP bitmap)
{
    BITMAP bm = {};
    GetObjectW(bitmap, sizeof(bm), &bm);
    HDC src = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(src, bitmap);
    int cx = r->right - r->left, cy = r->bottom - r->top;
    int w = bm.bmWidth < cx ? bm.bmWidth : cx, h = bm.bmHeight < cy ? bm.bmHeight : cy;
    BitBlt(dc, r->left, r->top, w, h, src, 0, 0, SRCCOPY);
    /* the edge pixel fills what the bitmap does not reach. */
    if (w < cx)
        StretchBlt(dc, r->left + w, r->top, cx - w, h, src, 0, 0, 1, 1, SRCCOPY);
    if (h < cy)
        StretchBlt(dc, r->left, r->top + h, cx, cy - h, src, 0, 0, 1, 1, SRCCOPY);
    SelectObject(src, old);
    DeleteDC(src);
}

static void paint_header(SHEET *s, const PAGE *p, HDC dc)
{
    if (p->Psp.dwFlags & PSP_HIDEHEADER)
        return;
    RECT client = {}, band, r;
    GetClientRect(s->Hwnd, &client);
    if (client.right <= 0 || client.bottom <= 0)
        return;
    band = client;
    band.top = 0;
    band.bottom = s->HeaderHeight;
    if (wizard_rect(s, 0, &r)) {
        if ((s->Flags & PSH_HEADER) && s->HeaderBitmap) {
            FillRect(dc, &band, GetSysColorBrush(COLOR_WINDOW));
            fill_bitmap(dc, &r, s->HeaderBitmap);
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        } else {
            SendMessageW(s->Hwnd, WM_CTLCOLORDLG, (WPARAM)dc, (LPARAM)s->Hwnd);
        }
    }
    SetBkMode(dc, TRANSPARENT);
    if ((p->Psp.dwFlags & PSP_USEHEADERTITLE) && p->Psp.pszHeaderTitle && wizard_rect(s, 1, &band))
        write_header_text(s, dc, &band, p->Psp.pszHeaderTitle, TRUE, 0);
    if ((p->Psp.dwFlags & PSP_USEHEADERSUBTITLE) && p->Psp.pszHeaderSubTitle && wizard_rect(s, 2, &band))
        write_header_text(s, dc, &band, p->Psp.pszHeaderSubTitle, FALSE, DT_EDITCONTROL | DT_NOPREFIX | DT_WORDBREAK);
}

/* the left side of an exterior page shows the watermark and the rest is the window color. */
static BOOL erase_exterior(SHEET *s, HDC dc)
{
    if (!s->Watermark)
        return FALSE;
    RECT r = {};
    GetClientRect(s->Hwnd, &r);
    r.left = 164;
    FillRect(dc, &r, GetSysColorBrush(COLOR_WINDOW));
    r.right = r.left;
    r.left = 0;
    fill_bitmap(dc, &r, s->Watermark);
    return TRUE;
}

static LRESULT CALLBACK wizard_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id,
                                         DWORD_PTR data)
{
    SHEET *s = (SHEET *)data;
    switch (msg) {
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, wizard_page_proc, 0);
        break;
    case WM_ERASEBKGND:
        if (erase_exterior(s, (HDC)wparam))
            return 1;
        break;
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
        if (s->Watermark) {
            LRESULT def = DefWindowProcW(hwnd, msg, wparam, lparam);
            if (def == DefSubclassProc(hwnd, msg, wparam, lparam)) {
                SetTextColor((HDC)wparam, GetSysColor(COLOR_WINDOWTEXT));
                SetBkColor((HDC)wparam, GetSysColor(COLOR_WINDOW));
                return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
            }
            return def;
        }
        break;
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

/* a hidden wizard button must not answer the mnemonics of the characters typed. */
static LRESULT CALLBACK button_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data)
{
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, button_proc, 0);
    } else if (msg == WM_GETDLGCODE) {
        LRESULT code = DefSubclassProc(hwnd, msg, wparam, lparam);
        if (!IsWindowVisible(hwnd) && lparam == 0)
            return code | DLGC_WANTCHARS;
        return code;
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

/* ------------------------------------------------------------------ headers set by messages */

static void set_header_text(SHEET *s, UINT index, WCHAR *text, BOOL title)
{
    if (!is_wizard97(s) || index >= s->Count || (s->Pages[index]->Psp.dwFlags & PSP_HIDEHEADER)) {
        free_slot(text);
        return;
    }
    PROPSHEETPAGEW *psp = &s->Pages[index]->Psp;
    DWORD flag = title ? PSP_USEHEADERTITLE : PSP_USEHEADERSUBTITLE;
    const WCHAR **slot = title ? &psp->pszHeaderTitle : &psp->pszHeaderSubTitle;
    const WCHAR *old = *slot;
    *slot = text;
    if ((psp->dwFlags & flag) && old)
        free_slot(old);
    psp->dwFlags |= flag;
    if ((int)index == s->CurIndex) {
        RECT r = {};
        GetClientRect(s->Hwnd, &r);
        r.bottom = s->HeaderHeight;
        InvalidateRect(s->Hwnd, &r, FALSE);
    }
}

/* ------------------------------------------------------------------ size changes */

static BOOL resize_dialog(SHEET *s, int dx, int dy, int top_shift)
{
    if (!dx && !dy && !top_shift)
        return FALSE;
    int vertical = dy + top_shift;
    HWND dlg = s->Hwnd;
    RECT r = {};
    HWND item;
    /* the tab control and the page change by the amount the page area did. */
    GetWindowRect(s->Tab, &r);
    MapWindowPoints(nullptr, dlg, (POINT *)&r, 2);
    SetWindowPos(s->Tab, nullptr, r.left, r.top, r.right - r.left + dx, r.bottom - r.top + vertical,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    if (s->Current)
        SetWindowPos(s->Current, nullptr, s->PageRect.left, s->PageRect.top, s->PageRect.right - s->PageRect.left,
                     s->PageRect.bottom - s->PageRect.top, SWP_NOZORDER | SWP_NOACTIVATE);
    const int *ids = g_normal_buttons;
    if (is_wizard(s)) {
        if ((item = get_item_rect(dlg, IDC_BOTTOMLINE, &r)))
            SetWindowPos(item, nullptr, r.left, r.top + vertical, r.right - r.left + dx, r.bottom - r.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        if ((item = get_item_rect(dlg, IDC_TOPLINE, &r)))
            SetWindowPos(item, nullptr, r.left, r.top, r.right - r.left + dx, r.bottom - r.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        ids = g_wizard_buttons;
    }
    for (; *ids; ids++) {
        item = get_item_rect(dlg, *ids, &r);
        if (item)
            SetWindowPos(item, nullptr, r.left + dx, r.top + vertical, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    GetWindowRect(dlg, &r);
    SetWindowPos(dlg, nullptr, 0, 0, r.right - r.left + dx, r.bottom - r.top + vertical,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    return TRUE;
}

static BOOL recalc_page_sizes(SHEET *s)
{
    int shift = 0;
    if (!is_wizard(s)) {
        RECT r = {};
        GetClientRect(s->Tab, &r);
        MapWindowPoints(s->Tab, s->Hwnd, (POINT *)&r, 2);
        SendMessageW(s->Tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&r);
        shift = r.top - s->PageRect.top;
        s->PageRect.top = r.top;
    }
    SIZE size;
    ideal_page_size(s, &size, SIZE_HEADER_PAGES);
    int dx = size.cx - (s->PageRect.right - s->PageRect.left);
    int dy = size.cy - (s->PageRect.bottom - s->PageRect.top);
    s->PageRect.right = s->PageRect.left + size.cx;
    s->PageRect.bottom = s->PageRect.top + size.cy;
    return resize_dialog(s, dx, dy, shift);
}

/* ------------------------------------------------------------------ the sheet template */

typedef struct TEMPLATE_CONTROL {
    WORD Class;
    const WCHAR *ClassName;
    DWORD Style;
    short X, Y, Cx, Cy;
    WORD Id;
    const WCHAR *Text;
} TEMPLATE_CONTROL;

#define CLASS_BUTTON 0x0080
#define CLASS_STATIC 0x0082

static const TEMPLATE_CONTROL g_normal_controls[] = {
    {CLASS_BUTTON, nullptr, 0x50030001, 4, 122, 50, 14, IDOK, L"OK"},
    {CLASS_BUTTON, nullptr, 0x50010000, 58, 122, 50, 14, IDCANCEL, L"Cancel"},
    {CLASS_BUTTON, nullptr, 0x58010000, 112, 122, 50, 14, IDC_APPLYNOW, L"&Apply"},
    {CLASS_BUTTON, nullptr, 0x50030000, 166, 122, 50, 14, IDHELP, L"Help"},
    {0, L"SysTabControl32", 0x50030200, 4, 4, 212, 114, IDC_TAB, L""},
};

static const TEMPLATE_CONTROL g_wizard_controls[] = {
    {0, L"SysTabControl32", 0x58020000, 7, 7, 276, 3, IDC_TAB, L""},
    {CLASS_STATIC, nullptr, 0x50001000, 7, 17, 276, 1, IDC_BOTTOMLINE, L""},
    {CLASS_BUTTON, nullptr, 0x50010000, 12, 26, 50, 14, IDC_BACK, L"< &Back"},
    {CLASS_BUTTON, nullptr, 0x50010001, 62, 26, 50, 14, IDC_NEXT, L"&Next >"},
    {CLASS_BUTTON, nullptr, 0x50010000, 119, 26, 50, 14, IDC_FINISH, L"Finish"},
    {CLASS_BUTTON, nullptr, 0x50010000, 176, 26, 50, 14, IDCANCEL, L"Cancel"},
    {CLASS_BUTTON, nullptr, 0x50030000, 233, 26, 50, 14, IDHELP, L"Help"},
    {CLASS_STATIC, nullptr, 0x50001000, 0, 35, 290, 1, IDC_TOPLINE, L""},
};

typedef struct TEMPLATE_WRITER {
    BYTE *Data;
    SIZE_T Size, Capacity;
} TEMPLATE_WRITER;

static void put(TEMPLATE_WRITER *w, const void *data, SIZE_T size)
{
    if (w->Size + size <= w->Capacity)
        memcpy(w->Data + w->Size, data, size);
    w->Size += size;
}

static void put_word(TEMPLATE_WRITER *w, WORD v)
{
    put(w, &v, 2);
}

static void put_dword(TEMPLATE_WRITER *w, DWORD v)
{
    put(w, &v, 4);
}

static void put_string(TEMPLATE_WRITER *w, const WCHAR *s)
{
    put(w, s, ((SIZE_T)lstrlenW(s) + 1) * sizeof(WCHAR));
}

static void align4(TEMPLATE_WRITER *w)
{
    while (w->Size & 3) {
        BYTE zero = 0;
        put(w, &zero, 1);
    }
}

/* the template of a sheet in the form a resource has, or the extended form for a sheet of shell fonts. */
static void write_template(TEMPLATE_WRITER *w, const SHEET *s, BOOL ex)
{
    BOOL wizard = is_wizard(s);
    const TEMPLATE_CONTROL *controls = wizard ? g_wizard_controls : g_normal_controls;
    WORD count = (WORD)(wizard ? sizeof(g_wizard_controls) / sizeof(g_wizard_controls[0])
                               : sizeof(g_normal_controls) / sizeof(g_normal_controls[0]));
    DWORD style = wizard ? 0x90c800c4 : 0x90c820c4;
    short cx = wizard ? 290 : 220, cy = wizard ? 46 : 140;
    if (ex)
        style |= DS_SHELLFONT;
    if (ex) {
        put_word(w, 1);
        put_word(w, 0xFFFF);
        put_dword(w, 0);
        put_dword(w, 0);
        put_dword(w, style);
    } else {
        put_dword(w, style);
        put_dword(w, 0);
    }
    put_word(w, count);
    put_word(w, 20);
    put_word(w, 20);
    put_word(w, (WORD)cx);
    put_word(w, (WORD)cy);
    put_word(w, 0);
    put_word(w, 0);
    put_word(w, 0);
    put_word(w, 8);
    if (ex) {
        /* the weight, then no italic and the default character set. */
        put_word(w, 0);
        put_word(w, 0x0100);
    }
    put_string(w, L"MS Shell Dlg");
    for (WORD i = 0; i < count; i++) {
        const TEMPLATE_CONTROL *c = &controls[i];
        align4(w);
        if (ex) {
            put_dword(w, 0);
            put_dword(w, 0);
            put_dword(w, c->Style);
        } else {
            put_dword(w, c->Style);
            put_dword(w, 0);
        }
        put_word(w, (WORD)c->X);
        put_word(w, (WORD)c->Y);
        put_word(w, (WORD)c->Cx);
        put_word(w, (WORD)c->Cy);
        if (ex)
            put_dword(w, c->Id);
        else
            put_word(w, c->Id);
        if (c->Class) {
            put_word(w, 0xFFFF);
            put_word(w, c->Class);
        } else {
            put_string(w, c->ClassName);
        }
        put_string(w, c->Text);
        put_word(w, 0);
    }
}

/* the template is made twice as big as it is so that the callback may add to it. */
static DLGTEMPLATE *build_template(const SHEET *s, BOOL ex)
{
    TEMPLATE_WRITER w = {nullptr, 0, 0};
    write_template(&w, s, ex);
    BYTE *data = CcAlloc(w.Size * 2);
    if (!data)
        return nullptr;
    w.Data = data;
    w.Capacity = w.Size * 2;
    w.Size = 0;
    write_template(&w, s, ex);
    return (DLGTEMPLATE *)data;
}

/* a sheet takes the font the shell picks only when every page asks for it in the extended form. */
static BOOL should_use_shell_font(SHEET *s)
{
    for (UINT i = 0; i < s->Count; i++) {
        PAGE_INFO info;
        if (get_page_info(s, s->Pages[i], &info, 0) && (!info.Ex || (info.Style & DS_SHELLFONT) != DS_SHELLFONT))
            return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ initialization of the sheet window */

static void create_header_font(SHEET *s)
{
    LOGFONTW lf = {};
    HFONT font = (HFONT)SendMessageW(s->Hwnd, WM_GETFONT, 0, 0);
    GetObjectW(font, sizeof(lf), &lf);
    lf.lfWeight = FW_BOLD;
    s->HeaderFont = CreateFontIndirectW(&lf);
}

static HBITMAP load_bitmap(const SHEET *s, UINT flag, const WCHAR *name, HBITMAP given, int cx, int cy)
{
    if (s->Flags & flag)
        return (HBITMAP)CopyImage(given, IMAGE_BITMAP, cx, cy, LR_COPYRETURNORG);
    return (HBITMAP)LoadImageW(s->H.hInstance, name, IMAGE_BITMAP, cx, cy, LR_CREATEDIBSECTION);
}

static void init_sheet(HWND hwnd, SHEET *s)
{
    SetPropW(hwnd, g_prop, s);
    s->Hwnd = hwnd;
    s->CurIndex = 0;
    s->ApplyAllowed = TRUE;
    if (!is_wizard(s)) {
        set_title(s);
    } else {
        SetWindowSubclass(GetDlgItem(hwnd, IDC_BACK), button_proc, 0, (DWORD_PTR)s);
        SetWindowSubclass(GetDlgItem(hwnd, IDC_NEXT), button_proc, 0, (DWORD_PTR)s);
        SetWindowSubclass(GetDlgItem(hwnd, IDC_FINISH), button_proc, 0, (DWORD_PTR)s);
    }
    if (s->Flags & PSH_USEICONID) {
        s->LoadedIcon = LoadImageW(s->H.hInstance, s->IconName, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
        s->H.hIcon = s->LoadedIcon;
    }
    if ((s->Flags & (PSH_USEHICON | PSH_USEICONID)) && s->H.hIcon)
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)s->H.hIcon);
    s->Tab = GetDlgItem(hwnd, IDC_TAB);

    UINT start = s->H.nStartPage;
    const WCHAR *start_title = nullptr;
    WCHAR loaded[128];
    if (s->Flags & PSH_USEPSTARTPAGE) {
        start_title = s->StartPage;
        start = 0;
        if (is_resource_id(start_title)) {
            loaded[0] = 0;
            LoadStringW(s->H.hInstance, (UINT)(UINT_PTR)start_title, loaded, 128);
            start_title = loaded;
        }
    }
    SendMessageW(s->Tab, WM_SETREDRAW, FALSE, 0);
    BOOL premature = FALSE;
    HIMAGELIST images = nullptr;
    for (UINT i = 0; i < s->Count;) {
        PAGE *p = s->Pages[i];
        PAGE_INFO info;
        if (!get_page_info(s, p, &info, PI_ICON | PI_FONT | PI_TITLE)) {
            remove_page_data(s, (int)i);
            continue;
        }
        compute_page_size(s, p, &info);
        TCITEMW item = {};
        item.mask = info.Rtl ? 15 : 11;
        item.pszText = info.Title;
        item.iImage = -1;
        if (info.Icon) {
            if (!images) {
                BOOL mirrored = (GetWindowLongW(s->Tab, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0;
                images = ImageList_Create(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                          mirrored ? (ILC_MASK | ILC_MIRROR) : ILC_MASK, 8, 4);
                SendMessageW(s->Tab, TCM_SETIMAGELIST, 0, (LPARAM)images);
            }
            item.iImage = ImageList_ReplaceIcon(images, -1, info.Icon);
            DestroyIcon(info.Icon);
        }
        SendMessageW(s->Tab, TCM_INSERTITEMW, TCM_APPEND, (LPARAM)&item);
        if (p->Psp.dwFlags & PSP_PREMATURE)
            premature = TRUE;
        if (start_title && !lstrcmpiW(start_title, info.Title))
            start = i;
        i++;
    }
    s->Images = images;
    SendMessageW(s->Tab, WM_SETREDRAW, TRUE, 0);
    if (s->H.pfnCallback)
        s->H.pfnCallback(hwnd, PSCB_INITIALIZED, 0);

    RECT window = {}, tab_client = {}, rc;
    GetWindowRect(hwnd, &window);
    if (s->Flags & PSH_WIZARD_LITE) {
        /* no margins around the page and no header. */
        s->Flags &= ~PSH_WIZARD97;
        RECT line = {}, client = {};
        HWND bottom = get_item_rect(hwnd, IDC_BOTTOMLINE, &line);
        if (bottom)
            SetWindowPos(bottom, nullptr, 0, line.top, window.right - window.left, line.bottom - line.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        GetClientRect(hwnd, &client);
        SetWindowPos(s->Tab, nullptr, 0, 0, client.right - client.left, line.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    GetClientRect(s->Tab, &tab_client);
    s->BaseCx = tab_client.right;
    s->BaseCy = tab_client.bottom;
    SetRect(&rc, 0, 0, tab_client.right, tab_client.bottom);
    for (UINT i = 0; i < s->Count; i++) {
        const PAGE *p = s->Pages[i];
        if (!is_wizard97(s) || !(p->Psp.dwFlags & PSP_HIDEHEADER)) {
            if (rc.bottom < p->Cy)
                rc.bottom = p->Cy;
        }
        if (rc.right < p->Cx)
            rc.right = p->Cx;
    }
    RECT display;
    if (!is_wizard(s)) {
        RECT y = rc;
        for (int pass = 0; pass < 2; pass++) {
            y = rc;
            SendMessageW(s->Tab, TCM_ADJUSTRECT, TRUE, (LPARAM)&y);
            int w = y.right - y.left, h = y.bottom - y.top;
            if (w < rc.right)
                w = rc.right;
            if (h < rc.bottom)
                h = rc.bottom;
            SetWindowPos(s->Tab, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            SetRect(&y, 0, 0, w, h);
        }
        rc = y;
        display = y;
        SendMessageW(s->Tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&display);
    } else {
        display = rc;
    }
    int header = 0;
    if (is_wizard97(s)) {
        RECT tab_rect = {};
        GetWindowRect(s->Tab, &tab_rect);
        MapWindowPoints(nullptr, hwnd, (POINT *)&tab_rect, 2);
        create_header_font(s);
        header = compute_header_height(s);
        /* an exterior page covers the sheet down to the buttons, and is not shorter than the header and the others. */
        RECT margin = {0, 0, 0, 14};
        SIZE exterior;
        MapDialogRect(hwnd, &margin);
        ideal_page_size(s, &exterior, SIZE_EXTERIOR_PAGES);
        if (header < exterior.cy - margin.bottom - rc.bottom)
            header = exterior.cy - margin.bottom - rc.bottom;
        s->HeaderHeight = header;
        SetWindowPos(s->Tab, nullptr, tab_rect.left, tab_rect.top + header, tab_rect.right - tab_rect.left,
                     tab_rect.bottom - tab_rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    int dy = rc.bottom - tab_client.bottom + header;
    int dx = rc.right - tab_client.right;
    int width = window.right - window.left + dx;
    int height = window.bottom - window.top + dy;

    /* the sheet goes a little right and below the top left corner of its owner, or where windows are put by default. */
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    RECT place = window;
    HWND parent = s->H.hwndParent;
    if (IsWindow(parent)) {
        GetWindowRect(parent, &place);
        if (IsWindowVisible(parent)) {
            LONG ex = GetWindowLongW(parent, GWL_EXSTYLE);
            place.top += GetSystemMetrics(SM_CYSMICON);
            if (ex & WS_EX_LAYOUTRTL)
                place.left = place.right - GetSystemMetrics(SM_CXSMICON) - width;
            else
                place.left += GetSystemMetrics(SM_CXSMICON);
        }
    } else {
        HWND probe = CreateWindowExW(0, L"Static", nullptr, 0, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr, nullptr,
                                     Comctl32Instance, nullptr);
        if (probe) {
            GetWindowRect(probe, &place);
            DestroyWindow(probe);
        } else {
            flags |= SWP_NOMOVE;
        }
    }
    MONITORINFO mi = {sizeof(mi)};
    int left = place.left, top = place.top;
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        if (mi.rcMonitor.right < left + width)
            left = mi.rcMonitor.right - width;
        if (mi.rcMonitor.left > left)
            left = mi.rcMonitor.left;
        if (mi.rcMonitor.bottom < top + height)
            top = mi.rcMonitor.bottom - height;
        if (mi.rcMonitor.top > top)
            top = mi.rcMonitor.top;
    }
    SetWindowPos(hwnd, nullptr, left, top, width, height, flags);

    /* the page area, in the coordinates of the sheet. */
    MapWindowPoints(s->Tab, hwnd, (POINT *)&display, 2);
    s->PageRect = display;

    RECT r = {};
    HWND item;
    if (is_wizard97(s) && (item = get_item_rect(hwnd, IDC_TOPLINE, &r)))
        SetWindowPos(item, nullptr, r.left, header, dx + r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    const int *ids;
    if (is_wizard(s)) {
        if ((item = get_item_rect(hwnd, IDC_BOTTOMLINE, &r)))
            SetWindowPos(item, nullptr, r.left, r.top + dy, dx + r.right - r.left, r.bottom - r.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        EnableWindow(GetDlgItem(hwnd, IDC_BACK), TRUE);
        ids = g_wizard_buttons;
        s->DefId = IDC_NEXT;
    } else {
        ids = g_normal_buttons;
        s->DefId = IDOK;
    }
    move_buttons(hwnd, ids, IDHELP, dx, dy);

    /* the buttons the sheet does not use give their room to the others. */
    if (!(s->Flags & PSH_HASHELP))
        remove_button(hwnd, IDHELP, ids);
    if (!is_wizard(s)) {
        if (s->Flags & PSH_NOAPPLYNOW)
            remove_button(hwnd, IDC_APPLYNOW, ids);
    } else if (!(s->Flags & PSH_WIZARDHASFINISH)) {
        remove_button(hwnd, IDC_FINISH, ids);
        RECT next = {};
        GetWindowRect(GetDlgItem(hwnd, IDC_NEXT), &next);
        MapWindowPoints(nullptr, hwnd, (POINT *)&next, 2);
        SetWindowPos(GetDlgItem(hwnd, IDC_FINISH), nullptr, next.left, next.top, next.right - next.left,
                     next.bottom - next.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (is_wizard97(s)) {
        if ((s->Flags & PSH_WATERMARK) && s->H.hbmWatermark)
            s->Watermark = load_bitmap(s, PSH_USEHBMWATERMARK, s->WatermarkName, s->H.hbmWatermark, 0, 0);
        if ((s->Flags & PSH_HEADER) && s->H.hbmHeader)
            s->HeaderBitmap = load_bitmap(s, PSH_USEHBMHEADER, s->HeaderName, s->H.hbmHeader, 0, 0);
    }

    SendMessageW(hwnd, DM_REPOSITION, 0, 0);
    SetForegroundWindow(hwnd);
    s->Result = 0;
    SendMessageW(s->Tab, TCM_SETCURSEL, start < s->Count ? start : 0, 0);
    page_change(s, 1);

    if (premature) {
        for (UINT i = 0; i < s->Count; i++) {
            PAGE *p = s->Pages[i];
            if (!(p->Psp.dwFlags & PSP_PREMATURE) || p->Hwnd)
                continue;
            p->Hwnd = create_page(s, p);
            if (p->Hwnd) {
                store_page_window(s, (int)i);
            } else {
                remove_page_data(s, (int)i);
                i--;
            }
        }
    }
}

/* ------------------------------------------------------------------ the dialog procedure */

static INT_PTR reply(HWND hwnd, LRESULT value)
{
    SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, (LONG_PTR)value);
    return TRUE;
}

/* tells every page the tab control and the pages themselves, as a change of the system does. */
static INT_PTR broadcast(SHEET *s, UINT msg, WPARAM wparam, LPARAM lparam)
{
    UINT count = s->Count;
    for (UINT i = 0; i < count && i < s->Count; i++)
        if (s->Pages[i]->Hwnd)
            SendMessageW(s->Pages[i]->Hwnd, msg, wparam, lparam);
    SendMessageW(s->Tab, msg, wparam, lparam);
    return TRUE;
}

static INT_PTR to_current_page(SHEET *s, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, BOOL tab_too)
{
    if (tab_too && s->Tab)
        SendMessageW(s->Tab, msg, wparam, lparam);
    HWND page = page_window(s, s->CurIndex);
    if (page && IsWindow(page))
        return reply(hwnd, SendMessageW(page, msg, wparam, lparam));
    return FALSE;
}

static INT_PTR on_notify(SHEET *s, HWND hwnd, NMHDR *hdr)
{
    switch (hdr->code) {
    case TCN_GETOBJECT: {
        NMOBJECTNOTIFY_ *n = (NMOBJECTNOTIFY_ *)hdr;
        n->hResult = E_FAIL;
        n->pObject = nullptr;
        if (n->iItem >= 0 && (UINT)n->iItem < s->Count) {
            PAGE *p = s->Pages[n->iItem];
            if (!p->Hwnd) {
                p->Hwnd = create_page(s, p);
                if (!p->Hwnd) {
                    remove_page_data(s, n->iItem);
                    return TRUE;
                }
                store_page_window(s, n->iItem);
            }
            NMOBJECTNOTIFY_ query = {};
            query.hdr.hwndFrom = hwnd;
            query.hdr.idFrom = (UINT_PTR)GetDlgCtrlID(hwnd);
            query.hdr.code = PSN_GETOBJECT;
            query.iItem = -1;
            query.piid = n->piid;
            query.hResult = E_NOINTERFACE;
            SendMessageW(p->Hwnd, WM_NOTIFY, query.hdr.idFrom, (LPARAM)&query);
            n->hResult = query.hResult;
            n->pObject = query.hResult >= 0 ? query.pObject : nullptr;
        }
        return TRUE;
    }
    case TCN_SELCHANGING: {
        BOOL refused = page_changing(s);
        if (!refused)
            SetWindowPos(s->Current, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return reply(hwnd, refused);
    }
    case TCN_SELCHANGE:
        page_change(s, 1);
        return TRUE;
    }
    return FALSE;
}

static INT_PTR on_set_title(SHEET *s, DWORD style, void *title, BOOL ansi)
{
    void *text = title;
    if (!is_resource_id(title)) {
        text = ansi ? (void *)CcStrDupAtoW(title) : (void *)CcStrDupW(title);
        if (!text)
            return TRUE;
    }
    if (!text)
        return TRUE;
    free_slot(s->Caption);
    s->Flags = (s->Flags & ~PSH_PROPTITLE) | (style & PSH_PROPTITLE);
    s->Caption = text;
    set_title(s);
    return TRUE;
}

static INT_PTR on_set_header(SHEET *s, HWND hwnd, UINT index, void *text, BOOL ansi, BOOL title)
{
    void *copy = text;
    if (!is_resource_id(text)) {
        copy = ansi ? (void *)CcStrDupAtoW(text) : (void *)CcStrDupW(text);
        if (!copy)
            return TRUE;
    } else if (!text) {
        return TRUE;
    }
    set_header_text(s, index, copy, title);
    return TRUE;
}

static INT_PTR CALLBACK sheet_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    SHEET *s = get_sheet(hwnd);
    if (!s) {
        if (msg == WM_INITDIALOG)
            init_sheet(hwnd, (SHEET *)lparam);
        return 0;
    }
    switch (msg) {
    case WM_INITDIALOG:
        init_sheet(hwnd, (SHEET *)lparam);
        return 0;
    case WM_COMMAND:
        return on_command(s, LOWORD(wparam), (HWND)lparam, HIWORD(wparam));
    case WM_SYSCOMMAND: {
        UINT command = (UINT)(wparam & 0xFFF0);
        if (command == SC_HOTKEY)
            return handle_hotkey((HWND)lparam);
        if (command != SC_CLOSE)
            return 0;
        UINT id = IDCANCEL;
        if (!is_wizard(s))
            id = (s->State & STATE_CANCELTOCLOSE) ? IDOK : ID_CLOSEBOX;
        return on_command(s, id, nullptr, 0);
    }
    case WM_NOTIFY:
        return on_notify(s, hwnd, (NMHDR *)lparam);
    case WM_HELP:
        return TRUE;
    case WM_CONTEXTMENU:
        DefWindowProcW(hwnd, msg, wparam, lparam);
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(hwnd, &ps);
        if (ps.rcPaint.bottom - ps.rcPaint.top > 0 && ps.rcPaint.right - ps.rcPaint.left > 0) {
            if (is_wizard97(s) && s->CurIndex >= 0 && (UINT)s->CurIndex < s->Count)
                paint_header(s, s->Pages[s->CurIndex], dc);
            if (ps.fErase)
                SendMessageW(hwnd, WM_ERASEBKGND, (WPARAM)dc, 0);
        }
        EndPaint(hwnd, &ps);
        return TRUE;
    }
    case WM_DESTROY: {
        int count = (int)SendMessageW(s->Tab, TCM_GETITEMCOUNT, 0, 0);
        for (int i = 0; i < count; i++)
            if (page_window(s, i))
                EnableThemeDialogTexture(page_window(s, i), ETDT_DISABLE);
        if (s->Images)
            ImageList_Destroy((HIMAGELIST)SendMessageW(s->Tab, TCM_GETIMAGELIST, 0, 0));
        s->Images = nullptr;
        if (s->Watermark && !((s->Flags & PSH_USEHBMWATERMARK) && s->Watermark == s->H.hbmWatermark))
            DeleteObject(s->Watermark);
        if (s->HeaderBitmap && !((s->Flags & PSH_USEHBMHEADER) && s->HeaderBitmap == s->H.hbmHeader))
            DeleteObject(s->HeaderBitmap);
        s->Watermark = s->HeaderBitmap = nullptr;
        if (s->HeaderFont)
            DeleteObject(s->HeaderFont);
        s->HeaderFont = nullptr;
        if (s->LoadedIcon)
            DestroyIcon(s->LoadedIcon);
        s->LoadedIcon = nullptr;
        return TRUE;
    }
    case WM_NCDESTROY:
        sheet_ncdestroy(s, hwnd);
        return 0;
    case WM_SYSCOLORCHANGE:
    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
        return broadcast(s, msg, wparam, lparam);
    case WM_ACTIVATE:
    case WM_ACTIVATEAPP:
        return to_current_page(s, hwnd, msg, wparam, lparam, FALSE);
    case WM_ENABLE:
    case WM_QUERYENDSESSION:
    case WM_ENDSESSION:
    case WM_DEVICECHANGE:
        return to_current_page(s, hwnd, msg, wparam, lparam, TRUE);
    case WM_QUERYNEWPALETTE:
    case WM_PALETTECHANGED:
        if (msg == WM_PALETTECHANGED && (HWND)wparam == hwnd)
            return 0;
        to_current_page(s, hwnd, msg, wparam, lparam, TRUE);
        return TRUE;
    case PSM_SETCURSEL:
        return reply(hwnd, page_set_selection(s, (int)wparam, (const void *)lparam, 1));
    case PSM_REMOVEPAGE:
        remove_page(s, (UINT)wparam, (HPROPSHEETPAGE)lparam);
        return TRUE;
    case PSM_ADDPAGE:
        return reply(hwnd, insert_page(s, 0xFFFF, (HPROPSHEETPAGE)lparam));
    case PSM_INSERTPAGE:
        return reply(hwnd, insert_page(s, wparam, (HPROPSHEETPAGE)lparam));
    case PSM_CHANGED:
        page_changed(s, (HWND)wparam);
        return TRUE;
    case PSM_UNCHANGED:
        page_unchanged(s, (HWND)wparam);
        return TRUE;
    case PSM_RESTARTWINDOWS:
        s->Restart |= ID_PSRESTARTWINDOWS;
        return TRUE;
    case PSM_REBOOTSYSTEM:
        s->Restart |= ID_PSREBOOTSYSTEM;
        return TRUE;
    case PSM_CANCELTOCLOSE:
        if (!(s->State & STATE_CANCELTOCLOSE)) {
            s->State |= STATE_CANCELTOCLOSE;
            SetDlgItemTextW(hwnd, IDOK, L"Close");
            EnableWindow(GetDlgItem(hwnd, IDCANCEL), FALSE);
        }
        return TRUE;
    case PSM_QUERYSIBLINGS:
        return reply(hwnd, query_siblings(s, wparam, lparam));
    case PSM_APPLY:
        return reply(hwnd, button_pushed(s, IDC_APPLYNOW));
    case PSM_SETTITLEA:
        return on_set_title(s, (DWORD)wparam, (void *)lparam, TRUE);
    case PSM_SETTITLEW:
        return on_set_title(s, (DWORD)wparam, (void *)lparam, FALSE);
    case PSM_SETWIZBUTTONS:
        set_wizard_buttons(s, (DWORD)lparam);
        return TRUE;
    case PSM_PRESSBUTTON:
        if (wparam <= PSBTN_MAX)
            on_command(s, (UINT)g_press_ids[wparam], nullptr, 0);
        return TRUE;
    case PSM_SETCURSELID: {
        int index = find_page_index(s, s->CurIndex, (UINT_PTR)lparam, 1);
        return reply(hwnd, index == -1 ? 0 : page_set_selection(s, index, nullptr, 1));
    }
    case PSM_SETFINISHTEXTA:
        set_finish_text(s, (const void *)lparam, TRUE);
        return TRUE;
    case PSM_SETFINISHTEXTW:
        set_finish_text(s, (const void *)lparam, FALSE);
        return TRUE;
    case PSM_GETTABCONTROL:
        return reply(hwnd, (LRESULT)s->Tab);
    case PSM_ISDIALOGMESSAGE:
        return reply(hwnd, is_dialog_message(s, (MSG *)lparam));
    case PSM_GETCURRENTPAGEHWND:
        return reply(hwnd, (LRESULT)s->Current);
    case PSM_SETHEADERTITLEA:
        return on_set_header(s, hwnd, (UINT)wparam, (void *)lparam, TRUE, TRUE);
    case PSM_SETHEADERTITLEW:
        return on_set_header(s, hwnd, (UINT)wparam, (void *)lparam, FALSE, TRUE);
    case PSM_SETHEADERSUBTITLEA:
        return on_set_header(s, hwnd, (UINT)wparam, (void *)lparam, TRUE, FALSE);
    case PSM_SETHEADERSUBTITLEW:
        return on_set_header(s, hwnd, (UINT)wparam, (void *)lparam, FALSE, FALSE);
    case PSM_HWNDTOINDEX:
        return reply(hwnd, find_page_by_window(s, (HWND)wparam));
    case PSM_INDEXTOHWND:
        return reply(hwnd, (LRESULT)(wparam < s->Count ? s->Pages[wparam]->Hwnd : nullptr));
    case PSM_PAGETOINDEX:
        return reply(hwnd, find_page_by_handle(s, (const void *)lparam));
    case PSM_INDEXTOPAGE:
        return reply(hwnd, (LRESULT)(wparam < s->Count ? (HPROPSHEETPAGE)s->Pages[wparam] : nullptr));
    case PSM_IDTOINDEX:
        return reply(hwnd, find_page_index(s, s->CurIndex, (UINT_PTR)lparam, 0));
    case PSM_INDEXTOID: {
        UINT_PTR id = wparam < s->Count ? (UINT_PTR)s->Pages[wparam]->Psp.pszTemplate : 0;
        return reply(hwnd, id < 0x10000 ? (LRESULT)id : 0);
    }
    case PSM_GETRESULT:
        if (s->Current)
            return reply(hwnd, -1);
        if (s->Result > 0 && s->Restart)
            return reply(hwnd, s->Restart);
        return reply(hwnd, s->Result);
    case PSM_RECALCPAGESIZES: {
        BOOL changed = recalc_page_sizes(s);
        if (changed)
            recalc_page_sizes(s);
        return reply(hwnd, changed);
    }
    case WM_USER + 122:
        if (s->ApplyAllowed) {
            s->ApplyAllowed = FALSE;
            EnableWindow(GetDlgItem(hwnd, IDC_APPLYNOW), FALSE);
        }
        return TRUE;
    case WM_USER + 123:
        s->ApplyAllowed = TRUE;
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ the sheet */

static void free_sheet(SHEET *s)
{
    free_slot(s->StartPage);
    free_slot(s->IconName);
    free_slot(s->WatermarkName);
    free_slot(s->HeaderName);
    free_slot(s->Caption);
    CcFree(s);
}

static void sheet_ncdestroy(SHEET *s, HWND hwnd)
{
    s->Current = nullptr;
    for (int i = (int)s->Count - 1; i >= 0; i--) {
        ULONG_PTR cookie = activate_context(s, s->Pages[i]);
        DestroyPropertySheetPage((HPROPSHEETPAGE)s->Pages[i]);
        deactivate_context(cookie);
    }
    s->Count = 0;
    s->Tab = nullptr;
    if (s->ActCtx)
        ReleaseActCtx(s->ActCtx);
    s->ActCtx = nullptr;
    RemovePropW(hwnd, g_prop);
    s->Hwnd = nullptr;
    if (s->Modeless)
        free_sheet(s);
}

/* copies a string of the header as the sheet keeps it, a resource id staying what it is. */
static LPWSTR copy_header_string(const void *s, BOOL ansi)
{
    if (is_resource_id(s))
        return (LPWSTR)s;
    return ansi ? CcStrDupAtoW(s) : CcStrDupW(s);
}

/* destroys the pages of a header the sheet could not be made of; pages given as structures were not made yet. */
static void destroy_page_array(const PROPSHEETHEADERW_V2 *h)
{
    if (!(h->dwFlags & PSH_PROPSHEETPAGE) && h->phpage)
        for (int i = (int)h->nPages - 1; i >= 0; i--)
            DestroyPropertySheetPage(h->phpage[i]);
}

static SHEET *create_sheet(const PROPSHEETHEADERW_V2 *h, BOOL ansi)
{
    if ((h->dwSize != PSH_V1_SIZE && h->dwSize != PSH_V2_SIZE) || (h->dwFlags & 0xE0000000))
        return nullptr;
    SHEET *s = CcAlloc(sizeof(SHEET));
    if (!s)
        return nullptr;
    memcpy(&s->H, h, h->dwSize);
    s->Flags = h->dwFlags;
    s->Ansi = ansi;
    s->Modeless = (h->dwFlags & PSH_MODELESS) != 0;
    if (!(s->Flags & PSH_USECALLBACK))
        s->H.pfnCallback = nullptr;
    /* the aero wizard is shown as a wizard in the new style. */
    if (s->Flags & PSH_AEROWIZARD)
        s->Flags = (s->Flags & ~PSH_AEROWIZARD) | PSH_WIZARD97;
    /* the old numbering of wizard 97 is drawn like the new one. */
    if (s->Flags & PSH_WIZARD97_OLD)
        s->Flags = (s->Flags & ~PSH_WIZARD97_OLD) | PSH_WIZARD97;
    s->Caption = copy_header_string(h->pszCaption, ansi);
    if (s->Flags & PSH_USEICONID)
        s->IconName = copy_header_string(h->pszIcon, ansi);
    if (s->Flags & PSH_USEPSTARTPAGE)
        s->StartPage = copy_header_string(h->pStartPage, ansi);
    if ((s->Flags & WIZARD97_MASK) && h->dwSize >= PSH_V2_SIZE) {
        if ((s->Flags & (PSH_WATERMARK | PSH_USEHBMWATERMARK)) == PSH_WATERMARK)
            s->WatermarkName = copy_header_string(h->pszbmWatermark, ansi);
        if ((s->Flags & (PSH_HEADER | PSH_USEHBMHEADER)) == PSH_HEADER)
            s->HeaderName = copy_header_string(h->pszbmHeader, ansi);
    }
    if (h->dwSize < PSH_V2_SIZE) {
        s->H.hbmWatermark = nullptr;
        s->H.hplWatermark = nullptr;
        s->H.hbmHeader = nullptr;
    }
    GetCurrentActCtx(&s->ActCtx);
    if (s->Flags & PSH_PROPSHEETPAGE) {
        const PROPSHEETPAGEW *src = h->ppsp;
        for (UINT i = 0; i < h->nPages && src; i++) {
            HPROPSHEETPAGE page = create_page_handle(src, ansi);
            if (page)
                s->Pages[s->Count++] = (PAGE *)page;
            src = (const PROPSHEETPAGEW *)((const BYTE *)src + src->dwSize);
        }
    } else {
        for (UINT i = 0; h->phpage && i < h->nPages; i++)
            if (is_page(h->phpage[i]))
                s->Pages[s->Count++] = (PAGE *)h->phpage[i];
    }
    if (!(s->Flags & PSH_HASHELP)) {
        for (UINT i = 0; i < s->Count; i++)
            if (s->Pages[i]->Psp.dwFlags & PSP_HASHELP) {
                s->Flags |= PSH_HASHELP;
                break;
            }
    }
    return s;
}

static INT_PTR run_sheet(SHEET *s)
{
    INT_PTR failure = -1;
    if (!s->Count)
        return failure;
    HWND parent = s->H.hwndParent;
    HWND restore_focus = GetFocus();
    if (!(s->Flags & PSH_MODELESS) && parent) {
        while (parent && (GetWindowLongW(parent, GWL_STYLE) & WS_CHILD))
            parent = GetParent(parent);
        if (parent == GetDesktopWindow() || EnableWindow(parent, FALSE))
            parent = nullptr;
    }

    DLGTEMPLATE *tmpl = nullptr;
    BOOL ex = should_use_shell_font(s);
    if (ex)
        s->State |= STATE_SHELLFONT;
    tmpl = build_template(s, ex);
    HWND dlg = nullptr;
    if (tmpl) {
        DWORD *style = (DWORD *)tmpl;
        if (HIWORD(tmpl->style) == 0xFFFF)
            style = (DWORD *)((BYTE *)tmpl + 12);
        if (s->Flags & PSH_NOCONTEXTHELP)
            *style &= ~DS_CONTEXTHELP;
        if (is_wizard(s) && (s->Flags & PSH_WIZARDCONTEXTHELP))
            *style |= DS_CONTEXTHELP;
        if (s->H.pfnCallback)
            s->H.pfnCallback(nullptr, PSCB_PRECREATE, (LPARAM)tmpl);
        ULONG_PTR cookie = activate_context(s, nullptr);
        dlg = CreateDialogIndirectParamW(Comctl32Instance, tmpl, s->H.hwndParent, sheet_proc, (LPARAM)s);
        deactivate_context(cookie);
        CcFree(tmpl);
    }
    if (!dlg) {
        if (parent && !(s->Flags & PSH_MODELESS))
            EnableWindow(parent, TRUE);
        for (int i = (int)s->Count - 1; i >= 0; i--) {
            ULONG_PTR cookie = activate_context(s, s->Pages[i]);
            DestroyPropertySheetPage((HPROPSHEETPAGE)s->Pages[i]);
            deactivate_context(cookie);
        }
        s->Count = 0;
        return failure;
    }
    if (s->Flags & PSH_MODELESS)
        return (INT_PTR)dlg;

    MSG msg = {};
    while (s->Current && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!is_dialog_message(s, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (s->Current) {
        /* the thread was asked to quit with the sheet open: it is cancelled and the request is passed on. */
        button_pushed(s, IDCANCEL);
        PostQuitMessage((int)msg.wParam);
    }
    int result = s->Result;
    if (s->H.hwndParent && GetActiveWindow() == dlg)
        SetActiveWindow(s->H.hwndParent);
    if (parent)
        EnableWindow(parent, TRUE);
    if (IsWindow(restore_focus))
        SetFocus(restore_focus);
    int restart = s->Restart;
    DestroyWindow(dlg);
    if (result > 0 && restart)
        return restart;
    return result;
}

static INT_PTR property_sheet(const PROPSHEETHEADERW_V2 *h, BOOL ansi)
{
    if (!h)
        return -1;
    if (h->nPages >= MAX_PAGES) {
        destroy_page_array(h);
        return -1;
    }
    SHEET *s = create_sheet(h, ansi);
    if (!s) {
        destroy_page_array(h);
        return -1;
    }
    INT_PTR result = run_sheet(s);
    if (!s->Modeless || result == -1)
        free_sheet(s);
    return result;
}

WINCOMMCTRLAPI INT_PTR WINAPI PropertySheetA(LPCPROPSHEETHEADERA header)
{
    return property_sheet((const PROPSHEETHEADERW_V2 *)header, TRUE);
}

WINCOMMCTRLAPI INT_PTR WINAPI PropertySheetW(LPCPROPSHEETHEADERW header)
{
    return property_sheet(header, FALSE);
}
