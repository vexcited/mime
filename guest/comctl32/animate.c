/*
 * the animation control, SysAnimate32: plays a silent AVI clip of run length coded frames, such as the file copy
 * animations of setup programs, from a resource of type AVI or from a file. The control keeps the clip's frames
 * where they are in memory and draws them through StretchDIBits, a delta frame over the frames before it, as the
 * control of windows does.
 */
#include "comctl32_private.h"

#define TIMER_PLAY 0x2A
/* how much longer a timer waits while nothing of the control is visible. */
#define HIDDEN_DELAY 4000
#define CCM_DPISCALE_ (CCM_FIRST + 0xC)
#define OBJID_QUERYCLASSNAMEIDX_ (-12)
/* the class name index that accessibility asks for, 0x1000E on windows. */
#define ANIMATE_CLASS_INDEX 65550
/* the header of the stream format, with its color table: up to 256 entries. */
#define FORMAT_BYTES (sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD))
#define AVIIF_KEYFRAME_ 0x10
#define AVISF_VIDEO_PALCHANGES_ 0x10000

typedef struct CLIP_FRAME {
    const BYTE *Data;
    DWORD Size;
    BOOL Key;
} CLIP_FRAME;

typedef struct CLIP {
    /* where the clip lives: a resource that stays loaded, or memory of ours. */
    BYTE *Owned;
    int Width, Height;
    DWORD Delay;
    int Count;
    CLIP_FRAME *Frames;
    BYTE Format[FORMAT_BYTES];
    DWORD RawSize;
    /* what has been drawn into the picture, so a later frame can go on from it. */
    HBITMAP Picture;
    HDC PictureDc;
    HGDIOBJ PictureOld;
    int Composed;
    COLORREF Transparent;
} CLIP;

typedef struct ANIMATE {
    HWND Hwnd;
    HWND Parent;
    UINT Id;
    DWORD Style;
    BOOL FirstPaint;
    BOOL Scaled;
    CLIP *Clip;
    RECT Box;
    int Frame;
    int From, To;
    int Repeat;
    UINT_PTR Timer;
    int Dpi;
} ANIMATE;

static ANIMATE *get_data(HWND hwnd)
{
    return (ANIMATE *)GetWindowLongPtrW(hwnd, 0);
}

/* ------------------------------------------------------------------ the clip */

static void picture_free(CLIP *c)
{
    if (c->PictureDc) {
        SelectObject(c->PictureDc, c->PictureOld);
        DeleteDC(c->PictureDc);
    }
    if (c->Picture)
        DeleteObject(c->Picture);
    c->PictureDc = nullptr;
    c->Picture = nullptr;
    c->Composed = -1;
}

static void clip_free(CLIP *c)
{
    if (!c)
        return;
    picture_free(c);
    CcFree(c->Frames);
    CcFree(c->Owned);
    CcFree(c);
}

/* the chunks of a clip, as little endian words. */
static DWORD word_at(const BYTE *p)
{
    return (DWORD)p[0] | (DWORD)p[1] << 8 | (DWORD)p[2] << 16 | (DWORD)p[3] << 24;
}

#define FOURCC_(a, b, c, d) ((DWORD)(a) | (DWORD)(b) << 8 | (DWORD)(c) << 16 | (DWORD)(d) << 24)

/*
 * reads the headers of an AVI of one video stream of run length coded or raw 8 bit frames and the index of its
 * frames. The list of the headers and the lists of the streams are read through, any other list is skipped. A clip
 * whose stream changes its palette, has frames in a list that is not the movie, or has no index is refused.
 */
static BOOL clip_parse(CLIP *c, const BYTE *data, SIZE_T size)
{
    if (size < 12 || word_at(data) != FOURCC_('R', 'I', 'F', 'F') || word_at(data + 8) != FOURCC_('A', 'V', 'I', ' '))
        return FALSE;
    SIZE_T end = 8 + (SIZE_T)word_at(data + 4);
    if (end > size)
        end = size;
    const BYTE *movie = nullptr, *index = nullptr;
    SIZE_T index_size = 0;
    BOOL header = FALSE, format = FALSE;
    int streams = 0, video = -1;
    DWORD total = 0;
    for (SIZE_T at = 12; at + 8 <= end;) {
        DWORD id = word_at(data + at), length = word_at(data + at + 4);
        SIZE_T body = at + 8, next = body + (((SIZE_T)length + 1) & ~(SIZE_T)1);
        if (id == FOURCC_('L', 'I', 'S', 'T')) {
            if (length < 4 || body + 4 > end)
                return FALSE;
            DWORD kind = word_at(data + body);
            if (kind == FOURCC_('m', 'o', 'v', 'i'))
                movie = data + body + 4;
            /* the header lists are read through, the others skipped. */
            if (kind == FOURCC_('h', 'd', 'r', 'l') || kind == FOURCC_('s', 't', 'r', 'l'))
                next = body + 4;
        } else if (id == FOURCC_('a', 'v', 'i', 'h')) {
            if (length < 40 || body + 40 > end)
                return FALSE;
            header = TRUE;
            total = word_at(data + body + 16);
            c->Width = (int)word_at(data + body + 32);
            c->Height = (int)word_at(data + body + 36);
            c->Delay = word_at(data + body) / 1000;
            /* no frames to prime, at most a video and an audio stream. */
            if (word_at(data + body + 20) != 0 || word_at(data + body + 24) > 2)
                return FALSE;
        } else if (id == FOURCC_('s', 't', 'r', 'h')) {
            if (length < 36 || body + 36 > end)
                return FALSE;
            if (video < 0 && word_at(data + body) == FOURCC_('v', 'i', 'd', 's')) {
                video = streams;
                if (word_at(data + body + 8) & AVISF_VIDEO_PALCHANGES_)
                    return FALSE;
            }
            streams++;
        } else if (id == FOURCC_('s', 't', 'r', 'f')) {
            if (video >= 0 && !format && streams == video + 1) {
                format = TRUE;
                if (length < sizeof(BITMAPINFOHEADER) || body + length > end || word_at(data + body) != 40)
                    return FALSE;
                const BYTE *h = data + body;
                /* raw or run length coded, of the size the header gave. */
                if (word_at(h + 16) > 1 || (int)word_at(h + 4) != c->Width || (int)word_at(h + 8) != c->Height)
                    return FALSE;
                memcpy(c->Format, h, length < FORMAT_BYTES ? length : FORMAT_BYTES);
                BITMAPINFOHEADER *bi = (BITMAPINFOHEADER *)c->Format;
                LONG stride = ((LONG)bi->biWidth * bi->biBitCount + 31) / 32 * 4;
                c->RawSize = (DWORD)(stride * (bi->biHeight < 0 ? -bi->biHeight : bi->biHeight));
            }
        } else if (id == FOURCC_('i', 'd', 'x', '1')) {
            index = data + body;
            index_size = body + length <= end ? length : end - body;
        }
        if (next <= at)
            return FALSE;
        at = next;
    }
    if (!header || !format || !movie || !index || total == 0 || c->Width <= 0 || c->Height <= 0)
        return FALSE;
    /* the frames of the video stream, in the order of the index. */
    SIZE_T entries = index_size / 16;
    c->Frames = CcAlloc(sizeof(CLIP_FRAME) * (SIZE_T)(entries ? entries : 1));
    if (!c->Frames)
        return FALSE;
    int count = 0;
    for (SIZE_T i = 0; i < entries && (DWORD)count < total; i++) {
        const BYTE *e = index + i * 16;
        int a = e[0] >= 'a' ? e[0] - 'a' + 10 : e[0] >= 'A' ? e[0] - 'A' + 10 : e[0] - '0';
        int b = e[1] >= 'a' ? e[1] - 'a' + 10 : e[1] >= 'A' ? e[1] - 'A' + 10 : e[1] - '0';
        if (a * 16 + b != video)
            continue;
        /* the offset is counted from the type of the movie list, which is four bytes before it. */
        SIZE_T offset = (SIZE_T)word_at(e + 8) + 4, length = word_at(e + 12);
        const BYTE *frame = movie + offset;
        if (frame < data || frame + length > data + size)
            return FALSE;
        c->Frames[count].Data = frame;
        c->Frames[count].Size = (DWORD)length;
        c->Frames[count].Key = (word_at(e + 4) & AVIIF_KEYFRAME_) != 0;
        count++;
    }
    if (count == 0)
        return FALSE;
    c->Count = count;
    return TRUE;
}

static CLIP *clip_open(const BYTE *data, SIZE_T size, BOOL copy)
{
    CLIP *c = CcAlloc(sizeof(*c));
    if (!c)
        return nullptr;
    c->Composed = -1;
    if (copy) {
        c->Owned = CcAlloc(size);
        if (!c->Owned) {
            CcFree(c);
            return nullptr;
        }
        memcpy(c->Owned, data, size);
        data = c->Owned;
    }
    if (!clip_parse(c, data, size)) {
        clip_free(c);
        return nullptr;
    }
    return c;
}

static CLIP *clip_from_resource(HINSTANCE module, LPCWSTR name)
{
    if (!IS_INTRESOURCE(name) && !*name)
        return nullptr;
    HRSRC found = FindResourceW(module, name, L"AVI");
    if (!found)
        return nullptr;
    HGLOBAL loaded = LoadResource(module, found);
    const BYTE *data = loaded ? LockResource(loaded) : nullptr;
    /* the resource stays where it is, the clip points into it. */
    return data ? clip_open(data, SizeofResource(module, found), FALSE) : nullptr;
}

static CLIP *clip_from_file(LPCWSTR path)
{
    if (IS_INTRESOURCE(path))
        return nullptr;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    DWORD size = GetFileSize(file, nullptr), read = 0;
    BYTE *data = size != INVALID_FILE_SIZE && size <= 0x10000000 ? CcAlloc(size) : nullptr;
    CLIP *c = nullptr;
    if (data && ReadFile(file, data, size, &read, nullptr) && read == size)
        c = clip_open(data, size, FALSE);
    if (c) {
        /* the clip keeps the memory. */
        c->Owned = data;
    } else {
        CcFree(data);
    }
    CloseHandle(file);
    return c;
}

/* the picture of a frame: the frames from the last key one drawn over each other, or on from the last one drawn. */
static BOOL clip_compose(CLIP *c, HDC like, int frame)
{
    if (frame < 0 || frame >= c->Count)
        return FALSE;
    if (!c->Picture) {
        c->PictureDc = CreateCompatibleDC(like);
        c->Picture = c->PictureDc ? CreateCompatibleBitmap(like, c->Width, c->Height) : nullptr;
        if (!c->Picture) {
            picture_free(c);
            return FALSE;
        }
        c->PictureOld = SelectObject(c->PictureDc, c->Picture);
        c->Composed = -1;
    }
    if (c->Composed == frame)
        return TRUE;
    int key = frame;
    while (key > 0 && !c->Frames[key].Key)
        key--;
    int first = c->Composed >= 0 && c->Composed < frame && c->Composed >= key ? c->Composed + 1 : key;
    BITMAPINFO *bi = (BITMAPINFO *)c->Format;
    for (int i = first; i <= frame; i++) {
        /* a frame of no bytes repeats the one before. */
        if (!c->Frames[i].Size)
            continue;
        /* a frame is raw when it has the size of one, else it is run length coded. */
        bi->bmiHeader.biCompression = c->Frames[i].Size != c->RawSize ? BI_RLE8 : BI_RGB;
        bi->bmiHeader.biSizeImage = c->Frames[i].Size;
        StretchDIBits(c->PictureDc, 0, 0, c->Width, c->Height, 0, 0, c->Width, c->Height, c->Frames[i].Data, bi,
                      DIB_RGB_COLORS, SRCCOPY);
    }
    c->Composed = frame;
    return TRUE;
}

/* ------------------------------------------------------------------ the control */

static void notify(ANIMATE *a, int code)
{
    if (a->Parent)
        PostMessageW(a->Parent, WM_COMMAND, MAKEWPARAM(a->Id, code), (LPARAM)a->Hwnd);
}

static int stop(ANIMATE *a)
{
    KillTimer(a->Hwnd, a->Timer);
    a->Timer = 0;
    notify(a, ACN_STOP);
    return 1;
}

static HBRUSH parent_brush(ANIMATE *a, HDC dc)
{
    HBRUSH brush = a->Parent ? (HBRUSH)SendMessageW(a->Parent, WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)a->Hwnd) : nullptr;
    return brush ? brush : GetSysColorBrush(COLOR_BTNFACE);
}

static void draw(ANIMATE *a, HDC target)
{
    if (!a->Clip)
        return;
    RECT client = {};
    GetClientRect(a->Hwnd, &client);
    CC_DRAW_BUFFER buffer;
    HDC dc = CcBeginBuffer(&buffer, target, &client);
    FillRect(dc, &client, parent_brush(a, dc));
    if (clip_compose(a->Clip, dc, a->Frame)) {
        int w = a->Box.right - a->Box.left, h = a->Box.bottom - a->Box.top;
        if (a->Style & ACS_TRANSPARENT)
            GdiTransparentBlt(dc, a->Box.left, a->Box.top, w, h, a->Clip->PictureDc, 0, 0, a->Clip->Width,
                              a->Clip->Height, a->Clip->Transparent);
        else
            StretchBlt(dc, a->Box.left, a->Box.top, w, h, a->Clip->PictureDc, 0, 0, a->Clip->Width, a->Clip->Height,
                       SRCCOPY);
    }
    CcEndBuffer(&buffer);
}

/* a window that is first painted with no clip opens the one its text names, as ACS_AUTOPLAY clips are made. */
static BOOL open_clip(ANIMATE *a, HINSTANCE module, LPCWSTR name, BOOL from_text);

static void first_paint(ANIMATE *a)
{
    if (!a->FirstPaint)
        return;
    a->FirstPaint = FALSE;
    if (!a->Clip && (a->Style & WS_CHILD))
        open_clip(a, nullptr, nullptr, TRUE);
}

static BOOL open_clip(ANIMATE *a, HINSTANCE module, LPCWSTR name, BOOL from_text)
{
    WCHAR text[260];
    if (from_text) {
        GetWindowTextW(a->Hwnd, text, 260);
        name = text;
    }
    if (!module)
        module = (HINSTANCE)GetWindowLongPtrW(a->Hwnd, GWLP_HINSTANCE);
    stop(a);
    clip_free(a->Clip);
    a->Clip = nullptr;
    a->From = 0;
    if (!name || (!IS_INTRESOURCE(name) && !*name))
        return FALSE;
    CLIP *c = clip_from_resource(module, name);
    if (!c)
        c = clip_from_file(name);
    if (!c)
        return FALSE;
    a->Clip = c;
    a->Frame = 0;
    a->To = c->Count - 1;
    int w = c->Width, h = c->Height;
    if (a->Scaled) {
        w = MulDiv(w, a->Dpi, 96);
        h = MulDiv(h, a->Dpi, 96);
    }
    SetRect(&a->Box, 0, 0, w, h);
    if ((a->Style & ACS_TRANSPARENT) && a->Parent) {
        /* the color of the corner of the first frame is the one that is not drawn. */
        HDC screen = GetDC(a->Hwnd);
        if (screen && clip_compose(c, screen, 0))
            c->Transparent = GetPixel(c->PictureDc, 0, 0);
        SendMessageW(a->Parent, WM_CTLCOLORSTATIC, (WPARAM)screen, (LPARAM)a->Hwnd);
        if (screen)
            ReleaseDC(a->Hwnd, screen);
    }
    if (a->Style & ACS_CENTER) {
        RECT client = {};
        GetClientRect(a->Hwnd, &client);
        OffsetRect(&a->Box, (client.right - w) / 2, (client.bottom - h) / 2);
    } else {
        /* the window takes the size of the clip. */
        RECT fit = a->Box;
        AdjustWindowRectEx(&fit, (DWORD)GetWindowLongW(a->Hwnd, GWL_STYLE), FALSE,
                           (DWORD)GetWindowLongW(a->Hwnd, GWL_EXSTYLE));
        SetWindowPos(a->Hwnd, nullptr, 0, 0, fit.right - fit.left, fit.bottom - fit.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (a->Style & ACS_AUTOPLAY)
        PostMessageW(a->Hwnd, ACM_PLAY, (WPARAM)-1, MAKELPARAM(0, 0xFFFF));
    else
        InvalidateRect(a->Hwnd, nullptr, TRUE);
    return TRUE;
}

/* from and to are frame numbers, to of -1 the last one; repeat is how many times, -1 for ever. */
static BOOL play(ANIMATE *a, int repeat, int from, int to)
{
    if (!a->Clip)
        return FALSE;
    stop(a);
    int count = a->Clip->Count;
    a->Repeat = repeat;
    if (from >= count)
        from = count - 1;
    if (to == -1)
        to = count - 1;
    int last = count - 1, wanted = to >= 0 ? to : 0;
    if (wanted < count)
        last = wanted;
    a->To = last;
    if (from < 0) {
        from = a->From;
    } else {
        a->Frame = from;
        a->From = from;
    }
    if (from == last || repeat == 0) {
        InvalidateRect(a->Hwnd, nullptr, TRUE);
        return TRUE;
    }
    InvalidateRect(a->Hwnd, nullptr, FALSE);
    UpdateWindow(a->Hwnd);
    notify(a, ACN_START);
    a->Timer = SetTimer(a->Hwnd, TIMER_PLAY, a->Clip->Delay, nullptr);
    return TRUE;
}

/* shows the frame it is at and moves to the next: 0 when the playing is over, negative while hidden. */
static int tick(ANIMATE *a)
{
    if (!a->Clip)
        return 0;
    HDC dc = GetDC(a->Hwnd);
    RECT visible = {};
    if (GetClipBox(dc, &visible) == NULLREGION) {
        /* nothing shows: stay where the play began, and check again later. */
        a->Frame = a->From;
        ReleaseDC(a->Hwnd, dc);
        return a->Repeat != 0 ? -1 : 0;
    }
    draw(a, dc);
    if (a->Frame >= a->To) {
        BOOL done = a->Repeat == 0;
        if (a->Repeat > 0) {
            a->Repeat--;
            done = a->Repeat == 0;
        }
        if (!done)
            a->Frame = a->From;
    } else {
        a->Frame++;
    }
    ReleaseDC(a->Hwnd, dc);
    return a->Repeat != 0;
}

LRESULT CALLBACK AnimateProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    ANIMATE *a = get_data(hwnd);
    if (msg == WM_NCCREATE) {
        a = CcAlloc(sizeof(*a));
        if (!a)
            return FALSE;
        const CREATESTRUCTW *cs = (const CREATESTRUCTW *)lparam;
        a->Hwnd = hwnd;
        a->Parent = cs->hwndParent;
        a->Id = (UINT)(UINT_PTR)cs->hMenu;
        a->Style = cs->style;
        a->FirstPaint = TRUE;
        a->Dpi = (int)GetDpiForWindow(hwnd);
        if (!a->Dpi)
            a->Dpi = 96;
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)a);
        /* the picture is not mirrored. */
        LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
        if (ex & WS_EX_LAYOUTRTL)
            SetWindowLongW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYOUTRTL);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (!a)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
    case WM_DESTROY:
        /* closes the clip, which stops it. */
        open_clip(a, nullptr, nullptr, FALSE);
        SetWindowLongPtrW(hwnd, 0, 0);
        CcFree(a);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case ACM_OPENA: {
        WCHAR wide[260];
        LPCWSTR name = (LPCWSTR)lparam;
        if ((ULONG_PTR)lparam >= 0x10000) {
            MultiByteToWideChar(CP_ACP, 0, (LPCSTR)lparam, -1, wide, 260);
            name = wide;
        }
        return open_clip(a, (HINSTANCE)wparam, name, FALSE);
    }
    case ACM_OPENW:
        return open_clip(a, (HINSTANCE)wparam, (LPCWSTR)lparam, FALSE);
    case ACM_PLAY:
        return play(a, (int)wparam, (short)LOWORD(lparam), (short)HIWORD(lparam));
    case ACM_STOP:
        return stop(a);
    case ACM_ISPLAYING:
        return a->Timer != 0;
    case CCM_DPISCALE_:
        a->Scaled = (wparam & 1) != 0;
        return 1;
    case WM_SIZE:
        if ((a->Style & ACS_CENTER) && a->Clip) {
            int w = a->Box.right - a->Box.left, h = a->Box.bottom - a->Box.top;
            SetRect(&a->Box, (LOWORD(lparam) - w) / 2, (HIWORD(lparam) - h) / 2, 0, 0);
            a->Box.right = a->Box.left + w;
            a->Box.bottom = a->Box.top + h;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_PAINT: {
        PAINTSTRUCT ps;
        first_paint(a);
        HDC dc = BeginPaint(hwnd, &ps);
        draw(a, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        first_paint(a);
        draw(a, (HDC)wparam);
        return 0;
    case WM_CLOSE:
        stop(a);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_ERASEBKGND:
        return 1;
    case WM_GETOBJECT:
        if ((LONG)lparam == OBJID_QUERYCLASSNAMEIDX_)
            return ANIMATE_CLASS_INDEX;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_STYLECHANGED:
        if ((int)wparam == GWL_STYLE)
            a->Style = ((const STYLESTRUCT *)lparam)->styleNew;
        return 0;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_TIMER:
        if (wparam == TIMER_PLAY) {
            int more = tick(a);
            if (more) {
                UINT delay = a->Clip ? a->Clip->Delay : 0;
                a->Timer = SetTimer(hwnd, TIMER_PLAY, more < 0 ? delay + HIDDEN_DELAY : delay, nullptr);
            } else {
                stop(a);
            }
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
