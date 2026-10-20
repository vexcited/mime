#define MIME_NTDLL
#define NTSYSCALLAPI
#include <mime/ntrtl.h>
#include <mime/ntuser.h>
#include <mime/wow32.h>
#include <mime/win32/d3dkmthk.h>
#include "mime_svc_win32k.h"
#include "wow32_layout.h"

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

/* object attributes of x86 code, with the name they point at. */
static OBJECT_ATTRIBUTES oa32(ULONG value, UNICODE_STRING *name)
{
    const MIME_OBJECT_ATTRIBUTES32 *a = ptr32(value);
    OBJECT_ATTRIBUTES out = {sizeof(out)};
    if (!a)
        return out;
    out.RootDirectory = (HANDLE)(LONG_PTR)(LONG)a->RootDirectory;
    out.Attributes = a->Attributes;
    if (a->ObjectName) {
        *name = str32(a->ObjectName);
        out.ObjectName = name;
    }
    out.SecurityDescriptor = ptr32(a->SecurityDescriptor);
    out.SecurityQualityOfService = ptr32(a->SecurityQualityOfService);
    return out;
}

/* the x86 MSG ends with the point: 28 bytes, a word more would be written over what follows it. */
typedef struct MSG32 {
    ULONG Hwnd, Message, Wparam, Lparam, Time;
    POINT Point;
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
    return (MSG){ptr32(m->Hwnd), m->Message, m->Wparam, (LONG_PTR)(ULONG_PTR)m->Lparam, m->Time, m->Point, 0};
}

/* a counted string's length back into the x86 one the call filled. */
static void set_length32(ULONG value, const UNICODE_STRING *s)
{
    if (value)
        ((MIME_STRING32 *)ptr32(value))->Length = s->Length;
}

static PVOID allocate(SIZE_T size)
{
    PVOID base = nullptr;
    return NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size, MEM_RESERVE | MEM_COMMIT,
                                              PAGE_READWRITE))
               ? base
               : nullptr;
}

static void release(PVOID base)
{
    SIZE_T size = 0;
    if (base)
        NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
}

/* the 48-byte x86 MENUITEMINFOW widened; the 44-byte one before hbmpItem too. */
static BOOLEAN menu_item64(const ULONG *p, MENUITEMINFOW *info)
{
    if (!p || (p[0] != 48 && p[0] != 44))
        return FALSE;
    *info = (MENUITEMINFOW){sizeof(*info), p[1], p[2], p[3], p[4], ptr32(p[5]), ptr32(p[6]), ptr32(p[7]), p[8],
                            ptr32(p[9]), p[10], p[0] == 48 ? ptr32(p[11]) : nullptr};
    return TRUE;
}

/*
 * the x86 OUTLINETEXTMETRICW: the same fields up to the four name offsets,
 * which are 4 bytes each with no padding before them, then the strings.
 */
static ULONG outline_metrics32(const ULONG *a)
{
    const ULONG names = offsetof(OUTLINETEXTMETRICW, otmusMinimumPPEM) + sizeof(UINT), size32 = names + 16;
    const ULONG delta = sizeof(OUTLINETEXTMETRICW) - size32;
    ULONG64 v[4] = {a[0], 0, 0, a[3]};
    ULONG need = (ULONG)MxCallService(MIME_SVC_NtGdiGetOutlineTextMetricsInternalW, v);
    if (need < sizeof(OUTLINETEXTMETRICW))
        return 0;
    if (!a[2])
        return need - delta;
    PUCHAR native = allocate(2 * (SIZE_T)need);
    if (!native)
        return 0;
    v[1] = need;
    v[2] = (ULONG_PTR)native;
    ULONG n = 0;
    if (MxCallService(MIME_SVC_NtGdiGetOutlineTextMetricsInternalW, v) == need) {
        OUTLINETEXTMETRICW *otm = (OUTLINETEXTMETRICW *)native;
        PUCHAR out = native + need;
        memcpy(out, native, names);
        ((OUTLINETEXTMETRICW *)out)->otmSize = need - delta;
        ULONG offsets[4] = {(ULONG)(ULONG_PTR)otm->otmpFamilyName - delta, (ULONG)(ULONG_PTR)otm->otmpFaceName - delta,
                            (ULONG)(ULONG_PTR)otm->otmpStyleName - delta, (ULONG)(ULONG_PTR)otm->otmpFullName - delta};
        memcpy(out + names, offsets, sizeof(offsets));
        memcpy(out + size32, native + sizeof(OUTLINETEXTMETRICW), need - sizeof(OUTLINETEXTMETRICW));
        n = a[1] < need - delta ? a[1] : need - delta;
        memcpy(ptr32(a[2]), out, n);
    }
    release(native);
    return n;
}

/* lists of handles, or of atoms with handles, narrowed into the x86 buffer. */
static ULONG handle_list32(ULONG number, const ULONG *a)
{
    BOOLEAN props = number == MIME_SVC_NtUserBuildPropList;
    ULONG max = props ? a[1] : a[4], count = 0;
    SIZE_T item = props ? sizeof(PROPLISTITEM) : sizeof(HWND);
    PVOID list = max ? allocate(max * item) : nullptr;
    if (max && !list)
        return (ULONG)STATUS_NO_MEMORY;
    ULONG64 v[7];
    if (props) {
        v[0] = a[0];
        v[1] = max;
        v[2] = (ULONG_PTR)list;
        v[3] = (ULONG_PTR)&count;
    } else {
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = max;
        v[5] = (ULONG_PTR)list;
        v[6] = (ULONG_PTR)&count;
    }
    NTSTATUS status = (NTSTATUS)MxCallService(number, v);
    ULONG *out = ptr32(props ? a[2] : a[5]);
    for (ULONG i = 0; out && i < count && i < max; i++) {
        if (props) {
            const PROPLISTITEM *p = (const PROPLISTITEM *)list + i;
            out[2 * i] = p->Atom;
            out[2 * i + 1] = (ULONG)(ULONG_PTR)p->Data;
        } else {
            out[i] = (ULONG)(ULONG_PTR)((HWND *)list)[i];
        }
    }
    ULONG count_at = props ? a[3] : a[6];
    if (count_at)
        *(ULONG *)ptr32(count_at) = count;
    release(list);
    return (ULONG)status;
}

/* the 28-byte x86 INPUT records widened, sent in batches. */
static ULONG send_input32(const ULONG *a)
{
    if (a[2] != 28 || !a[1])
        return 0;
    const ULONG *in = ptr32(a[1]);
    INPUT batch[32];
    ULONG sent = 0;
    for (ULONG i = 0; i < a[0];) {
        ULONG n = 0;
        for (; n < RTL_NUMBER_OF(batch) && i < a[0]; n++, i++) {
            const ULONG *p = in + 7 * i;
            INPUT *input = &batch[n];
            memset(input, 0, sizeof(*input));
            input->type = p[0];
            if (p[0] == INPUT_MOUSE)
                input->mi = (MOUSEINPUT){(LONG)p[1], (LONG)p[2], p[3], p[4], p[5], p[6]};
            else if (p[0] == INPUT_KEYBOARD)
                input->ki = (KEYBDINPUT){(WORD)p[1], (WORD)(p[1] >> 16), p[2], p[3], p[4]};
            else
                input->hi = (HARDWAREINPUT){p[1], (WORD)p[2], (WORD)(p[2] >> 16)};
        }
        ULONG64 v[3] = {n, (ULONG_PTR)batch, sizeof(INPUT)};
        ULONG done = (ULONG)MxCallService(MIME_SVC_NtUserSendInput, v);
        sent += done;
        if (done < n)
            break;
    }
    return sent;
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
    if (type == 0x50) {
        /* EXTLOGPEN narrows its hatch, the style entries move up. */
        EXTLOGPEN native;
        v[1] = sizeof(native);
        v[2] = (ULONG_PTR)&native;
        if (!MxCallService(MIME_SVC_NtGdiExtGetObjectW, v))
            return 0;
        if (!a[2])
            return 24;
        if (a[1] < 24)
            return 0;
        ULONG out[6] = {native.elpPenStyle, native.elpWidth, native.elpBrushStyle, native.elpColor,
                        (ULONG)native.elpHatch, 0};
        memcpy(ptr32(a[2]), out, sizeof(out));
        return 24;
    }
    return (ULONG)MxCallService(MIME_SVC_NtGdiExtGetObjectW, v);
}

/* ------------------------------------------------------------------ raw input */

typedef struct RAWINPUTDEVICE32 {
    USHORT Page, Usage;
    ULONG Flags, Target;
} RAWINPUTDEVICE32;

static ULONG register_raw32(const ULONG *a)
{
    const RAWINPUTDEVICE32 *in = ptr32(a[0]);
    UINT count = a[1];
    if (a[2] != sizeof(RAWINPUTDEVICE32) || !in || !count || count > 64) {
        ULONG64 v[3] = {a[0], a[1], sizeof(RAWINPUTDEVICE) + 1};
        return (ULONG)MxCallService(MIME_SVC_NtUserRegisterRawInputDevices, v);
    }
    RAWINPUTDEVICE devices[64];
    for (UINT i = 0; i < count; i++)
        devices[i] = (RAWINPUTDEVICE){in[i].Page, in[i].Usage, in[i].Flags, ptr32(in[i].Target)};
    ULONG64 v[3] = {(ULONG_PTR)devices, count, sizeof(RAWINPUTDEVICE)};
    return (ULONG)MxCallService(MIME_SVC_NtUserRegisterRawInputDevices, v);
}

static ULONG registered_raw32(const ULONG *a)
{
    RAWINPUTDEVICE devices[64];
    UINT *count = ptr32(a[1]);
    BOOLEAN size_ok = a[2] == sizeof(RAWINPUTDEVICE32);
    UINT have = count ? *count : 0;
    if (have > 64)
        have = 64;
    ULONG64 v[3] = {a[0] ? (ULONG_PTR)devices : 0, a[1], size_ok ? sizeof(RAWINPUTDEVICE) : 0};
    UINT inner = have;
    if (count && a[0])
        v[1] = (ULONG_PTR)&inner;
    ULONG result = (ULONG)MxCallService(MIME_SVC_NtUserGetRegisteredRawInputDevices, v);
    if (count && a[0]) {
        *count = inner;
        if (result != (ULONG)-1) {
            RAWINPUTDEVICE32 *out = ptr32(a[0]);
            for (UINT i = 0; i < result; i++)
                out[i] = (RAWINPUTDEVICE32){devices[i].usUsagePage, devices[i].usUsage, devices[i].dwFlags,
                                            (ULONG)(ULONG_PTR)devices[i].hwndTarget};
        }
    }
    return result;
}

/* the 16-byte x86 header before the same device data. */
static ULONG raw_data32(const ULONG *a)
{
    UINT *size = ptr32(a[3]);
    /* room for a hid packet's reports too. */
    BYTE buffer[256];
    UINT inner = sizeof(buffer);
    ULONG64 v[5] = {a[0], a[1], (ULONG_PTR)buffer, (ULONG_PTR)&inner, a[4] == 16 ? sizeof(RAWINPUTHEADER) : 0};
    ULONG result = (ULONG)MxCallService(MIME_SVC_NtUserGetRawInputData, v);
    if (result == (ULONG)-1)
        return result;
    const RAWINPUTHEADER *h = (const RAWINPUTHEADER *)buffer;
    UINT body = result - sizeof(RAWINPUTHEADER);
    UINT need = a[1] == RID_HEADER ? 16 : 16 + body;
    if (!size)
        return (ULONG)-1;
    if (!a[2]) {
        *size = need;
        return 0;
    }
    if (*size < need) {
        NtCurrentTeb()->LastErrorValue = 122; /* ERROR_INSUFFICIENT_BUFFER */
        return (ULONG)-1;
    }
    ULONG *out = ptr32(a[2]);
    out[0] = h->dwType;
    out[1] = 16 + (ULONG)(h->dwSize - sizeof(RAWINPUTHEADER));
    out[2] = (ULONG)(ULONG_PTR)h->hDevice;
    out[3] = (ULONG)h->wParam;
    if (a[1] != RID_HEADER)
        memcpy(out + 4, buffer + sizeof(RAWINPUTHEADER), body);
    return need;
}

/* windows hands x86 programs this buffer with 64-bit headers, the data 8 bytes later. */
static ULONG raw_buffer32(const ULONG *a)
{
    ULONG64 v[3] = {a[0], a[1], a[2] == 16 ? sizeof(RAWINPUTHEADER) : 0};
    return (ULONG)MxCallService(MIME_SVC_NtUserGetRawInputBuffer, v);
}

static ULONG raw_devices32(const ULONG *a)
{
    RAWINPUTDEVICELIST list[64];
    UINT *count = ptr32(a[1]);
    UINT inner = count ? (*count > 64 ? 64 : *count) : 0;
    ULONG64 v[3] = {a[0] ? (ULONG_PTR)list : 0, count ? (ULONG_PTR)&inner : 0,
                    a[2] == 8 ? sizeof(RAWINPUTDEVICELIST) : 0};
    ULONG result = (ULONG)MxCallService(MIME_SVC_NtUserGetRawInputDeviceList, v);
    if (count)
        *count = inner;
    if (a[0] && result != (ULONG)-1) {
        ULONG *out = ptr32(a[0]);
        for (UINT i = 0; i < result; i++) {
            out[i * 2] = (ULONG)(ULONG_PTR)list[i].hDevice;
            out[i * 2 + 1] = list[i].dwType;
        }
    }
    return result;
}

ULONG Mx32Win32kCall(ULONG number, const ULONG *a)
{
    ULONG64 v[16] = {};
    switch (number) {
#include "x86/win32k_calls.h"
    case MIME_SVC_NtUserInitializeClientPfnArrays: {
        const ULONG *p = ptr32(a[0]);
        if (!p || p[0] != 12 + 4 * (MIME_FNID_COUNT + 1))
            return FALSE;
        MIME_CLIENT_INFO info = {sizeof(info), MIME_CLIENT_WOW, ptr32(p[2])};
        for (ULONG i = 0; i < MIME_FNID_COUNT; i++)
            info.Procs[i] = ptr32(p[i + 3]);
        info.DefWindowProcW = ptr32(p[3 + MIME_FNID_COUNT]);
        v[0] = (ULONG_PTR)&info;
        return (ULONG)MxCallService(number, v);
    }
    /* the vulkan connection: its memory lies below 4 gib in a 32-bit process, so pointers narrow. */
    case MIME_SVC_NtGdiMimeVkOpen: {
        PVOID base = nullptr;
        v[0] = (ULONG_PTR)&base;
        ULONG status = (ULONG)MxCallService(number, v);
        if (NT_SUCCESS(status) && a[0])
            *(PULONG)ptr32(a[0]) = (ULONG)(ULONG_PTR)base;
        return status;
    }
    case MIME_SVC_NtGdiMimeVkAlloc: {
        PVOID base = nullptr;
        v[0] = a[0];
        v[1] = (ULONG_PTR)&base;
        v[2] = a[2];
        ULONG status = (ULONG)MxCallService(number, v);
        if (NT_SUCCESS(status) && a[1])
            *(PULONG)ptr32(a[1]) = (ULONG)(ULONG_PTR)base;
        return status;
    }
    case MIME_SVC_NtGdiMimeGpuOpen: {
        PVOID base = nullptr;
        ULONG size = 0;
        v[0] = (ULONG_PTR)&base;
        v[1] = (ULONG_PTR)&size;
        ULONG status = (ULONG)MxCallService(number, v);
        if (NT_SUCCESS(status) && a[0])
            *(PULONG)ptr32(a[0]) = (ULONG)(ULONG_PTR)base;
        if (NT_SUCCESS(status) && a[1])
            *(PULONG)ptr32(a[1]) = size;
        return status;
    }
    case MIME_SVC_NtGdiMimeGpuAddBlock: {
        PVOID base = nullptr;
        ULONG index = 0;
        v[0] = a[0];
        v[1] = (ULONG_PTR)&base;
        v[2] = (ULONG_PTR)&index;
        ULONG status = (ULONG)MxCallService(number, v);
        if (NT_SUCCESS(status) && a[1])
            *(PULONG)ptr32(a[1]) = (ULONG)(ULONG_PTR)base;
        if (NT_SUCCESS(status) && a[2])
            *(PULONG)ptr32(a[2]) = index;
        return status;
    }
    case MIME_SVC_NtGdiMimeGpuWait:
        v[0] = a[0] | (ULONG64)a[1] << 32;
        v[1] = a[2];
        return (ULONG)MxCallService(number, v);
    case MIME_SVC_NtGdiMimeGpuGetSurface: {
        /* MIME_GPU_SURFACE is a window handle and five longs. */
        MIME_GPU_SURFACE surface = {};
        v[0] = a[0];
        v[1] = (ULONG_PTR)&surface;
        ULONG ok = (ULONG)MxCallService(number, v);
        PULONG out = ptr32(a[1]);
        if (ok && out) {
            out[0] = (ULONG)(ULONG_PTR)surface.Window;
            out[1] = surface.HostWindow;
            out[2] = (ULONG)surface.X;
            out[3] = (ULONG)surface.Y;
            out[4] = surface.Width;
            out[5] = surface.Height;
        }
        return ok;
    }
    case MIME_SVC_NtGdiMimeAudioCreate: {
        PVOID base = nullptr;
        ULONG index = 0;
        v[0] = (ULONG_PTR)&base;
        v[1] = (ULONG_PTR)&index;
        ULONG status = (ULONG)MxCallService(number, v);
        if (NT_SUCCESS(status) && a[0])
            *(PULONG)ptr32(a[0]) = (ULONG)(ULONG_PTR)base;
        if (NT_SUCCESS(status) && a[1])
            *(PULONG)ptr32(a[1]) = index;
        return status;
    }
    case MIME_SVC_NtGdiMimeVkFree:
        v[0] = a[0];
        return (ULONG)MxCallService(number, v);
    case MIME_SVC_NtGdiMimeVkWait:
        v[0] = a[0] | (ULONG64)a[1] << 32;
        v[1] = a[2];
        return (ULONG)MxCallService(number, v);
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
        LPWSTR menu = nullptr;
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
    /* DEVMODEW has no pointers, only the device name is a counted string. */
    case MIME_SVC_NtUserEnumDisplaySettings:
    case MIME_SVC_NtUserChangeDisplaySettings: {
        UNICODE_STRING device = str32(a[0]);
        v[0] = a[0] ? (ULONG_PTR)&device : 0;
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        return (ULONG)MxCallService(number, v);
    }
    /* the x86 layout: memory, format, width, height, pitch, device dc, color table, then dc and bitmap back. */
    case MIME_SVC_NtGdiDdDDICreateDCFromMemory: {
        ULONG *p = ptr32(a[0]);
        if (!p)
            return STATUS_INVALID_PARAMETER;
        D3DKMT_CREATEDCFROMMEMORY desc = {ptr32(p[0]), p[1], p[2], p[3], p[4], ptr32(p[5]), ptr32(p[6])};
        v[0] = (ULONG_PTR)&desc;
        ULONG status = (ULONG)MxCallService(number, v);
        if (NT_SUCCESS(status)) {
            p[7] = (ULONG)(ULONG_PTR)desc.hDc;
            p[8] = (ULONG)(ULONG_PTR)desc.hBitmap;
        }
        return status;
    }
    case MIME_SVC_NtGdiDdDDIDestroyDCFromMemory: {
        const ULONG *p = ptr32(a[0]);
        if (!p)
            return STATUS_INVALID_PARAMETER;
        D3DKMT_DESTROYDCFROMMEMORY desc = {ptr32(p[0]), ptr32(p[1])};
        v[0] = (ULONG_PTR)&desc;
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
        PVOID bits = nullptr;
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
        MSG msg = {};
        v[0] = (ULONG_PTR)&msg;
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        if (number == MIME_SVC_NtUserPeekMessage)
            v[4] = a[4];
        ULONG result = (ULONG)MxCallService(number, v);
        if (result != (ULONG)-1 && (result || number == MIME_SVC_NtUserGetMessage))
            *(MSG32 *)ptr32(a[0]) = (MSG32){(ULONG)(ULONG_PTR)msg.hwnd, msg.message, (ULONG)msg.wParam,
                                          (ULONG)msg.lParam, msg.time, msg.pt};
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
        PAINTSTRUCT ps = {};
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
        /* the window and both parameters are sign extended, as a pointer below 2 GiB is not changed by it and
           a number like -16 stays itself. */
        v[0] = (LONG64)(LONG)a[0];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        UINT msg = a[1];
        /* the structures of the message the 32-bit program points to are made over in the layout of win32k. */
        UCHAR copy[160];
        PVOID user = ptr32(a[3]);
        ULONG fixed = a[5] <= MIME_MSGCALL_SENDCALLBACK && user ? lay_size32(msg) : 0;
        WINDOWPOS32_LAYOUT *pos32 = nullptr;
        BOOLEAN calc = a[5] <= MIME_MSGCALL_SENDCALLBACK && user && msg == WM_NCCALCSIZE && a[2];
        if (fixed) {
            lay_to64(msg, user, copy);
            v[3] = (ULONG_PTR)copy;
        } else if (calc) {
            /* three rectangles and the position they come with, which the program points to. */
            NCCALCSIZE_PARAMS *params = (void *)copy;
            memset(copy, 0, sizeof(copy));
            memcpy(params->rgrc, user, sizeof(params->rgrc));
            pos32 = ptr32(*(ULONG *)((PUCHAR)user + sizeof(params->rgrc)));
            if (pos32) {
                params->lppos = (WINDOWPOS *)(copy + sizeof(*params));
                lay_to64(WM_WINDOWPOSCHANGING, pos32, params->lppos);
            }
            v[3] = (ULONG_PTR)copy;
        } else if (a[5] <= MIME_MSGCALL_SENDCALLBACK && user && msg == WM_COPYDATA) {
            const ULONG *cd = user;
            *(COPYDATASTRUCT *)copy = (COPYDATASTRUCT){cd[0], cd[1], ptr32(cd[2])};
            v[3] = (ULONG_PTR)copy;
        } else if (a[5] <= MIME_MSGCALL_SENDCALLBACK && user && msg == WM_MDICREATE) {
            const ULONG *m = user;
            *(MDICREATESTRUCTW *)copy = (MDICREATESTRUCTW){ptr32(m[0]), ptr32(m[1]), lay_handle(m[2]), (int)m[3], (int)m[4],
                                                           (int)m[5], (int)m[6], m[7], (LONG_PTR)(LONG)m[8]};
            v[3] = (ULONG_PTR)copy;
        }
        MIME_SEND_TIMEOUT timeout;
        MIME_SEND_CALLBACK callback;
        MIME_BROADCAST broadcast;
        if (a[5] == MIME_MSGCALL_SENDTIMEOUT && a[4]) {
            const ULONG *p = ptr32(a[4]);
            timeout = (MIME_SEND_TIMEOUT){p[0], p[1], p[2]};
            v[4] = (ULONG_PTR)&timeout;
        } else if (a[5] == MIME_MSGCALL_SENDCALLBACK && a[4]) {
            const ULONG *p = ptr32(a[4]);
            callback = (MIME_SEND_CALLBACK){ptr32(p[0]), p[1]};
            v[4] = (ULONG_PTR)&callback;
        } else if (a[5] == MIME_MSGCALL_BROADCAST && a[4]) {
            const ULONG *p = ptr32(a[4]);
            broadcast = (MIME_BROADCAST){p[0], p[1], lay_handle(p[2]), lay_handle(p[3])};
            v[4] = (ULONG_PTR)&broadcast;
        }
        ULONG result = (ULONG)MxCallService(number, v);
        if (fixed && lay_writes(msg)) {
            lay_to32(msg, copy, user);
        } else if (calc && pos32) {
            const NCCALCSIZE_PARAMS *params = (const void *)copy;
            memcpy(user, params->rgrc, sizeof(params->rgrc));
            lay_to32(WM_WINDOWPOSCHANGING, params->lppos, pos32);
        } else if (calc) {
            memcpy(user, copy, 3 * sizeof(RECT));
        }
        if (a[5] == MIME_MSGCALL_SENDTIMEOUT && a[4])
            ((ULONG *)ptr32(a[4]))[2] = (ULONG)timeout.Result;
        else if (a[5] == MIME_MSGCALL_BROADCAST && a[4]) {
            ULONG *p = ptr32(a[4]);
            p[0] = broadcast.Recipients;
            p[2] = lay_narrow(broadcast.Hwnd);
            p[3] = lay_narrow(broadcast.Desktop);
        }
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
    case MIME_SVC_NtUserUnregisterClass:
    case MIME_SVC_NtUserDefSetText: {
        BOOLEAN text = number == MIME_SVC_NtUserDefSetText;
        UNICODE_STRING name = str32(a[text]);
        v[0] = a[0];
        v[1] = a[1];
        v[text] = a[text] ? (ULONG_PTR)&name : 0;
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtUserGetClassName: {
        UNICODE_STRING name = str32(a[2]);
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2] ? (ULONG_PTR)&name : 0;
        ULONG result = (ULONG)MxCallService(number, v);
        set_length32(a[2], &name);
        return result;
    }
    case MIME_SVC_NtUserBuildHwndList:
    case MIME_SVC_NtUserBuildPropList:
        return handle_list32(number, a);
    case MIME_SVC_NtUserSendInput:
        return send_input32(a);
    case MIME_SVC_NtUserGetIconInfo: {
        ICONINFO info = {};
        UNICODE_STRING module = str32(a[2]), resource = str32(a[3]);
        v[0] = a[0];
        v[1] = a[1] ? (ULONG_PTR)&info : 0;
        v[2] = a[2] ? (ULONG_PTR)&module : 0;
        v[3] = a[3] ? (ULONG_PTR)&resource : 0;
        v[4] = a[4];
        v[5] = a[5];
        ULONG result = (ULONG)MxCallService(number, v);
        if (result && a[1]) {
            ULONG out[5] = {info.fIcon, info.xHotspot, info.yHotspot, (ULONG)(ULONG_PTR)info.hbmMask,
                            (ULONG)(ULONG_PTR)info.hbmColor};
            memcpy(ptr32(a[1]), out, sizeof(out));
        }
        if (result) {
            set_length32(a[2], &module);
            set_length32(a[3], &resource);
        }
        return result;
    }
    case MIME_SVC_NtUserThunkedMenuInfo: {
        /* the 28-byte x86 MENUINFO. */
        const ULONG *p = ptr32(a[1]);
        if (!p || p[0] != 28)
            return FALSE;
        MENUINFO info = {sizeof(info), p[1], p[2], p[3], ptr32(p[4]), p[5], p[6]};
        v[0] = a[0];
        v[1] = (ULONG_PTR)&info;
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtUserMimeGetMenuItemInfo: {
        ULONG *p = ptr32(a[3]);
        MENUITEMINFOW info;
        if (!menu_item64(p, &info))
            return FALSE;
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = (ULONG_PTR)&info;
        ULONG result = (ULONG)MxCallService(number, v);
        if (result) {
            ULONG out[12] = {p[0], info.fMask, info.fType, info.fState, info.wID, (ULONG)(ULONG_PTR)info.hSubMenu,
                             (ULONG)(ULONG_PTR)info.hbmpChecked, (ULONG)(ULONG_PTR)info.hbmpUnchecked,
                             (ULONG)info.dwItemData, (ULONG)(ULONG_PTR)info.dwTypeData, info.cch,
                             (ULONG)(ULONG_PTR)info.hbmpItem};
            memcpy(p, out, p[0]);
        }
        return result;
    }
    case MIME_SVC_NtUserGetCursorInfo: {
        /* the 20-byte x86 CURSORINFO. */
        ULONG *p = ptr32(a[0]);
        if (!p || p[0] != 20)
            return FALSE;
        CURSORINFO info = {sizeof(info)};
        v[0] = (ULONG_PTR)&info;
        ULONG result = (ULONG)MxCallService(number, v);
        if (result) {
            p[1] = info.flags;
            p[2] = (ULONG)(ULONG_PTR)info.hCursor;
            p[3] = (ULONG)info.ptScreenPos.x;
            p[4] = (ULONG)info.ptScreenPos.y;
        }
        return result;
    }
    case MIME_SVC_NtUserSetClipboardData:
    case MIME_SVC_NtUserGetClipboardData: {
        /* the 16-byte x86 MIME_CLIPBOARD_DATA. */
        BOOLEAN get = number == MIME_SVC_NtUserGetClipboardData;
        ULONG *p = ptr32(a[get ? 1 : 2]);
        MIME_CLIPBOARD_DATA data = {};
        if (p)
            data = (MIME_CLIPBOARD_DATA){ptr32(p[0]), p[1], p[2], p[3]};
        v[0] = a[0];
        v[1] = a[1];
        v[get ? 1 : 2] = p ? (ULONG_PTR)&data : 0;
        ULONG result = (ULONG)MxCallService(number, v);
        if (get && result && p) {
            p[1] = (ULONG)data.Size;
            p[2] = data.Sequence;
            p[3] = data.Gdi;
        }
        return result;
    }
    case MIME_SVC_NtGdiOpenDCW: {
        /* DEVMODEW holds no pointers. */
        UNICODE_STRING device = str32(a[0]), port = str32(a[2]);
        for (ULONG i = 0; i < 8; i++)
            v[i] = a[i];
        v[0] = a[0] ? (ULONG_PTR)&device : 0;
        v[2] = a[2] ? (ULONG_PTR)&port : 0;
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtGdiGetOutlineTextMetricsInternalW:
        return outline_metrics32(a);
    case MIME_SVC_NtUserMsgWaitForMultipleObjectsEx: {
        HANDLE handles[64];
        const ULONG *in = ptr32(a[1]);
        ULONG count = a[0] < 64 ? a[0] : 64;
        for (ULONG i = 0; in && i < count; i++)
            handles[i] = (HANDLE)(LONG_PTR)(LONG)in[i];
        v[0] = a[0];
        v[1] = in ? (ULONG_PTR)handles : 0;
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtUserRegisterRawInputDevices:
        return register_raw32(a);
    case MIME_SVC_NtUserGetRegisteredRawInputDevices:
        return registered_raw32(a);
    case MIME_SVC_NtUserGetRawInputData:
        return raw_data32(a);
    case MIME_SVC_NtUserGetRawInputBuffer:
        return raw_buffer32(a);
    case MIME_SVC_NtUserGetRawInputDeviceList:
        return raw_devices32(a);
    case MIME_SVC_NtUserGetRawInputDeviceInfo:
        /* the names and RID_DEVICE_INFO are the same in both. */
        for (ULONG i = 0; i < 4; i++)
            v[i] = a[i];
        return (ULONG)MxCallService(number, v);
    case MIME_SVC_NtUserCreateWindowStation:
    case MIME_SVC_NtUserCreateWindowStationEx:
    case MIME_SVC_NtUserOpenWindowStation:
    case MIME_SVC_NtUserCreateDesktopEx:
    case MIME_SVC_NtUserOpenDesktop: {
        /* the window station and desktop calls take object attributes first, then scalars. */
        UNICODE_STRING name, device;
        OBJECT_ATTRIBUTES oa = oa32(a[0], &name);
        v[0] = a[0] ? (ULONG_PTR)&oa : 0;
        for (ULONG i = 1; i < 7; i++)
            v[i] = a[i];
        if (number == MIME_SVC_NtUserCreateDesktopEx && a[1]) {
            device = str32(a[1]);
            v[1] = (ULONG_PTR)&device;
        }
        return (ULONG)MxCallService(number, v);
    }
    case MIME_SVC_NtUserGetObjectInformation:
    case MIME_SVC_NtUserSetObjectInformation:
    case MIME_SVC_NtUserBuildNameList:
        /* object information and name lists hold no pointers. */
        v[0] = (LONG64)(LONG)a[0];
        for (ULONG i = 1; i < 5; i++)
            v[i] = a[i];
        return (ULONG)MxCallService(number, v);
    default:
        Mx32Unsupported(number);
        return 0;
    }
}
