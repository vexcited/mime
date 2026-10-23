/*
 * drops from host programs onto windows of this process. win32k posts WM_MIME_HOSTDND to the target window's thread
 * and DispatchMessage runs it: ole32 handles windows with an IDropTarget, this file handles windows that only have
 * WS_EX_ACCEPTFILES by sending WM_DROPFILES with a DROPFILES block.
 */
#include "user32_private.h"
#include <mime/abi.h>

typedef void(WINAPI *DND_HANDLER)(HWND hwnd, LPARAM lparam);

#ifndef DROPEFFECT_COPY
#define DROPEFFECT_COPY 1
#endif

#ifndef CF_HDROP
#define CF_HDROP 15
#endif

/* the drag's DROPFILES block in program-owned global memory, or NULL when it has no files. */
static HGLOBAL fetch_files(ULONG session, HWND hwnd, const POINT *screen)
{
    ULONG total = 0;
    UCHAR first[4096];
    if (!NtUserMimeDndData(session, CF_HDROP, first, sizeof(first), 0, &total) || total < 20 || total > (64u << 20))
        return nullptr;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, total);
    UCHAR *block = memory ? GlobalLock(memory) : nullptr;
    if (!block) {
        if (memory)
            GlobalFree(memory);
        return nullptr;
    }
    memcpy(block, first, total < sizeof(first) ? total : sizeof(first));
    for (ULONG offset = sizeof(first); offset < total;) {
        ULONG n = total - offset < 65536 ? total - offset : 65536;
        ULONG again = 0;
        if (!NtUserMimeDndData(session, CF_HDROP, block + offset, n, offset, &again) || again != total) {
            GlobalUnlock(memory);
            GlobalFree(memory);
            return nullptr;
        }
        offset += n;
    }
    /* pt in client coordinates and fNC when the drop is outside the client area. */
    POINT client = *screen;
    ScreenToClient(hwnd, &client);
    RECT area;
    GetClientRect(hwnd, &area);
    LONG *at = (LONG *)(block + 4);
    at[0] = client.x;
    at[1] = client.y;
    at[2] = !PtInRect(&area, client);
    GlobalUnlock(memory);
    return memory;
}

static void reply(ULONG session, ULONG phase, ULONG effect)
{
    NtUserMimeDnd(MIME_DND_REPLY, session, phase << 8 | effect, nullptr);
}

/* without an IDropTarget, a WS_EX_ACCEPTFILES window only takes the dragged files. */
static void files_window(HWND hwnd, LPARAM lparam)
{
    ULONG phase = (ULONG)lparam & 15, session = (ULONG)lparam >> 4;
    ULONG state[MIME_DND_STATE_SIZE];
    if (phase == MIME_DND_PHASE_LEAVE || !NtUserMimeDnd(MIME_DND_STATE, session, 0, state))
        return;
    bool files = (state[0] & MIME_CLIP_HDROP) != 0;
    if (phase != MIME_DND_PHASE_DROP) {
        reply(session, phase, files && (state[1] & DROPEFFECT_COPY) ? DROPEFFECT_COPY : 0);
        return;
    }
    POINT screen = {(LONG)state[3], (LONG)state[4]};
    HGLOBAL drop = files ? fetch_files(session, hwnd, &screen) : nullptr;
    if (drop && !PostMessageW(hwnd, WM_DROPFILES, (WPARAM)drop, 0)) {
        GlobalFree(drop);
        drop = nullptr;
    }
    reply(session, phase, drop ? DROPEFFECT_COPY : 0);
}

void UserHostDnd(const MSG *msg)
{
    if (msg->wParam)
        ((DND_HANDLER)msg->wParam)(msg->hwnd, msg->lParam);
    else
        files_window(msg->hwnd, msg->lParam);
}
