/*
 * SendKeys: the text of keystrokes that a script gives, with the modifiers + ^ %, groups in parentheses, braces for keys
 * with names and repeat counts, and ~ for enter. The keys are sent to the window that has the focus.
 */
#include "wshom_private.h"

static const struct {
    const WCHAR *Name;
    BYTE Vk;
} g_keywords[] = {
    {L"BACKSPACE", VK_BACK}, {L"BS", VK_BACK}, {L"BKSP", VK_BACK}, {L"BREAK", VK_CANCEL}, {L"CAPSLOCK", VK_CAPITAL},
    {L"DELETE", VK_DELETE}, {L"DEL", VK_DELETE}, {L"DOWN", VK_DOWN}, {L"END", VK_END}, {L"ENTER", VK_RETURN},
    {L"ESC", VK_ESCAPE}, {L"HELP", VK_HELP}, {L"HOME", VK_HOME}, {L"INSERT", VK_INSERT}, {L"INS", VK_INSERT},
    {L"LEFT", VK_LEFT}, {L"NUMLOCK", VK_NUMLOCK}, {L"PGDN", VK_NEXT}, {L"PGUP", VK_PRIOR}, {L"PRTSC", VK_SNAPSHOT},
    {L"RIGHT", VK_RIGHT}, {L"SCROLLLOCK", VK_SCROLL}, {L"TAB", VK_TAB}, {L"UP", VK_UP},
    {L"F1", VK_F1}, {L"F2", VK_F2}, {L"F3", VK_F3}, {L"F4", VK_F4}, {L"F5", VK_F5}, {L"F6", VK_F6}, {L"F7", VK_F7},
    {L"F8", VK_F8}, {L"F9", VK_F9}, {L"F10", VK_F10}, {L"F11", VK_F11}, {L"F12", VK_F12}, {L"F13", VK_F13},
    {L"F14", VK_F14}, {L"F15", VK_F15}, {L"F16", VK_F16},
};

/* the keys that are held while the keys of a group go, and the ones sent so far. */
typedef struct KEYS {
    const WCHAR *Text;
    BOOL Shift, Control, Alt;
} KEYS;

static void key_event(BYTE vk, BOOL release)
{
    DWORD flags = release ? KEYEVENTF_KEYUP : 0;
    if (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_HOME || vk == VK_END ||
        vk == VK_PRIOR || vk == VK_NEXT || vk == VK_INSERT || vk == VK_DELETE)
        flags |= KEYEVENTF_EXTENDEDKEY;
    keybd_event(vk, (BYTE)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC), flags, 0);
}

static void modifiers(const KEYS *keys, BOOL release)
{
    if (!release) {
        if (keys->Shift)
            key_event(VK_SHIFT, FALSE);
        if (keys->Control)
            key_event(VK_CONTROL, FALSE);
        if (keys->Alt)
            key_event(VK_MENU, FALSE);
    } else {
        if (keys->Alt)
            key_event(VK_MENU, TRUE);
        if (keys->Control)
            key_event(VK_CONTROL, TRUE);
        if (keys->Shift)
            key_event(VK_SHIFT, TRUE);
    }
}

/* one key with the modifiers asked for and the ones its character needs. */
static void press_key(BYTE vk, BOOL need_shift, const KEYS *keys, int times)
{
    for (int i = 0; i < times; i++) {
        modifiers(keys, FALSE);
        if (need_shift && !keys->Shift)
            key_event(VK_SHIFT, FALSE);
        key_event(vk, FALSE);
        key_event(vk, TRUE);
        if (need_shift && !keys->Shift)
            key_event(VK_SHIFT, TRUE);
        modifiers(keys, TRUE);
    }
}

static void press_character(WCHAR c, const KEYS *keys, int times)
{
    SHORT scan = VkKeyScanW(c);
    if (scan == -1)
        return;
    BYTE vk = (BYTE)scan, state = (BYTE)(scan >> 8);
    KEYS extra = *keys;
    if ((state & 2) && !extra.Control)
        extra.Control = TRUE;
    if ((state & 4) && !extra.Alt)
        extra.Alt = TRUE;
    press_key(vk, (state & 1) != 0, &extra, times);
}

static HRESULT parse(KEYS *keys, const WCHAR **cursor, BOOL in_group);

/* the commands in braces: a key name or a character, with a repeat count after a space. */
static HRESULT parse_braces(KEYS *keys, const WCHAR **cursor)
{
    const WCHAR *p = *cursor + 1;
    const WCHAR *end = p;
    if (*end == '}')
        end++;
    while (*end && *end != '}')
        end++;
    if (*end != '}')
        return E_INVALIDARG;
    const WCHAR *space = p;
    while (space < end && *space != ' ')
        space++;
    int length = (int)(space - p);
    int times = 1;
    if (space < end) {
        times = 0;
        for (const WCHAR *d = space + 1; d < end; d++) {
            if (*d < '0' || *d > '9')
                return E_INVALIDARG;
            times = times * 10 + (*d - '0');
        }
    }
    BOOL matched = FALSE;
    for (int i = 0; i < (int)(sizeof(g_keywords) / sizeof(g_keywords[0])) && length > 1; i++) {
        if ((int)wcslen(g_keywords[i].Name) == length && !_wcsnicmp(p, g_keywords[i].Name, length)) {
            press_key(g_keywords[i].Vk, FALSE, keys, times);
            matched = TRUE;
            break;
        }
    }
    if (!matched) {
        if (length != 1)
            return E_INVALIDARG;
        press_character(*p, keys, times);
    }
    *cursor = end + 1;
    return S_OK;
}

static HRESULT parse(KEYS *keys, const WCHAR **cursor, BOOL in_group)
{
    KEYS pending = {nullptr, FALSE, FALSE, FALSE};
    const WCHAR *p = *cursor;
    while (*p) {
        WCHAR c = *p;
        if (c == ')') {
            if (!in_group)
                return E_INVALIDARG;
            *cursor = p + 1;
            return S_OK;
        }
        KEYS effective = *keys;
        effective.Shift |= pending.Shift;
        effective.Control |= pending.Control;
        effective.Alt |= pending.Alt;
        BOOL used = TRUE;
        switch (c) {
        case '+':
            pending.Shift = TRUE;
            p++;
            used = FALSE;
            break;
        case '^':
            pending.Control = TRUE;
            p++;
            used = FALSE;
            break;
        case '%':
            pending.Alt = TRUE;
            p++;
            used = FALSE;
            break;
        case '~':
            press_key(VK_RETURN, FALSE, &effective, 1);
            p++;
            break;
        case '(': {
            p++;
            modifiers(&effective, FALSE);
            KEYS group = {nullptr, FALSE, FALSE, FALSE};
            HRESULT hr = parse(&group, &p, TRUE);
            modifiers(&effective, TRUE);
            if (FAILED(hr))
                return hr;
            break;
        }
        case '{': {
            HRESULT hr = parse_braces(&effective, &p);
            if (FAILED(hr))
                return hr;
            break;
        }
        case '}':
            return E_INVALIDARG;
        default:
            press_character(c, &effective, 1);
            p++;
            break;
        }
        if (used)
            pending = (KEYS){nullptr, FALSE, FALSE, FALSE};
    }
    if (in_group)
        return E_INVALIDARG;
    *cursor = p;
    return S_OK;
}

HRESULT WshSendKeys(const WCHAR *text, BOOL wait)
{
    KEYS keys = {text, FALSE, FALSE, FALSE};
    const WCHAR *cursor = text;
    HRESULT hr = parse(&keys, &cursor, FALSE);
    /* no modifier is left held after a failure. */
    key_event(VK_SHIFT, TRUE);
    key_event(VK_CONTROL, TRUE);
    key_event(VK_MENU, TRUE);
    return hr;
}
