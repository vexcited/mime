#define MIME_NTDLL
#define NTSYSCALLAPI
#include <mime/ntrtl.h>
#include <mime/ntuser.h>
#include <mime/wow32.h>
#include "mime_svc_win32k.h"

void Mx32Unsupported(ULONG number);

ULONG64 MxCallService(ULONG number, const ULONG64 *arguments);

static PVOID ptr32(ULONG value)
{
    return (PVOID)(ULONG_PTR)value;
}

static UNICODE_STRING str32(ULONG value)
{
    const MIME_STRING32 *s = ptr32(value);
    return s ? (UNICODE_STRING){s->Length, s->MaximumLength, ptr32(s->Buffer)} : (UNICODE_STRING){0};
}

typedef struct MSG32 {
    ULONG Hwnd, Message, Wparam, Lparam, Time;
    POINT Point;
    ULONG Private;
} MSG32;

typedef struct PAINT32 {
    ULONG Dc;
    BOOL Erase;
    RECT Paint;
    BOOL Restore, IncUpdate;
    BYTE Reserved[32];
} PAINT32;

static MSG message64(const MSG32 *m)
{
    return (MSG){ptr32(m->Hwnd), m->Message, m->Wparam, (LONG_PTR)(ULONG_PTR)m->Lparam, m->Time, m->Point, m->Private};
}

/*
 * the object records with a pointer sized member: BITMAP and the DIBSECTION
 * that starts with one keep bmBits, LOGBRUSH keeps lbHatch. the others have
 * the same layout, so they pass through.
 */
static ULONG get_object32(const ULONG *a)
{
    ULONG64 v[16] = {a[0], a[1], a[2]};
    UCHAR type = (UCHAR)((a[0] >> 16) & 0xFF);
    if (type == 0x05) {
        DIBSECTION native;
        v[1] = a[1] >= 84 ? sizeof(DIBSECTION) : sizeof(BITMAP);
        v[2] = (ULONG_PTR)&native;
        ULONG n = (ULONG)MxCallService(MIME_SVC_NtGdiExtGetObjectW, v);
        if (!n)
            return 0;
        ULONG need = n == sizeof(DIBSECTION) ? 84 : 24;
        if (!a[2])
            return need;
        if (a[1] < 24)
            return 0;
        PUCHAR out = ptr32(a[2]);
        memcpy(out, &native.dsBm, 20);
        *(PULONG)(out + 20) = (ULONG)(ULONG_PTR)native.dsBm.bmBits;
        if (need == 84 && a[1] >= 84) {
            memcpy(out + 24, &native.dsBmih, 40);
            memcpy(out + 64, native.dsBitfields, 12);
            *(PULONG)(out + 76) = (ULONG)(ULONG_PTR)native.dshSection;
            *(PULONG)(out + 80) = native.dsOffset;
            return 84;
        }
        return 24;
    }
    if (type == 0x10) {
        LOGBRUSH native;
        v[1] = sizeof(native);
        v[2] = (ULONG_PTR)&native;
        if (!MxCallService(MIME_SVC_NtGdiExtGetObjectW, v))
            return 0;
        if (!a[2])
            return 12;
        if (a[1] < 12)
            return 0;
        ULONG out[3] = {native.lbStyle, native.lbColor, (ULONG)native.lbHatch};
        memcpy(ptr32(a[2]), out, sizeof(out));
        return 12;
    }
    return (ULONG)MxCallService(MIME_SVC_NtGdiExtGetObjectW, v);
}

ULONG Mx32Win32kCall(ULONG number, const ULONG *a)
{
    ULONG64 v[16] = {0};
    switch (number) {
#include "x86/win32k_calls.h"
    case MIME_SVC_NtUserInitializeClientPfnArrays: {
        const ULONG *p = ptr32(a[0]);
        if (!p || p[0] != 12 + 4 * (MIME_FNID_COUNT + 1))
            return FALSE;
        MIME_CLIENT_INFO info = {sizeof(info), 0, ptr32(p[2])};
        for (ULONG i = 0; i < MIME_FNID_COUNT; i++)
            info.Procs[i] = ptr32(p[i + 3]);
        info.DefWindowProcW = ptr32(p[3 + MIME_FNID_COUNT]);
        v[0] = (ULONG_PTR)&info;
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtUserRegisterClassExWOW: {
        const ULONG *p = ptr32(a[0]);
        if (!p || p[0] != 48)
            return 0;
        WNDCLASSEXW wc = {sizeof(wc), p[1], ptr32(p[2]), (int)p[3], (int)p[4], ptr32(p[5]),
                         ptr32(p[6]), ptr32(p[7]), ptr32(p[8]), ptr32(p[9]), ptr32(p[10]), ptr32(p[11])};
        UNICODE_STRING name = str32(a[1]), menu = str32(a[2]);
        v[0] = (ULONG_PTR)&wc;
        v[1] = (ULONG_PTR)&name;
        v[2] = a[2] ? (ULONG_PTR)&menu : 0;
        v[3] = a[3];
        v[4] = a[4];
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtUserGetClassInfoEx: {
        UNICODE_STRING name = str32(a[1]);
        WNDCLASSEXW wc = {sizeof(wc)};
        LPWSTR menu = NULL;
        v[0] = a[0];
        v[1] = a[1] ? (ULONG_PTR)&name : 0;
        v[2] = (ULONG_PTR)&wc;
        v[3] = (ULONG_PTR)&menu;
        v[4] = a[4];
        ULONG atom = (ULONG)MxCallService(number, v);
        ULONG *p = ptr32(a[2]);
        if (atom && p) {
            /* the 48-byte x86 WNDCLASSEXW, every pointer and handle narrowed. */
            ULONG out[12] = {48, wc.style, (ULONG)(ULONG_PTR)wc.lpfnWndProc, (ULONG)wc.cbClsExtra,
                (ULONG)wc.cbWndExtra, (ULONG)(ULONG_PTR)wc.hInstance, (ULONG)(ULONG_PTR)wc.hIcon,
                (ULONG)(ULONG_PTR)wc.hCursor, (ULONG)(ULONG_PTR)wc.hbrBackground, (ULONG)(ULONG_PTR)wc.lpszMenuName,
                (ULONG)(ULONG_PTR)wc.lpszClassName, (ULONG)(ULONG_PTR)wc.hIconSm};
            memcpy(p, out, sizeof(out));
        }
        if (atom && a[3])
            *(ULONG *)ptr32(a[3]) = (ULONG)(ULONG_PTR)menu;
        return atom;
    }
    case MIME_SVC_NtUserThunkedMenuItemInfo: {
        /* the 48-byte x86 MENUITEMINFOW, widened; the text comes separately as a counted string. */
        const ULONG *p = ptr32(a[4]);
        if (!p || p[0] != 48)
            return FALSE;
        MENUITEMINFOW info = {sizeof(info), p[1], p[2], p[3], p[4], ptr32(p[5]), ptr32(p[6]), ptr32(p[7]), p[8],
                              ptr32(p[9]), p[10], ptr32(p[11])};
        UNICODE_STRING text = str32(a[5]);
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = (ULONG_PTR)&info;
        v[5] = a[5] ? (ULONG_PTR)&text : 0;
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtUserCreateWindowEx: {
        UNICODE_STRING name = str32(a[1]), title = str32(a[2]);
        for (ULONG i = 0; i < 13; i++)
            v[i] = a[i];
        v[1] = a[1] ? (ULONG_PTR)&name : 0;
        v[2] = a[2] ? (ULONG_PTR)&title : 0;
        v[8] = (LONG64)(LONG)a[8];
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtUserFindWindowEx: {
        UNICODE_STRING klass = str32(a[2]), name = str32(a[3]);
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = a[2] ? (ULONG_PTR)&klass : 0;
        v[3] = a[3] ? (ULONG_PTR)&name : 0;
        v[4] = a[4];
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtUserTrackMouseEvent: {
        ULONG *p = ptr32(a[0]);
        if (!p || p[0] != 16)
            return FALSE;
        TRACKMOUSEEVENT track = {sizeof(track), p[1], ptr32(p[2]), p[3]};
        v[0] = (ULONG_PTR)&track;
        ULONG result = (ULONG)MxCallService(number, v);
        if (result) {
            p[1] = track.dwFlags;
            p[2] = (ULONG)(ULONG_PTR)track.hwndTrack;
            p[3] = track.dwHoverTime;
        }
        return result;
    }
    case MIME_SVC_NtUserSetCursorIconData: {
        const ULONG *p = ptr32(a[3]);
        if (!p)
            return FALSE;
        UNICODE_STRING module = str32(a[1]), resource = str32(a[2]);
        MIME_CURSOR_DATA data = {p[0], p[1], p[2], p[3], p[4], p[5], ptr32(p[6]), ptr32(p[7])};
        v[0] = a[0];
        v[1] = a[1] ? (ULONG_PTR)&module : 0;
        v[2] = a[2] ? (ULONG_PTR)&resource : 0;
        v[3] = (ULONG_PTR)&data;
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtGdiCreateDIBSection: {
        PVOID bits = NULL;
        for (ULONG i = 0; i < 8; i++)
            v[i] = a[i];
        v[8] = a[8] ? (ULONG_PTR)&bits : 0;
        ULONG result = (ULONG)MxCallService(number, v);
        if (result && (ULONG_PTR)bits > UINT32_MAX) {
            v[0] = result;
            MxCallService(MIME_SVC_NtGdiDeleteObjectApp, v);
            return 0;
        }
        if (result && a[8])
            *(ULONG *)ptr32(a[8]) = (ULONG)(ULONG_PTR)bits;
        return result;
    }
    case MIME_SVC_NtUserGetMessage:
    case MIME_SVC_NtUserPeekMessage: {
        MSG msg = {0};
        v[0] = (ULONG_PTR)&msg;
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        if (number == MIME_SVC_NtUserPeekMessage)
            v[4] = a[4];
        ULONG result = (ULONG)MxCallService(number, v);
        if (result != (ULONG)-1 && (result || number == MIME_SVC_NtUserGetMessage))
            *(MSG32 *)ptr32(a[0]) = (MSG32){(ULONG)(ULONG_PTR)msg.hwnd, msg.message, (ULONG)msg.wParam,
                                          (ULONG)msg.lParam, msg.time, msg.pt, msg.lPrivate};
        return result;
    }
    case MIME_SVC_NtUserDispatchMessage:
    case MIME_SVC_NtUserTranslateMessage: {
        MSG msg = message64(ptr32(a[0]));
        v[0] = (ULONG_PTR)&msg;
        if (number == MIME_SVC_NtUserTranslateMessage)
            v[1] = a[1];
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtUserBeginPaint:
    case MIME_SVC_NtUserEndPaint: {
        PAINT32 *p = ptr32(a[1]);
        PAINTSTRUCT ps = {0};
        if (number == MIME_SVC_NtUserEndPaint) {
            ps.hdc = ptr32(p->Dc);
            ps.fErase = p->Erase;
            ps.rcPaint = p->Paint;
            ps.fRestore = p->Restore;
            ps.fIncUpdate = p->IncUpdate;
            memcpy(ps.rgbReserved, p->Reserved, 32);
        }
        v[0] = a[0];
        v[1] = (ULONG_PTR)&ps;
        ULONG result = (ULONG)MxCallService(number, v);
        if (number == MIME_SVC_NtUserBeginPaint && result) {
            *p = (PAINT32){(ULONG)(ULONG_PTR)ps.hdc, ps.fErase, ps.rcPaint, ps.fRestore, ps.fIncUpdate, {0}};
            memcpy(p->Reserved, ps.rgbReserved, 32);
        }
        return result;
    }
    case MIME_SVC_NtUserMessageCall: {
        for (ULONG i = 0; i < 7; i++)
            v[i] = a[i];
        WINDOWPOS pos;
        ULONG *p = ptr32(a[3]);
        BOOLEAN windowpos = p && (a[1] == WM_WINDOWPOSCHANGING || a[1] == WM_WINDOWPOSCHANGED);
        if (windowpos) {
            pos = (WINDOWPOS){ptr32(p[0]), (HWND)(LONG_PTR)(LONG)p[1], (int)p[2], (int)p[3], (int)p[4], (int)p[5], p[6]};
            v[3] = (ULONG_PTR)&pos;
        }
        MIME_SEND_TIMEOUT timeout;
        if (a[5] == MIME_MSGCALL_SENDTIMEOUT && a[4]) {
            const ULONG *p = ptr32(a[4]);
            timeout = (MIME_SEND_TIMEOUT){p[0], p[1], p[2]};
            v[4] = (ULONG_PTR)&timeout;
        }
        ULONG result = (ULONG)MxCallService(number, v);
        if (windowpos) {
            p[0] = (ULONG)(ULONG_PTR)pos.hwnd;
            p[1] = (ULONG)(ULONG_PTR)pos.hwndInsertAfter;
            p[2] = pos.x; p[3] = pos.y; p[4] = pos.cx; p[5] = pos.cy; p[6] = pos.flags;
        }
        if (a[5] == MIME_MSGCALL_SENDTIMEOUT && a[4])
            ((ULONG *)ptr32(a[4]))[2] = (ULONG)timeout.Result;
        return result;
    }
    case MIME_SVC_NtUserSystemParametersInfo:
    case MIME_SVC_NtUserSystemParametersInfoForDpi:
        /* the supported metrics, rectangles and font records have no pointers. */
        for (ULONG i = 0; i < (number == MIME_SVC_NtUserSystemParametersInfo ? 4u : 5u); i++)
            v[i] = a[i];
        return (ULONG)MxCallService(number, v);
    case MIME_SVC_NtGdiHfontCreate:
        for (ULONG i = 0; i < 5; i++)
            v[i] = a[i];
        return (ULONG)MxCallService(number, v);
    case MIME_SVC_NtUserRegisterWindowMessage: {
        UNICODE_STRING name = str32(a[0]);
        v[0] = a[0] ? (ULONG_PTR)&name : 0;
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtUserSetWindowsHookEx: {
        /* the hook procedure stays an x86 address, called back like window procedures. */
        UNICODE_STRING module = str32(a[1]);
        v[0] = a[0];
        v[1] = a[1] ? (ULONG_PTR)&module : 0;
        v[2] = a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = a[4];
        v[5] = a[5];
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtGdiEnumFonts:
        /* the face records hold no pointers, so the buffer layout is shared. */
        for (ULONG i = 0; i < 8; i++)
            v[i] = a[i];
        return (ULONG)MxCallService(number, v);
    case MIME_SVC_NtGdiExtGetObjectW:
        return get_object32(a);
    case MIME_SVC_NtUserCallHwnd:
    case MIME_SVC_NtUserCallOneParam:
        v[0] = a[0];
        v[1] = a[1];
        return (ULONG)MxCallService(number, v);
    case MIME_SVC_NtUserCallHwndParam: {
        v[0] = a[0]; v[1] = a[1]; v[2] = a[2];
        if (a[2] == MIME_HWNDPARAM_MAPPOINTS) {
            const ULONG *p = ptr32(a[1]);
            MIME_MAP_POINTS points = {ptr32(p[0]), p[1], ptr32(p[2])};
            v[1] = (ULONG_PTR)&points;
            return (ULONG)MxCallService(number, v);
        }
        if (a[2] == MIME_HWNDPARAM_SETICON) {
            const ULONG *p = ptr32(a[1]);
            MIME_SET_ICON icon = {p[0], ptr32(p[1])};
            v[1] = (ULONG_PTR)&icon;
            return (ULONG)MxCallService(number, v);
        }
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtUserCallTwoParam:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return (ULONG)MxCallService(number, v);
    default:
        Mx32Unsupported(number);
        return 0;
    }
}
