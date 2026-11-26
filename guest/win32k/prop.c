/* window properties use atom keys. user32 interns string keys as global atoms. */
#include "win32k.h"

/* frame.c keeps the regions of DwmEnableBlurBehindWindow beside the windows. */
void FrameSetBlurRegion(WND *w, const REGION *region);
void FrameForgetBlurRegion(WND *w);

static WND_PROP *find_prop(WND *w, ATOM atom)
{
    for (ULONG i = 0; i < w->PropCount; i++)
        if (w->Props[i].Atom == atom)
            return &w->Props[i];
    return nullptr;
}

void PropFreeWindow(WND *w)
{
    FrameForgetBlurRegion(w);
    W32Free(w->Props);
    w->Props = nullptr;
    w->PropCount = 0;
    w->PropCapacity = 0;
}

/* user32 names properties with global atoms, which the kernel keeps. */
ATOM PropAtom(PCWSTR name)
{
    return MimeFindGlobalAtom(name);
}

HANDLE PropGetData(WND *w, ATOM atom)
{
    WND_PROP *p = atom ? find_prop(w, atom) : nullptr;
    return p ? p->Data : nullptr;
}

/* DWM properties stored by dwmapi affect the host window. */
static void prop_changed(WND *w, ATOM atom)
{
    if (!WndIsTopLevel(w))
        return;
    if (atom == PropAtom(L"DwmCorner") || atom == PropAtom(L"DwmDarkMode") ||
        atom == PropAtom(L"DwmBorderColor") || atom == PropAtom(L"DwmCaptionColor") ||
        atom == PropAtom(L"DwmTextColor")) {
        if (atom != PropAtom(L"DwmCorner"))
            w->Flags |= WNDF_FRAME_DIRTY;
        WndUpdateHostConfig(w, FALSE);
        if (w->Thread)
            QueueWake(w->Thread, QS_PAINT);
    }
    /* dwmapi hands over the region of blur behind, which the program may delete as soon as the call returns. */
    if (atom == PropAtom(L"DwmBlurRegion")) {
        HANDLE handle = PropGetData(w, atom);
        GDI_REGION *r = handle ? (GDI_REGION *)GdiLookup(handle, GDI_TYPE_REGION) : nullptr;
        FrameSetBlurRegion(w, r ? &r->Region : nullptr);
    }
    /* the frame under the client area changed: the whole window is composed again. */
    if ((atom == PropAtom(L"DwmMarginsLR") || atom == PropAtom(L"DwmMarginsTB") ||
         atom == PropAtom(L"DwmBlurBehind") || atom == PropAtom(L"DwmBlurRegion")) &&
        w->Surface) {
        FrameSurfaceAlpha(w);
        RECT all = {0, 0, w->Surface->Width, w->Surface->Height};
        SurfaceMarkDirty(w->Surface, &all);
        if (w->Thread == W32GetThread() && (w->Flags & WNDF_PRESENTED))
            PaintPresent(w);
        else if (w->Thread)
            QueueWake(w->Thread, QS_PAINT);
    }
}

BOOL NTAPI NtUserSetProp(HWND hwnd, ATOM atom, HANDLE data)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = FALSE;
    ULONG error = w ? (atom ? 0 : ERROR_INVALID_PARAMETER) : ERROR_INVALID_WINDOW_HANDLE;
    if (!error) {
        WND_PROP *p = find_prop(w, atom);
        if (!p && w->PropCount == w->PropCapacity) {
            ULONG cap = w->PropCapacity ? w->PropCapacity * 2 : 4;
            WND_PROP *props = W32Alloc(cap * sizeof(WND_PROP));
            if (props) {
                if (w->PropCount)
                    memcpy(props, w->Props, w->PropCount * sizeof(WND_PROP));
                W32Free(w->Props);
                w->Props = props;
                w->PropCapacity = cap;
            }
        }
        if (!p && w->PropCount < w->PropCapacity) {
            p = &w->Props[w->PropCount++];
            p->Atom = atom;
        }
        if (p) {
            p->Data = data;
            ok = TRUE;
            prop_changed(w, atom);
        } else {
            error = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    UserLeave();
    if (error)
        W32SetLastError(error);
    return ok;
}

HANDLE NTAPI NtUserGetProp(HWND hwnd, ATOM atom)
{
    UserEnterRead();
    WND *w = WndLookup(hwnd);
    WND_PROP *p = w ? find_prop(w, atom) : nullptr;
    HANDLE data = p ? p->Data : nullptr;
    UserLeave();
    return data;
}

/*
 * SetPropW and GetPropW of user32 26300 pass a name as it is, and the kernel finds or makes its global atom: the
 * same one GlobalFindAtom gives RemoveProp.
 */
BOOL NTAPI NtUserSetProp2(HWND hwnd, PUNICODE_STRING user_name, HANDLE data)
{
    PWSTR name = nullptr;
    if (!NT_SUCCESS(W32CaptureString(user_name, &name, nullptr)) || !name) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ATOM atom = MimeAddGlobalAtom(name);
    W32Free(name);
    if (!atom) {
        W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    return NtUserSetProp(hwnd, atom, data);
}

HANDLE NTAPI NtUserGetProp2(HWND hwnd, PUNICODE_STRING user_name)
{
    PWSTR name = nullptr;
    if (!NT_SUCCESS(W32CaptureString(user_name, &name, nullptr)) || !name)
        return nullptr;
    ATOM atom = MimeFindGlobalAtom(name);
    W32Free(name);
    return atom ? NtUserGetProp(hwnd, atom) : nullptr;
}

HANDLE NTAPI NtUserRemoveProp(HWND hwnd, ATOM atom)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    WND_PROP *p = w ? find_prop(w, atom) : nullptr;
    HANDLE data = nullptr;
    if (p) {
        data = p->Data;
        ULONG i = (ULONG)(p - w->Props);
        memmove(&w->Props[i], &w->Props[i + 1], (w->PropCount - i - 1) * sizeof(WND_PROP));
        w->PropCount--;
        prop_changed(w, atom);
    }
    UserLeave();
    return data;
}

NTSTATUS NTAPI NtUserBuildPropList(HWND hwnd, ULONG max, PROPLISTITEM *user_list, ULONG *user_count)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    ULONG count = w ? w->PropCount : 0;
    PROPLISTITEM *copy = nullptr;
    NTSTATUS status = w ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
    if (w && count) {
        copy = W32Alloc(count * sizeof(PROPLISTITEM));
        if (copy)
            for (ULONG i = 0; i < count; i++) {
                copy[i].Atom = w->Props[i].Atom;
                copy[i].Data = w->Props[i].Data;
            }
        else
            status = STATUS_NO_MEMORY;
    }
    UserLeave();
    if (NT_SUCCESS(status) && count > max)
        status = STATUS_BUFFER_TOO_SMALL;
    if (NT_SUCCESS(status) && count && !NT_SUCCESS(MimeCopyToUser(user_list, copy, count * sizeof(PROPLISTITEM))))
        status = STATUS_ACCESS_VIOLATION;
    if ((NT_SUCCESS(status) || status == STATUS_BUFFER_TOO_SMALL) && user_count &&
        !NT_SUCCESS(MimeCopyToUser(user_count, &count, sizeof(count))))
        status = STATUS_ACCESS_VIOLATION;
    W32Free(copy);
    return status;
}
