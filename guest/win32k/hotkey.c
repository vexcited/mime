#include "win32k.h"

typedef struct HOTKEY {
    LIST_ENTRY Links;
    W32THREAD *Thread;
    HWND Hwnd;
    int Id;
    UINT Modifiers;
    UINT Vk;
} HOTKEY;

#define MOD_KEYS (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN)

static LIST_ENTRY g_hotkeys = {&g_hotkeys, &g_hotkeys};
/* the lParam of the hot key reported last, which MOD_NOREPEAT does not report again until a key goes up. */
static LPARAM g_last;

/* the rules below are those of win32kfull's _RegisterHotKey, FindHotKey and xxxDoHotKeyStuff. */
BOOLEAN HotkeyRegister(W32THREAD *t, HWND hwnd, int id, UINT modifiers, UINT vk)
{
    if (!t)
        return FALSE;
    if (modifiers & 0xFFFF95B0) {
        W32SetLastError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    /* the flags of the shell and of the input manager. */
    if (modifiers & 0x2840) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    WND *w = hwnd ? WndLookup(hwnd) : nullptr;
    if (hwnd && !w) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (vk == VK_PACKET)
        return FALSE;
    if (w && w->Thread != t) {
        W32SetLastError(ERROR_WINDOW_OF_OTHER_THREAD);
        return FALSE;
    }
    HOTKEY *same = nullptr;
    for (PLIST_ENTRY e = g_hotkeys.Flink; e != &g_hotkeys; e = e->Flink) {
        HOTKEY *hk = CONTAINING_RECORD(e, HOTKEY, Links);
        /* a combination is taken even by the hot key being registered again. */
        if (hk->Vk == vk && !((hk->Modifiers ^ modifiers) & MOD_KEYS)) {
            W32SetLastError(ERROR_HOTKEY_ALREADY_REGISTERED);
            return FALSE;
        }
        /* windows looks for the identifier in the hash bucket of the new key only: elsewhere the old hot key stays. */
        if (hk->Thread == t && hk->Hwnd == hwnd && hk->Id == id && !((hk->Vk ^ vk) & 0x7F))
            same = hk;
    }
    if (!same) {
        same = W32Alloc(sizeof(*same));
        if (!same) {
            W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        same->Thread = t;
        same->Hwnd = hwnd;
        same->Id = id;
        InsertTailList(&g_hotkeys, &same->Links);
    }
    same->Modifiers = modifiers;
    same->Vk = vk;
    g_last = 0;
    return TRUE;
}

static BOOLEAN remove_matching(W32THREAD *t, HWND hwnd, const int *id)
{
    BOOLEAN found = FALSE;
    for (PLIST_ENTRY e = g_hotkeys.Flink; e != &g_hotkeys;) {
        HOTKEY *hk = CONTAINING_RECORD(e, HOTKEY, Links);
        e = e->Flink;
        if ((t && hk->Thread != t) || (id ? hk->Hwnd != hwnd || hk->Id != *id : !t && hk->Hwnd != hwnd))
            continue;
        RemoveEntryList(&hk->Links);
        W32Free(hk);
        found = TRUE;
    }
    return found;
}

BOOLEAN HotkeyUnregister(W32THREAD *t, HWND hwnd, int id)
{
    if (hwnd && !WndLookup(hwnd)) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (!t || !remove_matching(t, hwnd, &id)) {
        W32SetLastError(ERROR_HOTKEY_NOT_REGISTERED);
        return FALSE;
    }
    g_last = 0;
    return TRUE;
}

void HotkeyRemoveWindow(WND *w)
{
    remove_matching(nullptr, WndHandle(w), nullptr);
}

void HotkeyRemoveThread(W32THREAD *t)
{
    remove_matching(t, nullptr, nullptr);
}

/* TRUE when the key press is a hot key, which no window gets as a key. the release is an ordinary one. */
BOOLEAN HotkeyInput(const MimeInputEvent *ev)
{
    if (ev->type == MIME_INPUT_KEY_UP) {
        g_last = 0;
        return FALSE;
    }
    UINT modifiers = ((ev->modifiers & MIME_MOD_ALT) ? MOD_ALT : 0) | ((ev->modifiers & MIME_MOD_CONTROL) ? MOD_CONTROL : 0) |
                     ((ev->modifiers & MIME_MOD_SHIFT) ? MOD_SHIFT : 0) | ((ev->modifiers & MIME_MOD_WIN) ? MOD_WIN : 0);
    for (PLIST_ENTRY e = g_hotkeys.Flink; e != &g_hotkeys; e = e->Flink) {
        HOTKEY *hk = CONTAINING_RECORD(e, HOTKEY, Links);
        if (hk->Vk != ev->vk || (hk->Modifiers & MOD_KEYS) != modifiers)
            continue;
        if (RawInputNoHotkeys())
            return FALSE;
        LPARAM lparam = MAKELPARAM(modifiers, hk->Vk);
        if (!(hk->Modifiers & MOD_NOREPEAT) || g_last != lparam)
            QueuePost(hk->Thread, hk->Hwnd, WM_HOTKEY, (WPARAM)(LONG_PTR)hk->Id, lparam);
        g_last = lparam;
        return TRUE;
    }
    return FALSE;
}
