/* system metrics, colors and SystemParametersInfo, using Windows 10 and 11 values scaled to the requested DPI. */
#include "win32k.h"

BRUSH *GdiCreateSystemBrush(COLORREF color, int sys_color);

W32_DISPLAY W32Display;

#define NUM_SYSCOLORS 31

/* the light colors of Windows 10 and 11. */
static const COLORREF g_default_colors[NUM_SYSCOLORS] = {
    RGB(0xC8, 0xC8, 0xC8), /* COLOR_SCROLLBAR */
    RGB(0x00, 0x00, 0x00), /* COLOR_BACKGROUND */
    RGB(0x99, 0xB4, 0xD1), /* COLOR_ACTIVECAPTION */
    RGB(0xBF, 0xCD, 0xDB), /* COLOR_INACTIVECAPTION */
    RGB(0xF0, 0xF0, 0xF0), /* COLOR_MENU */
    RGB(0xFF, 0xFF, 0xFF), /* COLOR_WINDOW */
    RGB(0x64, 0x64, 0x64), /* COLOR_WINDOWFRAME */
    RGB(0x00, 0x00, 0x00), /* COLOR_MENUTEXT */
    RGB(0x00, 0x00, 0x00), /* COLOR_WINDOWTEXT */
    RGB(0x00, 0x00, 0x00), /* COLOR_CAPTIONTEXT */
    RGB(0xB4, 0xB4, 0xB4), /* COLOR_ACTIVEBORDER */
    RGB(0xF4, 0xF7, 0xFC), /* COLOR_INACTIVEBORDER */
    RGB(0xAB, 0xAB, 0xAB), /* COLOR_APPWORKSPACE */
    RGB(0x00, 0x78, 0xD7), /* COLOR_HIGHLIGHT */
    RGB(0xFF, 0xFF, 0xFF), /* COLOR_HIGHLIGHTTEXT */
    RGB(0xF0, 0xF0, 0xF0), /* COLOR_BTNFACE */
    RGB(0xA0, 0xA0, 0xA0), /* COLOR_BTNSHADOW */
    RGB(0x6D, 0x6D, 0x6D), /* COLOR_GRAYTEXT */
    RGB(0x00, 0x00, 0x00), /* COLOR_BTNTEXT */
    RGB(0x00, 0x00, 0x00), /* COLOR_INACTIVECAPTIONTEXT */
    RGB(0xFF, 0xFF, 0xFF), /* COLOR_BTNHIGHLIGHT */
    RGB(0x69, 0x69, 0x69), /* COLOR_3DDKSHADOW */
    RGB(0xE3, 0xE3, 0xE3), /* COLOR_3DLIGHT */
    RGB(0x00, 0x00, 0x00), /* COLOR_INFOTEXT */
    RGB(0xFF, 0xFF, 0xE1), /* COLOR_INFOBK */
    RGB(0xB5, 0xB5, 0xB5), /* unused slot 25. */
    RGB(0x00, 0x66, 0xCC), /* COLOR_HOTLIGHT */
    RGB(0xB9, 0xD1, 0xEA), /* COLOR_GRADIENTACTIVECAPTION */
    RGB(0xD7, 0xE4, 0xF2), /* COLOR_GRADIENTINACTIVECAPTION */
    RGB(0x00, 0x78, 0xD7), /* COLOR_MENUHILIGHT */
    RGB(0xF0, 0xF0, 0xF0), /* COLOR_MENUBAR */
};

/*
 * the colors of the high contrast black scheme: white on black, the selection black on light blue, hot tracking in
 * yellow and disabled text in green. the values are what Windows 10 shows, from memory, none was read from a file.
 */
static const COLORREF g_high_contrast_colors[NUM_SYSCOLORS] = {
    RGB(0x00, 0x00, 0x00), /* COLOR_SCROLLBAR */
    RGB(0x00, 0x00, 0x00), /* COLOR_BACKGROUND */
    RGB(0x00, 0x00, 0x00), /* COLOR_ACTIVECAPTION */
    RGB(0x00, 0x00, 0x00), /* COLOR_INACTIVECAPTION */
    RGB(0x00, 0x00, 0x00), /* COLOR_MENU */
    RGB(0x00, 0x00, 0x00), /* COLOR_WINDOW */
    RGB(0xFF, 0xFF, 0xFF), /* COLOR_WINDOWFRAME */
    RGB(0xFF, 0xFF, 0xFF), /* COLOR_MENUTEXT */
    RGB(0xFF, 0xFF, 0xFF), /* COLOR_WINDOWTEXT */
    RGB(0xFF, 0xFF, 0xFF), /* COLOR_CAPTIONTEXT */
    RGB(0xFF, 0xFF, 0xFF), /* COLOR_ACTIVEBORDER */
    RGB(0x00, 0x00, 0x00), /* COLOR_INACTIVEBORDER */
    RGB(0x00, 0x00, 0x00), /* COLOR_APPWORKSPACE */
    RGB(0x1A, 0xEB, 0xFF), /* COLOR_HIGHLIGHT */
    RGB(0x00, 0x00, 0x00), /* COLOR_HIGHLIGHTTEXT */
    RGB(0x00, 0x00, 0x00), /* COLOR_BTNFACE */
    RGB(0xFF, 0xFF, 0xFF), /* COLOR_BTNSHADOW */
    RGB(0x3F, 0xF2, 0x3F), /* COLOR_GRAYTEXT */
    RGB(0xFF, 0xFF, 0xFF), /* COLOR_BTNTEXT */
    RGB(0x3F, 0xF2, 0x3F), /* COLOR_INACTIVECAPTIONTEXT */
    RGB(0xFF, 0xFF, 0xFF), /* COLOR_BTNHIGHLIGHT */
    RGB(0xFF, 0xFF, 0xFF), /* COLOR_3DDKSHADOW */
    RGB(0xFF, 0xFF, 0xFF), /* COLOR_3DLIGHT */
    RGB(0xFF, 0xFF, 0xFF), /* COLOR_INFOTEXT */
    RGB(0x00, 0x00, 0x00), /* COLOR_INFOBK */
    RGB(0x00, 0x00, 0x00), /* unused slot 25. */
    RGB(0xFF, 0xFF, 0x00), /* COLOR_HOTLIGHT */
    RGB(0x00, 0x00, 0x00), /* COLOR_GRADIENTACTIVECAPTION */
    RGB(0x00, 0x00, 0x00), /* COLOR_GRADIENTINACTIVECAPTION */
    RGB(0x1A, 0xEB, 0xFF), /* COLOR_MENUHILIGHT */
    RGB(0x00, 0x00, 0x00), /* COLOR_MENUBAR */
};

#define SPI_SETHIGHCONTRAST 0x0043
#define HCF_HIGHCONTRASTON 0x1
#define HCF_AVAILABLE 0x2
static DWORD g_high_contrast_flags;

BOOLEAN MetricsHighContrast(void)
{
    return (g_high_contrast_flags & HCF_HIGHCONTRASTON) != 0;
}

static COLORREF g_colors[NUM_SYSCOLORS];
static BRUSH *g_brushes[NUM_SYSCOLORS];

/* read system DPI once from host boot information, as on Windows. */
static void read_display(void)
{
    const volatile MimeBootInfo *boot = HalMimeGetBootInfo();
    W32Display.Width = boot->display.width ? (LONG)boot->display.width : 1920;
    W32Display.Height = boot->display.height ? (LONG)boot->display.height : 1080;
    W32Display.Work = (RECT){(LONG)boot->display.work_x, (LONG)boot->display.work_y,
                             (LONG)(boot->display.work_x + boot->display.work_width),
                             (LONG)(boot->display.work_y + boot->display.work_height)};
    if (RectEmpty(&W32Display.Work))
        W32Display.Work = (RECT){0, 0, W32Display.Width, W32Display.Height};
    W32Display.RefreshHz = boot->display.refresh_hz ? boot->display.refresh_hz : 60;
    DisplayApplyMode();
    MonitorsRefresh();
}

void MetricsInitialize(void)
{
    const MimeBootInfo *boot = HalMimeGetBootInfo();
    /* wait for the host desktop connection while the guest boots. */
    LARGE_INTEGER began;
    KeQuerySystemTime(&began);
    while (!__atomic_load_n(&boot->display_ready, __ATOMIC_ACQUIRE)) {
        LARGE_INTEGER now;
        KeQuerySystemTime(&now);
        if (now.QuadPart - began.QuadPart > 10 * 10000000ll) {
            DbgPrint("win32k: the host did not report its display\n");
            break;
        }
#ifdef __x86_64__
        __asm__ volatile("pause");
#else
        __asm__ volatile("yield");
#endif
    }
    read_display();
    MonitorsTakeChanged();
    W32Display.Dpi = boot->display.dpi ? boot->display.dpi : 96;
    W32Display.Stretch = (boot->display.flags & MIME_DISPLAY_STRETCH) != 0;
    for (int i = 0; i < NUM_SYSCOLORS; i++) {
        g_colors[i] = g_default_colors[i];
        g_brushes[i] = GdiCreateSystemBrush(g_colors[i], i);
    }
    W32SystemFont = W32MessageFontForDpi(W32Display.Dpi);
}

/* counts the changes of the display settings, which GdiEntry13 of gdi32 gives to programs that cache what they read of them. */
static ULONG g_display_uniqueness = 1;

ULONG MetricsDisplayUniqueness(void)
{
    return g_display_uniqueness;
}

void MetricsDisplayChanged(void)
{
    LONG width = W32Display.Width, height = W32Display.Height;
    RECT work = W32Display.Work;
    DWORD bits = W32Display.Bits;
    read_display();
    if (MonitorsTakeChanged() || width != W32Display.Width || height != W32Display.Height ||
        bits != W32Display.Bits || memcmp(&work, &W32Display.Work, sizeof(work))) {
        g_display_uniqueness++;
        WndDisplayChanged();
    }
}

UINT W32SystemDpi(void)
{
    return W32Display.Dpi ? W32Display.Dpi : 96;
}

COLORREF MetricsSysColor(int index)
{
    return index >= 0 && index < NUM_SYSCOLORS ? g_colors[index] : 0;
}

HBRUSH MetricsSysColorBrush(int index)
{
    return index >= 0 && index < NUM_SYSCOLORS && g_brushes[index] ? (HBRUSH)g_brushes[index]->Header.Handle : nullptr;
}

static int scale(int v, UINT dpi)
{
    return W32MulDiv(v, (LONG)dpi, 96);
}

/* the sizes of the screen in the pixels of an awareness. */
static int screen(int v, ULONG awareness)
{
    return DpiScale(v, W32SystemDpi(), DpiScaleOf(awareness));
}

/* a metric at a dpi, with screen sizes in the pixels of an awareness. */
int MetricsGetFor(int index, UINT dpi, ULONG awareness)
{
    if (!dpi)
        dpi = DpiScaleOf(awareness);
    /* the sizing border is 3 fixed pixels plus the scaled border width, like Windows. */
    int frame = 3 + scale(1, dpi), padded = scale(4, dpi);
    switch (index) {
    case SM_CXSCREEN:
        return screen(W32Display.Width, awareness);
    case SM_CYSCREEN:
        return screen(W32Display.Height, awareness);
    case SM_CXVIRTUALSCREEN: {
        RECT all = MonitorVirtualScreen();
        return screen(all.right, awareness) - screen(all.left, awareness);
    }
    case SM_CYVIRTUALSCREEN: {
        RECT all = MonitorVirtualScreen();
        return screen(all.bottom, awareness) - screen(all.top, awareness);
    }
    case SM_XVIRTUALSCREEN:
        return screen(MonitorVirtualScreen().left, awareness);
    case SM_YVIRTUALSCREEN:
        return screen(MonitorVirtualScreen().top, awareness);
    case SM_CMONITORS:
        return (int)MonitorCount();
    case SM_CXVSCROLL:
    case SM_CYHSCROLL:
    case SM_CYVTHUMB:
    case SM_CXHTHUMB:
    case SM_CYVSCROLL:
    case SM_CXHSCROLL:
        return scale(17, dpi);
    case SM_CYCAPTION:
    case SM_CYSMCAPTION:
        return scale(23, dpi);
    case SM_CXBORDER:
    case SM_CYBORDER:
    case SM_CXFOCUSBORDER:
    case SM_CYFOCUSBORDER:
        return 1;
    case SM_CXDLGFRAME:
    case SM_CYDLGFRAME:
        return scale(3, dpi);
    case SM_CXICON:
    case SM_CYICON:
    case SM_CXCURSOR:
    case SM_CYCURSOR:
        return scale(32, dpi);
    case SM_CYMENU:
        return scale(20, dpi);
    case SM_CXFULLSCREEN:
        return screen(W32Display.Work.right, awareness) - screen(W32Display.Work.left, awareness);
    case SM_CYFULLSCREEN:
        return screen(W32Display.Work.bottom, awareness) - screen(W32Display.Work.top, awareness) - scale(23, dpi);
    case SM_MOUSEPRESENT:
    case SM_MOUSEWHEELPRESENT:
    case SM_SAMEDISPLAYFORMAT:
        return 1;
    case SM_CMOUSEBUTTONS:
        return 5;
    case SM_CXMIN:
    case SM_CXMINTRACK:
        return scale(136, dpi);
    case SM_CYMIN:
    case SM_CYMINTRACK:
        return scale(39, dpi);
    case SM_CXSIZE:
        return scale(36, dpi);
    case SM_CYSIZE:
    case SM_CXSMSIZE:
    case SM_CYSMSIZE:
        return scale(22, dpi);
    case SM_CXFRAME:
    case SM_CYFRAME:
        return frame;
    case SM_CXPADDEDBORDER:
        return padded;
    case SM_CXDOUBLECLK:
    case SM_CYDOUBLECLK:
    case SM_CXDRAG:
    case SM_CYDRAG:
        return scale(4, dpi);
    case SM_CXICONSPACING:
    case SM_CYICONSPACING:
        return scale(75, dpi);
    case SM_CXEDGE:
    case SM_CYEDGE:
        return scale(2, dpi);
    case SM_CXMINSPACING:
    case SM_CXMINIMIZED:
        return scale(160, dpi);
    case SM_CYMINSPACING:
    case SM_CYMINIMIZED:
        return scale(28, dpi);
    case SM_CXSMICON:
    case SM_CYSMICON:
        return scale(16, dpi);
    case SM_CXMENUSIZE:
    case SM_CYMENUSIZE:
        return scale(19, dpi);
    case SM_CXMENUCHECK:
    case SM_CYMENUCHECK:
        return scale(15, dpi);
    case SM_CXMAXTRACK:
        return screen(W32Display.Width, awareness) + 2 * (frame + padded);
    case SM_CYMAXTRACK:
        return screen(W32Display.Height, awareness) + 2 * (frame + padded);
    case SM_CXMAXIMIZED:
        return screen(W32Display.Work.right, awareness) - screen(W32Display.Work.left, awareness) + 2 * (frame + padded);
    case SM_CYMAXIMIZED:
        return screen(W32Display.Work.bottom, awareness) - screen(W32Display.Work.top, awareness) + 2 * (frame + padded);
    case SM_ARRANGE:
        return 8;
    default:
        return 0;
    }
}

int MetricsGet(int index, UINT dpi)
{
    return MetricsGetFor(index, dpi, DpiCurrentAwareness());
}

static void fill_nonclient(NONCLIENTMETRICSW *m, UINT dpi)
{
    UINT size = m->cbSize;
    memset(m, 0, sizeof(*m));
    m->cbSize = size;
    m->iBorderWidth = 1;
    m->iScrollWidth = scale(17, dpi);
    m->iScrollHeight = scale(17, dpi);
    m->iCaptionWidth = scale(36, dpi);
    m->iCaptionHeight = scale(23, dpi);
    m->lfCaptionFont = W32CaptionFontForDpi(dpi);
    m->iSmCaptionWidth = scale(22, dpi);
    m->iSmCaptionHeight = scale(23, dpi);
    m->lfSmCaptionFont = W32CaptionFontForDpi(dpi);
    m->iMenuWidth = scale(19, dpi);
    m->iMenuHeight = scale(19, dpi);
    m->lfMenuFont = W32MessageFontForDpi(dpi);
    m->lfStatusFont = W32MessageFontForDpi(dpi);
    m->lfMessageFont = W32MessageFontForDpi(dpi);
    if (size >= sizeof(NONCLIENTMETRICSW))
        m->iPaddedBorderWidth = scale(4, dpi);
}

/* data is a user pointer; the caller holds the user lock. */
/* SPI_SETKEYBOARDCUES: the mnemonics and focus rectangles show without the keyboard having been used. */
static BOOLEAN g_keyboard_cues;

BOOLEAN MetricsKeyboardCues(void)
{
    return g_keyboard_cues;
}

/* font smoothing: on, the kind (1 standard, 2 ClearType), the gamma as thousandths and the stripe order (0 BGR, 1 RGB). */
#define SPI_SETFONTSMOOTHING 0x004B
#define SPI_SETFONTSMOOTHINGTYPE 0x200B
#define SPI_SETFONTSMOOTHINGCONTRAST 0x200D
#define SPI_SETFONTSMOOTHINGORIENTATION 0x2013

static BOOLEAN g_font_smoothing = TRUE;
static UINT g_smoothing_type = 2;
static UINT g_smoothing_contrast = 1200;
static UINT g_smoothing_orientation = 1;

BOOLEAN MetricsFontSmoothing(UINT *type, UINT *contrast, UINT *orientation)
{
    *type = g_smoothing_type;
    *contrast = g_smoothing_contrast;
    *orientation = g_smoothing_orientation;
    return g_font_smoothing;
}

BOOL MetricsParametersInfo(UINT action, UINT param, PVOID data, UINT ini, UINT dpi)
{
    if (!dpi)
        dpi = DpiScaleOf(DpiCurrentAwareness());
    BOOL b;
    UINT u;
    switch (action) {
    case SPI_GETNONCLIENTMETRICS: {
        NONCLIENTMETRICSW m;
        if (!NT_SUCCESS(MimeCopyFromUser(&m.cbSize, data, sizeof(UINT))))
            return FALSE;
        if (m.cbSize != sizeof(NONCLIENTMETRICSW) && m.cbSize != sizeof(NONCLIENTMETRICSW) - sizeof(int))
            return FALSE;
        fill_nonclient(&m, dpi);
        return NT_SUCCESS(MimeCopyToUser(data, &m, m.cbSize));
    }
    case SPI_GETICONMETRICS: {
        ICONMETRICSW m;
        if (!NT_SUCCESS(MimeCopyFromUser(&m.cbSize, data, sizeof(UINT))) || m.cbSize != sizeof(m))
            return FALSE;
        m.iHorzSpacing = scale(75, dpi);
        m.iVertSpacing = scale(75, dpi);
        m.iTitleWrap = 1;
        m.lfFont = W32MessageFontForDpi(dpi);
        return NT_SUCCESS(MimeCopyToUser(data, &m, sizeof(m)));
    }
    case SPI_GETICONTITLELOGFONT: {
        LOGFONTW lf = W32MessageFontForDpi(dpi);
        return NT_SUCCESS(MimeCopyToUser(data, &lf, sizeof(lf)));
    }
    case SPI_GETWORKAREA: {
        RECT work = DpiWorkArea(DpiCurrentAwareness());
        return NT_SUCCESS(MimeCopyToUser(data, &work, sizeof(RECT)));
    }
    case SPI_GETBEEP:
    case SPI_GETFONTSMOOTHING:
        b = g_font_smoothing;
        return NT_SUCCESS(MimeCopyToUser(data, &b, sizeof(b)));
    case SPI_SETFONTSMOOTHING:
        g_font_smoothing = param != 0;
        return TRUE;
    case SPI_SETFONTSMOOTHINGTYPE:
        if ((ULONG_PTR)data != 1 && (ULONG_PTR)data != 2)
            return FALSE;
        g_smoothing_type = (UINT)(ULONG_PTR)data;
        return TRUE;
    case SPI_SETFONTSMOOTHINGCONTRAST:
        if ((ULONG_PTR)data < 1000 || (ULONG_PTR)data > 2200)
            return FALSE;
        g_smoothing_contrast = (UINT)(ULONG_PTR)data;
        return TRUE;
    case SPI_SETFONTSMOOTHINGORIENTATION:
        if ((ULONG_PTR)data > 1)
            return FALSE;
        g_smoothing_orientation = (UINT)(ULONG_PTR)data;
        return TRUE;
    case SPI_GETDRAGFULLWINDOWS:
    case SPI_GETMENUANIMATION:
    case SPI_GETCOMBOBOXANIMATION:
    case SPI_GETLISTBOXSMOOTHSCROLLING:
    case SPI_GETGRADIENTCAPTIONS:
    case SPI_GETHOTTRACKING:
    case SPI_GETMENUFADE:
    case SPI_GETSELECTIONFADE:
    case SPI_GETTOOLTIPANIMATION:
    case SPI_GETTOOLTIPFADE:
    case SPI_GETCURSORSHADOW:
    case SPI_GETUIEFFECTS:
    case SPI_GETCLIENTAREAANIMATION:
    case SPI_GETICONTITLEWRAP:
    case 0x1022: /* SPI_GETFLATMENU */
    case 0x1024: /* SPI_GETDROPSHADOW */
    case SPI_GETSHOWIMEUI:
        b = TRUE;
        return NT_SUCCESS(MimeCopyToUser(data, &b, sizeof(b)));
    case SPI_GETKEYBOARDCUES:
        b = g_keyboard_cues;
        return NT_SUCCESS(MimeCopyToUser(data, &b, sizeof(b)));
    case SPI_SETKEYBOARDCUES:
        g_keyboard_cues = data != nullptr;
        return TRUE;
    case SPI_GETSNAPTODEFBUTTON:
    case SPI_GETSCREENREADER:
    case SPI_GETSHOWSOUNDS:
    case SPI_GETSCREENSAVEACTIVE:
    case SPI_GETSCREENSAVERRUNNING:
    case SPI_GETLOWPOWERACTIVE:
    case SPI_GETPOWEROFFACTIVE:
    case SPI_GETACTIVEWINDOWTRACKING:
    case SPI_GETMENUDROPALIGNMENT:
    case SPI_GETKEYBOARDPREF:
        b = FALSE;
        return NT_SUCCESS(MimeCopyToUser(data, &b, sizeof(b)));
    case SPI_GETFONTSMOOTHINGTYPE:
        u = g_smoothing_type;
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_GETFONTSMOOTHINGCONTRAST:
        u = g_smoothing_contrast;
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_GETFONTSMOOTHINGORIENTATION:
        u = g_smoothing_orientation;
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_GETBORDER:
        u = 1;
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_GETWHEELSCROLLLINES:
    case SPI_GETWHEELSCROLLCHARS:
        u = 3;
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_GETMENUSHOWDELAY:
    case SPI_GETMOUSEHOVERTIME:
        u = 400;
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_GETMOUSEHOVERWIDTH:
    case SPI_GETMOUSEHOVERHEIGHT:
        u = 4;
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_GETCARETWIDTH:
    case SPI_GETFOCUSBORDERWIDTH:
    case SPI_GETFOCUSBORDERHEIGHT:
        u = 1;
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_GETMOUSESPEED:
        u = 10;
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_GETKEYBOARDSPEED:
        u = 31;
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_GETKEYBOARDDELAY:
        u = 1;
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_GETFOREGROUNDLOCKTIMEOUT:
        u = 200000;
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_GETSCREENSAVETIMEOUT:
        u = 0;
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_ICONHORIZONTALSPACING:
    case SPI_ICONVERTICALSPACING:
        u = (UINT)scale(75, dpi);
        return data ? NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u))) : TRUE;
    case SPI_GETMOUSE: {
        int v[3] = {6, 10, 1};
        return NT_SUCCESS(MimeCopyToUser(data, v, sizeof(v)));
    }
    case SPI_GETHIGHCONTRAST: {
        struct {
            UINT cbSize;
            DWORD dwFlags;
            LPWSTR lpszDefaultScheme;
        } hc;
        /* validate the size before writing; smaller buffers must fail. */
        if (!NT_SUCCESS(MimeCopyFromUser(&hc.cbSize, data, sizeof(UINT))) || hc.cbSize != sizeof(hc))
            return FALSE;
        hc.dwFlags = g_high_contrast_flags | HCF_AVAILABLE;
        hc.lpszDefaultScheme = nullptr;
        return NT_SUCCESS(MimeCopyToUser(data, &hc, sizeof(hc)));
    }
    case SPI_SETHIGHCONTRAST: {
        struct {
            UINT cbSize;
            DWORD dwFlags;
            LPWSTR lpszDefaultScheme;
        } hc;
        if (!NT_SUCCESS(MimeCopyFromUser(&hc, data, sizeof(hc))) || hc.cbSize != sizeof(hc))
            return FALSE;
        /* the scheme is always high contrast black, whatever name is given. */
        BOOLEAN was = MetricsHighContrast();
        g_high_contrast_flags = hc.dwFlags & HCF_HIGHCONTRASTON;
        if (was != MetricsHighContrast()) {
            for (int i = 0; i < NUM_SYSCOLORS; i++) {
                g_colors[i] = MetricsHighContrast() ? g_high_contrast_colors[i] : g_default_colors[i];
                if (g_brushes[i])
                    g_brushes[i]->Color = g_colors[i];
            }
            WndSystemChanged();
        }
        return TRUE;
    }
    case SPI_GETANIMATION: {
        struct {
            UINT cbSize;
            int iMinAnimate;
        } a = {sizeof(a), 1};
        UINT size;
        if (!NT_SUCCESS(MimeCopyFromUser(&size, data, sizeof(size))) || size != sizeof(a))
            return FALSE;
        return NT_SUCCESS(MimeCopyToUser(data, &a, sizeof(a)));
    }
    case SPI_GETDEFAULTINPUTLANG: {
        HKL hkl = (HKL)(ULONG_PTR)0x04090409;
        return NT_SUCCESS(MimeCopyToUser(data, &hkl, sizeof(hkl)));
    }
    case SPI_GETDESKWALLPAPER:
        if (param && data) {
            WCHAR empty = 0;
            return NT_SUCCESS(MimeCopyToUser(data, &empty, sizeof(empty)));
        }
        return TRUE;
    default:
        /* setters succeed without effect; unknown getters fail. */
        return (action & 1) == 1 && action > 0x0001 ? FALSE : TRUE;
    }
}

/* older SPI_ pairs are get then set; newer pairs reverse that order. the work area is an exception. */
static BOOL sets_parameter(UINT action)
{
    if (action == 0x2F)
        return TRUE;
    if (action == 0x30)
        return FALSE;
    return action >= 0x1000 ? (action & 1) : !(action & 1);
}

BOOL NTAPI NtUserSystemParametersInfo(UINT action, UINT param, PVOID data, UINT ini)
{
    if (sets_parameter(action) && W32UiLimited(W32_UILIMIT_SYSTEMPARAMETERS)) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    UserEnter();
    BOOL ok = MetricsParametersInfo(action, param, data, ini, 0);
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserSystemParametersInfoForDpi(UINT action, UINT param, PVOID data, UINT ini, UINT dpi)
{
    if (sets_parameter(action) && W32UiLimited(W32_UILIMIT_SYSTEMPARAMETERS)) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    UserEnter();
    BOOL ok = MetricsParametersInfo(action, param, data, ini, dpi);
    UserLeave();
    return ok;
}
