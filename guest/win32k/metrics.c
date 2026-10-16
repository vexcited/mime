/*
 * system metrics, system colors and SystemParametersInfo, with the
 * windows 11 values scaled to the requested dpi.
 */
#include "win32k.h"

BRUSH *GdiCreateSystemBrush(COLORREF color, int sys_color);

W32_DISPLAY W32Display;

#define NUM_SYSCOLORS 31

/* windows 11 light theme. */
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
    RGB(0xB5, 0xB5, 0xB5), /* unused slot 25 */
    RGB(0x00, 0x66, 0xCC), /* COLOR_HOTLIGHT */
    RGB(0xB9, 0xD1, 0xEA), /* COLOR_GRADIENTACTIVECAPTION */
    RGB(0xD7, 0xE4, 0xF2), /* COLOR_GRADIENTINACTIVECAPTION */
    RGB(0x00, 0x78, 0xD7), /* COLOR_MENUHILIGHT */
    RGB(0xF0, 0xF0, 0xF0), /* COLOR_MENUBAR */
};

static COLORREF g_colors[NUM_SYSCOLORS];
static BRUSH *g_brushes[NUM_SYSCOLORS];

/* the display the host reports in the boot information; the dpi is read once, like the windows system dpi. */
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
}

void MetricsInitialize(void)
{
    const MimeBootInfo *boot = HalMimeGetBootInfo();
    read_display();
    W32Display.Dpi = boot->display.dpi ? boot->display.dpi : 96;
    for (int i = 0; i < NUM_SYSCOLORS; i++) {
        g_colors[i] = g_default_colors[i];
        g_brushes[i] = GdiCreateSystemBrush(g_colors[i], i);
    }
    W32SystemFont = W32MessageFontForDpi(W32Display.Dpi);
}

/* the host display changed size or work area. */
void MetricsDisplayChanged(void)
{
    LONG width = W32Display.Width, height = W32Display.Height;
    RECT work = W32Display.Work;
    read_display();
    if (width != W32Display.Width || height != W32Display.Height || memcmp(&work, &W32Display.Work, sizeof(work)))
        WndDisplayChanged();
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

int MetricsGet(int index, UINT dpi)
{
    if (!dpi)
        dpi = W32SystemDpi();
    /* the sizing border is 3 fixed pixels plus the scaled border width, like windows. */
    int frame = 3 + scale(1, dpi), padded = scale(4, dpi);
    switch (index) {
    case SM_CXSCREEN:
    case SM_CXVIRTUALSCREEN:
        return W32Display.Width;
    case SM_CYSCREEN:
    case SM_CYVIRTUALSCREEN:
        return W32Display.Height;
    case SM_XVIRTUALSCREEN:
    case SM_YVIRTUALSCREEN:
        return 0;
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
        return W32Display.Work.right - W32Display.Work.left;
    case SM_CYFULLSCREEN:
        return W32Display.Work.bottom - W32Display.Work.top - scale(23, dpi);
    case SM_MOUSEPRESENT:
    case SM_MOUSEWHEELPRESENT:
    case SM_SAMEDISPLAYFORMAT:
    case SM_CMONITORS:
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
        return W32Display.Width + 2 * (frame + padded);
    case SM_CYMAXTRACK:
        return W32Display.Height + 2 * (frame + padded);
    case SM_CXMAXIMIZED:
        return W32Display.Work.right - W32Display.Work.left + 2 * (frame + padded);
    case SM_CYMAXIMIZED:
        return W32Display.Work.bottom - W32Display.Work.top + 2 * (frame + padded);
    case SM_ARRANGE:
        return 8;
    default:
        return 0;
    }
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
BOOL MetricsParametersInfo(UINT action, UINT param, PVOID data, UINT ini, UINT dpi)
{
    if (!dpi)
        dpi = W32SystemDpi();
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
    case SPI_GETWORKAREA:
        return NT_SUCCESS(MimeCopyToUser(data, &W32Display.Work, sizeof(RECT)));
    case SPI_GETBEEP:
    case SPI_GETFONTSMOOTHING:
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
        u = 2; /* FE_FONTSMOOTHINGCLEARTYPE */
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_GETFONTSMOOTHINGCONTRAST:
        u = 1200;
        return NT_SUCCESS(MimeCopyToUser(data, &u, sizeof(u)));
    case SPI_GETFONTSMOOTHINGORIENTATION:
        u = 1;
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
        /* the size is checked before anything is written: callers pass smaller buffers and expect a failure. */
        if (!NT_SUCCESS(MimeCopyFromUser(&hc.cbSize, data, sizeof(UINT))) || hc.cbSize != sizeof(hc))
            return FALSE;
        hc.dwFlags = 0x00000002; /* HCF_AVAILABLE */
        hc.lpszDefaultScheme = nullptr;
        return NT_SUCCESS(MimeCopyToUser(data, &hc, sizeof(hc)));
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

BOOL NTAPI NtUserSystemParametersInfo(UINT action, UINT param, PVOID data, UINT ini)
{
    UserEnter();
    BOOL ok = MetricsParametersInfo(action, param, data, ini, 0);
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserSystemParametersInfoForDpi(UINT action, UINT param, PVOID data, UINT ini, UINT dpi)
{
    UserEnter();
    BOOL ok = MetricsParametersInfo(action, param, data, ini, dpi);
    UserLeave();
    return ok;
}
