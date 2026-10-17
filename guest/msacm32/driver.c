/*
 * the list of drivers: the installable ones of the registry's Drivers32
 * key named msacm.*, the internal pcm converter after them, and those a
 * program adds, local ones first for their thread. their order is the
 * priority the user's "Priority v4.00" key keeps.
 */
#include "msacm32_private.h"

WINUSERAPI DWORD WINAPI CharLowerBuffW(LPWSTR s, DWORD n);

#define ACM_KEY L"Software\\Microsoft\\Multimedia\\Audio Compression Manager\\"
#define DRIVERS32 L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Drivers32"
#define PCM_ALIAS L"Internal PCM Converter"

static ACMDRIVERID_ *g_ids;
static BOOL g_booted;
/* the thread deferring the saving of priorities between ACM_DRIVERPRIORITYF_BEGIN and END. */
static DWORD g_priority_thread;

static HANDLE heap(void)
{
    return GetProcessHeap();
}

static BOOL listed(const ACMDRIVERID_ *id, DWORD flags, DWORD thread)
{
    if (id->thread != thread)
        return FALSE;
    if (!(flags & ENUM_NOTIFY) && (id->state & ID_NOTIFY))
        return FALSE;
    if ((flags & ACM_DRIVERENUMF_NOLOCAL) && (id->state & ID_LOCAL))
        return FALSE;
    if (!(flags & ACM_DRIVERENUMF_DISABLED) && (id->state & ID_DISABLED))
        return FALSE;
    return (flags & ENUM_BROKEN) || !id->broken;
}

/* the next driver the thread sees: its local drivers, then the global ones. ENUM_ALL walks the whole list. */
ACMDRIVERID_ *AcmNextId(ACMDRIVERID_ *after, DWORD flags)
{
    if (flags == ENUM_ALL)
        return after ? after->next : g_ids;
    DWORD thread = after ? after->thread : GetCurrentThreadId();
    ACMDRIVERID_ *p = after ? after->next : g_ids;
    for (;;) {
        for (; p; p = p->next)
            if (listed(p, flags, thread))
                return p;
        if (!thread)
            return nullptr;
        thread = 0;
        p = g_ids;
    }
}

DWORD AcmIdSupport(const ACMDRIVERID_ *id)
{
    return id->support | (id->state & (ID_DISABLED | ID_LOCAL | ID_NOTIFY));
}

BOOL AcmHasFormatTag(const ACMDRIVERID_ *id, DWORD tag)
{
    for (DWORD i = 0; i < id->format_count; i++)
        if (id->formats[i].tag == tag)
            return TRUE;
    return FALSE;
}

BOOL AcmHasFilterTag(const ACMDRIVERID_ *id, DWORD tag)
{
    for (DWORD i = 0; i < id->filter_count; i++)
        if (id->filters[i].tag == tag)
            return TRUE;
    return FALSE;
}

/* the driver id of a driver id, a driver or a stream, the handle being valid. */
ACMDRIVERID_ *AcmIdOf(HACMOBJ handle)
{
    DWORD type = *(DWORD *)handle;
    if (type == ACM_DRIVER)
        return ((ACMDRIVER_ *)handle)->id;
    if (type == ACM_STREAM)
        return ((ACMSTREAM_ *)handle)->driver->id;
    return (ACMDRIVERID_ *)handle;
}

/* ------------------------------------------------------------------ loading */

static MMRESULT load(ACMDRIVERID_ *id);

LRESULT AcmIdMessage(ACMDRIVERID_ *id, UINT message, LPARAM param1, LPARAM param2)
{
    if (!id)
        return MMSYSERR_INVALHANDLE;
    RtlEnterCriticalSection(&id->lock);
    LRESULT r;
    if (!(id->state & ID_LOADED) && (message < DRV_LOAD || message > DRV_OPEN) && (r = load(id)))
        ;
    else if (id->proc == (DRIVERPROC)(LONG_PTR)-1)
        r = MMSYSERR_ERROR;
    else if (id->proc)
        r = id->proc(id->id, (HDRVR)id, message, param1, param2);
    else if (id->hdrvr)
        r = SendDriverMessage(id->hdrvr, message, param1, param2);
    else
        r = MMSYSERR_INVALHANDLE;
    RtlLeaveCriticalSection(&id->lock);
    return r;
}

/* a message to an open driver; the caller holds the lock of the driver or of the stream sending it. */
LRESULT AcmDriverMessage(ACMDRIVER_ *driver, UINT message, LPARAM param1, LPARAM param2)
{
    if (!driver)
        return MMSYSERR_INVALHANDLE;
    if (driver->proc == (DRIVERPROC)(LONG_PTR)-1)
        return MMSYSERR_ERROR;
    if (driver->proc)
        return driver->proc(driver->instance, (HDRVR)driver->id, message, param1, param2);
    if (driver->hdrvr)
        return SendDriverMessage(driver->hdrvr, message, param1, param2);
    return MMSYSERR_INVALHANDLE;
}

static BOOL terminated(const WCHAR *s, int count)
{
    for (int i = 0; i < count; i++)
        if (!s[i])
            return TRUE;
    return FALSE;
}

static MMRESULT read_tags(ACMDRIVERID_ *id, BOOL filters)
{
    DWORD count = filters ? id->filter_count : id->format_count;
    ACMTAG **tags = filters ? &id->filters : &id->formats;
    HeapFree(heap(), 0, *tags);
    *tags = nullptr;
    if (!count)
        return filters ? MMSYSERR_NOERROR : MMSYSERR_ERROR;
    ACMTAG *list = HeapAlloc(heap(), 0, count * sizeof(*list));
    if (!list)
        return MMSYSERR_NOMEM;
    for (DWORD i = 0; i < count; i++) {
        MMRESULT r;
        if (filters) {
            ACMFILTERTAGDETAILSW d = {sizeof(d), i};
            r = (MMRESULT)AcmIdMessage(id, ACMDM_FILTERTAG_DETAILS, (LPARAM)&d, ACM_FILTERTAGDETAILSF_INDEX);
            list[i] = (ACMTAG){d.dwFilterTag, d.cbFilterSize};
        } else {
            ACMFORMATTAGDETAILSW d = {sizeof(d), i};
            r = (MMRESULT)AcmIdMessage(id, ACMDM_FORMATTAG_DETAILS, (LPARAM)&d, ACM_FORMATTAGDETAILSF_INDEX);
            list[i] = (ACMTAG){d.dwFormatTag, d.cbFormatSize};
        }
        if (r) {
            HeapFree(heap(), 0, list);
            return r;
        }
    }
    *tags = list;
    return MMSYSERR_NOERROR;
}

/* opens a driver for the life of its id and caches what it supports. */
static MMRESULT load(ACMDRIVERID_ *id)
{
    if (id->state & ID_LOADED)
        return MMSYSERR_NOERROR;
    if (id->proc) {
        if (!(id->state & ID_NOTIFY)) {
            if (!AcmIdMessage(id, DRV_LOAD, 0, 0)) {
                id->broken = TRUE;
                return MMSYSERR_NODRIVER;
            }
            AcmIdMessage(id, DRV_ENABLE, 0, 0);
            id->id = (DWORD_PTR)AcmIdMessage(id, DRV_OPEN, 0, 0);
            if (!id->id) {
                id->broken = TRUE;
                return MMSYSERR_NODRIVER;
            }
        }
    } else {
        id->hdrvr = OpenDriver(id->alias, L"Drivers32", 0);
        if (!id->hdrvr) {
            id->broken = TRUE;
            return MMSYSERR_NODRIVER;
        }
    }
    id->state |= ID_LOADED;
    if (id->state & ID_NOTIFY)
        return MMSYSERR_NOERROR;
    id->broken = TRUE;
    ACMDRIVERDETAILSW *d = HeapAlloc(heap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d)
        return MMSYSERR_NOMEM;
    d->cbStruct = sizeof(*d);
    MMRESULT r = (MMRESULT)AcmIdMessage(id, ACMDM_DRIVER_DETAILS, (LPARAM)d, 0);
    if (!r && !d->vdwACM)
        r = MMSYSERR_NOTSUPPORTED;
    if (!r) {
        if (HIWORD(d->vdwACM) > 0x0500 || d->cbStruct != sizeof(*d) ||
            d->fccType != ACMDRIVERDETAILS_FCCTYPE_AUDIOCODEC || d->fccComp != ACMDRIVERDETAILS_FCCCOMP_UNDEFINED ||
            !terminated(d->szShortName, ACMDRIVERDETAILS_SHORTNAME_CHARS) ||
            !terminated(d->szLongName, ACMDRIVERDETAILS_LONGNAME_CHARS) ||
            !terminated(d->szCopyright, ACMDRIVERDETAILS_COPYRIGHT_CHARS) ||
            !terminated(d->szLicensing, ACMDRIVERDETAILS_LICENSING_CHARS) ||
            !terminated(d->szFeatures, ACMDRIVERDETAILS_FEATURES_CHARS))
            r = MMSYSERR_ERROR;
    }
    if (!r) {
        id->support = d->fdwSupport & 0x7FFFFFFF;
        id->format_count = d->cFormatTags;
        id->filter_count = d->cFilterTags;
        r = read_tags(id, FALSE);
        if (!r)
            r = read_tags(id, TRUE);
        if (!r) {
            id->broken = FALSE;
            if (AcmIdMessage(id, DRV_QUERYCONFIGURE, 0, 0))
                id->state |= ID_CONFIGURABLE;
            if (!AcmIdMessage(id, ACMDM_DRIVER_ABOUT, -1, 0))
                id->state |= ID_ABOUT;
        }
    }
    HeapFree(heap(), 0, d);
    return r;
}

/* closes the driver of a loaded id; open drivers keep it. */
static MMRESULT unload(ACMDRIVERID_ *id)
{
    if (!(id->state & ID_LOADED))
        return MMSYSERR_NOERROR;
    if (id->drivers)
        return MMSYSERR_ALLOCATED;
    if (!id->proc) {
        if (id->hdrvr && !CloseDriver(id->hdrvr, 0, 0))
            return MMSYSERR_ERROR;
    } else if (!(id->state & ID_NOTIFY)) {
        if (!AcmIdMessage(id, DRV_CLOSE, 0, 0))
            return MMSYSERR_ERROR;
        AcmIdMessage(id, DRV_DISABLE, 0, 0);
        AcmIdMessage(id, DRV_FREE, 0, 0);
    }
    id->state &= ~ID_LOADED;
    id->hdrvr = nullptr;
    if (!(id->state & ID_NOTIFY))
        id->id = 0;
    return MMSYSERR_NOERROR;
}

/* ------------------------------------------------------------------ open drivers */

MMRESULT AcmOpenDriver(ACMDRIVER_ **out, ACMDRIVERID_ *id)
{
    *out = nullptr;
    RtlEnterCriticalSection(&id->lock);
    MMRESULT r = load(id);
    if (r) {
        RtlLeaveCriticalSection(&id->lock);
        return r;
    }
    if (id->state & ID_NOTIFY) {
        RtlLeaveCriticalSection(&id->lock);
        return MMSYSERR_INVALHANDLE;
    }
    if (id->state & ID_DISABLED) {
        RtlLeaveCriticalSection(&id->lock);
        return MMSYSERR_NOTENABLED;
    }
    ACMDRIVER_ *d = HeapAlloc(heap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d) {
        RtlLeaveCriticalSection(&id->lock);
        return MMSYSERR_NOMEM;
    }
    d->type = ACM_DRIVER;
    d->id = id;
    d->thread = GetCurrentThreadId();
    RtlInitializeCriticalSection(&d->lock);
    ACMDRVOPENDESCW desc = {sizeof(desc), ACMDRIVERDETAILS_FCCTYPE_AUDIOCODEC, ACMDRIVERDETAILS_FCCCOMP_UNDEFINED,
                            0x05000000};
    desc.pszSectionName = L"Drivers32";
    desc.pszAliasName = id->alias;
    if (id->proc) {
        d->proc = id->proc;
        d->instance = (DWORD_PTR)AcmIdMessage(id, DRV_OPEN, 0, (LPARAM)&desc);
    } else {
        d->hdrvr = OpenDriver(id->alias, L"Drivers32", (LPARAM)&desc);
    }
    if (!d->instance && !d->hdrvr) {
        RtlLeaveCriticalSection(&id->lock);
        RtlDeleteCriticalSection(&d->lock);
        HeapFree(heap(), 0, d);
        return desc.dwError ? desc.dwError : MMSYSERR_ERROR;
    }
    RtlEnterCriticalSection(&AcmLock);
    d->next = id->drivers;
    id->drivers = d;
    RtlLeaveCriticalSection(&AcmLock);
    RtlLeaveCriticalSection(&id->lock);
    *out = d;
    return MMSYSERR_NOERROR;
}

MMRESULT AcmCloseDriver(ACMDRIVER_ *d)
{
    ACMDRIVERID_ *id = d->id;
    if (d->streams)
        return ACMERR_BUSY;
    RtlEnterCriticalSection(&id->lock);
    if (d->hdrvr ? !CloseDriver(d->hdrvr, 0, 0) : !AcmDriverMessage(d, DRV_CLOSE, 0, 0)) {
        RtlLeaveCriticalSection(&id->lock);
        return MMSYSERR_ERROR;
    }
    RtlEnterCriticalSection(&AcmLock);
    for (ACMDRIVER_ **p = &id->drivers; *p; p = &(*p)->next)
        if (*p == d) {
            *p = d->next;
            break;
        }
    d->type = 0;
    RtlLeaveCriticalSection(&AcmLock);
    RtlLeaveCriticalSection(&id->lock);
    RtlDeleteCriticalSection(&d->lock);
    HeapFree(heap(), 0, d);
    return MMSYSERR_NOERROR;
}

/* ------------------------------------------------------------------ priorities */

static HKEY open_acm_key(const WCHAR *name)
{
    HKEY root = nullptr, key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, ACM_KEY, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_CREATE_SUB_KEY, nullptr, &root,
                        nullptr))
        return nullptr;
    RegCreateKeyExW(root, name, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr);
    RegCloseKey(root);
    return key;
}

/* renumbers the global drivers from 1 in their order. */
static void refresh_priorities(void)
{
    DWORD n = 1;
    for (ACMDRIVERID_ *id = g_ids; id; id = id->next)
        if (!id->thread && !(id->state & (ID_NOTIFY | ID_LOCAL)) && !id->broken)
            id->priority = n++;
}

/* enables or disables a driver and moves it to a priority among the global drivers, -1 being the last. */
static void set_priority(ACMDRIVERID_ *id, DWORD priority, DWORD flags)
{
    if ((flags & ACM_DRIVERPRIORITYF_ABLEMASK) == ACM_DRIVERPRIORITYF_ENABLE)
        id->state &= ~ID_DISABLED;
    else if ((flags & ACM_DRIVERPRIORITYF_ABLEMASK) == ACM_DRIVERPRIORITYF_DISABLE)
        id->state |= ID_DISABLED;
    if (!priority || priority == id->priority)
        return;
    for (ACMDRIVERID_ **p = &g_ids; *p; p = &(*p)->next)
        if (*p == id) {
            *p = id->next;
            break;
        }
    id->next = nullptr;
    ACMDRIVERID_ *previous = nullptr;
    DWORD n = 1;
    for (ACMDRIVERID_ *p = g_ids; p; previous = p, p = p->next)
        if (!(p->state & (ID_NOTIFY | ID_LOCAL)) && n++ == priority)
            break;
    if (previous) {
        id->next = previous->next;
        previous->next = id;
    } else {
        id->next = g_ids;
        g_ids = id;
    }
}

/* a value of the priority key: "1, alias" for an enabled driver, "0, alias" for a disabled one. */
static BOOL priority_matches(const ACMDRIVERID_ *id, const WCHAR *value)
{
    return (value[0] == '0' || value[0] == '1') && value[1] == ',' && value[2] == ' ' && !lstrcmpW(value + 3, id->alias);
}

/* puts the drivers in the order the user's key keeps; TRUE when something moved. */
static BOOL restore_priorities(void)
{
    HKEY key = open_acm_key(L"Priority v4.00");
    BOOL changed = FALSE;
    DWORD priority = 1;
    for (DWORD n = 1; key; n++) {
        WCHAR name[32], value[176];
        DWORD type, size = sizeof(value) - sizeof(WCHAR);
        wsprintfW(name, L"Priority%u", n);
        if (RegQueryValueExW(key, name, nullptr, &type, (BYTE *)value, &size) || type != REG_SZ)
            break;
        value[size / sizeof(WCHAR)] = 0;
        for (ACMDRIVERID_ *id = AcmNextId(nullptr, ACM_DRIVERENUMF_DISABLED | ACM_DRIVERENUMF_NOLOCAL); id;
             id = AcmNextId(id, ACM_DRIVERENUMF_DISABLED | ACM_DRIVERENUMF_NOLOCAL)) {
            if (!priority_matches(id, value))
                continue;
            if (id->priority != priority)
                changed = TRUE;
            set_priority(id, priority, value[0] == '1' ? ACM_DRIVERPRIORITYF_ENABLE : ACM_DRIVERPRIORITYF_DISABLE);
            priority++;
            break;
        }
    }
    if (key)
        RegCloseKey(key);
    refresh_priorities();
    return changed;
}

/*
 * writes the order of the global drivers, keeping the values of drivers not
 * installed now before the driver that followed them.
 */
static void save_priorities(void)
{
    HKEY key = open_acm_key(L"Priority v4.00");
    if (!key)
        return;
    typedef struct ENTRY {
        WCHAR value[176];
        ACMDRIVERID_ *id;
        BOOL written;
    } ENTRY;
    ENTRY *old = nullptr;
    DWORD count = 0;
    for (;; count++) {
        WCHAR name[32];
        DWORD type, size = sizeof(old->value) - sizeof(WCHAR);
        ENTRY *grown = old ? HeapReAlloc(heap(), HEAP_ZERO_MEMORY, old, (count + 1) * sizeof(*old))
                           : HeapAlloc(heap(), HEAP_ZERO_MEMORY, sizeof(*old));
        if (!grown)
            break;
        old = grown;
        wsprintfW(name, L"Priority%u", count + 1);
        if (RegQueryValueExW(key, name, nullptr, &type, (BYTE *)old[count].value, &size) || type != REG_SZ)
            break;
        old[count].value[size / sizeof(WCHAR)] = 0;
        for (ACMDRIVERID_ *id = AcmNextId(nullptr, ACM_DRIVERENUMF_DISABLED | ACM_DRIVERENUMF_NOLOCAL); id;
             id = AcmNextId(id, ACM_DRIVERENUMF_DISABLED | ACM_DRIVERENUMF_NOLOCAL))
            if (priority_matches(id, old[count].value))
                old[count].id = id;
    }
    DWORD n = 1;
    WCHAR name[32], value[176];
    for (ACMDRIVERID_ *id = AcmNextId(nullptr, ACM_DRIVERENUMF_DISABLED | ACM_DRIVERENUMF_NOLOCAL); id;
         id = AcmNextId(id, ACM_DRIVERENUMF_DISABLED | ACM_DRIVERENUMF_NOLOCAL)) {
        /* the absent drivers listed before this one. */
        DWORD at = 0;
        while (at < count && old[at].id != id)
            at++;
        for (DWORD i = 0; i < at && i < count; i++)
            if (!old[i].id && !old[i].written) {
                wsprintfW(name, L"Priority%u", n++);
                RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)old[i].value,
                               (lstrlenW(old[i].value) + 1) * sizeof(WCHAR));
                old[i].written = TRUE;
            }
        wsprintfW(name, L"Priority%u", n++);
        wsprintfW(value, L"%u, %s", !(id->state & ID_DISABLED), id->alias);
        RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, (lstrlenW(value) + 1) * sizeof(WCHAR));
    }
    for (DWORD i = 0; i < count; i++)
        if (!old[i].id && !old[i].written) {
            wsprintfW(name, L"Priority%u", n++);
            RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)old[i].value, (lstrlenW(old[i].value) + 1) * sizeof(WCHAR));
        }
    for (;; n++) {
        wsprintfW(name, L"Priority%u", n);
        if (RegQueryValueExW(key, name, nullptr, nullptr, nullptr, nullptr))
            break;
        RegDeleteValueW(key, name);
    }
    RegCloseKey(key);
    HeapFree(heap(), 0, old);
}

/* tells every loaded, enabled driver and notification window that the list changed. */
static void broadcast(void)
{
    for (ACMDRIVERID_ *id = AcmNextId(nullptr, ENUM_ALL); id; id = AcmNextId(id, ENUM_ALL)) {
        if ((id->state & (ID_DISABLED | ID_LOADED)) != ID_LOADED)
            continue;
        if ((id->add & ACM_DRIVERADDF_TYPEMASK) == ACM_DRIVERADDF_NOTIFYHWND) {
            if (IsWindow((HWND)id->param))
                PostMessageW((HWND)id->param, (UINT)id->id, 0, 0);
        } else {
            AcmIdMessage(id, ACMDM_DRIVER_NOTIFY, 0, 0);
        }
    }
}

/* after the list of global drivers changed: saved and told, unless a thread defers it. */
static void list_changed(void)
{
    refresh_priorities();
    if (g_priority_thread)
        return;
    save_priorities();
    broadcast();
}

/* the order another process may have saved, re-read before enumerations as windows does. */
static void sync_priorities(void)
{
    if (AcmEnumerating() || g_priority_thread)
        return;
    RtlEnterCriticalSection(&AcmLock);
    if (restore_priorities())
        broadcast();
    RtlLeaveCriticalSection(&AcmLock);
}

/* ------------------------------------------------------------------ adding and removing */

static ACMDRIVERID_ *find(LPARAM param, DWORD flags)
{
    DWORD thread = (flags & ACM_DRIVERADDF_GLOBAL) ? 0 : GetCurrentThreadId();
    DWORD type = flags & ACM_DRIVERADDF_TYPEMASK;
    for (ACMDRIVERID_ *id = g_ids; id; id = id->next) {
        if (id->thread != thread)
            continue;
        if (type == ACM_DRIVERADDF_NAME ? !lstrcmpW((const WCHAR *)param, id->alias) : id->param == param)
            return id;
    }
    return nullptr;
}

static void free_id(ACMDRIVERID_ *id)
{
    for (ACMDRIVERID_ **p = &g_ids; *p; p = &(*p)->next)
        if (*p == id) {
            *p = id->next;
            break;
        }
    id->type = 0;
    HeapFree(heap(), 0, id->formats);
    HeapFree(heap(), 0, id->filters);
    RtlDeleteCriticalSection(&id->lock);
    HeapFree(heap(), 0, id);
}

static MMRESULT add(ACMDRIVERID_ **out, HINSTANCE module, LPARAM param, DWORD priority, DWORD flags)
{
    *out = nullptr;
    if (flags & 0x3FFFFFF0)
        return MMSYSERR_INVALFLAG;
    DWORD type = flags & ACM_DRIVERADDF_TYPEMASK;
    WCHAR alias[144];
    switch (type) {
    case ACM_DRIVERADDF_NAME:
        if (!param)
            return MMSYSERR_INVALPARAM;
        AcmCopyString(alias, (const WCHAR *)param, 144);
        break;
    case ACM_DRIVERADDF_NOTIFYHWND:
        if (flags & ACM_DRIVERADDF_GLOBAL)
            return MMSYSERR_INVALPARAM;
        if (!IsWindow((HWND)param))
            return MMSYSERR_INVALHANDLE;
        if (!module || !GetModuleFileNameW(module, alias, 144))
            return MMSYSERR_INVALPARAM;
        CharLowerBuffW(alias, lstrlenW(alias));
        break;
    case ACM_DRIVERADDF_FUNCTION:
        if (priority)
            return MMSYSERR_INVALPARAM;
        [[fallthrough]];
    case 2:
        if (!param || !module || !GetModuleFileNameW(module, alias, 144))
            return MMSYSERR_INVALPARAM;
        CharLowerBuffW(alias, lstrlenW(alias));
        break;
    default:
        return MMSYSERR_INVALPARAM;
    }
    if (find(type == ACM_DRIVERADDF_NAME ? (LPARAM)alias : param, flags))
        return MMSYSERR_ERROR;
    ACMDRIVERID_ *id = HeapAlloc(heap(), HEAP_ZERO_MEMORY, sizeof(*id));
    if (!id)
        return MMSYSERR_NOMEM;
    id->type = ACM_ID;
    id->add = flags;
    id->param = param;
    RtlInitializeCriticalSection(&id->lock);
    lstrcpyW(id->alias, alias);
    if (type == 2 || type == ACM_DRIVERADDF_NOTIFYHWND) {
        id->state |= ID_NOTIFY;
        id->id = priority;
    }
    if (type == 2 || type == ACM_DRIVERADDF_FUNCTION)
        id->proc = (DRIVERPROC)param;
    else if (type == ACM_DRIVERADDF_NOTIFYHWND)
        id->proc = (DRIVERPROC)(LONG_PTR)-1;
    if (flags & ACM_DRIVERADDF_GLOBAL) {
        ACMDRIVERID_ **tail = &g_ids;
        while (*tail)
            tail = &(*tail)->next;
        *tail = id;
    } else {
        id->state |= ID_LOCAL;
        id->thread = GetCurrentThreadId();
        id->next = g_ids;
        g_ids = id;
    }
    MMRESULT r = load(id);
    if (r) {
        unload(id);
        free_id(id);
        return r;
    }
    *out = id;
    return MMSYSERR_NOERROR;
}

/* the drivers32 entries named msacm.*, then the pcm converter unless the user turned it off. */
BOOL AcmBoot(void)
{
    if (g_booted)
        return TRUE;
    RtlEnterCriticalSection(&AcmLock);
    if (!g_booted) {
        g_booted = TRUE;
        HKEY key;
        if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, DRIVERS32, 0, KEY_QUERY_VALUE, &key)) {
            for (DWORD i = 0;; i++) {
                WCHAR name[144], value[4];
                DWORD name_size = 144, size = sizeof(value), type;
                LSTATUS status = RegEnumValueW(key, i, name, &name_size, nullptr, &type, (BYTE *)value, &size);
                if (status == ERROR_NO_MORE_ITEMS)
                    break;
                if (status && status != ERROR_MORE_DATA)
                    continue;
                CharLowerBuffW(name, name_size);
                if (CompareStringOrdinal(name, name_size < 6 ? name_size : 6, L"msacm.", -1, TRUE) != CSTR_EQUAL)
                    continue;
                if (CompareStringOrdinal(name, name_size < 13 ? name_size : 13, L"msacm.voxacm1", -1, TRUE) ==
                    CSTR_EQUAL)
                    continue;
                if (status == ERROR_SUCCESS && size >= sizeof(WCHAR) && value[0] == '*')
                    continue;
                ACMDRIVERID_ *id;
                add(&id, nullptr, (LPARAM)name, 0, ACM_DRIVERADDF_NAME | ACM_DRIVERADDF_GLOBAL);
            }
            RegCloseKey(key);
        }
        HKEY acm = open_acm_key(L"MSACM");
        DWORD off = 0, size = sizeof(off), type;
        if (acm) {
            if (RegQueryValueExW(acm, L"NoPCMConverter", nullptr, &type, (BYTE *)&off, &size) || type != REG_DWORD)
                off = 0;
            RegCloseKey(acm);
        }
        ACMDRIVERID_ *pcm;
        if (!off && !add(&pcm, AcmModule, (LPARAM)AcmPcmDriverProc, 0,
                         ACM_DRIVERADDF_FUNCTION | ACM_DRIVERADDF_GLOBAL))
            lstrcpyW(pcm->alias, PCM_ALIAS);
        restore_priorities();
    }
    RtlLeaveCriticalSection(&AcmLock);
    return TRUE;
}

/* ------------------------------------------------------------------ exports */

MSACMAPI MMRESULT WINAPI acmDriverEnum(ACMDRIVERENUMCB callback, DWORD_PTR instance, DWORD flags)
{
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    if (!callback)
        return MMSYSERR_INVALPARAM;
    if (flags & 0x0FFFFFFF)
        return MMSYSERR_INVALFLAG;
    sync_priorities();
    AcmEnterEnum();
    for (ACMDRIVERID_ *id = AcmNextId(nullptr, flags); id; id = AcmNextId(id, flags))
        if (!callback((HACMDRIVERID)id, instance, AcmIdSupport(id)))
            break;
    AcmLeaveEnum();
    return MMSYSERR_NOERROR;
}

MSACMAPI MMRESULT WINAPI acmDriverID(HACMOBJ hao, LPHACMDRIVERID phadid, DWORD flags)
{
    if (!AcmValid(hao, ACM_ANY))
        return MMSYSERR_INVALHANDLE;
    if (!AcmWritable(phadid, sizeof(*phadid)))
        return MMSYSERR_INVALPARAM;
    if (flags)
        return MMSYSERR_INVALFLAG;
    *phadid = (HACMDRIVERID)AcmIdOf(hao);
    return MMSYSERR_NOERROR;
}

static MMRESULT add_export(LPHACMDRIVERID phadid, HINSTANCE module, LPARAM param, DWORD priority, DWORD flags)
{
    if (!AcmWritable(phadid, sizeof(*phadid)))
        return MMSYSERR_INVALPARAM;
    if (flags & 0xFFFFFFF0)
        return MMSYSERR_INVALFLAG;
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    if (AcmEnumerating())
        return ACMERR_NOTPOSSIBLE;
    RtlEnterCriticalSection(&AcmLock);
    ACMDRIVERID_ *id;
    MMRESULT r = add(&id, module, param, priority, flags);
    *phadid = (HACMDRIVERID)id;
    if (!r && !(id->state & (ID_LOCAL | ID_NOTIFY)))
        list_changed();
    RtlLeaveCriticalSection(&AcmLock);
    return r;
}

MSACMAPI MMRESULT WINAPI acmDriverAddW(LPHACMDRIVERID phadid, HINSTANCE module, LPARAM param, DWORD priority,
                                       DWORD flags)
{
    return add_export(phadid, module, param, priority, flags);
}

MSACMAPI MMRESULT WINAPI acmDriverAddA(LPHACMDRIVERID phadid, HINSTANCE module, LPARAM param, DWORD priority,
                                       DWORD flags)
{
    if ((flags & ACM_DRIVERADDF_TYPEMASK) != ACM_DRIVERADDF_NAME || !param)
        return add_export(phadid, module, param, priority, flags);
    WCHAR name[144];
    AcmToWide(name, (const char *)param, 144);
    return add_export(phadid, module, (LPARAM)name, priority, flags);
}

MSACMAPI MMRESULT WINAPI acmDriverRemove(HACMDRIVERID hadid, DWORD flags)
{
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    if (!AcmValid(hadid, ACM_ID))
        return MMSYSERR_INVALHANDLE;
    if (flags & ~1u)
        return MMSYSERR_INVALFLAG;
    if (AcmEnumerating())
        return ACMERR_NOTPOSSIBLE;
    ACMDRIVERID_ *id = (ACMDRIVERID_ *)hadid;
    RtlEnterCriticalSection(&AcmLock);
    DWORD state = id->state;
    MMRESULT r = MMSYSERR_NOERROR;
    /* the undocumented flag 1 uninstalls the driver from the registry too. */
    if (flags & 1) {
        HKEY key;
        if (state & ID_LOCAL)
            r = ACMERR_NOTPOSSIBLE;
        else if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, DRIVERS32, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key))
            r = MMSYSERR_NODRIVER;
        else {
            if (RegDeleteValueW(key, id->alias))
                r = MMSYSERR_NODRIVER;
            RegCloseKey(key);
        }
    }
    if (!r)
        r = unload(id);
    if (!r) {
        free_id(id);
        if (!(state & (ID_LOCAL | ID_NOTIFY)))
            list_changed();
    }
    RtlLeaveCriticalSection(&AcmLock);
    return r;
}

MSACMAPI MMRESULT WINAPI acmDriverPriority(HACMDRIVERID hadid, DWORD priority, DWORD flags)
{
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    if (AcmEnumerating())
        return ACMERR_NOTPOSSIBLE;
    if (flags & 0xFFFCFFFC)
        return MMSYSERR_INVALFLAG;
    DWORD me = GetCurrentThreadId();
    RtlEnterCriticalSection(&AcmLock);
    MMRESULT r = MMSYSERR_NOERROR;
    if (g_priority_thread && g_priority_thread != me) {
        r = MMSYSERR_ALLOCATED;
        goto out;
    }
    if (!g_priority_thread && restore_priorities())
        broadcast();
    if (!hadid) {
        switch (flags & ACM_DRIVERPRIORITYF_DEFERMASK) {
        case ACM_DRIVERPRIORITYF_BEGIN:
            if (g_priority_thread)
                r = MMSYSERR_ALLOCATED;
            else
                g_priority_thread = me;
            break;
        case ACM_DRIVERPRIORITYF_END:
            if (g_priority_thread != me)
                r = MMSYSERR_ALLOCATED;
            else {
                g_priority_thread = 0;
                save_priorities();
                broadcast();
            }
            break;
        default:
            r = MMSYSERR_INVALFLAG;
        }
        goto out;
    }
    if (!AcmValid(hadid, ACM_ID)) {
        r = MMSYSERR_INVALHANDLE;
        goto out;
    }
    ACMDRIVERID_ *id = (ACMDRIVERID_ *)hadid;
    if (priority) {
        if (id->state & (ID_LOCAL | ID_NOTIFY)) {
            r = MMSYSERR_NOTSUPPORTED;
            goto out;
        }
        if (priority != (DWORD)-1) {
            DWORD count = 0;
            for (ACMDRIVERID_ *p = AcmNextId(nullptr, ACM_DRIVERENUMF_DISABLED | ACM_DRIVERENUMF_NOLOCAL); p;
                 p = AcmNextId(p, ACM_DRIVERENUMF_DISABLED | ACM_DRIVERENUMF_NOLOCAL))
                count++;
            if (priority > count) {
                r = MMSYSERR_INVALPARAM;
                goto out;
            }
        }
    }
    set_priority(id, priority, flags);
    if (!(id->state & (ID_LOCAL | ID_NOTIFY)))
        list_changed();
out:
    RtlLeaveCriticalSection(&AcmLock);
    return r;
}

MSACMAPI MMRESULT WINAPI acmDriverOpen(LPHACMDRIVER phad, HACMDRIVERID hadid, DWORD flags)
{
    if (!AcmWritable(phad, sizeof(*phad)))
        return MMSYSERR_INVALPARAM;
    *phad = nullptr;
    if (!AcmValid(hadid, ACM_ID))
        return MMSYSERR_INVALHANDLE;
    if (flags)
        return MMSYSERR_INVALFLAG;
    return AcmOpenDriver((ACMDRIVER_ **)phad, (ACMDRIVERID_ *)hadid);
}

MSACMAPI MMRESULT WINAPI acmDriverClose(HACMDRIVER had, DWORD flags)
{
    if (!AcmValid(had, ACM_DRIVER))
        return MMSYSERR_INVALHANDLE;
    if (flags)
        return MMSYSERR_INVALFLAG;
    return AcmCloseDriver((ACMDRIVER_ *)had);
}

/* the configuration dialog, through the driver of the id. */
static LRESULT configure(ACMDRIVERID_ *id, HWND window)
{
    if (!(id->state & ID_CONFIGURABLE))
        return DRVCNF_CANCEL;
    return AcmIdMessage(id, DRV_CONFIGURE, (LPARAM)window, 0);
}

MSACMAPI LRESULT WINAPI acmDriverMessage(HACMDRIVER had, UINT message, LPARAM param1, LPARAM param2)
{
    BOOL any = FALSE;
    if (message < ACMDM_USER || message > ACMDM_USER + 0x1FFF) {
        if (message == DRV_CONFIGURE) {
            if ((!param1 || IsWindow((HWND)param1)) && !param2 && AcmValid(had, ACM_ANY))
                return configure(AcmIdOf((HACMOBJ)had), (HWND)param1);
            return 0;
        }
        if (message == DRV_QUERYCONFIGURE) {
            if (param1)
                return MMSYSERR_INVALPARAM;
        } else if (message == ACMDM_DRIVER_ABOUT) {
            if ((ULONG_PTR)(param1 + 1) > 1 && !IsWindow((HWND)param1))
                return MMSYSERR_INVALHANDLE;
        } else {
            return MMSYSERR_INVALPARAM;
        }
        if (param2)
            return MMSYSERR_INVALPARAM;
        any = TRUE;
    }
    if (!AcmValid(had, ACM_ANY))
        return MMSYSERR_INVALHANDLE;
    DWORD type = *(DWORD *)had;
    if (type == ACM_DRIVER) {
        ACMDRIVER_ *d = (ACMDRIVER_ *)had;
        RtlEnterCriticalSection(&d->lock);
        LRESULT r = AcmDriverMessage(d, message, param1, param2);
        RtlLeaveCriticalSection(&d->lock);
        return r;
    }
    if (type == ACM_ID && any)
        return AcmIdMessage((ACMDRIVERID_ *)had, message, param1, param2);
    return MMSYSERR_INVALHANDLE;
}

/* the details the driver gives, with the disabled and local bits of its state. */
static MMRESULT details(ACMDRIVERID_ *id, ACMDRIVERDETAILSW *out)
{
    if (id->state & ID_NOTIFY)
        return MMSYSERR_NOTSUPPORTED;
    ACMDRIVERDETAILSW *d = HeapAlloc(heap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d)
        return MMSYSERR_NOMEM;
    d->cbStruct = sizeof(*d);
    MMRESULT r = (MMRESULT)AcmIdMessage(id, ACMDM_DRIVER_DETAILS, (LPARAM)d, 0);
    if (r || !d->vdwACM)
        r = MMSYSERR_NOTSUPPORTED;
    else {
        DWORD size = d->cbStruct < out->cbStruct ? d->cbStruct : out->cbStruct;
        memcpy(out, d, size);
        out->cbStruct = size;
        if (size > offsetof(ACMDRIVERDETAILSW, fdwSupport)) {
            if (out->fdwSupport & ~0x1Fu)
                r = MMSYSERR_ERROR;
            else
                out->fdwSupport |= id->state & (ID_DISABLED | ID_LOCAL);
        }
    }
    HeapFree(heap(), 0, d);
    return r;
}

MSACMAPI MMRESULT WINAPI acmDriverDetailsW(HACMDRIVERID hadid, LPACMDRIVERDETAILSW padd, DWORD flags)
{
    if (!AcmValid(hadid, ACM_ANY) || *(DWORD *)hadid == ACM_STREAM)
        return MMSYSERR_INVALHANDLE;
    ACMDRIVERID_ *id = AcmIdOf((HACMOBJ)hadid);
    if (!AcmWritable(padd, sizeof(DWORD)) || padd->cbStruct < sizeof(DWORD) || !AcmWritable(padd, padd->cbStruct))
        return MMSYSERR_INVALPARAM;
    if (flags)
        return MMSYSERR_INVALFLAG;
    return details(id, padd);
}

MSACMAPI MMRESULT WINAPI acmDriverDetailsA(HACMDRIVERID hadid, LPACMDRIVERDETAILSA padd, DWORD flags)
{
    if (!AcmWritable(padd, sizeof(DWORD)) || padd->cbStruct < sizeof(DWORD) || !AcmWritable(padd, padd->cbStruct))
        return MMSYSERR_INVALPARAM;
    ACMDRIVERDETAILSW w = {sizeof(w)};
    MMRESULT r = acmDriverDetailsW(hadid, &w, flags);
    if (r)
        return r;
    ACMDRIVERDETAILSA a;
    memcpy(&a, &w, offsetof(ACMDRIVERDETAILSA, szShortName));
    AcmToAnsi(a.szShortName, w.szShortName, sizeof(a.szShortName));
    AcmToAnsi(a.szLongName, w.szLongName, sizeof(a.szLongName));
    AcmToAnsi(a.szCopyright, w.szCopyright, sizeof(a.szCopyright));
    AcmToAnsi(a.szLicensing, w.szLicensing, sizeof(a.szLicensing));
    AcmToAnsi(a.szFeatures, w.szFeatures, sizeof(a.szFeatures));
    DWORD size = padd->cbStruct < sizeof(a) ? padd->cbStruct : sizeof(a);
    a.cbStruct = size;
    memcpy(padd, &a, size);
    return MMSYSERR_NOERROR;
}
