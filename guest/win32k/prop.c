/*
 * window properties: atom keyed handles attached to a window. string keys
 * are global atoms added by user32, like windows.
 */
#include "win32k.h"

static WND_PROP *find_prop(WND *w, ATOM atom)
{
    for (ULONG i = 0; i < w->PropCount; i++)
        if (w->Props[i].Atom == atom)
            return &w->Props[i];
    return nullptr;
}

void PropFreeWindow(WND *w)
{
    W32Free(w->Props);
    w->Props = nullptr;
    w->PropCount = 0;
    w->PropCapacity = 0;
}

HANDLE PropGetData(WND *w, ATOM atom)
{
    WND_PROP *p = atom ? find_prop(w, atom) : nullptr;
    return p ? p->Data : nullptr;
}

/* the dwm attributes dwmapi stores as properties change how the host shows the window. */
static void prop_changed(WND *w, ATOM atom)
{
    if (atom == AtomFind(L"MimeDwmCorner") && WndIsTopLevel(w))
        WndUpdateHostConfig(w, FALSE);

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
    UserEnter();
    WND *w = WndLookup(hwnd);
    WND_PROP *p = w ? find_prop(w, atom) : nullptr;
    HANDLE data = p ? p->Data : nullptr;
    UserLeave();
    return data;
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
