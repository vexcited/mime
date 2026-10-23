/*
 * helpers the controls share: memory, strings, parent notifications and off screen painting.
 */
#include "comctl32_private.h"

void *CcAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void *CcReAlloc(void *p, SIZE_T size)
{
    if (!p)
        return CcAlloc(size);
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p, size ? size : 1);
}

void CcFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

LPWSTR CcStrDupW(LPCWSTR s)
{
    if (!s)
        return nullptr;
    SIZE_T n = wcslen(s) + 1;
    LPWSTR d = CcAlloc(n * sizeof(WCHAR));
    if (d)
        memcpy(d, s, n * sizeof(WCHAR));
    return d;
}

LPWSTR CcStrDupAtoW(LPCSTR s)
{
    if (!s)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    LPWSTR d = CcAlloc((SIZE_T)(n > 0 ? n : 1) * sizeof(WCHAR));
    if (d && n > 0)
        MultiByteToWideChar(CP_ACP, 0, s, -1, d, n);
    return d;
}

BOOL CcSetText(LPWSTR *slot, LPCWSTR text)
{
    LPWSTR copy = nullptr;
    if (text == LPSTR_TEXTCALLBACKW)
        copy = LPSTR_TEXTCALLBACKW;
    else if (text && !(copy = CcStrDupW(text)))
        return FALSE;
    if (*slot != LPSTR_TEXTCALLBACKW)
        CcFree(*slot);
    *slot = copy;
    return TRUE;
}

BOOL CcSetTextA(LPWSTR *slot, LPCSTR text)
{
    if (text == LPSTR_TEXTCALLBACKA)
        return CcSetText(slot, LPSTR_TEXTCALLBACKW);
    LPWSTR w = CcStrDupAtoW(text);
    if (text && !w)
        return FALSE;
    if (*slot != LPSTR_TEXTCALLBACKW)
        CcFree(*slot);
    *slot = w;
    return TRUE;
}

int CcCopyText(LPWSTR dst, int max, LPCWSTR src)
{
    if (!dst || max <= 0)
        return 0;
    int n = 0;
    if (src && src != LPSTR_TEXTCALLBACKW)
        for (; n < max - 1 && src[n]; n++)
            dst[n] = src[n];
    dst[n] = 0;
    return n;
}

int CcCopyTextA(LPSTR dst, int max, LPCWSTR src)
{
    if (!dst || max <= 0)
        return 0;
    int n = 0;
    if (src && src != LPSTR_TEXTCALLBACKW)
        n = WideCharToMultiByte(CP_ACP, 0, src, -1, dst, max, nullptr, nullptr);
    if (n <= 0) {
        /* too long: copy what fits. */
        n = src && src != LPSTR_TEXTCALLBACKW
                ? WideCharToMultiByte(CP_ACP, 0, src, (int)wcslen(src), dst, max - 1, nullptr, nullptr)
                : 0;
        if (n < 0)
            n = 0;
        dst[n] = 0;
        return n;
    }
    dst[n - 1] = 0;
    return n - 1;
}

void CcNotifyInit(CC_NOTIFY *n, HWND hwnd, HWND parent)
{
    n->Hwnd = hwnd;
    n->Parent = parent;
    n->Unicode = parent && SendMessageW(parent, WM_NOTIFYFORMAT, (WPARAM)hwnd, NF_QUERY) == NFR_UNICODE;
}

LRESULT CcNotifyFormat(CC_NOTIFY *n, HWND from, LPARAM command)
{
    if (command == NF_QUERY)
        return NFR_UNICODE;
    if (command == NF_REQUERY) {
        n->Unicode = n->Parent && SendMessageW(n->Parent, WM_NOTIFYFORMAT, (WPARAM)n->Hwnd, NF_QUERY) == NFR_UNICODE;
        return n->Unicode ? NFR_UNICODE : NFR_ANSI;
    }
    return 0;
}

/* the unicode notification codes and their ansi twins. */
static const struct {
    UINT W, A;
} g_ansi_codes[] = {
    {LVN_GETDISPINFOW, LVN_GETDISPINFOA},       {LVN_SETDISPINFOW, LVN_SETDISPINFOA},
    {LVN_BEGINLABELEDITW, LVN_BEGINLABELEDITA}, {LVN_ENDLABELEDITW, LVN_ENDLABELEDITA},
    {LVN_ODFINDITEMW, LVN_ODFINDITEMA},         {LVN_GETINFOTIPW, LVN_GETINFOTIPA},
    {LVN_INCREMENTALSEARCHW, LVN_INCREMENTALSEARCHA},
    {HDN_ITEMCHANGINGW, HDN_ITEMCHANGINGA},     {HDN_ITEMCHANGEDW, HDN_ITEMCHANGEDA},
    {HDN_ITEMCLICKW, HDN_ITEMCLICKA},           {HDN_ITEMDBLCLICKW, HDN_ITEMDBLCLICKA},
    {HDN_DIVIDERDBLCLICKW, HDN_DIVIDERDBLCLICKA}, {HDN_BEGINTRACKW, HDN_BEGINTRACKA},
    {HDN_ENDTRACKW, HDN_ENDTRACKA},             {HDN_TRACKW, HDN_TRACKA},
    {HDN_GETDISPINFOW, HDN_GETDISPINFOA},       {TVN_SELCHANGINGW, TVN_SELCHANGINGA},
    {TVN_SELCHANGEDW, TVN_SELCHANGEDA},         {TVN_GETDISPINFOW, TVN_GETDISPINFOA},
    {TVN_SETDISPINFOW, TVN_SETDISPINFOA},       {TVN_ITEMEXPANDINGW, TVN_ITEMEXPANDINGA},
    {TVN_ITEMEXPANDEDW, TVN_ITEMEXPANDEDA},     {TVN_BEGINDRAGW, TVN_BEGINDRAGA},
    {TVN_DELETEITEMW, TVN_DELETEITEMA},         {TTN_GETDISPINFOW, TTN_GETDISPINFOA},
};

LRESULT CcNotify(CC_NOTIFY *n, UINT code, NMHDR *hdr)
{
    if (!n->Parent || !IsWindow(n->Parent))
        return 0;
    if (!n->Unicode)
        for (SIZE_T i = 0; i < sizeof(g_ansi_codes) / sizeof(g_ansi_codes[0]); i++)
            if (g_ansi_codes[i].W == code) {
                code = g_ansi_codes[i].A;
                break;
            }
    hdr->hwndFrom = n->Hwnd;
    hdr->idFrom = (UINT_PTR)GetDlgCtrlID(n->Hwnd);
    hdr->code = code;
    return SendMessageW(n->Parent, WM_NOTIFY, hdr->idFrom, (LPARAM)hdr);
}

LRESULT CcNotifyHdr(CC_NOTIFY *n, UINT code)
{
    NMHDR hdr;
    return CcNotify(n, code, &hdr);
}

LRESULT CcNotifyMouse(CC_NOTIFY *n, UINT code, DWORD_PTR item, DWORD_PTR data, POINT pt)
{
    NMMOUSE nm = {};
    nm.dwItemSpec = item;
    nm.dwItemData = data;
    nm.pt = pt;
    return CcNotify(n, code, &nm.hdr);
}

HDC CcBeginBuffer(CC_DRAW_BUFFER *b, HDC target, const RECT *rect)
{
    b->Target = target;
    b->Rect = *rect;
    int w = rect->right - rect->left, h = rect->bottom - rect->top;
    b->Dc = CreateCompatibleDC(target);
    b->Bitmap = w > 0 && h > 0 ? CreateCompatibleBitmap(target, w, h) : nullptr;
    if (!b->Dc || !b->Bitmap) {
        if (b->Dc)
            DeleteDC(b->Dc);
        if (b->Bitmap)
            DeleteObject(b->Bitmap);
        b->Dc = nullptr;
        b->Bitmap = nullptr;
        return target;
    }
    b->Old = SelectObject(b->Dc, b->Bitmap);
    SetWindowOrgEx(b->Dc, rect->left, rect->top, nullptr);
    SelectObject(b->Dc, GetCurrentObject(target, OBJ_FONT));
    SetTextColor(b->Dc, GetTextColor(target));
    SetBkColor(b->Dc, GetBkColor(target));
    return b->Dc;
}

void CcEndBuffer(CC_DRAW_BUFFER *b)
{
    if (!b->Dc)
        return;
    SetWindowOrgEx(b->Dc, 0, 0, nullptr);
    BitBlt(b->Target, b->Rect.left, b->Rect.top, b->Rect.right - b->Rect.left, b->Rect.bottom - b->Rect.top, b->Dc, 0,
           0, SRCCOPY);
    SelectObject(b->Dc, b->Old);
    DeleteObject(b->Bitmap);
    DeleteDC(b->Dc);
    b->Dc = nullptr;
}

void CcFillSolid(HDC dc, const RECT *rect, COLORREF color)
{
    COLORREF old = SetBkColor(dc, color);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, rect, nullptr, 0, nullptr);
    SetBkColor(dc, old);
}

static HFONT g_icon_title_font;

HFONT CcIconTitleFont(void)
{
    if (!g_icon_title_font) {
        LOGFONTW lf;
        if (SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0))
            g_icon_title_font = CreateFontIndirectW(&lf);
        if (!g_icon_title_font)
            g_icon_title_font = GetStockObject(DEFAULT_GUI_FONT);
    }
    return g_icon_title_font;
}

BOOL CcFocusCuesHidden(HWND hwnd)
{
    return (SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) != 0;
}

int CcScale(HWND hwnd, int value)
{
    UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
    if (!dpi)
        dpi = 96;
    return MulDiv(value, (int)dpi, 96);
}
