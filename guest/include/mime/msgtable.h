/*
 * how the window manager treats messages below WM_USER: which cannot be posted, which a program may not post, and
 * which only the window manager sends.
 *
 * each set is a sorted list, one number per line with the message name and why it is listed. messages at WM_USER or
 * above are in none of them, since their parameters belong to the program.
 */
#ifndef MIME_MSGTABLE_H
#define MIME_MSGTABLE_H

#include <mime/nt.h>

/*
 * messages that cannot be posted or sent without waiting: they carry a pointer or handle of the sender that is gone
 * by the time a posted message is read, or the sender needs the receiver's answer.
 */
static const USHORT g_sync_only_messages[] = {
    0x0001, /* WM_CREATE, a CREATESTRUCT */
    0x000C, /* WM_SETTEXT, a string */
    0x000D, /* WM_GETTEXT, a buffer to fill */
    0x000E, /* WM_GETTEXTLENGTH, the length is the answer */
    0x0014, /* WM_ERASEBKGND, a device context */
    0x001A, /* WM_SETTINGCHANGE, the name of the section */
    0x001B, /* WM_DEVMODECHANGE, the name of the device */
    0x0024, /* WM_GETMINMAXINFO, a structure to fill */
    0x0027, /* WM_ICONERASEBKGND, a device context */
    0x002B, /* WM_DRAWITEM, a DRAWITEMSTRUCT */
    0x002C, /* WM_MEASUREITEM, a MEASUREITEMSTRUCT to fill */
    0x002D, /* WM_DELETEITEM, a DELETEITEMSTRUCT */
    0x0031, /* WM_GETFONT, a font handle is the answer */
    0x0038, /* the request of the old help engine, a structure */
    0x0039, /* WM_COMPAREITEM, a COMPAREITEMSTRUCT */
    0x003C, /* undocumented, the window manager's own */
    0x0046, /* WM_WINDOWPOSCHANGING, a WINDOWPOS the receiver may change */
    0x0047, /* WM_WINDOWPOSCHANGED, a WINDOWPOS */
    0x0049, /* WM_COPYGLOBALDATA, global memory of the sender */
    0x004A, /* WM_COPYDATA, a COPYDATASTRUCT */
    0x0053, /* WM_HELP, a HELPINFO */
    0x0059, /* undocumented */
    0x005A, /* undocumented */
    0x0070, /* undocumented, the window manager's own */
    0x007C, /* WM_STYLECHANGING, a STYLESTRUCT */
    0x007D, /* WM_STYLECHANGED, a STYLESTRUCT */
    0x0081, /* WM_NCCREATE, a CREATESTRUCT */
    0x0083, /* WM_NCCALCSIZE, rectangles the receiver changes */
    0x0085, /* WM_NCPAINT, a region handle */
    0x0087, /* WM_GETDLGCODE, a message that may be there */
    0x008B, /* undocumented, a rectangle */
    0x008C, /* undocumented, the window manager's own */
    0x0090, /* undocumented, the drawing of a menu bar: WM_UAHDESTROYWINDOW */
    0x0091, /* undocumented, WM_UAHDRAWMENU */
    0x0092, /* undocumented, WM_UAHDRAWMENUITEM */
    0x0093, /* undocumented, WM_UAHINITMENU */
    0x0094, /* undocumented, WM_UAHMEASUREMENUITEM */
    0x0095, /* undocumented, WM_UAHNCPAINTMENUPOPUP */
    0x00B0, /* EM_GETSEL, two numbers to fill */
    0x00B2, /* EM_GETRECT, a rectangle to fill */
    0x00B3, /* EM_SETRECT, a rectangle */
    0x00B4, /* EM_SETRECTNP, a rectangle */
    0x00C2, /* EM_REPLACESEL, a string */
    0x00C4, /* EM_GETLINE, a buffer to fill */
    0x00CB, /* EM_SETTABSTOPS, a list of numbers */
    0x00E3, /* SBM_GETRANGE, two numbers to fill */
    0x00E9, /* SBM_SETSCROLLINFO, a SCROLLINFO */
    0x00EA, /* SBM_GETSCROLLINFO, a SCROLLINFO to fill */
    0x00EB, /* SBM_GETSCROLLBARINFO, a structure to fill */
    0x0108, /* undocumented */
    0x010A, /* undocumented */
    0x0110, /* WM_INITDIALOG, the answer says whether the focus is set */
    0x011A, /* WM_GESTURENOTIFY, a structure */
    0x011C, /* undocumented */
    0x0124, /* WM_MENUGETOBJECT, a structure to fill */
    0x0132, /* WM_CTLCOLORMSGBOX, a brush is the answer */
    0x0133, /* WM_CTLCOLOREDIT */
    0x0134, /* WM_CTLCOLORLISTBOX */
    0x0135, /* WM_CTLCOLORBTN */
    0x0136, /* WM_CTLCOLORDLG */
    0x0137, /* WM_CTLCOLORSCROLLBAR */
    0x0138, /* WM_CTLCOLORSTATIC */
    0x0140, /* CB_GETEDITSEL, two numbers to fill */
    0x0143, /* CB_ADDSTRING, a string */
    0x0148, /* CB_GETLBTEXT, a buffer to fill */
    0x0149, /* CB_GETLBTEXTLEN, the length is the answer */
    0x014A, /* CB_INSERTSTRING, a string */
    0x014C, /* CB_FINDSTRING, a string */
    0x014D, /* CB_SELECTSTRING, a string */
    0x0152, /* CB_GETDROPPEDCONTROLRECT, a rectangle to fill */
    0x0158, /* CB_FINDSTRINGEXACT, a string */
    0x0164, /* CB_GETCOMBOBOXINFO, a structure to fill */
    0x0180, /* LB_ADDSTRING, a string */
    0x0181, /* LB_INSERTSTRING, a string */
    0x0189, /* LB_GETTEXT, a buffer to fill */
    0x018A, /* LB_GETTEXTLEN, the length is the answer */
    0x018C, /* LB_SELECTSTRING, a string */
    0x018F, /* LB_FINDSTRING, a string */
    0x0191, /* LB_GETSELITEMS, a list to fill */
    0x0192, /* LB_SETTABSTOPS, a list of numbers */
    0x0196, /* LB_ADDFILE, a file name */
    0x0198, /* LB_GETITEMRECT, a rectangle to fill */
    0x01A2, /* LB_FINDSTRINGEXACT, a string */
    0x01AA, /* undocumented, LB_INSERTSTRING_UPPER */
    0x01AB, /* undocumented, LB_INSERTSTRING_LOWER */
    0x01AC, /* undocumented, LB_ADDSTRING_UPPER */
    0x01AD, /* undocumented, LB_ADDSTRING_LOWER */
    0x01B2, /* LB_GETLISTBOXINFO, the answer is a number the receiver computes */
    0x01EB, /* undocumented, a number to fill */
    0x0210, /* WM_PARENTNOTIFY */
    0x0213, /* WM_NEXTMENU, an MDINEXTMENU */
    0x0214, /* WM_SIZING, a rectangle the receiver changes */
    0x0216, /* WM_MOVING, a rectangle the receiver changes */
    0x0220, /* WM_MDICREATE, an MDICREATESTRUCT */
    0x0229, /* WM_MDIGETACTIVE, a flag to fill */
    0x022A, /* undocumented, a structure of drops */
    0x022B, /* undocumented, a structure of drops */
    0x022D, /* undocumented, a structure of drops */
    0x022E, /* undocumented, a structure of drops */
    0x022F, /* undocumented, a structure of drops */
    0x024D, /* undocumented */
    0x0281, /* WM_IME_SETCONTEXT */
    0x0283, /* WM_IME_CONTROL */
    0x0288, /* WM_IME_REQUEST */
    0x02E0, /* WM_DPICHANGED, a suggested rectangle */
    0x02E1, /* undocumented */
    0x02E2, /* WM_DPICHANGED_BEFOREPARENT */
    0x02E3, /* WM_DPICHANGED_AFTERPARENT */
    0x02E4, /* WM_GETDPISCALEDSIZE, a size to fill */
    0x02E5, /* undocumented */
    0x02E6, /* undocumented, the window manager's own */
    0x02FA, /* undocumented */
    0x0309, /* WM_PAINTCLIPBOARD, a handle of the clipboard owner's */
    0x030B, /* WM_SIZECLIPBOARD, a handle of the clipboard owner's */
    0x030C, /* WM_ASKCBFORMATNAME, a buffer to fill */
    0x0314, /* undocumented, the window manager's own */
    0x032C, /* undocumented, the window manager's own */
    0x033F, /* WM_GETTITLEBARINFOEX, a structure to fill */
    0x0341, /* undocumented, the window manager's own */
    0x0344, /* undocumented, the window manager's own */
};

/* the messages a program may not post: the window manager keeps them for itself. */
static const USHORT g_private_messages[] = {
    0x003C, /* undocumented */
    0x02E6, /* undocumented */
    0x02ED, /* undocumented */
    0x02EE, /* undocumented */
    0x02FB, /* undocumented */
    0x032C, /* undocumented */
    0x032D, /* undocumented */
    0x032E, /* undocumented */
    0x032F, /* undocumented */
    0x0341, /* undocumented */
    0x0342, /* undocumented */
    0x0343, /* undocumented */
    0x0344, /* undocumented */
    0x0345, /* undocumented */
    0x0346, /* undocumented */
    0x0347, /* undocumented */
    0x0348, /* undocumented */
    0x0349, /* undocumented */
};

/* the messages that exist inside the window manager only: a program that sends one is answered with nothing done. */
static const USHORT g_kernel_only_messages[] = {
    0x003A, /* undocumented */
    0x003C, /* undocumented */
    0x0070, /* undocumented */
    0x008C, /* undocumented */
    0x011B, /* undocumented */
    0x0238, /* WM_POINTERDEVICECHANGE, announced by the input stack */
    0x0239, /* WM_POINTERDEVICEINRANGE */
    0x023A, /* WM_POINTERDEVICEOUTOFRANGE */
    0x023B, /* undocumented */
    0x023C, /* undocumented */
    0x023D, /* undocumented */
    0x02C8, /* undocumented */
    0x02C9, /* undocumented */
    0x02E6, /* undocumented */
    0x02FB, /* undocumented */
    0x0314, /* undocumented */
    0x0324, /* undocumented */
    0x0325, /* undocumented */
    0x0329, /* undocumented */
    0x032C, /* undocumented */
    0x032D, /* undocumented */
    0x032E, /* undocumented */
    0x032F, /* undocumented */
    0x0343, /* undocumented */
    0x0345, /* undocumented */
    0x0347, /* undocumented */
};

#define MSG_FIRST_PROGRAM_MESSAGE 0x0400
#define MSG_DEVICECHANGE 0x0219
#define MSG_POWERBROADCAST 0x0218
/* a device or power event that carries a structure sets this bit of its wparam. */
#define MSG_EVENT_HAS_DATA 0x8000

static inline BOOLEAN MimeMsgListed(const USHORT *list, ULONG count, UINT msg)
{
    ULONG low = 0, high = count;
    while (low < high) {
        ULONG middle = low + (high - low) / 2;
        if (list[middle] < msg)
            low = middle + 1;
        else
            high = middle;
    }
    return low < count && list[low] == msg;
}

/*
 * a device change with data can never be posted or sent without waiting. a power broadcast with data cannot be
 * posted, and sending it waits anyway.
 */
static inline BOOLEAN MimeMsgIsSyncOnly(UINT msg, WPARAM wparam, BOOLEAN post)
{
    if (msg >= MSG_FIRST_PROGRAM_MESSAGE)
        return FALSE;
    if (MimeMsgListed(g_sync_only_messages, RTL_NUMBER_OF(g_sync_only_messages), msg))
        return TRUE;
    BOOLEAN has_data = (wparam & MSG_EVENT_HAS_DATA) != 0;
    return (msg == MSG_DEVICECHANGE && has_data) || (msg == MSG_POWERBROADCAST && has_data && post);
}

static inline BOOLEAN MimeMsgIsPrivate(UINT msg)
{
    return msg < MSG_FIRST_PROGRAM_MESSAGE &&
           MimeMsgListed(g_private_messages, RTL_NUMBER_OF(g_private_messages), msg);
}

static inline BOOLEAN MimeMsgKernelOnly(UINT msg)
{
    return msg < MSG_FIRST_PROGRAM_MESSAGE &&
           MimeMsgListed(g_kernel_only_messages, RTL_NUMBER_OF(g_kernel_only_messages), msg);
}

#endif
