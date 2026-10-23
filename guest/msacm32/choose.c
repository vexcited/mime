/*
 * the format and filter choosers: windows' "Sound Selection" and "Filter
 * Selection" dialogs, a name, a tag and its attributes, with the names a
 * user saved under HKCU\Software\Microsoft\Multimedia\Audio.
 */
#include "msacm32_private.h"

WINUSERAPI LONG WINAPI TabbedTextOutW(HDC dc, int x, int y, LPCWSTR text, int count, int tabs, const INT *positions,
                                      int origin);
WINUSERAPI UINT WINAPI RegisterWindowMessageW(LPCWSTR name);

#define ID_NAME 100
#define ID_TAG 101
#define ID_ITEM 102
#define ID_SAVE 103
#define ID_REMOVE 104
#define ID_HELP 9
#define ID_SAVE_TEXT 101
#define ID_SAVE_EDIT 100

#define AUDIO_KEY L"Software\\Microsoft\\Multimedia\\Audio"
#define PROP L"MSACM Chooser Prop"

typedef UINT(CALLBACK *HOOK)(HWND, UINT, WPARAM, LPARAM);

typedef struct CHOOSER {
    BOOL filter;
    DWORD style;
    HWND owner;
    LPCWSTR title;
    LPWSTR name;
    DWORD name_size;
    DWORD enum_flags;
    const void *enum_item;    /* pwfxEnum or pwfltrEnum. */
    HOOK hook;
    LPARAM custom;
    const void *initial;      /* the caller's format or filter, with INITTO. */
    HWND dialog, names, tags, items, ok, cancel, help, save, remove;
    int tab;
    UINT help_message;
    void *chosen;             /* our copy of the selection at OK. */
    WCHAR chosen_name[128];
} CHOOSER;

static HANDLE heap(void)
{
    return GetProcessHeap();
}

static DWORD item_size(const CHOOSER *c, const void *item)
{
    return c->filter ? ((const WAVEFILTER *)item)->cbStruct : AcmFormatSize(item);
}

static void *copy_item(const CHOOSER *c, const void *item)
{
    DWORD size = item_size(c, item);
    void *copy = HeapAlloc(heap(), 0, size);
    if (copy)
        memcpy(copy, item, size);
    return copy;
}

static BOOL same_item(const CHOOSER *c, const void *a, const void *b)
{
    DWORD size = item_size(c, a);
    return size == item_size(c, b) && !memcmp(a, b, size);
}

/* ------------------------------------------------------------------ the template */

typedef struct TEMPLATE {
    WORD *p;
    WORD data[1024];
} TEMPLATE;

static void put_word(TEMPLATE *t, WORD w)
{
    *t->p++ = w;
}

static void put_dword(TEMPLATE *t, DWORD d)
{
    put_word(t, LOWORD(d));
    put_word(t, HIWORD(d));
}

static void put_text(TEMPLATE *t, const WCHAR *s)
{
    do
        put_word(t, *s);
    while (*s++);
}

static void align(TEMPLATE *t)
{
    if ((ULONG_PTR)t->p & 3)
        put_word(t, 0);
}

static void control(TEMPLATE *t, DWORD style, short x, short y, short cx, short cy, DWORD id, WORD atom,
                    const WCHAR *text)
{
    align(t);
    put_dword(t, 0);
    put_dword(t, 0);
    put_dword(t, style);
    put_word(t, (WORD)x);
    put_word(t, (WORD)y);
    put_word(t, (WORD)cx);
    put_word(t, (WORD)cy);
    put_dword(t, id);
    put_word(t, 0xFFFF);
    put_word(t, atom);
    put_text(t, text);
    put_word(t, 0);
}

static void header(TEMPLATE *t, WORD count, short cx, short cy, const WCHAR *title)
{
    t->p = t->data;
    put_word(t, 1);
    put_word(t, 0xFFFF);
    put_dword(t, 0);
    put_dword(t, 0);
    put_dword(t, 0x90C800C0);
    put_word(t, count);
    put_word(t, 10);
    put_word(t, 20);
    put_word(t, (WORD)cx);
    put_word(t, (WORD)cy);
    put_word(t, 0);
    put_word(t, 0);
    put_text(t, title);
    put_word(t, 8);
    put_word(t, 0);
    put_word(t, 0);
    put_text(t, L"MS Shell Dlg");
}

/* windows' dialog 70, or 71 for filters. */
static void chooser_template(TEMPLATE *t, BOOL filter)
{
    header(t, 11, 225, 100, filter ? L"Filter Selection" : L"Sound Selection");
    control(t, 0x50000000, 5, 5, 115, 8, 0xFFFF, 0x82, L"&Name:");
    control(t, 0x50210103, 5, 15, 115, 60, ID_NAME, 0x85, L"");
    control(t, 0x50010000, 125, 14, 45, 14, ID_SAVE, 0x80, L"&Save As...");
    control(t, 0x50010000, 175, 14, 45, 14, ID_REMOVE, 0x80, L"&Remove");
    control(t, 0x50000000, 5, 41, 44, 8, 0xFFFF, 0x82, filter ? L"&Filter:" : L"&Format:");
    control(t, 0x50210103, 50, 39, 170, 60, ID_TAG, 0x85, L"");
    control(t, 0x50000000, 5, 59, 44, 8, 0xFFFF, 0x82, L"&Attributes:");
    control(t, 0x50210213, 50, 57, 170, 60, ID_ITEM, 0x85, L"");
    control(t, 0x50010001, 48, 80, 40, 14, IDOK, 0x80, L"OK");
    control(t, 0x50010000, 92, 80, 40, 14, IDCANCEL, 0x80, L"Cancel");
    control(t, 0x50010000, 136, 80, 40, 14, ID_HELP, 0x80, L"&Help");
}

/* windows' dialog 75. */
static void save_template(TEMPLATE *t, BOOL filter)
{
    header(t, 4, 175, 45, L"Save As");
    control(t, 0x50020000, 6, 8, 116, 10, ID_SAVE_TEXT, 0x82, filter ? L"Save this filter as" : L"Save this format as");
    control(t, 0x50810080, 6, 18, 116, 12, ID_SAVE_EDIT, 0x81, L"");
    control(t, 0x50010001, 127, 6, 40, 14, IDOK, 0x80, L"OK");
    control(t, 0x50010000, 127, 23, 40, 14, IDCANCEL, 0x80, L"Cancel");
}

/* ------------------------------------------------------------------ saved names */

static HKEY names_key(const CHOOSER *c, REGSAM access)
{
    HKEY key = nullptr;
    WCHAR path[96];
    lstrcpyW(path, AUDIO_KEY L"\\");
    lstrcatW(path, c->filter ? L"WaveFilters" : L"WaveFormats");
    RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, access, nullptr, &key, nullptr);
    return key;
}

/* a name windows setup created, which the user cannot remove. */
static BOOL system_name(const WCHAR *name)
{
    WCHAR list[512];
    DWORD size = sizeof(list) - sizeof(WCHAR), type;
    if (RegGetValueW(HKEY_CURRENT_USER, AUDIO_KEY, L"SystemFormats", RRF_RT_REG_SZ, &type, list, &size))
        return FALSE;
    for (WCHAR *p = list; *p;) {
        WCHAR *end = p;
        while (*end && *end != ',')
            end++;
        WCHAR saved = *end;
        *end = 0;
        BOOL match = !lstrcmpiW(p, name);
        *end = saved;
        if (match)
            return TRUE;
        p = saved ? end + 1 : end;
    }
    return FALSE;
}

static BOOL hook_accepts(CHOOSER *c, WPARAM what, LPARAM item)
{
    return !c->hook || c->hook(c->dialog, c->filter ? MM_ACM_FILTERCHOOSE : MM_ACM_FORMATCHOOSE, what, item);
}

/* whether the drivers and restrictions accept a saved item. */
static BOOL usable(CHOOSER *c, const void *item, DWORD size)
{
    if (c->filter) {
        const WAVEFILTER *f = item;
        if (size < sizeof(WAVEFILTER) || f->cbStruct != size)
            return FALSE;
        if ((c->enum_flags & ACM_FILTERENUMF_DWFILTERTAG) && f->dwFilterTag != ((const WAVEFILTER *)c->enum_item)->dwFilterTag)
            return FALSE;
        ACMFILTERDETAILSW d = {sizeof(d), 0, f->dwFilterTag, 0, (LPWAVEFILTER)f, size};
        return !acmFilterDetailsW(nullptr, &d, ACM_FILTERDETAILSF_FILTER) &&
               hook_accepts(c, FILTERCHOOSE_CUSTOM_VERIFY, (LPARAM)f);
    }
    const WAVEFORMATEX *f = item;
    if (size < sizeof(PCMWAVEFORMAT) || AcmFormatSize(f) != size)
        return FALSE;
    const WAVEFORMATEX *e = c->enum_item;
    if ((c->enum_flags & ACM_FORMATENUMF_WFORMATTAG) && f->wFormatTag != e->wFormatTag)
        return FALSE;
    if ((c->enum_flags & ACM_FORMATENUMF_NCHANNELS) && f->nChannels != e->nChannels)
        return FALSE;
    if ((c->enum_flags & ACM_FORMATENUMF_NSAMPLESPERSEC) && f->nSamplesPerSec != e->nSamplesPerSec)
        return FALSE;
    if ((c->enum_flags & ACM_FORMATENUMF_WBITSPERSAMPLE) && f->wBitsPerSample != e->wBitsPerSample)
        return FALSE;
    if ((c->enum_flags & ACM_FORMATENUMF_CONVERT) &&
        acmStreamOpen(nullptr, nullptr, (LPWAVEFORMATEX)e, (LPWAVEFORMATEX)f, nullptr, 0, 0, ACM_STREAMOPENF_QUERY))
        return FALSE;
    ACMFORMATDETAILSW d = {sizeof(d), 0, f->wFormatTag, 0, (LPWAVEFORMATEX)f, size};
    return !acmFormatDetailsW(nullptr, &d, ACM_FORMATDETAILSF_FORMAT) &&
           hook_accepts(c, FORMATCHOOSE_CUSTOM_VERIFY, (LPARAM)f);
}

static void free_list(HWND combo)
{
    int n = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; i++) {
        LRESULT data = SendMessageW(combo, CB_GETITEMDATA, i, 0);
        if (data && data != CB_ERR)
            HeapFree(heap(), 0, (void *)data);
    }
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
}

static void fill_names(CHOOSER *c)
{
    free_list(c->names);
    SendMessageW(c->names, CB_ADDSTRING, 0, (LPARAM)L"[untitled]");
    HKEY key = names_key(c, KEY_QUERY_VALUE);
    for (DWORD i = 0; key; i++) {
        WCHAR name[128];
        BYTE value[512];
        DWORD name_size = 128, size = sizeof(value), type;
        LSTATUS r = RegEnumValueW(key, i, name, &name_size, nullptr, &type, value, &size);
        if (r == ERROR_NO_MORE_ITEMS)
            break;
        if (r || type != REG_BINARY || !usable(c, value, size))
            continue;
        LRESULT at = SendMessageW(c->names, CB_ADDSTRING, 0, (LPARAM)name);
        if (at != CB_ERR)
            SendMessageW(c->names, CB_SETITEMDATA, at, (LPARAM)copy_item(c, value));
    }
    if (key)
        RegCloseKey(key);
}

/* ------------------------------------------------------------------ tags and items */

typedef struct FILL {
    CHOOSER *c;
    DWORD tag;
    BOOL any;
} FILL;

static BOOL CALLBACK any_format(HACMDRIVERID id, LPACMFORMATDETAILSW d, DWORD_PTR instance, DWORD support)
{
    FILL *f = (FILL *)instance;
    if (d->pwfx->wFormatTag != f->tag || !hook_accepts(f->c, FORMATCHOOSE_FORMAT_VERIFY, (LPARAM)d->pwfx))
        return TRUE;
    f->any = TRUE;
    return FALSE;
}

static BOOL CALLBACK any_filter(HACMDRIVERID id, LPACMFILTERDETAILSW d, DWORD_PTR instance, DWORD support)
{
    FILL *f = (FILL *)instance;
    if (!hook_accepts(f->c, FILTERCHOOSE_FILTER_VERIFY, (LPARAM)d->pwfltr))
        return TRUE;
    f->any = TRUE;
    return FALSE;
}

static BOOL CALLBACK add_item_format(HACMDRIVERID id, LPACMFORMATDETAILSW d, DWORD_PTR instance, DWORD support)
{
    FILL *f = (FILL *)instance;
    if (d->pwfx->wFormatTag != f->tag || !hook_accepts(f->c, FORMATCHOOSE_FORMAT_VERIFY, (LPARAM)d->pwfx))
        return TRUE;
    /* the attributes and the rate in kilobytes, split at the tab stop. */
    WCHAR text[ACMFORMATDETAILS_FORMAT_CHARS + 24];
    wsprintfW(text, L"%s\t%lu kb/sec", d->szFormat, d->pwfx->nAvgBytesPerSec >> 10);
    LRESULT at = SendMessageW(f->c->items, CB_ADDSTRING, 0, (LPARAM)text);
    if (at != CB_ERR)
        SendMessageW(f->c->items, CB_SETITEMDATA, at, (LPARAM)copy_item(f->c, d->pwfx));
    return TRUE;
}

static BOOL CALLBACK add_item_filter(HACMDRIVERID id, LPACMFILTERDETAILSW d, DWORD_PTR instance, DWORD support)
{
    FILL *f = (FILL *)instance;
    if (!hook_accepts(f->c, FILTERCHOOSE_FILTER_VERIFY, (LPARAM)d->pwfltr))
        return TRUE;
    LRESULT at = SendMessageW(f->c->items, CB_ADDSTRING, 0, (LPARAM)d->szFilter);
    if (at != CB_ERR)
        SendMessageW(f->c->items, CB_SETITEMDATA, at, (LPARAM)copy_item(f->c, d->pwfltr));
    return TRUE;
}

static void enum_items(CHOOSER *c, DWORD tag, void *callback, FILL *fill)
{
    if (c->filter) {
        DWORD size;
        if (AcmMaxFilterSize(nullptr, &size))
            return;
        WAVEFILTER *f = HeapAlloc(heap(), HEAP_ZERO_MEMORY, size);
        if (!f)
            return;
        f->cbStruct = size;
        f->dwFilterTag = tag;
        ACMFILTERDETAILSW d = {sizeof(d), 0, tag, 0, f, size};
        acmFilterEnumW(nullptr, &d, (ACMFILTERENUMCBW)callback, (DWORD_PTR)fill, ACM_FILTERENUMF_DWFILTERTAG);
        HeapFree(heap(), 0, f);
        return;
    }
    DWORD size;
    if (AcmMaxFormatSize(nullptr, &size))
        return;
    WAVEFORMATEX *f = HeapAlloc(heap(), HEAP_ZERO_MEMORY, size);
    if (!f)
        return;
    DWORD flags = c->enum_flags | ACM_FORMATENUMF_WFORMATTAG;
    if (c->enum_item)
        memcpy(f, c->enum_item, AcmFormatSize(c->enum_item) < size ? AcmFormatSize(c->enum_item) : size);
    if (flags & (ACM_FORMATENUMF_CONVERT | ACM_FORMATENUMF_SUGGEST)) {
        /* the source stays in pwfx, so every tag comes and the callbacks keep this one. */
        flags &= ~ACM_FORMATENUMF_WFORMATTAG;
    } else {
        f->wFormatTag = (WORD)tag;
    }
    ACMFORMATDETAILSW d = {sizeof(d), 0, tag, 0, f, size};
    acmFormatEnumW(nullptr, &d, (ACMFORMATENUMCBW)callback, (DWORD_PTR)fill, flags);
    HeapFree(heap(), 0, f);
}

static BOOL CALLBACK add_format_tag(HACMDRIVERID id, LPACMFORMATTAGDETAILSW d, DWORD_PTR instance, DWORD support)
{
    CHOOSER *c = (CHOOSER *)instance;
    if (!hook_accepts(c, FORMATCHOOSE_FORMATTAG_VERIFY, d->dwFormatTag))
        return TRUE;
    FILL fill = {c, d->dwFormatTag};
    enum_items(c, d->dwFormatTag, any_format, &fill);
    if (!fill.any)
        return TRUE;
    LRESULT at = SendMessageW(c->tags, CB_ADDSTRING, 0, (LPARAM)d->szFormatTag);
    if (at != CB_ERR)
        SendMessageW(c->tags, CB_SETITEMDATA, at, d->dwFormatTag);
    return TRUE;
}

static BOOL CALLBACK add_filter_tag(HACMDRIVERID id, LPACMFILTERTAGDETAILSW d, DWORD_PTR instance, DWORD support)
{
    CHOOSER *c = (CHOOSER *)instance;
    if (!hook_accepts(c, FILTERCHOOSE_FILTERTAG_VERIFY, d->dwFilterTag))
        return TRUE;
    FILL fill = {c, d->dwFilterTag};
    enum_items(c, d->dwFilterTag, any_filter, &fill);
    if (!fill.any)
        return TRUE;
    LRESULT at = SendMessageW(c->tags, CB_ADDSTRING, 0, (LPARAM)d->szFilterTag);
    if (at != CB_ERR)
        SendMessageW(c->tags, CB_SETITEMDATA, at, d->dwFilterTag);
    return TRUE;
}

static void fill_tags(CHOOSER *c)
{
    SendMessageW(c->tags, CB_RESETCONTENT, 0, 0);
    if (c->filter) {
        ACMFILTERTAGDETAILSW d = {sizeof(d)};
        acmFilterTagEnumW(nullptr, &d, add_filter_tag, (DWORD_PTR)c, 0);
    } else {
        ACMFORMATTAGDETAILSW d = {sizeof(d)};
        acmFormatTagEnumW(nullptr, &d, add_format_tag, (DWORD_PTR)c, 0);
    }
}

static void fill_items(CHOOSER *c)
{
    free_list(c->items);
    LRESULT at = SendMessageW(c->tags, CB_GETCURSEL, 0, 0);
    if (at == CB_ERR)
        return;
    DWORD tag = (DWORD)SendMessageW(c->tags, CB_GETITEMDATA, at, 0);
    FILL fill = {c, tag};
    enum_items(c, tag, c->filter ? (void *)add_item_filter : (void *)add_item_format, &fill);
    SendMessageW(c->items, CB_SETCURSEL, 0, 0);
}

static const void *selected_item(CHOOSER *c)
{
    LRESULT at = SendMessageW(c->items, CB_GETCURSEL, 0, 0);
    if (at == CB_ERR)
        return nullptr;
    LRESULT data = SendMessageW(c->items, CB_GETITEMDATA, at, 0);
    return data == CB_ERR ? nullptr : (const void *)data;
}

static DWORD tag_of(const CHOOSER *c, const void *item)
{
    return c->filter ? ((const WAVEFILTER *)item)->dwFilterTag : ((const WAVEFORMATEX *)item)->wFormatTag;
}

/* selects an item's tag and attributes, FALSE when the lists lack it. */
static BOOL select_item(CHOOSER *c, const void *item)
{
    int n = (int)SendMessageW(c->tags, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; i++)
        if ((DWORD)SendMessageW(c->tags, CB_GETITEMDATA, i, 0) == tag_of(c, item)) {
            SendMessageW(c->tags, CB_SETCURSEL, i, 0);
            fill_items(c);
            int m = (int)SendMessageW(c->items, CB_GETCOUNT, 0, 0);
            for (int j = 0; j < m; j++) {
                LRESULT data = SendMessageW(c->items, CB_GETITEMDATA, j, 0);
                if (data && data != CB_ERR && same_item(c, (const void *)data, item)) {
                    SendMessageW(c->items, CB_SETCURSEL, j, 0);
                    return TRUE;
                }
            }
            return FALSE;
        }
    return FALSE;
}

/* selects the name whose item the attributes show, or untitled. */
static void match_name(CHOOSER *c)
{
    const void *item = selected_item(c);
    int n = (int)SendMessageW(c->names, CB_GETCOUNT, 0, 0), found = 0;
    for (int i = 1; item && i < n; i++) {
        LRESULT data = SendMessageW(c->names, CB_GETITEMDATA, i, 0);
        if (data && data != CB_ERR && same_item(c, (const void *)data, item)) {
            found = i;
            break;
        }
    }
    SendMessageW(c->names, CB_SETCURSEL, found, 0);
    EnableWindow(c->remove, found > 0);
}

/* ------------------------------------------------------------------ dialogs */

static INT_PTR CALLBACK save_proc(HWND dialog, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_INITDIALOG) {
        SetPropW(dialog, PROP, (HANDLE)lparam);
        SendDlgItemMessageW(dialog, ID_SAVE_EDIT, EM_LIMITTEXT, 127, 0);
        return TRUE;
    }
    if (msg == WM_COMMAND && (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL)) {
        if (LOWORD(wparam) == IDOK) {
            WCHAR *name = GetPropW(dialog, PROP);
            GetDlgItemTextW(dialog, ID_SAVE_EDIT, name, 128);
            /* windows trims the spaces around a name and refuses a blank one. */
            WCHAR *p = name;
            while (*p == ' ' || *p == '\t')
                p++;
            int n = lstrlenW(p);
            while (n && (p[n - 1] == ' ' || p[n - 1] == '\t'))
                n--;
            memmove(name, p, n * sizeof(WCHAR));
            name[n] = 0;
            if (!n) {
                MessageBoxW(dialog, L"A name cannot be blank.", nullptr, MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            if (name[0] == '[') {
                MessageBoxW(dialog, L"An invalid name has been entered.  Enter a different name.", nullptr,
                            MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
        }
        RemovePropW(dialog, PROP);
        EndDialog(dialog, LOWORD(wparam) == IDOK);
        return TRUE;
    }
    return FALSE;
}

static void save_as(CHOOSER *c)
{
    const void *item = selected_item(c);
    if (!item)
        return;
    TEMPLATE t;
    save_template(&t, c->filter);
    WCHAR name[128] = {};
    if (DialogBoxIndirectParamW(AcmModule, (LPCDLGTEMPLATEW)t.data, c->dialog, save_proc, (LPARAM)name) != 1)
        return;
    if (SendMessageW(c->names, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)name) != CB_ERR) {
        MessageBoxW(c->dialog, L"A custom selection already exists by this name.", nullptr, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    HKEY key = names_key(c, KEY_SET_VALUE);
    if (!key)
        return;
    RegSetValueExW(key, name, 0, REG_BINARY, item, item_size(c, item));
    RegCloseKey(key);
    LRESULT at = SendMessageW(c->names, CB_ADDSTRING, 0, (LPARAM)name);
    if (at != CB_ERR) {
        SendMessageW(c->names, CB_SETITEMDATA, at, (LPARAM)copy_item(c, item));
        SendMessageW(c->names, CB_SETCURSEL, at, 0);
        EnableWindow(c->remove, TRUE);
    }
}

static void remove_name(CHOOSER *c)
{
    LRESULT at = SendMessageW(c->names, CB_GETCURSEL, 0, 0);
    if (at == CB_ERR || !at)
        return;
    WCHAR name[128];
    if (SendMessageW(c->names, CB_GETLBTEXTLEN, at, 0) >= 128)
        return;
    SendMessageW(c->names, CB_GETLBTEXT, at, (LPARAM)name);
    if (system_name(name)) {
        MessageBeep(MB_ICONEXCLAMATION);
        return;
    }
    HKEY key = names_key(c, KEY_SET_VALUE);
    if (key) {
        RegDeleteValueW(key, name);
        RegCloseKey(key);
    }
    LRESULT data = SendMessageW(c->names, CB_GETITEMDATA, at, 0);
    if (data && data != CB_ERR)
        HeapFree(heap(), 0, (void *)data);
    SendMessageW(c->names, CB_DELETESTRING, at, 0);
    match_name(c);
}

static void init_dialog(CHOOSER *c, HWND dialog)
{
    c->dialog = dialog;
    c->names = GetDlgItem(dialog, ID_NAME);
    c->tags = GetDlgItem(dialog, ID_TAG);
    c->items = GetDlgItem(dialog, ID_ITEM);
    c->ok = GetDlgItem(dialog, IDOK);
    c->cancel = GetDlgItem(dialog, IDCANCEL);
    c->help = GetDlgItem(dialog, ID_HELP);
    c->save = GetDlgItem(dialog, ID_SAVE);
    c->remove = GetDlgItem(dialog, ID_REMOVE);
    RECT r = {};
    GetWindowRect(c->items, &r);
    c->tab = 2 * (r.right - r.left) / 3;
    if (c->title)
        SetWindowTextW(dialog, c->title);
    if (c->style & ACMFORMATCHOOSE_STYLEF_CONTEXTHELP)
        SetWindowLongW(dialog, GWL_EXSTYLE, GetWindowLongW(dialog, GWL_EXSTYLE) | WS_EX_CONTEXTHELP);
    if (c->help) {
        if (c->style & ACMFORMATCHOOSE_STYLEF_SHOWHELP) {
            c->help_message = RegisterWindowMessageW(ACMHELPMSGSTRINGW);
        } else if (!(c->style & (ACMFORMATCHOOSE_STYLEF_ENABLETEMPLATE | ACMFORMATCHOOSE_STYLEF_ENABLETEMPLATEHANDLE))) {
            /* without help, ok and cancel are centered again. */
            ShowWindow(c->help, SW_HIDE);
            RECT d, ok, cancel;
            GetWindowRect(dialog, &d);
            GetWindowRect(c->ok, &ok);
            GetWindowRect(c->cancel, &cancel);
            int x = (d.right + ok.left - cancel.right - d.left) / 2;
            POINT p = {x + d.left, ok.top};
            ScreenToClient(dialog, &p);
            MoveWindow(c->ok, p.x, p.y, ok.right - ok.left, ok.bottom - ok.top, FALSE);
            p = (POINT){x + d.left + cancel.left - ok.left, cancel.top};
            ScreenToClient(dialog, &p);
            MoveWindow(c->cancel, p.x, p.y, cancel.right - cancel.left, cancel.bottom - cancel.top, FALSE);
        }
    }
    fill_tags(c);
    fill_names(c);
    EnableWindow(c->remove, FALSE);
    BOOL selected = FALSE;
    if (c->initial)
        selected = select_item(c, c->initial);
    else if (c->name && c->name[0]) {
        LRESULT at = SendMessageW(c->names, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)c->name);
        if (at != CB_ERR && at > 0) {
            LRESULT data = SendMessageW(c->names, CB_GETITEMDATA, at, 0);
            selected = data && data != CB_ERR && select_item(c, (const void *)data);
        }
    }
    if (!selected) {
        /* pcm, or the first tag. */
        int n = (int)SendMessageW(c->tags, CB_GETCOUNT, 0, 0), i = n - 1;
        while (i > 0 && SendMessageW(c->tags, CB_GETITEMDATA, i, 0) != (c->filter ? 0 : WAVE_FORMAT_PCM))
            i--;
        SendMessageW(c->tags, CB_SETCURSEL, i < 0 ? 0 : i, 0);
        fill_items(c);
    }
    match_name(c);
    EnableWindow(c->ok, selected_item(c) != nullptr);
}

static void draw_item(CHOOSER *c, DRAWITEMSTRUCT *d)
{
    COLORREF background = 0, text = 0;
    BOOL selected = d->itemState & ODS_SELECTED;
    if (selected) {
        background = SetBkColor(d->hDC, GetSysColor(COLOR_HIGHLIGHT));
        text = SetTextColor(d->hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));
    }
    HBRUSH brush = CreateSolidBrush(GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    if (!brush)
        return;
    if (d->itemAction == ODA_DRAWENTIRE || d->itemAction == ODA_SELECT) {
        FillRect(d->hDC, &d->rcItem, brush);
        LRESULT n = SendMessageW(d->hwndItem, CB_GETLBTEXTLEN, d->itemID, 0);
        WCHAR line[160];
        if (n > 0 && n < 160) {
            SendMessageW(d->hwndItem, CB_GETLBTEXT, d->itemID, (LPARAM)line);
            TabbedTextOutW(d->hDC, d->rcItem.left, d->rcItem.top, line, (int)n, 1, &c->tab, d->rcItem.left);
        }
    } else if (d->itemAction == ODA_FOCUS) {
        DrawFocusRect(d->hDC, &d->rcItem);
    }
    DeleteObject(brush);
    if (selected) {
        SetBkColor(d->hDC, background);
        SetTextColor(d->hDC, text);
    }
}

static INT_PTR CALLBACK chooser_proc(HWND dialog, UINT msg, WPARAM wparam, LPARAM lparam)
{
    CHOOSER *c = msg == WM_INITDIALOG ? (CHOOSER *)lparam : GetPropW(dialog, PROP);
    if (!c)
        return FALSE;
    if (msg == WM_INITDIALOG) {
        SetPropW(dialog, PROP, c);
        c->dialog = dialog;
        UINT focus = c->hook ? c->hook(dialog, WM_INITDIALOG, wparam, c->custom) : TRUE;
        init_dialog(c, dialog);
        return focus;
    }
    if (c->hook && msg != WM_DRAWITEM && msg != WM_MEASUREITEM && c->hook(dialog, msg, wparam, lparam))
        return TRUE;
    switch (msg) {
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *m = (MEASUREITEMSTRUCT *)lparam;
        HWND item = GetDlgItem(dialog, m->CtlID);
        HDC dc = GetWindowDC(item);
        if (!dc)
            return FALSE;
        TEXTMETRICW tm = {};
        GetTextMetricsW(dc, &tm);
        ReleaseDC(item, dc);
        m->itemHeight = tm.tmExternalLeading + 1 + tm.tmAscent;
        return TRUE;
    }
    case WM_DRAWITEM:
        if (wparam != ID_ITEM)
            return FALSE;
        draw_item(c, (DRAWITEMSTRUCT *)lparam);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDOK: {
            const void *item = selected_item(c);
            if (!item)
                return TRUE;
            c->chosen = copy_item(c, item);
            LRESULT at = SendMessageW(c->names, CB_GETCURSEL, 0, 0);
            c->chosen_name[0] = 0;
            if (at != CB_ERR && at > 0 && SendMessageW(c->names, CB_GETLBTEXTLEN, at, 0) < 128)
                SendMessageW(c->names, CB_GETLBTEXT, at, (LPARAM)c->chosen_name);
            free_list(c->items);
            free_list(c->names);
            RemovePropW(dialog, PROP);
            EndDialog(dialog, c->chosen ? 1 : 3);
            return TRUE;
        }
        case IDCANCEL:
            free_list(c->items);
            free_list(c->names);
            RemovePropW(dialog, PROP);
            EndDialog(dialog, 0);
            return TRUE;
        case ID_HELP:
            if (c->help_message && c->owner)
                SendMessageW(c->owner, c->help_message, (WPARAM)dialog, 0);
            return TRUE;
        case ID_SAVE:
            save_as(c);
            return TRUE;
        case ID_REMOVE:
            remove_name(c);
            return TRUE;
        case ID_TAG:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                fill_items(c);
                match_name(c);
                EnableWindow(c->ok, selected_item(c) != nullptr);
            }
            return TRUE;
        case ID_ITEM:
            if (HIWORD(wparam) == CBN_SELCHANGE)
                match_name(c);
            return TRUE;
        case ID_NAME:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                LRESULT at = SendMessageW(c->names, CB_GETCURSEL, 0, 0);
                LRESULT data = at > 0 ? SendMessageW(c->names, CB_GETITEMDATA, at, 0) : 0;
                if (data && data != CB_ERR)
                    select_item(c, (const void *)data);
                EnableWindow(c->remove, at > 0);
            }
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* 1 for OK, 0 for cancel, -1 when the dialog could not be created. */
static INT_PTR run(CHOOSER *c, HINSTANCE instance, LPCWSTR template_name)
{
    if (c->style & ACMFORMATCHOOSE_STYLEF_ENABLETEMPLATEHANDLE)
        return DialogBoxIndirectParamW(AcmModule, (LPCDLGTEMPLATEW)instance, c->owner, chooser_proc, (LPARAM)c);
    if (c->style & ACMFORMATCHOOSE_STYLEF_ENABLETEMPLATE)
        return DialogBoxParamW(instance, template_name, c->owner, chooser_proc, (LPARAM)c);
    TEMPLATE t;
    chooser_template(&t, c->filter);
    return DialogBoxIndirectParamW(AcmModule, (LPCDLGTEMPLATEW)t.data, c->owner, chooser_proc, (LPARAM)c);
}

static BOOL valid_string(const WCHAR *s, DWORD count)
{
    if (!AcmReadable(s, count * sizeof(WCHAR)))
        return FALSE;
    for (DWORD i = 0; i < count; i++)
        if (!s[i])
            return TRUE;
    return FALSE;
}

static MMRESULT result(INT_PTR r)
{
    return r == -1 ? MMSYSERR_INVALPARAM : r == 0 ? ACMERR_CANCELED : r == 1 ? MMSYSERR_NOERROR : MMSYSERR_NOMEM;
}

MSACMAPI MMRESULT WINAPI acmFormatChooseW(LPACMFORMATCHOOSEW fc)
{
    if (!AcmBoot())
        return MMSYSERR_NOERROR;
    if (!AcmWritable(fc, sizeof(DWORD)) || !AcmWritable(fc, fc->cbStruct) || fc->cbStruct < sizeof(*fc))
        return MMSYSERR_INVALPARAM;
    if (fc->fdwStyle & 0xFFFFFF03)
        return MMSYSERR_INVALFLAG;
    if (!AcmWritable(fc->pwfx, fc->cbwfx) || !valid_string(fc->szFormat, ACMFORMATDETAILS_FORMAT_CHARS) ||
        !valid_string(fc->szFormatTag, ACMFORMATTAGDETAILS_FORMATTAG_CHARS) ||
        (fc->pszName && !valid_string(fc->pszName, fc->cchName)))
        return MMSYSERR_INVALPARAM;
    DWORD flags = fc->fdwEnum;
    if ((flags & 0xFE00FFFF) || ((flags & ACM_FORMATENUMF_HARDWARE) &&
                                 !(flags & (ACM_FORMATENUMF_INPUT | ACM_FORMATENUMF_OUTPUT))))
        return MMSYSERR_INVALFLAG;
    if (flags & 0x003F0000) {
        if (!fc->pwfxEnum)
            return MMSYSERR_INVALPARAM;
        if (flags & (ACM_FORMATENUMF_CONVERT | ACM_FORMATENUMF_SUGGEST) ? !AcmReadableFormat(fc->pwfxEnum)
                                                                        : !AcmReadable(fc->pwfxEnum, sizeof(PCMWAVEFORMAT)))
            return MMSYSERR_INVALPARAM;
    } else if (fc->pwfxEnum) {
        return MMSYSERR_INVALPARAM;
    }
    if ((fc->fdwStyle & ACMFORMATCHOOSE_STYLEF_ENABLEHOOK) && !fc->pfnHook)
        return MMSYSERR_INVALPARAM;
    CHOOSER c = {FALSE, fc->fdwStyle, fc->hwndOwner, fc->pszTitle, fc->pszName, fc->cchName, flags, fc->pwfxEnum};
    if (fc->fdwStyle & ACMFORMATCHOOSE_STYLEF_ENABLEHOOK) {
        c.hook = fc->pfnHook;
        c.custom = fc->lCustData;
    }
    if ((fc->fdwStyle & ACMFORMATCHOOSE_STYLEF_INITTOWFXSTRUCT) && AcmReadableFormat(fc->pwfx))
        c.initial = fc->pwfx;
    MMRESULT r = result(run(&c, fc->hInstance, fc->pszTemplateName));
    if (r || !c.chosen) {
        HeapFree(heap(), 0, c.chosen);
        return r;
    }
    const WAVEFORMATEX *chosen = c.chosen;
    DWORD size = AcmFormatSize(chosen);
    if (fc->cbwfx < size) {
        HeapFree(heap(), 0, c.chosen);
        return ACMERR_NOTPOSSIBLE;
    }
    fc->cbwfx = size;
    memcpy(fc->pwfx, chosen, size);
    ACMFORMATTAGDETAILSW tag = {sizeof(tag), 0, chosen->wFormatTag};
    if (!acmFormatTagDetailsW(nullptr, &tag, ACM_FORMATTAGDETAILSF_FORMATTAG))
        AcmCopyString(fc->szFormatTag, tag.szFormatTag, ACMFORMATTAGDETAILS_FORMATTAG_CHARS);
    ACMFORMATDETAILSW d = {sizeof(d), 0, chosen->wFormatTag, 0, (LPWAVEFORMATEX)chosen, size};
    if (!acmFormatDetailsW(nullptr, &d, ACM_FORMATDETAILSF_FORMAT))
        AcmCopyString(fc->szFormat, d.szFormat, ACMFORMATDETAILS_FORMAT_CHARS);
    if (fc->pszName && fc->cchName)
        AcmCopyString(fc->pszName, c.chosen_name, fc->cchName);
    HeapFree(heap(), 0, c.chosen);
    return MMSYSERR_NOERROR;
}

MSACMAPI MMRESULT WINAPI acmFilterChooseW(LPACMFILTERCHOOSEW fc)
{
    if (!AcmBoot())
        return MMSYSERR_NOERROR;
    if (!AcmWritable(fc, sizeof(DWORD)) || !AcmWritable(fc, fc->cbStruct) || fc->cbStruct < sizeof(*fc))
        return MMSYSERR_INVALPARAM;
    if (fc->fdwStyle & 0xFFFFFF03)
        return MMSYSERR_INVALFLAG;
    if (!AcmWritable(fc->pwfltr, fc->cbwfltr) || !valid_string(fc->szFilter, ACMFILTERDETAILS_FILTER_CHARS) ||
        !valid_string(fc->szFilterTag, ACMFILTERTAGDETAILS_FILTERTAG_CHARS) ||
        (fc->pszName && !valid_string(fc->pszName, fc->cchName)))
        return MMSYSERR_INVALPARAM;
    if (fc->fdwEnum & ~ACM_FILTERENUMF_DWFILTERTAG)
        return MMSYSERR_INVALFLAG;
    if (fc->fdwEnum ? !AcmReadableFilter(fc->pwfltrEnum) : fc->pwfltrEnum != nullptr)
        return MMSYSERR_INVALPARAM;
    if ((fc->fdwStyle & ACMFILTERCHOOSE_STYLEF_ENABLEHOOK) && !fc->pfnHook)
        return MMSYSERR_INVALPARAM;
    CHOOSER c = {TRUE, fc->fdwStyle, fc->hwndOwner, fc->pszTitle, fc->pszName, fc->cchName, fc->fdwEnum, fc->pwfltrEnum};
    if (fc->fdwStyle & ACMFILTERCHOOSE_STYLEF_ENABLEHOOK) {
        c.hook = fc->pfnHook;
        c.custom = fc->lCustData;
    }
    if ((fc->fdwStyle & ACMFILTERCHOOSE_STYLEF_INITTOWFILTERSTRUCT) && AcmReadableFilter(fc->pwfltr))
        c.initial = fc->pwfltr;
    MMRESULT r = result(run(&c, fc->hInstance, fc->pszTemplateName));
    if (r || !c.chosen) {
        HeapFree(heap(), 0, c.chosen);
        return r;
    }
    const WAVEFILTER *chosen = c.chosen;
    if (fc->cbwfltr < chosen->cbStruct) {
        HeapFree(heap(), 0, c.chosen);
        return ACMERR_NOTPOSSIBLE;
    }
    fc->cbwfltr = chosen->cbStruct;
    memcpy(fc->pwfltr, chosen, chosen->cbStruct);
    ACMFILTERTAGDETAILSW tag = {sizeof(tag), 0, chosen->dwFilterTag};
    if (!acmFilterTagDetailsW(nullptr, &tag, ACM_FILTERTAGDETAILSF_FILTERTAG))
        AcmCopyString(fc->szFilterTag, tag.szFilterTag, ACMFILTERTAGDETAILS_FILTERTAG_CHARS);
    ACMFILTERDETAILSW d = {sizeof(d), 0, chosen->dwFilterTag, 0, (LPWAVEFILTER)chosen, chosen->cbStruct};
    if (!acmFilterDetailsW(nullptr, &d, ACM_FILTERDETAILSF_FILTER))
        AcmCopyString(fc->szFilter, d.szFilter, ACMFILTERDETAILS_FILTER_CHARS);
    if (fc->pszName && fc->cchName)
        AcmCopyString(fc->pszName, c.chosen_name, fc->cchName);
    HeapFree(heap(), 0, c.chosen);
    return MMSYSERR_NOERROR;
}

/* ------------------------------------------------------------------ ansi */

static WCHAR *wide_copy(const char *s)
{
    if (!s || IS_INTRESOURCE(s))
        return (WCHAR *)s;
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    WCHAR *w = n > 0 ? HeapAlloc(heap(), 0, n * sizeof(WCHAR)) : nullptr;
    if (w)
        MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
    return w;
}

static void wide_free(const char *s, WCHAR *w)
{
    if (s && !IS_INTRESOURCE(s))
        HeapFree(heap(), 0, w);
}

/* the hook of an ansi chooser sees the ansi structure. */
MSACMAPI MMRESULT WINAPI acmFormatChooseA(LPACMFORMATCHOOSEA fc)
{
    if (!AcmWritable(fc, sizeof(DWORD)) || !AcmWritable(fc, fc->cbStruct) || fc->cbStruct < sizeof(*fc))
        return MMSYSERR_INVALPARAM;
    ACMFORMATCHOOSEW w = {sizeof(w), fc->fdwStyle, fc->hwndOwner, fc->pwfx, fc->cbwfx};
    w.pszTitle = wide_copy(fc->pszTitle);
    w.cchName = fc->cchName;
    WCHAR *name = nullptr;
    if (fc->pszName && fc->cchName) {
        name = HeapAlloc(heap(), HEAP_ZERO_MEMORY, fc->cchName * sizeof(WCHAR));
        if (name)
            MultiByteToWideChar(CP_ACP, 0, fc->pszName, -1, name, fc->cchName);
        w.pszName = name;
    }
    w.fdwEnum = fc->fdwEnum;
    w.pwfxEnum = fc->pwfxEnum;
    w.hInstance = fc->hInstance;
    w.pszTemplateName = (fc->fdwStyle & ACMFORMATCHOOSE_STYLEF_ENABLETEMPLATE) ? wide_copy(fc->pszTemplateName)
                                                                                : (LPCWSTR)fc->pszTemplateName;
    w.lCustData = fc->lCustData;
    w.pfnHook = (ACMFORMATCHOOSEHOOKPROCW)fc->pfnHook;
    MMRESULT r = acmFormatChooseW(&w);
    if (!r) {
        fc->cbwfx = w.cbwfx;
        AcmToAnsi(fc->szFormatTag, w.szFormatTag, ACMFORMATTAGDETAILS_FORMATTAG_CHARS);
        AcmToAnsi(fc->szFormat, w.szFormat, ACMFORMATDETAILS_FORMAT_CHARS);
        if (name)
            AcmToAnsi(fc->pszName, name, fc->cchName);
    }
    wide_free(fc->pszTitle, (WCHAR *)w.pszTitle);
    if (fc->fdwStyle & ACMFORMATCHOOSE_STYLEF_ENABLETEMPLATE)
        wide_free(fc->pszTemplateName, (WCHAR *)w.pszTemplateName);
    HeapFree(heap(), 0, name);
    return r;
}

MSACMAPI MMRESULT WINAPI acmFilterChooseA(LPACMFILTERCHOOSEA fc)
{
    if (!AcmWritable(fc, sizeof(DWORD)) || !AcmWritable(fc, fc->cbStruct) || fc->cbStruct < sizeof(*fc))
        return MMSYSERR_INVALPARAM;
    ACMFILTERCHOOSEW w = {sizeof(w), fc->fdwStyle, fc->hwndOwner, fc->pwfltr, fc->cbwfltr};
    w.pszTitle = wide_copy(fc->pszTitle);
    w.cchName = fc->cchName;
    WCHAR *name = nullptr;
    if (fc->pszName && fc->cchName) {
        name = HeapAlloc(heap(), HEAP_ZERO_MEMORY, fc->cchName * sizeof(WCHAR));
        if (name)
            MultiByteToWideChar(CP_ACP, 0, fc->pszName, -1, name, fc->cchName);
        w.pszName = name;
    }
    w.fdwEnum = fc->fdwEnum;
    w.pwfltrEnum = fc->pwfltrEnum;
    w.hInstance = fc->hInstance;
    w.pszTemplateName = (fc->fdwStyle & ACMFILTERCHOOSE_STYLEF_ENABLETEMPLATE) ? wide_copy(fc->pszTemplateName)
                                                                                : (LPCWSTR)fc->pszTemplateName;
    w.lCustData = fc->lCustData;
    w.pfnHook = (ACMFILTERCHOOSEHOOKPROCW)fc->pfnHook;
    MMRESULT r = acmFilterChooseW(&w);
    if (!r) {
        fc->cbwfltr = w.cbwfltr;
        AcmToAnsi(fc->szFilterTag, w.szFilterTag, ACMFILTERTAGDETAILS_FILTERTAG_CHARS);
        AcmToAnsi(fc->szFilter, w.szFilter, ACMFILTERDETAILS_FILTER_CHARS);
        if (name)
            AcmToAnsi(fc->pszName, name, fc->cchName);
    }
    wide_free(fc->pszTitle, (WCHAR *)w.pszTitle);
    if (fc->fdwStyle & ACMFILTERCHOOSE_STYLEF_ENABLETEMPLATE)
        wide_free(fc->pszTemplateName, (WCHAR *)w.pszTemplateName);
    HeapFree(heap(), 0, name);
    return r;
}
