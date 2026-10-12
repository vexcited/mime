/*
 * the Static control: text, icons, bitmaps, frames and rectangles.
 */
#include "user32_private.h"

typedef struct STATIC_DATA {
    HFONT Font;
    HANDLE Image;
    UINT ImageType;
    BOOL OwnsImage;
} STATIC_DATA;

static STATIC_DATA *get_data(HWND hwnd)
{
    return (STATIC_DATA *)GetWindowLongPtrW(hwnd, 0);
}

static UINT static_type(HWND hwnd)
{
    return (UINT)GetWindowLongW(hwnd, GWL_STYLE) & SS_TYPEMASK;
}

static void size_to_image(HWND hwnd, STATIC_DATA *s)
{
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    if (style & (SS_CENTERIMAGE | SS_REALSIZECONTROL))
        return;
    int cx = 0, cy = 0;
    if (s->ImageType == IMAGE_ICON || s->ImageType == IMAGE_CURSOR) {
        ICONINFO info;
        BITMAP bm;
        if (s->Image && GetIconInfo(s->Image, &info)) {
            if (GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(bm), &bm)) {
                cx = bm.bmWidth;
                cy = info.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
            }
            DeleteObject(info.hbmColor);
            DeleteObject(info.hbmMask);
        }
    } else if (s->ImageType == IMAGE_BITMAP) {
        BITMAP bm;
        if (s->Image && GetObjectW(s->Image, sizeof(bm), &bm)) {
            cx = bm.bmWidth;
            cy = bm.bmHeight;
        }
    }
    if (cx && cy)
        SetWindowPos(hwnd, NULL, 0, 0, cx, cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* CStatic::LoadImageW of comctl32 v6. */
static void load_icon(HWND hwnd, STATIC_DATA *s, const CREATESTRUCTW *cs, HINSTANCE inst, LPCWSTR name)
{
    s->ImageType = IMAGE_ICON;
    s->OwnsImage = TRUE;
    if (cs->style & SS_REALSIZEIMAGE) {
        s->Image = LoadImageW(inst, name, IMAGE_ICON, 0, 0, 0);
        return;
    }
    UINT dpi = GetDpiForWindow(hwnd);
    int cx = GetSystemMetricsForDpi(SM_CXICON, dpi), cy = GetSystemMetricsForDpi(SM_CYICON, dpi);
    if (cs->style & SS_REALSIZECONTROL) {
        cx = cs->cx;
        cy = cs->cy;
    }
    s->Image = UserLoadIconWithScaleDown(inst, name, cx, cy);
    /* our user32 resolves its own module to the system cursors, real user32 has none. */
    if (!s->Image && inst) {
        s->Image = inst != User32Instance ? LoadCursorW(inst, name) : NULL;
        if (s->Image) {
            s->OwnsImage = FALSE;
            return;
        }
        s->Image = UserLoadIconWithScaleDown(NULL, name, cx, cy);
    }
}

static HANDLE set_image(HWND hwnd, STATIC_DATA *s, HANDLE image, UINT type)
{
    HANDLE old = s->Image;
    if (s->OwnsImage && old && old != image)
        DestroyIcon(old);
    s->Image = image;
    s->ImageType = type;
    s->OwnsImage = FALSE;
    size_to_image(hwnd, s);
    InvalidateRect(hwnd, NULL, TRUE);
    return old;
}

static HBRUSH ctl_brush(HWND hwnd, HDC dc)
{
    HWND parent = GetParent(hwnd);
    HBRUSH b = parent ? (HBRUSH)SendMessageW(parent, WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)hwnd) : NULL;
    if (!b)
        b = (HBRUSH)DefWindowProcW(parent ? parent : hwnd, WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)hwnd);
    return b;
}

static void paint_text(HWND hwnd, STATIC_DATA *s, HDC dc, const RECT *client)
{
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    UINT type = style & SS_TYPEMASK;
    UINT flags;
    switch (type) {
    case SS_CENTER:
        flags = DT_CENTER | DT_EXPANDTABS | DT_WORDBREAK;
        break;
    case SS_RIGHT:
        flags = DT_RIGHT | DT_EXPANDTABS | DT_WORDBREAK;
        break;
    case SS_SIMPLE:
        flags = DT_LEFT | DT_SINGLELINE;
        break;
    case SS_LEFTNOWORDWRAP:
        flags = DT_LEFT | DT_EXPANDTABS;
        break;
    default:
        flags = DT_LEFT | DT_EXPANDTABS | DT_WORDBREAK;
        break;
    }
    if (style & SS_NOPREFIX)
        flags |= DT_NOPREFIX;
    if (style & SS_EDITCONTROL)
        flags |= DT_EDITCONTROL;
    if (style & SS_CENTERIMAGE)
        flags |= DT_SINGLELINE | DT_VCENTER;
    switch (style & SS_ELLIPSISMASK) {
    case SS_ENDELLIPSIS:
        flags |= DT_END_ELLIPSIS | DT_SINGLELINE;
        break;
    case SS_PATHELLIPSIS:
        flags |= DT_PATH_ELLIPSIS | DT_SINGLELINE;
        break;
    case SS_WORDELLIPSIS:
        flags |= DT_WORD_ELLIPSIS | DT_SINGLELINE;
        break;
    }
    HBRUSH brush = ctl_brush(hwnd, dc);
    if (type != SS_SIMPLE)
        FillRect(dc, client, brush);
    if (!IsWindowEnabled(hwnd))
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    int n = GetWindowTextLengthW(hwnd);
    LPWSTR text = UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!text)
        return;
    GetWindowTextW(hwnd, text, n + 1);
    HGDIOBJ old = SelectObject(dc, s->Font ? s->Font : GetStockObject(SYSTEM_FONT));
    RECT r = *client;
    if (type == SS_SIMPLE)
        ExtTextOutW(dc, 0, 0, ETO_OPAQUE | ETO_CLIPPED, &r, text, (UINT)n, NULL);
    else
        DrawTextW(dc, text, n, &r, flags);
    SelectObject(dc, old);
    UserFree(text);
}

static void paint(HWND hwnd, STATIC_DATA *s, HDC dc)
{
    RECT client;
    GetClientRect(hwnd, &client);
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    switch (style & SS_TYPEMASK) {
    case SS_LEFT:
    case SS_CENTER:
    case SS_RIGHT:
    case SS_SIMPLE:
    case SS_LEFTNOWORDWRAP:
        paint_text(hwnd, s, dc, &client);
        break;
    case SS_ICON: {
        FillRect(dc, &client, ctl_brush(hwnd, dc));
        if (s->Image) {
            int cx = client.right, cy = client.bottom;
            int x = 0, y = 0;
            if (style & SS_CENTERIMAGE) {
                cx = GetSystemMetricsForDpi(SM_CXICON, UserWindowDpi(hwnd));
                cy = cx;
                x = (client.right - cx) / 2;
                y = (client.bottom - cy) / 2;
            }
            DrawIconEx(dc, x, y, s->Image, cx, cy, 0, NULL, DI_NORMAL);
        }
        break;
    }
    case SS_BITMAP: {
        FillRect(dc, &client, ctl_brush(hwnd, dc));
        BITMAP bm;
        if (s->Image && GetObjectW(s->Image, sizeof(bm), &bm)) {
            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, s->Image);
            int x = (style & SS_CENTERIMAGE) ? (client.right - bm.bmWidth) / 2 : 0;
            int y = (style & SS_CENTERIMAGE) ? (client.bottom - bm.bmHeight) / 2 : 0;
            BitBlt(dc, x, y, bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
        }
        break;
    }
    case SS_BLACKRECT:
        FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOWFRAME));
        break;
    case SS_GRAYRECT:
        FillRect(dc, &client, GetSysColorBrush(COLOR_BTNSHADOW));
        break;
    case SS_WHITERECT:
        FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
        break;
    case SS_BLACKFRAME:
        FrameRect(dc, &client, GetSysColorBrush(COLOR_WINDOWFRAME));
        break;
    case SS_GRAYFRAME:
        FrameRect(dc, &client, GetSysColorBrush(COLOR_BTNSHADOW));
        break;
    case SS_WHITEFRAME:
        FrameRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
        break;
    case SS_ETCHEDHORZ:
        DrawEdge(dc, &client, EDGE_ETCHED, BF_TOP);
        break;
    case SS_ETCHEDVERT:
        DrawEdge(dc, &client, EDGE_ETCHED, BF_LEFT);
        break;
    case SS_ETCHEDFRAME:
        DrawEdge(dc, &client, EDGE_ETCHED, BF_RECT);
        break;
    case SS_OWNERDRAW: {
        DRAWITEMSTRUCT dis = {ODT_STATIC, (UINT)GetDlgCtrlID(hwnd), 0, ODA_DRAWENTIRE, 0, hwnd, dc, client, 0};
        if (!IsWindowEnabled(hwnd))
            dis.itemState |= ODS_DISABLED;
        SendMessageW(GetParent(hwnd), WM_DRAWITEM, (WPARAM)dis.CtlID, (LPARAM)&dis);
        break;
    }
    }
    if (style & SS_SUNKEN)
        DrawEdge(dc, &client, BDR_SUNKENOUTER, BF_RECT);
}

LRESULT CALLBACK StaticProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    STATIC_DATA *s = get_data(hwnd);
    if (!s && msg != WM_NCCREATE)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
    case WM_NCCREATE: {
        s = UserAlloc(sizeof(STATIC_DATA));
        if (!s)
            return FALSE;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)s);
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        UINT type = (UINT)cs->style & SS_TYPEMASK;
        if (type == SS_ICON || type == SS_BITMAP) {
            /* the window text names the image resource; it is not shown. */
            LPCWSTR name = cs->lpszName;
            if (name && !IS_INTRESOURCE(name) && name[0] == 0xFFFF)
                name = MAKEINTRESOURCEW(name[1]);
            else if (name && !IS_INTRESOURCE(name) && name[0] == L'#')
                {
                UINT id = 0;
                for (LPCWSTR q = name + 1; *q >= L'0' && *q <= L'9'; q++)
                    id = id * 10 + (UINT)(*q - L'0');
                name = MAKEINTRESOURCEW(id);
            }
            if (name && (IS_INTRESOURCE(name) || name[0])) {
                HINSTANCE inst = cs->hInstance;
                if (type == SS_ICON) {
                    load_icon(hwnd, s, cs, inst, name);
                } else {
                    s->Image = LoadImageW(inst, name, IMAGE_BITMAP, 0, 0, 0);
                    s->ImageType = IMAGE_BITMAP;
                }
            }
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    case WM_CREATE:
        if (s->Image)
            size_to_image(hwnd, s);
        return 0;
    case WM_NCDESTROY:
        if (s->OwnsImage && s->Image)
            DestroyIcon(s->Image);
        SetWindowLongPtrW(hwnd, 0, 0);
        UserFree(s);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC dc = msg == WM_PAINT ? BeginPaint(hwnd, &ps) : (HDC)wparam;
        if (dc)
            paint(hwnd, s, dc);
        if (msg == WM_PAINT)
            EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST:
        if (!(GetWindowLongW(hwnd, GWL_STYLE) & SS_NOTIFY))
            return HTTRANSPARENT;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (GetWindowLongW(hwnd, GWL_STYLE) & SS_NOTIFY)
            SendMessageW(GetParent(hwnd), WM_COMMAND,
                         MAKEWPARAM(GetDlgCtrlID(hwnd), msg == WM_LBUTTONDOWN ? STN_CLICKED : STN_DBLCLK),
                         (LPARAM)hwnd);
        return 0;
    case WM_SETFONT:
        s->Font = (HFONT)wparam;
        if (lparam)
            InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_GETFONT:
        return (LRESULT)s->Font;
    case WM_SETTEXT: {
        UINT type = static_type(hwnd);
        if (type == SS_ICON || type == SS_BITMAP)
            return TRUE;
        LRESULT r = DefWindowProcW(hwnd, msg, wparam, lparam);
        InvalidateRect(hwnd, NULL, TRUE);
        return r;
    }
    case WM_ENABLE:
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_STATIC;
    case STM_SETICON:
        return (LRESULT)set_image(hwnd, s, (HANDLE)wparam, IMAGE_ICON);
    case STM_GETICON:
        return (LRESULT)s->Image;
    case STM_SETIMAGE:
        return (LRESULT)set_image(hwnd, s, (HANDLE)lparam, (UINT)wparam);
    case STM_GETIMAGE:
        return (LRESULT)s->Image;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
