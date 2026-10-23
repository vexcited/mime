/* the header control, SysHeader32: column titles the user resizes, clicks and reorders. */
#include "comctl32_private.h"

#define HDI_NOAUTOFORMAT 0x8000
#define HDI_ALL 0x03FF

typedef struct HDR_ITEM {
    int Cxy;
    int Fmt;
    LPWSTR Text;
    HBITMAP Bitmap;
    int Image;
    LPARAM Param;
    UINT FilterType;
} HDR_ITEM;

typedef struct HEADER {
    HWND Hwnd;
    CC_NOTIFY Notify;
    DWORD Style;
    HDR_ITEM *Items;
    /* the item index at each position, left to right. */
    int *Order;
    int Count, Capacity;
    HFONT Font;
    HTHEME Theme;
    HIMAGELIST Images, StateImages;
    int BitmapMargin, TextMargin;
    int Focused;
    int Hot, HotDivider;
    UINT FilterTimeout;
    BOOL Tracking, FullDrag;
    int TrackItem, TrackOffset, TrackStart, TrackLine;
    int Pressed;
    BOOL PressedInside, Dragging;
    POINT Down;
    int DropOrder;
} HEADER;

static HEADER *get_data(HWND hwnd)
{
    return (HEADER *)GetWindowLongPtrW(hwnd, 0);
}

static int order_of(HEADER *h, int index)
{
    for (int k = 0; k < h->Count; k++)
        if (h->Order[k] == index)
            return k;
    return -1;
}

static void item_edges(HEADER *h, int index, int *left, int *right)
{
    int x = 0;
    for (int k = 0; k < h->Count; k++) {
        int i = h->Order[k];
        if (i == index) {
            *left = x;
            *right = x + h->Items[i].Cxy;
            return;
        }
        x += h->Items[i].Cxy;
    }
    *left = *right = x;
}

static void item_rect(HEADER *h, int index, RECT *r)
{
    GetClientRect(h->Hwnd, r);
    item_edges(h, index, (int *)&r->left, (int *)&r->right);
}

static HFONT current_font(HEADER *h)
{
    return h->Font ? h->Font : GetStockObject(SYSTEM_FONT);
}

static int text_height(HEADER *h)
{
    HDC dc = GetDC(h->Hwnd);
    HGDIOBJ old = SelectObject(dc, current_font(h));
    int cy;
    SIZE size = {};
    RECT extent;
    if (h->Theme && SUCCEEDED(GetThemeTextExtent(h->Theme, dc, CC_HP_HEADERITEM, 1, L"M", 1, DT_NOPREFIX, nullptr,
                                                 &extent)))
        cy = extent.bottom - extent.top;
    else {
        GetTextExtentPoint32W(dc, L"...", 3, &size);
        cy = size.cy;
    }
    SelectObject(dc, old);
    ReleaseDC(h->Hwnd, dc);
    return cy;
}

static int layout_height(HEADER *h)
{
    if (h->Style & HDS_HIDDEN)
        return 0;
    int cy = text_height(h) + 2 * GetSystemMetrics(SM_CYEDGE);
    if (h->Theme) {
        SIZE size = {};
        HDC dc = GetDC(h->Hwnd);
        if (SUCCEEDED(GetThemePartSize(h->Theme, dc, CC_HP_HEADERITEM, 1, nullptr, TS_TRUE, &size)) && size.cy > cy)
            cy = size.cy;
        ReleaseDC(h->Hwnd, dc);
        POINT normal = {};
        if (SUCCEEDED(GetThemePosition(h->Theme, CC_HP_HEADERITEM, 0, TMT_NORMALSIZE, &normal)) && normal.y > cy)
            cy = normal.y;
    }
    if (h->Style & HDS_FILTERBAR)
        cy += 2 * GetSystemMetrics(SM_CYEDGE) + text_height(h) + 1;
    return cy;
}

static void open_theme(HEADER *h)
{
    if (h->Theme)
        CloseThemeData(h->Theme);
    h->Theme = OpenThemeData(h->Hwnd, L"Header");
}

/* ------------------------------------------------------------------ notifications */

/* an item as an ansi parent sees it, with the text converted. */
typedef struct ITEM_COPY {
    HDITEMA Ansi;
    char *Buffer;
} ITEM_COPY;

static void *notify_item(HEADER *h, HDITEMW *item, ITEM_COPY *copy)
{
    if (!item || h->Notify.Unicode)
        return item;
    memcpy(&copy->Ansi, item, sizeof(copy->Ansi));
    copy->Buffer = nullptr;
    if ((item->mask & HDI_TEXT) && item->pszText && !CcIsCallback(item->pszText)) {
        int n = WideCharToMultiByte(CP_ACP, 0, item->pszText, -1, nullptr, 0, nullptr, nullptr);
        copy->Buffer = CcAlloc((SIZE_T)(n > 0 ? n : 1));
        if (copy->Buffer)
            WideCharToMultiByte(CP_ACP, 0, item->pszText, -1, copy->Buffer, n, nullptr, nullptr);
        copy->Ansi.pszText = copy->Buffer;
    }
    return &copy->Ansi;
}

static LRESULT notify_header(HEADER *h, UINT code, int index, int button, HDITEMW *item)
{
    NMHEADERW nm = {};
    ITEM_COPY copy = {};
    nm.iItem = index;
    nm.iButton = button;
    nm.pitem = notify_item(h, item, &copy);
    LRESULT r = CcNotify(&h->Notify, code, &nm.hdr);
    CcFree(copy.Buffer);
    return r;
}

static void get_dispinfo(HEADER *h, int index, UINT mask, LPWSTR text, int max, int *image)
{
    HDR_ITEM *it = &h->Items[index];
    if (h->Notify.Unicode) {
        NMHDDISPINFOW nm = {};
        nm.iItem = index;
        nm.mask = mask;
        nm.pszText = text;
        nm.cchTextMax = max;
        nm.iImage = it->Image;
        nm.lParam = it->Param;
        if (text && max > 0)
            text[0] = 0;
        CcNotify(&h->Notify, HDN_GETDISPINFOW, &nm.hdr);
        if ((mask & HDI_TEXT) && text && nm.pszText != text)
            CcCopyText(text, max, nm.pszText);
        if (image)
            *image = nm.iImage;
        if (nm.mask & HDI_DI_SETITEM) {
            if (mask & HDI_TEXT)
                CcSetText(&it->Text, text);
            if (mask & HDI_IMAGE)
                it->Image = nm.iImage;
        }
    } else {
        NMHDDISPINFOA nm = {};
        char buffer[260] = "";
        nm.iItem = index;
        nm.mask = mask;
        nm.pszText = buffer;
        nm.cchTextMax = sizeof(buffer);
        nm.iImage = it->Image;
        nm.lParam = it->Param;
        CcNotify(&h->Notify, HDN_GETDISPINFOW, &nm.hdr);
        if ((mask & HDI_TEXT) && text && max > 0) {
            text[0] = 0;
            if (nm.pszText)
                MultiByteToWideChar(CP_ACP, 0, nm.pszText, -1, text, max);
            text[max - 1] = 0;
        }
        if (image)
            *image = nm.iImage;
        if (nm.mask & HDI_DI_SETITEM) {
            if (mask & HDI_TEXT)
                CcSetText(&it->Text, text);
            if (mask & HDI_IMAGE)
                it->Image = nm.iImage;
        }
    }
}

/* ------------------------------------------------------------------ items */

static void invalidate_from(HEADER *h, int index)
{
    RECT r;
    GetClientRect(h->Hwnd, &r);
    if (index >= 0 && index < h->Count) {
        int left, right;
        item_edges(h, index, &left, &right);
        r.left = left;
    }
    InvalidateRect(h->Hwnd, &r, TRUE);
}

static int insert_item(HEADER *h, int index, const HDITEMW *src, BOOL ansi)
{
    if (!src || index < 0 || !src->mask)
        return -1;
    if (h->Count == h->Capacity) {
        int capacity = h->Capacity ? h->Capacity * 2 : 8;
        HDR_ITEM *items = CcReAlloc(h->Items, (SIZE_T)capacity * sizeof(HDR_ITEM));
        if (!items)
            return -1;
        h->Items = items;
        int *order = CcReAlloc(h->Order, (SIZE_T)capacity * sizeof(int));
        if (!order)
            return -1;
        h->Order = order;
        h->Capacity = capacity;
    }
    if (index > h->Count)
        index = h->Count;
    HDR_ITEM it = {};
    it.FilterType = HDFT_HASNOVALUE;
    UINT mask = src->mask;
    if (mask & HDI_WIDTH)
        it.Cxy = src->cxy < 0 ? 0 : src->cxy;
    if (mask & HDI_FORMAT)
        it.Fmt = src->fmt;
    if (mask & HDI_LPARAM)
        it.Param = src->lParam;
    if (mask & HDI_BITMAP)
        it.Bitmap = src->hbm;
    if (mask & HDI_IMAGE)
        it.Image = src->iImage;
    if (mask & HDI_TEXT) {
        BOOL ok = ansi ? CcSetTextA(&it.Text, (LPCSTR)src->pszText) : CcSetText(&it.Text, src->pszText);
        if (!ok)
            return -1;
    }
    if (!(mask & HDI_NOAUTOFORMAT)) {
        if (it.Text)
            it.Fmt |= HDF_STRING;
        else
            it.Fmt &= ~HDF_STRING;
        if (it.Bitmap)
            it.Fmt |= HDF_BITMAP;
        else
            it.Fmt &= ~HDF_BITMAP;
        if (mask & HDI_IMAGE)
            it.Fmt |= HDF_IMAGE;
    }
    memmove(&h->Items[index + 1], &h->Items[index], (SIZE_T)(h->Count - index) * sizeof(HDR_ITEM));
    h->Items[index] = it;
    for (int k = 0; k < h->Count; k++)
        if (h->Order[k] >= index)
            h->Order[k]++;
    int position = index;
    if ((mask & HDI_ORDER) && src->iOrder != index && src->iOrder <= h->Count && src->iOrder >= 0)
        position = src->iOrder;
    memmove(&h->Order[position + 1], &h->Order[position], (SIZE_T)(h->Count - position) * sizeof(int));
    h->Order[position] = index;
    h->Count++;
    invalidate_from(h, index);
    NotifyWinEvent(EVENT_OBJECT_CREATE, h->Hwnd, OBJID_CLIENT, index + 1);
    return index;
}

static BOOL delete_item(HEADER *h, int index)
{
    if (index < 0 || index >= h->Count)
        return FALSE;
    invalidate_from(h, index);
    if (h->Items[index].Text != LPSTR_TEXTCALLBACKW)
        CcFree(h->Items[index].Text);
    memmove(&h->Items[index], &h->Items[index + 1], (SIZE_T)(h->Count - index - 1) * sizeof(HDR_ITEM));
    int position = order_of(h, index);
    memmove(&h->Order[position], &h->Order[position + 1], (SIZE_T)(h->Count - position - 1) * sizeof(int));
    h->Count--;
    for (int k = 0; k < h->Count; k++)
        if (h->Order[k] > index)
            h->Order[k]--;
    h->Focused = 0;
    if (h->Hot >= h->Count)
        h->Hot = -1;
    NotifyWinEvent(EVENT_OBJECT_DESTROY, h->Hwnd, OBJID_CLIENT, index + 1);
    return TRUE;
}

static BOOL get_item(HEADER *h, int index, HDITEMW *dst, BOOL ansi)
{
    if (!dst || index < 0 || index >= h->Count)
        return FALSE;
    if (dst->mask & ~HDI_ALL)
        dst->mask &= 0x1F;
    HDR_ITEM *it = &h->Items[index];
    UINT mask = dst->mask;
    if (mask & HDI_WIDTH)
        dst->cxy = it->Cxy;
    if (mask & HDI_FORMAT)
        dst->fmt = it->Fmt;
    if (mask & HDI_LPARAM)
        dst->lParam = it->Param;
    if (mask & HDI_BITMAP)
        dst->hbm = it->Bitmap;
    if (mask & HDI_ORDER)
        dst->iOrder = order_of(h, index);
    if (mask & HDI_STATE)
        dst->state = order_of(h, index) == h->Focused && GetFocus() == h->Hwnd ? HDIS_FOCUSED : 0;
    if (mask & HDI_FILTER)
        dst->type = it->FilterType;
    UINT callback = 0;
    if ((mask & HDI_TEXT) && CcIsCallback(it->Text))
        callback |= HDI_TEXT;
    if ((mask & HDI_IMAGE) && it->Image == I_IMAGECALLBACK)
        callback |= HDI_IMAGE;
    WCHAR buffer[260];
    int image = it->Image;
    if (callback)
        get_dispinfo(h, index, callback, buffer, 260, &image);
    if (mask & HDI_IMAGE)
        dst->iImage = image;
    if (mask & HDI_TEXT) {
        LPCWSTR text = (callback & HDI_TEXT) ? buffer : it->Text;
        if (ansi)
            CcCopyTextA((LPSTR)dst->pszText, dst->cchTextMax, text);
        else
            CcCopyText(dst->pszText, dst->cchTextMax, text);
    }
    return TRUE;
}

static void set_width(HEADER *h, int index, int cxy)
{
    h->Items[index].Cxy = cxy < 0 ? 0 : cxy;
}

static BOOL set_item(HEADER *h, int index, HDITEMW *src, BOOL ansi)
{
    if (!src || index < 0 || index >= h->Count)
        return FALSE;
    if (!src->mask)
        return TRUE;
    /* an ansi caller's item goes to an ansi parent unconverted. */
    if (ansi == !h->Notify.Unicode) {
        NMHEADERW nm = {};
        nm.iItem = index;
        nm.pitem = src;
        if (CcNotify(&h->Notify, HDN_ITEMCHANGINGW, &nm.hdr))
            return FALSE;
    } else {
        HDITEMW w = *src;
        LPWSTR text = nullptr;
        if (ansi && (src->mask & HDI_TEXT) && src->pszText && (LPSTR)src->pszText != LPSTR_TEXTCALLBACKA)
            w.pszText = text = CcStrDupAtoW((LPCSTR)src->pszText);
        LRESULT r = notify_header(h, HDN_ITEMCHANGINGW, index, 0, &w);
        CcFree(text);
        if (r)
            return FALSE;
    }
    HDR_ITEM *it = &h->Items[index];
    UINT mask = src->mask;
    int old_width = it->Cxy;
    if (mask & HDI_WIDTH)
        set_width(h, index, src->cxy);
    if (mask & HDI_FORMAT)
        it->Fmt = src->fmt;
    if (mask & HDI_LPARAM)
        it->Param = src->lParam;
    if (mask & HDI_BITMAP)
        it->Bitmap = src->hbm;
    if (mask & HDI_IMAGE) {
        it->Image = src->iImage;
        if (!(mask & HDI_NOAUTOFORMAT))
            it->Fmt |= HDF_IMAGE;
    }
    if (mask & HDI_TEXT) {
        if (ansi)
            CcSetTextA(&it->Text, (LPCSTR)src->pszText);
        else
            CcSetText(&it->Text, src->pszText);
    }
    if (mask & HDI_FILTER)
        it->FilterType = src->type;
    if ((mask & HDI_ORDER) && src->iOrder >= 0 && src->iOrder < h->Count) {
        int from = order_of(h, index), to = src->iOrder;
        if (from != to) {
            if (from < to)
                memmove(&h->Order[from], &h->Order[from + 1], (SIZE_T)(to - from) * sizeof(int));
            else
                memmove(&h->Order[to + 1], &h->Order[to], (SIZE_T)(from - to) * sizeof(int));
            h->Order[to] = index;
        }
        InvalidateRect(h->Hwnd, nullptr, TRUE);
    }
    if ((mask & HDI_STATE) && (src->state & HDIS_FOCUSED))
        h->Focused = order_of(h, index);
    if ((mask & HDI_WIDTH) && old_width != it->Cxy)
        invalidate_from(h, index);
    else {
        RECT r;
        item_rect(h, index, &r);
        InvalidateRect(h->Hwnd, &r, TRUE);
    }
    if (ansi == !h->Notify.Unicode) {
        NMHEADERW nm = {};
        nm.iItem = index;
        nm.pitem = src;
        CcNotify(&h->Notify, HDN_ITEMCHANGEDW, &nm.hdr);
    } else {
        HDITEMW w = *src;
        LPWSTR text = nullptr;
        if (ansi && (src->mask & HDI_TEXT) && src->pszText && (LPSTR)src->pszText != LPSTR_TEXTCALLBACKA)
            w.pszText = text = CcStrDupAtoW((LPCSTR)src->pszText);
        notify_header(h, HDN_ITEMCHANGEDW, index, 0, &w);
        CcFree(text);
    }
    if (mask & HDI_FILTER) {
        NMHEADERW nm = {};
        nm.iItem = index;
        CcNotify(&h->Notify, HDN_FILTERCHANGE, &nm.hdr);
    }
    return TRUE;
}

/* like a user drag, with the change notifications. */
static void user_set_width(HEADER *h, int index, int cxy)
{
    HDITEMW item = {};
    item.mask = HDI_WIDTH;
    item.cxy = cxy;
    set_item(h, index, &item, FALSE);
}

/* ------------------------------------------------------------------ hit testing */

static int hit_test(HEADER *h, POINT pt, UINT *flags)
{
    RECT client;
    GetClientRect(h->Hwnd, &client);
    *flags = 0;
    if (pt.x < client.left)
        *flags |= HHT_TOLEFT;
    else if (pt.x >= client.right)
        *flags |= HHT_TORIGHT;
    if (pt.y < client.top)
        *flags |= HHT_ABOVE;
    else if (pt.y >= client.bottom)
        *flags |= HHT_BELOW;
    if (*flags)
        return -1;
    int x = 0;
    for (int k = 0; k < h->Count; k++) {
        int i = h->Order[k];
        int width = h->Items[i].Cxy, left = x, right = x + width;
        int zone = width / 4 < 8 ? width / 4 : 8;
        BOOL sizable = !(h->Style & HDS_NOSIZING);
        if (pt.x >= left && pt.x < right) {
            if (sizable && !(h->Items[i].Fmt & HDF_FIXEDWIDTH) && pt.x >= right - zone) {
                *flags = HHT_ONDIVIDER;
                return i;
            }
            if (k > 0 && sizable) {
                int prev = h->Order[k - 1];
                int pzone = h->Items[prev].Cxy / 4 < 8 ? h->Items[prev].Cxy / 4 : 8;
                if (pt.x < left + (zone > pzone ? zone : pzone) && !(h->Items[prev].Fmt & HDF_FIXEDWIDTH)) {
                    if (!h->Items[prev].Cxy && pt.x > left) {
                        *flags = HHT_ONDIVOPEN;
                        return prev;
                    }
                    if (pt.x < left + zone) {
                        *flags = HHT_ONDIVIDER;
                        return prev;
                    }
                }
            }
            *flags = HHT_ONHEADER;
            return i;
        }
        x = right;
    }
    /* the last divider still grabs a little to its right. */
    if (h->Count && !(h->Style & HDS_NOSIZING)) {
        int last = h->Order[h->Count - 1];
        if (pt.x < x + 8 && !(h->Items[last].Fmt & HDF_FIXEDWIDTH)) {
            *flags = HHT_ONDIVIDER;
            return last;
        }
    }
    *flags = HHT_NOWHERE;
    return -1;
}

/* ------------------------------------------------------------------ painting */

static int item_state(HEADER *h, int index)
{
    BOOL sorted = (h->Items[index].Fmt & (HDF_SORTUP | HDF_SORTDOWN)) != 0;
    int state = 1;
    if (h->Pressed == index && h->PressedInside)
        state = 3;
    else if (h->Hot == index)
        state = 2;
    return sorted ? state + 3 : state;
}

static void draw_item(HEADER *h, HDC dc, int index, const RECT *rect)
{
    HDR_ITEM *it = &h->Items[index];
    BOOL pressed = h->Pressed == index && h->PressedInside;
    RECT r = *rect;
    if (h->Theme) {
        DrawThemeBackground(h->Theme, dc, CC_HP_HEADERITEM, item_state(h, index), &r, nullptr);
    } else {
        CcFillSolid(dc, &r, GetSysColor(COLOR_BTNFACE));
        if (h->Style & HDS_BUTTONS) {
            if (pressed)
                DrawEdge(dc, &r, EDGE_SUNKEN, BF_RECT | BF_SOFT | BF_FLAT);
            else
                DrawEdge(dc, &r, EDGE_RAISED, BF_RECT | BF_SOFT);
        } else {
            DrawEdge(dc, &r, EDGE_ETCHED, BF_RIGHT | ((GetWindowLongW(h->Hwnd, GWL_STYLE) & WS_BORDER) ? 0 : BF_BOTTOM));
        }
    }
    if (it->Fmt & HDF_OWNERDRAW) {
        DRAWITEMSTRUCT dis = {};
        dis.CtlType = ODT_HEADER;
        dis.CtlID = (UINT)GetDlgCtrlID(h->Hwnd);
        dis.itemID = (UINT)index;
        dis.itemAction = ODA_DRAWENTIRE;
        dis.itemState = pressed ? ODS_SELECTED : 0;
        dis.hwndItem = h->Hwnd;
        dis.hDC = dc;
        dis.rcItem = r;
        dis.itemData = (ULONG_PTR)it->Param;
        if (SendMessageW(h->Notify.Parent, WM_DRAWITEM, dis.CtlID, (LPARAM)&dis))
            return;
    }
    if (pressed && !h->Theme)
        OffsetRect(&r, GetSystemMetrics(SM_CXBORDER), GetSystemMetrics(SM_CYBORDER));
    WCHAR buffer[260];
    LPCWSTR text = it->Text;
    int image = it->Image;
    UINT callback = (CcIsCallback(it->Text) ? HDI_TEXT : 0) | (image == I_IMAGECALLBACK ? HDI_IMAGE : 0);
    if (callback) {
        get_dispinfo(h, index, callback, buffer, 260, &image);
        if (callback & HDI_TEXT)
            text = buffer;
    }
    BOOL has_text = (it->Fmt & HDF_STRING) && text && !CcIsCallback(text);
    int len = has_text ? (int)wcslen(text) : 0;
    int text_cx = 0;
    if (len) {
        SIZE size;
        GetTextExtentPoint32W(dc, text, len, &size);
        text_cx = size.cx + 2 * h->TextMargin;
    }
    int image_cx = 0, image_cy = 0;
    HIMAGELIST list = (it->Fmt & HDF_IMAGE) && h->Images && image >= 0 ? h->Images : nullptr;
    if (list)
        ImageList_GetIconSize(list, &image_cx, &image_cy);
    BITMAP bm = {};
    if ((it->Fmt & HDF_BITMAP) && it->Bitmap && GetObjectW(it->Bitmap, sizeof(bm), &bm) && !list) {
        image_cx = bm.bmWidth;
        image_cy = bm.bmHeight;
    }
    int block_image = image_cx ? image_cx + 2 * h->BitmapMargin : 0;
    int sort_cx = 0;
    if ((it->Fmt & (HDF_SORTUP | HDF_SORTDOWN)) && !h->Theme)
        sort_cx = text_height(h) + 2 * GetSystemMetrics(SM_CXEDGE);
    int avail = r.right - r.left - sort_cx;
    int total = text_cx + block_image;
    if (total > avail)
        total = avail;
    int x = r.left;
    switch (it->Fmt & HDF_JUSTIFYMASK) {
    case HDF_RIGHT:
        x = r.left + avail - total;
        break;
    case HDF_CENTER:
        x = r.left + (avail - total) / 2;
        break;
    }
    if (x < r.left)
        x = r.left;
    BOOL image_right = (it->Fmt & HDF_BITMAP_ON_RIGHT) != 0;
    int image_x = image_right ? x + total - block_image + h->BitmapMargin : x + h->BitmapMargin;
    RECT text_rect = r;
    text_rect.left = image_right ? x : x + block_image;
    text_rect.right = image_right ? x + total - block_image : x + total;
    if (image_cx && image_x + image_cx <= r.right) {
        int y = r.top + (r.bottom - r.top - image_cy) / 2;
        if (list)
            ImageList_DrawEx(list, image, dc, image_x, y, 0, 0, CLR_DEFAULT, CLR_DEFAULT, ILD_NORMAL);
        else {
            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, it->Bitmap);
            BitBlt(dc, image_x, y, image_cx, image_cy, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
        }
    }
    if (len) {
        RECT tr = text_rect;
        tr.left += h->TextMargin;
        tr.right -= h->TextMargin;
        UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | DT_LEFT;
        if (h->Theme)
            DrawThemeText(h->Theme, dc, CC_HP_HEADERITEM, item_state(h, index), text, len, flags, 0, &tr);
        else {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(h->Hot == index ? COLOR_HOTLIGHT : COLOR_BTNTEXT));
            DrawTextW(dc, text, len, &tr, flags);
        }
    }
    if (it->Fmt & (HDF_SORTUP | HDF_SORTDOWN)) {
        if (h->Theme) {
            SIZE size = {};
            int state = (it->Fmt & HDF_SORTUP) ? 1 : 2;
            GetThemePartSize(h->Theme, dc, CC_HP_HEADERSORTARROW, state, nullptr, TS_TRUE, &size);
            RECT ar = {r.left + (r.right - r.left - size.cx) / 2, r.top, 0, 0};
            ar.right = ar.left + size.cx;
            ar.bottom = ar.top + size.cy;
            DrawThemeBackground(h->Theme, dc, CC_HP_HEADERSORTARROW, state, &ar, nullptr);
        } else {
            /* the sort arrow, in the space kept on the right. */
            int cx = r.right - sort_cx / 2 - 4, cy = (r.top + r.bottom) / 2;
            HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_GRAYTEXT));
            HGDIOBJ old = SelectObject(dc, pen);
            for (int k = 0; k < 4; k++) {
                int y = (it->Fmt & HDF_SORTUP) ? cy - 2 + k : cy + 1 - k;
                MoveToEx(dc, cx - k, y, nullptr);
                LineTo(dc, cx + k + 1, y);
            }
            SelectObject(dc, old);
            DeleteObject(pen);
        }
    }
    if (!h->Theme && GetFocus() == h->Hwnd && order_of(h, index) == h->Focused && !CcFocusCuesHidden(h->Hwnd)) {
        RECT fr = *rect;
        InflateRect(&fr, -2, -2);
        DrawFocusRect(dc, &fr);
    }
}

static LRESULT custom_draw(HEADER *h, HDC dc, DWORD stage, const RECT *rect, int index, UINT state)
{
    NMCUSTOMDRAW cd = {};
    cd.dwDrawStage = stage;
    cd.hdc = dc;
    cd.rc = *rect;
    cd.dwItemSpec = (DWORD_PTR)(index < 0 ? 0 : index);
    cd.uItemState = state;
    cd.lItemlParam = index >= 0 ? h->Items[index].Param : 0;
    LRESULT r = CcNotify(&h->Notify, NM_CUSTOMDRAW, &cd.hdr);
    return (r & 0xFE01) ? CDRF_DODEFAULT : r;
}

static void paint(HEADER *h, HDC dc)
{
    RECT client;
    GetClientRect(h->Hwnd, &client);
    HGDIOBJ old_font = SelectObject(dc, current_font(h));
    LRESULT pre = custom_draw(h, dc, CDDS_PREPAINT, &client, -1, 0);
    int x = 0;
    if (!(pre & CDRF_SKIPDEFAULT)) {
        for (int k = 0; k < h->Count; k++) {
            int i = h->Order[k];
            RECT r = client;
            r.left = x;
            r.right = x + h->Items[i].Cxy;
            x = r.right;
            if (r.right <= r.left)
                continue;
            UINT state = (h->Pressed == i && h->PressedInside ? CDIS_SELECTED : 0) |
                         (CcFocusCuesHidden(h->Hwnd) ? 0 : CDIS_SHOWKEYBOARDCUES);
            LRESULT item = CDRF_DODEFAULT;
            if (pre & CDRF_NOTIFYITEMDRAW)
                item = custom_draw(h, dc, CDDS_ITEMPREPAINT, &r, i, state);
            if (!(item & CDRF_SKIPDEFAULT))
                draw_item(h, dc, i, &r);
            if (item & CDRF_NOTIFYPOSTPAINT)
                custom_draw(h, dc, CDDS_ITEMPOSTPAINT, &r, i, state);
        }
    } else {
        for (int k = 0; k < h->Count; k++)
            x += h->Items[h->Order[k]].Cxy;
    }
    RECT rest = client;
    rest.left = x;
    if (rest.left < rest.right) {
        if (h->Theme) {
            rest.right = 32000;
            DrawThemeBackground(h->Theme, dc, CC_HP_HEADERITEM, 1, &rest, &client);
        } else {
            CcFillSolid(dc, &rest, GetSysColor(COLOR_BTNFACE));
            if (!(h->Style & HDS_FLAT) && !(h->Style & HDS_BUTTONS))
                DrawEdge(dc, &rest, EDGE_ETCHED, BF_BOTTOM);
            else if (!(h->Style & HDS_FLAT))
                DrawEdge(dc, &rest, EDGE_RAISED, BF_RECT | BF_SOFT);
        }
    }
    if (h->HotDivider >= 0) {
        int dx = 0;
        for (int k = 0; k < h->HotDivider && k < h->Count; k++)
            dx += h->Items[h->Order[k]].Cxy;
        RECT r = {dx - 1, client.top, dx + 1, client.bottom};
        CcFillSolid(dc, &r, GetSysColor(COLOR_HOTLIGHT));
    }
    if (pre & CDRF_NOTIFYPOSTPAINT)
        custom_draw(h, dc, CDDS_POSTPAINT, &client, -1, 0);
    SelectObject(dc, old_font);
}

/* the inverted line that follows a dragged divider when full drag is off. */
static void track_line(HEADER *h, int x)
{
    HWND target = GetParent(h->Hwnd);
    if (!target)
        target = h->Hwnd;
    RECT r;
    GetClientRect(target, &r);
    POINT pt = {x, 0};
    MapWindowPoints(h->Hwnd, target, &pt, 1);
    HDC dc = GetDCEx(target, nullptr, DCX_CACHE | DCX_CLIPSIBLINGS);
    PatBlt(dc, pt.x, r.top, 1, r.bottom - r.top, DSTINVERT);
    ReleaseDC(target, dc);
}

/* ------------------------------------------------------------------ mouse */

static void end_tracking(HEADER *h, BOOL cancel)
{
    if (!h->Tracking)
        return;
    h->Tracking = FALSE;
    if (!h->FullDrag && h->TrackLine != INT_MIN)
        track_line(h, h->TrackLine);
    h->TrackLine = INT_MIN;
    int index = h->TrackItem;
    if (index < 0 || index >= h->Count)
        return;
    HDITEMW item = {};
    item.mask = HDI_WIDTH;
    item.cxy = cancel ? h->TrackStart : h->FullDrag ? h->Items[index].Cxy : h->TrackOffset;
    LRESULT r = notify_header(h, HDN_ENDTRACKW, index, 0, &item);
    if (cancel ? h->FullDrag : (!r && !h->FullDrag))
        user_set_width(h, index, item.cxy);
}

static void begin_tracking(HEADER *h, int index, POINT pt)
{
    HDITEMW item = {};
    item.mask = HDI_WIDTH;
    item.cxy = h->Items[index].Cxy;
    if (notify_header(h, HDN_BEGINTRACKW, index, 0, &item))
        return;
    BOOL full = FALSE;
    SystemParametersInfoW(SPI_GETDRAGFULLWINDOWS, 0, &full, 0);
    h->FullDrag = (h->Style & HDS_FULLDRAG) && full;
    h->Tracking = TRUE;
    h->TrackItem = index;
    h->TrackStart = h->Items[index].Cxy;
    int left, right;
    item_edges(h, index, &left, &right);
    /* the width in progress, and the grab point's offset from the divider. */
    h->TrackOffset = h->TrackStart;
    h->Down.x = pt.x - right;
    h->TrackLine = INT_MIN;
    SetCapture(h->Hwnd);
    if (!h->FullDrag) {
        h->TrackLine = right;
        track_line(h, right);
    }
}

static void track_move(HEADER *h, POINT pt)
{
    int index = h->TrackItem, left, right;
    item_edges(h, index, &left, &right);
    int edge = pt.x - h->Down.x;
    if (edge < left)
        edge = left;
    int width = edge - left;
    if (h->FullDrag) {
        if (width != h->Items[index].Cxy) {
            user_set_width(h, index, width);
            UpdateWindow(h->Hwnd);
        }
        return;
    }
    HDITEMW item = {};
    item.mask = HDI_WIDTH;
    item.cxy = width;
    if (notify_header(h, HDN_TRACKW, index, 0, &item)) {
        if (h->TrackLine != INT_MIN)
            track_line(h, h->TrackLine);
        h->TrackLine = INT_MIN;
        h->Tracking = FALSE;
        ReleaseCapture();
        return;
    }
    h->TrackOffset = width;
    if (h->TrackLine != INT_MIN)
        track_line(h, h->TrackLine);
    h->TrackLine = left + width;
    track_line(h, h->TrackLine);
}

static void invalidate_item(HEADER *h, int index)
{
    if (index < 0 || index >= h->Count)
        return;
    RECT r;
    item_rect(h, index, &r);
    InvalidateRect(h->Hwnd, &r, TRUE);
}

static void set_hot(HEADER *h, int index)
{
    if (h->Hot == index)
        return;
    invalidate_item(h, h->Hot);
    h->Hot = index;
    invalidate_item(h, index);
    if (index >= 0) {
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, h->Hwnd, 0};
        TrackMouseEvent(&tme);
    }
}

static int drop_order(HEADER *h, int x)
{
    if (x <= 0)
        return 0;
    int left = 0;
    for (int k = 0; k < h->Count; k++) {
        int w = h->Items[h->Order[k]].Cxy;
        if (x < left + w)
            return x >= left + w / 2 ? k + 1 : k;
        left += w;
    }
    return h->Count;
}

static void mouse_move(HEADER *h, POINT pt)
{
    if (h->Tracking) {
        track_move(h, pt);
        return;
    }
    if (h->Pressed >= 0) {
        if (!h->Dragging && (h->Style & HDS_DRAGDROP) &&
            (CcAbs(pt.x - h->Down.x) > GetSystemMetrics(SM_CXDRAG) || CcAbs(pt.y - h->Down.y) > GetSystemMetrics(SM_CYDRAG))) {
            NMHEADERW nm = {};
            nm.iItem = h->Pressed;
            nm.iButton = 0;
            if (!CcNotify(&h->Notify, HDN_BEGINDRAG, &nm.hdr)) {
                h->Dragging = TRUE;
                h->PressedInside = FALSE;
                invalidate_item(h, h->Pressed);
            }
        }
        if (h->Dragging) {
            int order = drop_order(h, pt.x);
            if (order != h->HotDivider) {
                h->HotDivider = order;
                InvalidateRect(h->Hwnd, nullptr, TRUE);
            }
            return;
        }
        RECT r;
        item_rect(h, h->Pressed, &r);
        BOOL inside = PtInRect(&r, pt);
        if (inside != h->PressedInside) {
            h->PressedInside = inside;
            invalidate_item(h, h->Pressed);
        }
        return;
    }
    if ((h->Style & HDS_BUTTONS) && (h->Theme || (h->Style & HDS_HOTTRACK))) {
        UINT flags;
        int index = hit_test(h, pt, &flags);
        set_hot(h, (flags & HHT_ONHEADER) ? index : -1);
    }
}

static void button_down(HEADER *h, POINT pt, BOOL dblclk)
{
    UINT flags;
    int index = hit_test(h, pt, &flags);
    if (flags & (HHT_ONDIVIDER | HHT_ONDIVOPEN)) {
        if (dblclk) {
            NMHEADERW nm = {};
            nm.iItem = index;
            CcNotify(&h->Notify, HDN_DIVIDERDBLCLICKW, &nm.hdr);
            return;
        }
        begin_tracking(h, index, pt);
        return;
    }
    if ((flags & HHT_ONHEADER) && (h->Style & HDS_BUTTONS)) {
        if (dblclk) {
            NMHEADERW nm = {};
            nm.iItem = index;
            CcNotify(&h->Notify, HDN_ITEMDBLCLICKW, &nm.hdr);
        }
        h->Pressed = index;
        h->PressedInside = TRUE;
        h->Dragging = FALSE;
        h->Down = pt;
        SetCapture(h->Hwnd);
        invalidate_item(h, index);
    }
}

static void button_up(HEADER *h, POINT pt)
{
    if (h->Tracking) {
        end_tracking(h, FALSE);
        if (GetCapture() == h->Hwnd)
            ReleaseCapture();
        return;
    }
    if (h->Pressed < 0)
        return;
    int index = h->Pressed;
    BOOL inside = h->PressedInside, dragging = h->Dragging;
    h->Pressed = -1;
    h->PressedInside = FALSE;
    h->Dragging = FALSE;
    if (GetCapture() == h->Hwnd)
        ReleaseCapture();
    invalidate_item(h, index);
    if (dragging) {
        int order = h->HotDivider;
        h->HotDivider = -1;
        InvalidateRect(h->Hwnd, nullptr, TRUE);
        int from = order_of(h, index);
        if (order > from)
            order--;
        HDITEMW item = {};
        item.mask = HDI_ORDER;
        item.iOrder = order;
        if (!notify_header(h, HDN_ENDDRAG, index, 0, &item) && item.iOrder != -1)
            set_item(h, index, &item, FALSE);
        return;
    }
    if (inside) {
        NMHEADERW nm = {};
        nm.iItem = index;
        nm.iButton = 0;
        CcNotify(&h->Notify, HDN_ITEMCLICKW, &nm.hdr);
    }
}

static void cancel_mouse(HEADER *h)
{
    if (h->Tracking)
        end_tracking(h, TRUE);
    if (h->Pressed >= 0) {
        if (h->Dragging) {
            HDITEMW item = {};
            item.mask = HDI_ORDER;
            item.iOrder = -1;
            notify_header(h, HDN_ENDDRAG, h->Pressed, 0, &item);
            h->HotDivider = -1;
            InvalidateRect(h->Hwnd, nullptr, TRUE);
        }
        invalidate_item(h, h->Pressed);
        h->Pressed = -1;
        h->PressedInside = FALSE;
        h->Dragging = FALSE;
    }
}

/* ------------------------------------------------------------------ list view autosize */

int HeaderItemWidth(HWND header, int index)
{
    WCHAR name[16];
    if (!header || !GetClassNameW(header, name, 16) || _wcsicmp(name, WC_HEADERW))
        return 0;
    HEADER *h = get_data(header);
    if (!h || index < 0 || index >= h->Count)
        return 0;
    HDR_ITEM *it = &h->Items[index];
    WCHAR buffer[260];
    LPCWSTR text = it->Text;
    if (CcIsCallback(text)) {
        get_dispinfo(h, index, HDI_TEXT, buffer, 260, nullptr);
        text = buffer;
    }
    int width = 2 * h->TextMargin;
    HDC dc = GetDC(header);
    HGDIOBJ old = SelectObject(dc, current_font(h));
    if (text && *text) {
        SIZE size;
        GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
        width += size.cx;
    }
    if ((it->Fmt & (HDF_SORTUP | HDF_SORTDOWN)) && !h->Theme)
        width += text_height(h) + 2 * GetSystemMetrics(SM_CXEDGE);
    if (h->Theme) {
        RECT content = {0, 0, width, 10}, extent;
        if (SUCCEEDED(GetThemeBackgroundExtent(h->Theme, dc, CC_HP_HEADERITEM, 1, &content, &extent)) &&
            extent.right - extent.left > width)
            width = extent.right - extent.left;
    }
    SelectObject(dc, old);
    ReleaseDC(header, dc);
    int cx = 0, cy = 0;
    if ((it->Fmt & HDF_IMAGE) && h->Images && ImageList_GetIconSize(h->Images, &cx, &cy))
        width += cx + 2 * h->BitmapMargin;
    return width;
}

/* ------------------------------------------------------------------ window procedure */

static void destroy_items(HEADER *h)
{
    for (int i = 0; i < h->Count; i++)
        if (h->Items[i].Text != LPSTR_TEXTCALLBACKW)
            CcFree(h->Items[i].Text);
    CcFree(h->Items);
    CcFree(h->Order);
    h->Items = nullptr;
    h->Order = nullptr;
    h->Count = h->Capacity = 0;
}

LRESULT CALLBACK HeaderProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    HEADER *h = get_data(hwnd);
    if (!h && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
    case WM_NCCREATE: {
        h = CcAlloc(sizeof(*h));
        if (!h)
            return FALSE;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)h);
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        h->Hwnd = hwnd;
        h->Style = (DWORD)cs->style;
        h->Hot = -1;
        h->HotDivider = -1;
        h->Pressed = -1;
        h->TrackItem = -1;
        h->TrackLine = INT_MIN;
        CcNotifyInit(&h->Notify, hwnd, cs->hwndParent);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    case WM_CREATE: {
        int edge = GetSystemMetrics(SM_CXEDGE);
        h->TextMargin = 3 * edge;
        h->BitmapMargin = 3 * edge;
        h->FilterTimeout = 2 * GetDoubleClickTime();
        open_theme(h);
        return 0;
    }
    case WM_DESTROY:
        if (h->Theme)
            CloseThemeData(h->Theme);
        h->Theme = nullptr;
        return 0;
    case WM_NCDESTROY:
        destroy_items(h);
        SetWindowLongPtrW(hwnd, 0, 0);
        CcFree(h);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = wparam ? (HDC)wparam : BeginPaint(hwnd, &ps);
        if (dc) {
            RECT client;
            GetClientRect(hwnd, &client);
            CC_DRAW_BUFFER b;
            paint(h, wparam ? dc : CcBeginBuffer(&b, dc, &client));
            if (!wparam)
                CcEndBuffer(&b);
        }
        if (!wparam)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETFONT:
        if ((HFONT)wparam != h->Font) {
            h->Font = (HFONT)wparam;
            if (lparam)
                InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_GETFONT:
        return (LRESULT)h->Font;
    case WM_STYLECHANGED:
        if (wparam == (WPARAM)GWL_STYLE) {
            h->Style = ((STYLESTRUCT *)lparam)->styleNew;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_THEMECHANGED:
    case WM_DPICHANGED:
        open_theme(h);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTTAB;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        CcNotifyHdr(&h->Notify, msg == WM_SETFOCUS ? NM_SETFOCUS : NM_KILLFOCUS);
        if (h->Focused < h->Count)
            invalidate_item(h, h->Order[h->Focused]);
        return 0;
    case WM_KEYDOWN:
        if (GetFocus() == hwnd && h->Count) {
            NMHEADERW nm = {};
            nm.iItem = h->Order[h->Focused < h->Count ? h->Focused : 0];
            nm.iButton = (int)wparam;
            CcNotify(&h->Notify, HDN_ITEMKEYDOWN, &nm.hdr);
            int focus = h->Focused;
            if (wparam == VK_LEFT && focus > 0)
                focus--;
            else if (wparam == VK_RIGHT && focus < h->Count - 1)
                focus++;
            if (focus != h->Focused) {
                invalidate_item(h, h->Order[h->Focused]);
                h->Focused = focus;
                invalidate_item(h, h->Order[focus]);
            }
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        button_down(h, pt, msg == WM_LBUTTONDBLCLK);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        mouse_move(h, pt);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        button_up(h, pt);
        return 0;
    }
    case WM_MOUSELEAVE:
        set_hot(h, -1);
        return 0;
    case WM_RBUTTONDOWN:
        cancel_mouse(h);
        if (GetCapture() == hwnd)
            ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        if ((HWND)lparam != hwnd)
            cancel_mouse(h);
        return 0;
    case WM_RBUTTONUP:
        if (CcNotifyHdr(&h->Notify, NM_RCLICK))
            return 0;
        break;
    case WM_SETCURSOR:
        if ((HWND)wparam == hwnd && LOWORD(lparam) == HTCLIENT) {
            POINT pt;
            UINT flags;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            hit_test(h, pt, &flags);
            SetCursor(LoadCursorW(nullptr, (flags & (HHT_ONDIVIDER | HHT_ONDIVOPEN)) ? IDC_SIZEWE : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_NOTIFYFORMAT:
        return CcNotifyFormat(&h->Notify, (HWND)wparam, lparam);
    case WM_GETOBJECT:
        if ((LONG)lparam == -12)
            return 0x10010;
        break;
    case CCM_SETNOTIFYWINDOW: {
        HWND old = h->Notify.Parent;
        h->Notify.Parent = (HWND)wparam;
        return (LRESULT)old;
    }
    case CCM_SETUNICODEFORMAT: {
        BOOL old = h->Notify.Unicode;
        h->Notify.Unicode = wparam != 0;
        return old;
    }
    case CCM_GETUNICODEFORMAT:
        return h->Notify.Unicode;
    case CCM_SETVERSION:
    case CCM_GETVERSION:
        return 6;
    case HDM_GETITEMCOUNT:
        return h->Count;
    case HDM_INSERTITEMA:
    case HDM_INSERTITEMW:
        return insert_item(h, (int)wparam, (const HDITEMW *)lparam, msg == HDM_INSERTITEMA);
    case HDM_DELETEITEM:
        return delete_item(h, (int)wparam);
    case HDM_GETITEMA:
    case HDM_GETITEMW:
        return get_item(h, (int)wparam, (HDITEMW *)lparam, msg == HDM_GETITEMA);
    case HDM_SETITEMA:
    case HDM_SETITEMW:
        return set_item(h, (int)wparam, (HDITEMW *)lparam, msg == HDM_SETITEMA);
    case HDM_LAYOUT: {
        HDLAYOUT *layout = (HDLAYOUT *)lparam;
        if (!layout || !layout->prc || !layout->pwpos)
            return FALSE;
        int cy = layout_height(h);
        WINDOWPOS *wp = layout->pwpos;
        wp->hwnd = hwnd;
        wp->hwndInsertAfter = nullptr;
        wp->x = layout->prc->left;
        wp->y = layout->prc->top;
        wp->cx = layout->prc->right - layout->prc->left;
        wp->cy = cy;
        wp->flags = SWP_NOZORDER | SWP_NOACTIVATE;
        layout->prc->top += cy;
        return TRUE;
    }
    case HDM_HITTEST: {
        HDHITTESTINFO *info = (HDHITTESTINFO *)lparam;
        if (!info)
            return -1;
        info->iItem = hit_test(h, info->pt, &info->flags);
        return info->iItem;
    }
    case HDM_GETITEMRECT:
        if ((int)wparam < 0 || (int)wparam >= h->Count || !lparam)
            return FALSE;
        item_rect(h, (int)wparam, (RECT *)lparam);
        return TRUE;
    case HDM_SETIMAGELIST: {
        HIMAGELIST *slot = wparam == HDSIL_STATE ? &h->StateImages : &h->Images;
        HIMAGELIST old = *slot;
        *slot = (HIMAGELIST)lparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return (LRESULT)old;
    }
    case HDM_GETIMAGELIST:
        return (LRESULT)(wparam == HDSIL_STATE ? h->StateImages : h->Images);
    case HDM_ORDERTOINDEX:
        if ((int)wparam < 0 || (int)wparam >= h->Count)
            return (int)wparam;
        return h->Order[wparam];
    case HDM_GETORDERARRAY:
        if ((int)wparam != h->Count || !lparam)
            return FALSE;
        memcpy((int *)lparam, h->Order, (SIZE_T)h->Count * sizeof(int));
        return TRUE;
    case HDM_SETORDERARRAY:
        if ((int)wparam != h->Count || !lparam)
            return FALSE;
        memcpy(h->Order, (const int *)lparam, (SIZE_T)h->Count * sizeof(int));
        InvalidateRect(hwnd, nullptr, TRUE);
        NotifyWinEvent(EVENT_OBJECT_REORDER, hwnd, OBJID_CLIENT, 0);
        return TRUE;
    case HDM_SETHOTDIVIDER: {
        int index;
        if (wparam) {
            POINT pt = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
            RECT client;
            GetClientRect(hwnd, &client);
            InflateRect(&client, 0, 2 * GetSystemMetrics(SM_CYHSCROLL));
            index = pt.y >= client.top && pt.y < client.bottom ? drop_order(h, pt.x) : -1;
        } else {
            index = (int)lparam;
        }
        if (index != h->HotDivider) {
            h->HotDivider = index;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return index;
    }
    case HDM_SETBITMAPMARGIN: {
        h->BitmapMargin = (int)wparam;
        InvalidateRect(hwnd, nullptr, TRUE);
        return TRUE;
    }
    case HDM_GETBITMAPMARGIN:
        return h->BitmapMargin;
    case HDM_SETFILTERCHANGETIMEOUT: {
        UINT old = h->FilterTimeout;
        if (lparam)
            h->FilterTimeout = (UINT)lparam;
        return old;
    }
    case HDM_EDITFILTER:
        return FALSE;
    case HDM_CLEARFILTER:
        if ((int)wparam == -1)
            return TRUE;
        return (int)wparam >= 0 && (int)wparam < h->Count;
    case HDM_GETITEMDROPDOWNRECT:
        if (!lparam)
            return FALSE;
        SetRectEmpty((RECT *)lparam);
        return (int)wparam >= 0 && (int)wparam < h->Count && (h->Items[wparam].Fmt & HDF_SPLITBUTTON);
    case HDM_GETOVERFLOWRECT:
        if (!lparam)
            return FALSE;
        SetRectEmpty((RECT *)lparam);
        return TRUE;
    case HDM_GETFOCUSEDITEM:
        return h->Focused < h->Count ? h->Order[h->Focused] : h->Focused;
    case HDM_SETFOCUSEDITEM: {
        int index = (int)lparam;
        if (index == h->Count)
            return TRUE;
        HDITEMW item = {};
        item.mask = HDI_STATE;
        item.state = HDIS_FOCUSED;
        return set_item(h, index, &item, FALSE);
    }
    case HDM_CREATEDRAGIMAGE: {
        int index = (int)wparam;
        if (index < 0 || index >= h->Count)
            return 0;
        RECT r;
        item_rect(h, index, &r);
        int w = r.right - r.left, ht = r.bottom - r.top;
        if (w <= 0 || ht <= 0)
            return 0;
        HDC screen = GetDC(hwnd), mem = CreateCompatibleDC(screen);
        HBITMAP bm = CreateCompatibleBitmap(screen, w, ht);
        HGDIOBJ old = SelectObject(mem, bm);
        HGDIOBJ old_font = SelectObject(mem, current_font(h));
        SetWindowOrgEx(mem, r.left, r.top, nullptr);
        draw_item(h, mem, index, &r);
        SetWindowOrgEx(mem, 0, 0, nullptr);
        SelectObject(mem, old_font);
        SelectObject(mem, old);
        HIMAGELIST list = ImageList_Create(w, ht, ILC_COLORDDB | ILC_MASK, 1, 0);
        if (list)
            ImageList_AddMasked(list, bm, GetSysColor(COLOR_3DSHADOW));
        DeleteObject(bm);
        DeleteDC(mem);
        ReleaseDC(hwnd, screen);
        return (LRESULT)list;
    }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
