/*
 * the xml of a scheduled task as the schedule service of windows 11 26300 reads and writes it: what it refuses and
 * where it says the error is, and the form it keeps a task in, with its elements in an order of its own, the defaults
 * it always writes and the ones it drops.
 */
#include "services.h"
#include "taskxml.h"

#define TASK_NAMESPACE L"http://schemas.microsoft.com/windows/2004/02/mit/task"

/* ------------------------------------------------------------------ the tree */

static void free_attrs(TX_ATTR *a)
{
    while (a) {
        TX_ATTR *next = a->Next;
        SvcFree(a->Name);
        SvcFree(a->Value);
        SvcFree(a);
        a = next;
    }
}

void TxFree(TX_NODE *n)
{
    while (n) {
        TX_NODE *next = n->Next;
        TxFree(n->First);
        free_attrs(n->Attrs);
        SvcFree(n->Name);
        SvcFree(n->Text);
        SvcFree(n);
        n = next;
    }
}

TX_NODE *TxChild(const TX_NODE *n, const WCHAR *name)
{
    for (TX_NODE *c = n ? n->First : nullptr; c; c = c->Next)
        if (!wcscmp(c->Name, name))
            return c;
    return nullptr;
}

const WCHAR *TxText(const TX_NODE *n, const WCHAR *name)
{
    TX_NODE *c = TxChild(n, name);
    return c ? (c->Text ? c->Text : L"") : nullptr;
}

const WCHAR *TxAttr(const TX_NODE *n, const WCHAR *name)
{
    for (TX_ATTR *a = n ? n->Attrs : nullptr; a; a = a->Next)
        if (!wcscmp(a->Name, name))
            return a->Value;
    return nullptr;
}

static TX_NODE *new_node(const WCHAR *name, SIZE_T chars, ULONG line, ULONG column)
{
    TX_NODE *n = SvcAllocZero(sizeof(*n));
    if (n) {
        n->Name = SvcDupN(name, chars);
        n->Line = line;
        n->Column = column;
    }
    return n;
}

static void append(TX_NODE *parent, TX_NODE *child)
{
    child->Parent = parent;
    TX_NODE **link = &parent->First;
    while (*link)
        link = &(*link)->Next;
    *link = child;
}

/* ------------------------------------------------------------------ reading */

typedef struct READER {
    const WCHAR *At;
    ULONG Line, Column;
    HRESULT Error;
    TX_ERROR *Info;
} READER;

static void fail(READER *r, HRESULT error, ULONG line, ULONG column, const WCHAR *node, const WCHAR *value)
{
    if (r->Error)
        return;
    r->Error = error;
    r->Info->Line = line;
    r->Info->Column = column;
    r->Info->Node = SvcDup(node ? node : L"");
    r->Info->Value = SvcDup(value ? value : L"");
}

static WCHAR next(READER *r)
{
    WCHAR c = *r->At;
    if (!c)
        return 0;
    r->At++;
    if (c == L'\n') {
        r->Line++;
        r->Column = 1;
    } else {
        r->Column++;
    }
    return c;
}

static BOOL starts(READER *r, const WCHAR *text)
{
    return !wcsncmp(r->At, text, wcslen(text));
}

static void skip(READER *r, SIZE_T chars)
{
    while (chars-- && *r->At)
        next(r);
}

static BOOL space(WCHAR c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

static void skip_space(READER *r)
{
    while (space(*r->At))
        next(r);
}

static BOOL name_char(WCHAR c)
{
    return c > 0x7F || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_' ||
           c == L':' || c == L'-' || c == L'.';
}

static BOOL malformed(READER *r)
{
    fail(r, SCHED_E_MALFORMEDXML_, r->Line, r->Column, L"", L"");
    return FALSE;
}

/* the text up to the end, with the references of xml replaced. */
static WCHAR *decode(READER *r, WCHAR end, BOOL attribute)
{
    SIZE_T room = 64, used = 0;
    WCHAR *out = SvcAlloc(room * sizeof(WCHAR));
    if (!out)
        return nullptr;
    while (*r->At && *r->At != end && !(attribute ? FALSE : *r->At == L'<')) {
        WCHAR c = next(r);
        if (c == L'&') {
            static const struct {
                const WCHAR *Name;
                WCHAR Value;
            } named[] = {{L"lt;", L'<'}, {L"gt;", L'>'}, {L"amp;", L'&'}, {L"quot;", L'"'}, {L"apos;", L'\''}};
            BOOL done = FALSE;
            for (ULONG i = 0; i < RTL_NUMBER_OF(named) && !done; i++)
                if (starts(r, named[i].Name)) {
                    skip(r, wcslen(named[i].Name));
                    c = named[i].Value;
                    done = TRUE;
                }
            if (!done && *r->At == L'#') {
                next(r);
                BOOL hex = *r->At == L'x';
                if (hex)
                    next(r);
                ULONG value = 0;
                while (*r->At && *r->At != L';') {
                    WCHAR d = next(r);
                    ULONG digit = d >= L'0' && d <= L'9' ? (ULONG)(d - L'0')
                                  : hex && (d | 0x20) >= L'a' && (d | 0x20) <= L'f' ? (ULONG)((d | 0x20) - L'a' + 10)
                                                                                    : 99;
                    if (digit >= (hex ? 16u : 10u)) {
                        SvcFree(out);
                        malformed(r);
                        return nullptr;
                    }
                    value = value * (hex ? 16 : 10) + digit;
                }
                if (next(r) != L';' || !value || value > 0xFFFF) {
                    SvcFree(out);
                    malformed(r);
                    return nullptr;
                }
                c = (WCHAR)value;
                done = TRUE;
            }
            if (!done) {
                SvcFree(out);
                malformed(r);
                return nullptr;
            }
        }
        if (used + 2 >= room) {
            WCHAR *bigger = SvcAlloc(room * 2 * sizeof(WCHAR));
            if (!bigger) {
                SvcFree(out);
                return nullptr;
            }
            memcpy(bigger, out, used * sizeof(WCHAR));
            SvcFree(out);
            out = bigger;
            room *= 2;
        }
        out[used++] = c;
    }
    out[used] = 0;
    return out;
}

static void add_text(TX_NODE *n, const WCHAR *text, SIZE_T chars, BOOL cdata)
{
    SIZE_T had = n->Text ? wcslen(n->Text) : 0;
    WCHAR *joined = SvcAlloc((had + chars + 1) * sizeof(WCHAR));
    if (!joined)
        return;
    if (had)
        memcpy(joined, n->Text, had * sizeof(WCHAR));
    memcpy(joined + had, text, chars * sizeof(WCHAR));
    joined[had + chars] = 0;
    SvcFree(n->Text);
    n->Text = joined;
    n->Cdata |= cdata;
}

/* a comment, a processing instruction or the declaration, which say nothing of the task. */
static BOOL skip_misc(READER *r)
{
    if (starts(r, L"<!--")) {
        const WCHAR *end = wcsstr(r->At + 4, L"-->");
        if (!end)
            return malformed(r);
        skip(r, (SIZE_T)(end - r->At) + 3);
        return TRUE;
    }
    if (starts(r, L"<?")) {
        const WCHAR *end = wcsstr(r->At + 2, L"?>");
        if (!end)
            return malformed(r);
        skip(r, (SIZE_T)(end - r->At) + 2);
        return TRUE;
    }
    return FALSE;
}

static TX_NODE *read_element(READER *r, TX_NODE *parent, BOOL *bad_namespace);

static BOOL read_content(READER *r, TX_NODE *n, BOOL *bad_namespace)
{
    for (;;) {
        if (r->Error)
            return FALSE;
        if (!*r->At)
            return malformed(r);
        if (starts(r, L"</")) {
            skip(r, 2);
            ULONG line = r->Line, column = r->Column;
            const WCHAR *name = r->At;
            while (name_char(*r->At))
                next(r);
            SIZE_T chars = (SIZE_T)(r->At - name);
            skip_space(r);
            if (chars != wcslen(n->Name) || wcsncmp(name, n->Name, chars) || next(r) != L'>')
                return malformed(r);
            n->EndLine = line;
            n->EndColumn = column + 1;
            return TRUE;
        }
        if (starts(r, L"<![CDATA[")) {
            skip(r, 9);
            const WCHAR *end = wcsstr(r->At, L"]]>");
            if (!end)
                return malformed(r);
            add_text(n, r->At, (SIZE_T)(end - r->At), TRUE);
            skip(r, (SIZE_T)(end - r->At) + 3);
            continue;
        }
        if (skip_misc(r))
            continue;
        if (*r->At == L'<') {
            TX_NODE *child = read_element(r, n, bad_namespace);
            if (!child)
                return FALSE;
            continue;
        }
        WCHAR *text = decode(r, L'<', FALSE);
        if (!text)
            return FALSE;
        add_text(n, text, wcslen(text), FALSE);
        SvcFree(text);
    }
}

static TX_NODE *read_element(READER *r, TX_NODE *parent, BOOL *bad_namespace)
{
    next(r);
    ULONG line = r->Line, column = r->Column + 1;
    const WCHAR *name = r->At;
    while (name_char(*r->At))
        next(r);
    if (r->At == name) {
        malformed(r);
        return nullptr;
    }
    TX_NODE *n = new_node(name, (SIZE_T)(r->At - name), line, column);
    if (!n)
        return nullptr;
    if (parent)
        append(parent, n);
    for (;;) {
        skip_space(r);
        if (*r->At == L'/' || *r->At == L'>')
            break;
        ULONG aline = r->Line, acolumn = r->Column + 1;
        const WCHAR *aname = r->At;
        while (name_char(*r->At))
            next(r);
        SIZE_T achars = (SIZE_T)(r->At - aname);
        skip_space(r);
        if (!achars || next(r) != L'=') {
            malformed(r);
            return parent ? n : (TxFree(n), nullptr);
        }
        skip_space(r);
        WCHAR quote = next(r);
        if (quote != L'"' && quote != L'\'') {
            malformed(r);
            return parent ? n : (TxFree(n), nullptr);
        }
        WCHAR *value = decode(r, quote, TRUE);
        if (!value || next(r) != quote) {
            SvcFree(value);
            malformed(r);
            return parent ? n : (TxFree(n), nullptr);
        }
        TX_ATTR *a = SvcAllocZero(sizeof(*a));
        if (a) {
            a->Name = SvcDupN(aname, achars);
            a->Value = value;
            a->Line = aline;
            a->Column = acolumn;
            TX_ATTR **link = &n->Attrs;
            while (*link)
                link = &(*link)->Next;
            *link = a;
        } else {
            SvcFree(value);
        }
    }
    /* the namespace of the document is told by its root: a root without the one of tasks is refused at once. */
    if (!parent) {
        const WCHAR *ns = TxAttr(n, L"xmlns");
        if (wcscmp(n->Name, L"Task") || !ns || wcscmp(ns, TASK_NAMESPACE)) {
            *bad_namespace = TRUE;
            fail(r, SCHED_E_NAMESPACE_, line, column, n->Name, L"");
        }
    }
    if (*r->At == L'/') {
        next(r);
        if (next(r) != L'>') {
            malformed(r);
            return parent ? n : (TxFree(n), nullptr);
        }
        n->EndLine = line;
        n->EndColumn = column;
        return n;
    }
    next(r);
    if (!read_content(r, n, bad_namespace) && !parent) {
        TxFree(n);
        return nullptr;
    }
    return n;
}

static TX_NODE *parse(const WCHAR *text, TX_ERROR *info, HRESULT *error)
{
    READER r = {text, 1, 1, S_OK, info};
    if (*r.At == 0xFEFF)
        next(&r);
    for (;;) {
        skip_space(&r);
        if (r.Error)
            break;
        if (skip_misc(&r))
            continue;
        if (starts(&r, L"<!DOCTYPE")) {
            malformed(&r);
            break;
        }
        break;
    }
    TX_NODE *root = nullptr;
    BOOL bad_namespace = FALSE;
    if (!r.Error) {
        if (*r.At != L'<')
            malformed(&r);
        else
            root = read_element(&r, nullptr, &bad_namespace);
    }
    if (!r.Error) {
        for (;;) {
            skip_space(&r);
            if (!*r.At || r.Error)
                break;
            if (!skip_misc(&r)) {
                malformed(&r);
                break;
            }
        }
    }
    *error = r.Error;
    if (r.Error) {
        TxFree(root);
        return nullptr;
    }
    return root;
}

/* ------------------------------------------------------------------ the schema */

typedef enum {
    V_NONE,     /* an element of elements. */
    V_STRING,
    V_BOOL,
    V_INT,
    V_DURATION,
    V_TIME,
    V_GUID,
    V_EMPTY,    /* an element that holds nothing, as a day of the week. */
    V_ENUM,
} VALUE_KIND;

typedef struct SCHEMA SCHEMA;
struct SCHEMA {
    const WCHAR *Name;
    VALUE_KIND Kind;
    const SCHEMA *Children; /* ended by a null name. */
    const WCHAR *const *Values; /* of an enumeration, ended by null. */
    LONG Min, Max;
};

static const WCHAR *const g_instances[] = {L"Parallel", L"Queue", L"IgnoreNew", L"StopExisting", nullptr};
static const WCHAR *const g_logons[] = {L"S4U", L"Password", L"InteractiveToken", L"InteractiveTokenOrPassword",
                                        L"Group", L"ServiceAccount", L"None", nullptr};
static const WCHAR *const g_levels[] = {L"LeastPrivilege", L"HighestAvailable", nullptr};
static const WCHAR *const g_sid_types[] = {L"None", L"Unrestricted", nullptr};
static const WCHAR *const g_state_changes[] = {L"ConsoleConnect", L"ConsoleDisconnect", L"RemoteConnect",
                                               L"RemoteDisconnect", L"SessionLock", L"SessionUnlock", nullptr};
static const WCHAR *const g_compatibility[] = {L"AT", L"V1", L"V2", L"V2.1", L"V2.2", L"V2.3", nullptr};

#define END {nullptr, V_NONE, nullptr, nullptr, 0, 0}
#define LEAF(name, kind) {name, kind, nullptr, nullptr, 0, 0}
#define ENUM(name, values) {name, V_ENUM, nullptr, values, 0, 0}
#define RANGE(name, min, max) {name, V_INT, nullptr, nullptr, min, max}
#define PARENT(name, children) {name, V_NONE, children, nullptr, 0, 0}

static const SCHEMA g_registration[] = {
    LEAF(L"URI", V_STRING), LEAF(L"SecurityDescriptor", V_STRING), LEAF(L"Source", V_STRING), LEAF(L"Date", V_TIME),
    LEAF(L"Author", V_STRING), LEAF(L"Version", V_STRING), LEAF(L"Description", V_STRING),
    LEAF(L"Documentation", V_STRING), END,
};

static const SCHEMA g_repetition[] = {LEAF(L"Interval", V_DURATION), LEAF(L"Duration", V_DURATION),
                                      LEAF(L"StopAtDurationEnd", V_BOOL), END};

#define TRIGGER_BASE                                                                                                   \
    LEAF(L"Enabled", V_BOOL), LEAF(L"StartBoundary", V_TIME), LEAF(L"EndBoundary", V_TIME),                          \
        LEAF(L"ExecutionTimeLimit", V_DURATION), PARENT(L"Repetition", g_repetition)

static const SCHEMA g_days[] = {LEAF(L"Sunday", V_EMPTY), LEAF(L"Monday", V_EMPTY), LEAF(L"Tuesday", V_EMPTY),
                                LEAF(L"Wednesday", V_EMPTY), LEAF(L"Thursday", V_EMPTY), LEAF(L"Friday", V_EMPTY),
                                LEAF(L"Saturday", V_EMPTY), END};
static const SCHEMA g_months[] = {LEAF(L"January", V_EMPTY), LEAF(L"February", V_EMPTY), LEAF(L"March", V_EMPTY),
                                  LEAF(L"April", V_EMPTY), LEAF(L"May", V_EMPTY), LEAF(L"June", V_EMPTY),
                                  LEAF(L"July", V_EMPTY), LEAF(L"August", V_EMPTY), LEAF(L"September", V_EMPTY),
                                  LEAF(L"October", V_EMPTY), LEAF(L"November", V_EMPTY), LEAF(L"December", V_EMPTY), END};
static const SCHEMA g_month_days[] = {LEAF(L"Day", V_STRING), END};
static const SCHEMA g_weeks[] = {LEAF(L"Week", V_STRING), END};
static const SCHEMA g_by_day[] = {RANGE(L"DaysInterval", 1, 365), END};
static const SCHEMA g_by_week[] = {RANGE(L"WeeksInterval", 1, 52), PARENT(L"DaysOfWeek", g_days), END};
static const SCHEMA g_by_month[] = {PARENT(L"DaysOfMonth", g_month_days), PARENT(L"Months", g_months), END};
static const SCHEMA g_by_month_dow[] = {PARENT(L"Weeks", g_weeks), PARENT(L"DaysOfWeek", g_days),
                                        PARENT(L"Months", g_months), END};

static const SCHEMA g_boot[] = {TRIGGER_BASE, LEAF(L"Delay", V_DURATION), END};
static const SCHEMA g_logon[] = {TRIGGER_BASE, LEAF(L"Delay", V_DURATION), LEAF(L"UserId", V_STRING), END};
static const SCHEMA g_idle[] = {TRIGGER_BASE, END};
static const SCHEMA g_time[] = {TRIGGER_BASE, LEAF(L"RandomDelay", V_DURATION), END};
static const SCHEMA g_calendar[] = {TRIGGER_BASE,
                                    LEAF(L"RandomDelay", V_DURATION),
                                    PARENT(L"ScheduleByDay", g_by_day),
                                    PARENT(L"ScheduleByWeek", g_by_week),
                                    PARENT(L"ScheduleByMonth", g_by_month),
                                    PARENT(L"ScheduleByMonthDayOfWeek", g_by_month_dow),
                                    END};
static const SCHEMA g_session[] = {TRIGGER_BASE, LEAF(L"Delay", V_DURATION), LEAF(L"UserId", V_STRING),
                                   ENUM(L"StateChange", g_state_changes), END};
static const SCHEMA g_value_queries[] = {LEAF(L"Value", V_STRING), END};
static const SCHEMA g_event[] = {TRIGGER_BASE,
                                 LEAF(L"Subscription", V_STRING),
                                 LEAF(L"Delay", V_DURATION),
                                 LEAF(L"PeriodOfOccurrence", V_DURATION),
                                 RANGE(L"NumberOfOccurrences", 1, 32),
                                 LEAF(L"MatchingElement", V_STRING),
                                 PARENT(L"ValueQueries", g_value_queries),
                                 END};

static const SCHEMA g_triggers[] = {
    PARENT(L"BootTrigger", g_boot), PARENT(L"RegistrationTrigger", g_boot), PARENT(L"IdleTrigger", g_idle),
    PARENT(L"TimeTrigger", g_time), PARENT(L"EventTrigger", g_event), PARENT(L"LogonTrigger", g_logon),
    PARENT(L"SessionStateChangeTrigger", g_session), PARENT(L"CalendarTrigger", g_calendar), END,
};

static const SCHEMA g_idle_settings[] = {LEAF(L"Duration", V_DURATION), LEAF(L"WaitTimeout", V_DURATION),
                                         LEAF(L"StopOnIdleEnd", V_BOOL), LEAF(L"RestartOnIdle", V_BOOL), END};
static const SCHEMA g_network[] = {LEAF(L"Name", V_STRING), LEAF(L"Id", V_GUID), END};
static const SCHEMA g_restart[] = {LEAF(L"Interval", V_DURATION), RANGE(L"Count", 1, 999), END};
static const SCHEMA g_maintenance[] = {LEAF(L"Period", V_DURATION), LEAF(L"Deadline", V_DURATION),
                                       LEAF(L"Exclusive", V_BOOL), END};

static const SCHEMA g_settings[] = {
    LEAF(L"AllowStartOnDemand", V_BOOL),
    PARENT(L"RestartOnFailure", g_restart),
    ENUM(L"MultipleInstancesPolicy", g_instances),
    LEAF(L"DisallowStartIfOnBatteries", V_BOOL),
    LEAF(L"StopIfGoingOnBatteries", V_BOOL),
    LEAF(L"AllowHardTerminate", V_BOOL),
    LEAF(L"StartWhenAvailable", V_BOOL),
    LEAF(L"NetworkProfileName", V_STRING),
    LEAF(L"RunOnlyIfNetworkAvailable", V_BOOL),
    LEAF(L"WakeToRun", V_BOOL),
    LEAF(L"Enabled", V_BOOL),
    LEAF(L"Hidden", V_BOOL),
    LEAF(L"DeleteExpiredTaskAfter", V_DURATION),
    PARENT(L"IdleSettings", g_idle_settings),
    PARENT(L"NetworkSettings", g_network),
    LEAF(L"ExecutionTimeLimit", V_DURATION),
    RANGE(L"Priority", 0, 10),
    LEAF(L"RunOnlyIfIdle", V_BOOL),
    LEAF(L"UseUnifiedSchedulingEngine", V_BOOL),
    LEAF(L"DisallowStartOnRemoteAppSession", V_BOOL),
    PARENT(L"MaintenanceSettings", g_maintenance),
    LEAF(L"Volatile", V_BOOL),
    ENUM(L"Compatibility", g_compatibility),
    END,
};

static const SCHEMA g_privileges[] = {LEAF(L"Privilege", V_STRING), END};
/* the principal is a sequence: its elements in this order only. */
static const SCHEMA g_principal[] = {
    LEAF(L"UserId", V_STRING),
    LEAF(L"GroupId", V_STRING),
    LEAF(L"DisplayName", V_STRING),
    ENUM(L"LogonType", g_logons),
    ENUM(L"RunLevel", g_levels),
    ENUM(L"ProcessTokenSidType", g_sid_types),
    PARENT(L"RequiredPrivileges", g_privileges),
    END,
};
static const SCHEMA g_principals[] = {PARENT(L"Principal", g_principal), END};

static const SCHEMA g_exec[] = {LEAF(L"Command", V_STRING), LEAF(L"Arguments", V_STRING),
                                LEAF(L"WorkingDirectory", V_STRING), END};
static const SCHEMA g_com[] = {LEAF(L"ClassId", V_GUID), LEAF(L"Data", V_STRING), END};
static const SCHEMA g_actions[] = {PARENT(L"Exec", g_exec), PARENT(L"ComHandler", g_com), LEAF(L"SendEmail", V_NONE),
                                   LEAF(L"ShowMessage", V_NONE), END};

static const SCHEMA g_task[] = {PARENT(L"RegistrationInfo", g_registration), PARENT(L"Triggers", g_triggers),
                                PARENT(L"Settings", g_settings), LEAF(L"Data", V_STRING),
                                PARENT(L"Principals", g_principals), PARENT(L"Actions", g_actions), END};

/* ------------------------------------------------------------------ values */

/* local time and utc by the rules of the zone, else by the bias the system has now. */
BOOL TxLocalToUtc(const SYSTEMTIME *local, ULONGLONG *utc)
{
    SYSTEMTIME u;
    FILETIME ft, lft;
    if (TzSpecificLocalTimeToSystemTime(nullptr, local, &u) && SystemTimeToFileTime(&u, &ft)) {
        *utc = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        return TRUE;
    }
    if (!SystemTimeToFileTime(local, &lft) || !LocalFileTimeToFileTime(&lft, &ft))
        return FALSE;
    *utc = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return TRUE;
}

BOOL TxUtcToLocal(ULONGLONG utc, SYSTEMTIME *local)
{
    FILETIME ft = {(DWORD)utc, (DWORD)(utc >> 32)}, lft;
    SYSTEMTIME s;
    if (FileTimeToSystemTime(&ft, &s) && SystemTimeToTzSpecificLocalTime(nullptr, &s, local))
        return TRUE;
    return FileTimeToLocalFileTime(&ft, &lft) && FileTimeToSystemTime(&lft, local);
}

static BOOL digits(const WCHAR **p, int count, int *out)
{
    int value = 0;
    for (int i = 0; i < count; i++) {
        if ((*p)[i] < L'0' || (*p)[i] > L'9')
            return FALSE;
        value = value * 10 + ((*p)[i] - L'0');
    }
    *p += count;
    *out = value;
    return TRUE;
}

BOOL TxParseBool(const WCHAR *text, BOOL *out)
{
    if (!text)
        return FALSE;
    if (!wcscmp(text, L"true") || !wcscmp(text, L"1"))
        *out = TRUE;
    else if (!wcscmp(text, L"false") || !wcscmp(text, L"0"))
        *out = FALSE;
    else
        return FALSE;
    return TRUE;
}

/* an xs:duration, PnYnMnDTnHnMnS, in seconds: a month counts thirty days and a year 365. */
BOOL TxParseDuration(const WCHAR *text, LONGLONG *seconds)
{
    if (!text || *text++ != L'P' || !*text)
        return FALSE;
    LONGLONG total = 0;
    BOOL time = FALSE, any = FALSE;
    while (*text) {
        if (*text == L'T') {
            if (time)
                return FALSE;
            time = TRUE;
            text++;
            continue;
        }
        LONGLONG value = 0;
        const WCHAR *start = text;
        while (*text >= L'0' && *text <= L'9')
            value = value * 10 + (*text++ - L'0');
        if (text == start)
            return FALSE;
        if (*text == L'.') {
            text++;
            while (*text >= L'0' && *text <= L'9')
                text++;
        }
        WCHAR unit = *text++;
        if (!time && unit == L'Y')
            total += value * 365 * 86400;
        else if (!time && unit == L'M')
            total += value * 30 * 86400;
        else if (!time && unit == L'D')
            total += value * 86400;
        else if (time && unit == L'H')
            total += value * 3600;
        else if (time && unit == L'M')
            total += value * 60;
        else if (time && unit == L'S')
            total += value;
        else
            return FALSE;
        any = TRUE;
    }
    *seconds = total;
    return any;
}

/* an xs:dateTime as a local time when it names no zone, as a filetime of utc. */
BOOL TxParseTime(const WCHAR *text, ULONGLONG *filetime)
{
    SYSTEMTIME t = {};
    int y, mo, d, h, mi, s;
    const WCHAR *p = text;
    if (!p || !digits(&p, 4, &y) || *p++ != L'-' || !digits(&p, 2, &mo) || *p++ != L'-' || !digits(&p, 2, &d) ||
        *p++ != L'T' || !digits(&p, 2, &h) || *p++ != L':' || !digits(&p, 2, &mi) || *p++ != L':' || !digits(&p, 2, &s))
        return FALSE;
    if (*p == L'.') {
        p++;
        while (*p >= L'0' && *p <= L'9')
            p++;
    }
    t.wYear = (WORD)y;
    t.wMonth = (WORD)mo;
    t.wDay = (WORD)d;
    t.wHour = (WORD)h;
    t.wMinute = (WORD)mi;
    t.wSecond = (WORD)s;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59)
        return FALSE;
    FILETIME ft;
    if (*p == L'Z' || *p == L'+' || *p == L'-') {
        LONGLONG offset = 0;
        if (*p != L'Z') {
            int oh, om;
            int sign = *p == L'-' ? -1 : 1;
            p++;
            if (!digits(&p, 2, &oh) || *p++ != L':' || !digits(&p, 2, &om))
                return FALSE;
            offset = sign * ((LONGLONG)oh * 3600 + om * 60);
        } else {
            p++;
        }
        if (*p || !SystemTimeToFileTime(&t, &ft))
            return FALSE;
        *filetime = (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) - (ULONGLONG)(offset * 10000000);
        return TRUE;
    }
    return !*p && TxLocalToUtc(&t, filetime);
}

static BOOL valid_guid(const WCHAR *text)
{
    static const char shape[] = "{........-....-....-....-............}";
    if (!text || wcslen(text) != sizeof(shape) - 1)
        return FALSE;
    for (ULONG i = 0; i < sizeof(shape) - 1; i++) {
        WCHAR c = text[i];
        if (shape[i] == '.' ? !((c >= L'0' && c <= L'9') || ((c | 0x20) >= L'a' && (c | 0x20) <= L'f')) : c != shape[i])
            return FALSE;
    }
    return TRUE;
}

static BOOL valid_value(const SCHEMA *s, const WCHAR *text)
{
    BOOL b;
    LONGLONG d;
    ULONGLONG t;
    switch (s->Kind) {
    case V_BOOL:
        return TxParseBool(text, &b);
    case V_DURATION:
        return TxParseDuration(text, &d);
    case V_TIME:
        return TxParseTime(text, &t);
    case V_GUID:
        return valid_guid(text);
    case V_INT: {
        if (!text || !*text)
            return FALSE;
        LONG value = 0;
        for (const WCHAR *p = text; *p; p++) {
            if (*p < L'0' || *p > L'9' || value > 100000)
                return FALSE;
            value = value * 10 + (*p - L'0');
        }
        return value >= s->Min && value <= s->Max;
    }
    case V_ENUM:
        for (ULONG i = 0; s->Values[i]; i++)
            if (!wcscmp(text, s->Values[i]))
                return TRUE;
        return FALSE;
    default:
        return TRUE;
    }
}

static BOOL blank(const WCHAR *text)
{
    for (; text && *text; text++)
        if (!space(*text))
            return FALSE;
    return TRUE;
}

static void error_at(TX_ERROR *info, ULONG line, ULONG column, const WCHAR *node, const WCHAR *value)
{
    info->Line = line;
    info->Column = column;
    info->Node = SvcDup(node ? node : L"");
    info->Value = SvcDup(value ? value : L"");
}

/*
 * the elements below one of the schema: names it knows, each once, values it accepts. an attribute is refused but the
 * id of a principal and the context of the actions, as windows refuses even the ids of triggers and actions.
 */
static HRESULT check(const TX_NODE *n, const SCHEMA *children, BOOL sequence, TX_ERROR *info)
{
    ULONG last = 0;
    for (const TX_NODE *c = n->First; c; c = c->Next) {
        const SCHEMA *s = nullptr;
        ULONG index = 0;
        for (ULONG i = 0; children[i].Name; i++)
            if (!wcscmp(children[i].Name, c->Name)) {
                s = &children[i];
                index = i + 1;
            }
        if (!s || (s->Kind == V_NONE && !s->Children)) {
            error_at(info, c->Line, c->Column, c->Name, L"");
            return s ? SCHED_E_DEPRECATED_FEATURE_USED_ : SCHED_E_UNEXPECTEDNODE_;
        }
        if (sequence && index < last) {
            error_at(info, n->EndLine, n->EndColumn, n->Name, L"");
            return SCHED_E_UNEXPECTEDNODE_;
        }
        last = index;
        for (const TX_ATTR *a = c->Attrs; a; a = a->Next) {
            if ((!wcscmp(c->Name, L"Principal") && !wcscmp(a->Name, L"id")) ||
                (!wcscmp(c->Name, L"Actions") && !wcscmp(a->Name, L"Context")))
                continue;
            error_at(info, a->Line, a->Column, a->Name, L"");
            return SCHED_E_UNEXPECTEDNODE_;
        }
        for (const TX_NODE *o = n->First; o != c; o = o->Next)
            if (!wcscmp(o->Name, c->Name) && wcscmp(c->Name, L"Principal") && n->Parent && wcscmp(n->Name, L"Actions") &&
                wcscmp(n->Name, L"Triggers") && wcscmp(c->Name, L"Day") && wcscmp(c->Name, L"Week") &&
                wcscmp(c->Name, L"Value") && wcscmp(c->Name, L"Privilege")) {
                error_at(info, c->Line, c->Column, c->Name, L"");
                return SCHED_E_UNEXPECTEDNODE_;
            }
        if (s->Children) {
            if (!blank(c->Text)) {
                error_at(info, c->EndLine, c->EndColumn, c->Name, c->Text);
                return SCHED_E_INVALIDVALUE_;
            }
            HRESULT hr = check(c, s->Children, s->Children == g_principal, info);
            if (FAILED(hr))
                return hr;
        } else if (s->Kind == V_EMPTY) {
            if (c->First || !blank(c->Text)) {
                error_at(info, c->Line, c->Column, c->Name, L"");
                return SCHED_E_UNEXPECTEDNODE_;
            }
        } else {
            if (c->First) {
                error_at(info, c->First->Line, c->First->Column, c->First->Name, L"");
                return SCHED_E_UNEXPECTEDNODE_;
            }
            if (!valid_value(s, c->Text ? c->Text : L"")) {
                error_at(info, c->EndLine, c->EndColumn, c->Name, c->Text);
                return SCHED_E_INVALIDVALUE_;
            }
        }
    }
    return S_OK;
}

HRESULT TxRead(const WCHAR *text, TX_NODE **task, TX_ERROR *info)
{
    memset(info, 0, sizeof(*info));
    HRESULT hr;
    TX_NODE *root = parse(text, info, &hr);
    if (!root)
        return hr ? hr : SCHED_E_MALFORMEDXML_;
    for (const TX_ATTR *a = root->Attrs; a; a = a->Next)
        if (wcscmp(a->Name, L"xmlns") && wcscmp(a->Name, L"version")) {
            error_at(info, a->Line, a->Column, a->Name, L"");
            TxFree(root);
            return SCHED_E_UNEXPECTEDNODE_;
        }
    hr = check(root, g_task, FALSE, info);
    if (SUCCEEDED(hr) && (!TxChild(root, L"Actions") || !TxChild(root, L"Actions")->First)) {
        error_at(info, root->EndLine, root->EndColumn, L"Actions", L"");
        hr = SCHED_E_MISSINGNODE_;
    }
    if (FAILED(hr)) {
        TxFree(root);
        return hr;
    }
    *task = root;
    return S_OK;
}

void TxFreeError(TX_ERROR *info)
{
    SvcFree(info->Node);
    SvcFree(info->Value);
    memset(info, 0, sizeof(*info));
}

/* ------------------------------------------------------------------ writing */

void TxBufPut(TX_BUF *b, const WCHAR *text)
{
    if (!text || b->Failed)
        return;
    SIZE_T chars = wcslen(text);
    if (b->Used + chars + 1 > b->Room) {
        SIZE_T room = (b->Room ? b->Room * 2 : 1024) + chars;
        WCHAR *bigger = SvcAlloc(room * sizeof(WCHAR));
        if (!bigger) {
            b->Failed = TRUE;
            return;
        }
        if (b->Used)
            memcpy(bigger, b->Text, b->Used * sizeof(WCHAR));
        SvcFree(b->Text);
        b->Text = bigger;
        b->Room = room;
    }
    memcpy(b->Text + b->Used, text, chars * sizeof(WCHAR));
    b->Used += chars;
    b->Text[b->Used] = 0;
}

static void put_escaped(TX_BUF *b, const WCHAR *text, BOOL attribute)
{
    WCHAR one[2] = {};
    for (; text && *text; text++) {
        if (*text == L'&')
            TxBufPut(b, L"&amp;");
        else if (*text == L'<')
            TxBufPut(b, L"&lt;");
        else if (*text == L'>')
            TxBufPut(b, L"&gt;");
        else if (attribute && *text == L'"')
            TxBufPut(b, L"&quot;");
        else {
            one[0] = *text;
            TxBufPut(b, one);
        }
    }
}

static void indent(TX_BUF *b, int depth)
{
    for (int i = 0; i < depth; i++)
        TxBufPut(b, L"  ");
}

static void open_tag(TX_BUF *b, int depth, const WCHAR *name)
{
    indent(b, depth);
    TxBufPut(b, L"<");
    TxBufPut(b, name);
    TxBufPut(b, L">\r\n");
}

static void close_tag(TX_BUF *b, int depth, const WCHAR *name)
{
    indent(b, depth);
    TxBufPut(b, L"</");
    TxBufPut(b, name);
    TxBufPut(b, L">\r\n");
}

static void leaf(TX_BUF *b, int depth, const WCHAR *name, const WCHAR *value, BOOL cdata)
{
    indent(b, depth);
    TxBufPut(b, L"<");
    TxBufPut(b, name);
    if (!value || !*value) {
        TxBufPut(b, L" />\r\n");
        return;
    }
    TxBufPut(b, L">");
    if (cdata) {
        TxBufPut(b, L"<![CDATA[");
        TxBufPut(b, value);
        TxBufPut(b, L"]]>");
    } else {
        put_escaped(b, value, FALSE);
    }
    TxBufPut(b, L"</");
    TxBufPut(b, name);
    TxBufPut(b, L">\r\n");
}

static void empty_tag(TX_BUF *b, int depth, const WCHAR *name)
{
    indent(b, depth);
    TxBufPut(b, L"<");
    TxBufPut(b, name);
    TxBufPut(b, L" />\r\n");
}

/* a boolean in the canonical words, the default value of absent ones. */
static BOOL flag(const TX_NODE *n, const WCHAR *name, BOOL fallback)
{
    BOOL value;
    return TxParseBool(TxText(n, name), &value) ? value : fallback;
}

static void put_bool(TX_BUF *b, int depth, const WCHAR *name, BOOL value)
{
    leaf(b, depth, name, value ? L"true" : L"false", FALSE);
}

/* the children an element has of a list of names, in the order of the list, as they are. */
static void copy_listed(TX_BUF *b, int depth, const TX_NODE *n, const WCHAR *const *names)
{
    for (ULONG i = 0; names[i]; i++) {
        const WCHAR *value = TxText(n, names[i]);
        if (value)
            leaf(b, depth, names[i], value, FALSE);
    }
}

static void write_names(TX_BUF *b, int depth, const TX_NODE *parent, const WCHAR *name, const SCHEMA *names)
{
    const TX_NODE *n = TxChild(parent, name);
    if (!n)
        return;
    open_tag(b, depth, name);
    for (ULONG i = 0; names[i].Name; i++)
        if (TxChild(n, names[i].Name))
            empty_tag(b, depth + 1, names[i].Name);
    close_tag(b, depth, name);
}

static void write_repeated(TX_BUF *b, int depth, const TX_NODE *parent, const WCHAR *name, const WCHAR *item)
{
    const TX_NODE *n = TxChild(parent, name);
    if (!n)
        return;
    open_tag(b, depth, name);
    for (const TX_NODE *c = n->First; c; c = c->Next)
        leaf(b, depth + 1, item, c->Text, FALSE);
    close_tag(b, depth, name);
}

static void write_trigger(TX_BUF *b, const TX_NODE *t)
{
    open_tag(b, 2, t->Name);
    static const WCHAR *const head[] = {L"StartBoundary", L"EndBoundary", nullptr};
    copy_listed(b, 3, t, head);
    if (!flag(t, L"Enabled", TRUE))
        put_bool(b, 3, L"Enabled", FALSE);
    static const WCHAR *const limit[] = {L"ExecutionTimeLimit", nullptr};
    copy_listed(b, 3, t, limit);
    const TX_NODE *repetition = TxChild(t, L"Repetition");
    if (repetition) {
        open_tag(b, 3, L"Repetition");
        static const WCHAR *const parts[] = {L"Interval", L"Duration", nullptr};
        copy_listed(b, 4, repetition, parts);
        if (flag(repetition, L"StopAtDurationEnd", FALSE))
            put_bool(b, 4, L"StopAtDurationEnd", TRUE);
        close_tag(b, 3, L"Repetition");
    }
    static const WCHAR *const tail[] = {L"Delay", L"RandomDelay", L"UserId", L"StateChange", L"Subscription",
                                        L"PeriodOfOccurrence", L"NumberOfOccurrences", L"MatchingElement", nullptr};
    copy_listed(b, 3, t, tail);
    write_repeated(b, 3, t, L"ValueQueries", L"Value");
    const TX_NODE *by;
    if ((by = TxChild(t, L"ScheduleByDay"))) {
        open_tag(b, 3, L"ScheduleByDay");
        leaf(b, 4, L"DaysInterval", TxText(by, L"DaysInterval") ? TxText(by, L"DaysInterval") : L"1", FALSE);
        close_tag(b, 3, L"ScheduleByDay");
    }
    if ((by = TxChild(t, L"ScheduleByWeek"))) {
        open_tag(b, 3, L"ScheduleByWeek");
        leaf(b, 4, L"WeeksInterval", TxText(by, L"WeeksInterval") ? TxText(by, L"WeeksInterval") : L"1", FALSE);
        write_names(b, 4, by, L"DaysOfWeek", g_days);
        close_tag(b, 3, L"ScheduleByWeek");
    }
    if ((by = TxChild(t, L"ScheduleByMonth"))) {
        open_tag(b, 3, L"ScheduleByMonth");
        write_repeated(b, 4, by, L"DaysOfMonth", L"Day");
        write_names(b, 4, by, L"Months", g_months);
        close_tag(b, 3, L"ScheduleByMonth");
    }
    if ((by = TxChild(t, L"ScheduleByMonthDayOfWeek"))) {
        open_tag(b, 3, L"ScheduleByMonthDayOfWeek");
        write_repeated(b, 4, by, L"Weeks", L"Week");
        write_names(b, 4, by, L"DaysOfWeek", g_days);
        write_names(b, 4, by, L"Months", g_months);
        close_tag(b, 3, L"ScheduleByMonthDayOfWeek");
    }
    close_tag(b, 2, t->Name);
}

static void write_settings(TX_BUF *b, const TX_NODE *s, BOOL unified_default, BOOL enabled)
{
    open_tag(b, 1, L"Settings");
    if (!flag(s, L"AllowHardTerminate", TRUE))
        put_bool(b, 2, L"AllowHardTerminate", FALSE);
    if (!flag(s, L"AllowStartOnDemand", TRUE))
        put_bool(b, 2, L"AllowStartOnDemand", FALSE);
    static const WCHAR *const expiry[] = {L"DeleteExpiredTaskAfter", nullptr};
    copy_listed(b, 2, s, expiry);
    put_bool(b, 2, L"DisallowStartIfOnBatteries", flag(s, L"DisallowStartIfOnBatteries", TRUE));
    put_bool(b, 2, L"StopIfGoingOnBatteries", flag(s, L"StopIfGoingOnBatteries", TRUE));
    const WCHAR *limit = TxText(s, L"ExecutionTimeLimit");
    LONGLONG seconds;
    if (limit && TxParseDuration(limit, &seconds) && seconds != 72 * 3600)
        leaf(b, 2, L"ExecutionTimeLimit", limit, FALSE);
    if (flag(s, L"Hidden", FALSE))
        put_bool(b, 2, L"Hidden", TRUE);
    const WCHAR *policy = TxText(s, L"MultipleInstancesPolicy");
    leaf(b, 2, L"MultipleInstancesPolicy", policy ? policy : L"IgnoreNew", FALSE);
    const WCHAR *priority = TxText(s, L"Priority");
    if (priority && wcscmp(priority, L"7"))
        leaf(b, 2, L"Priority", priority, FALSE);
    const TX_NODE *restart = TxChild(s, L"RestartOnFailure");
    if (restart) {
        open_tag(b, 2, L"RestartOnFailure");
        static const WCHAR *const parts[] = {L"Count", L"Interval", nullptr};
        copy_listed(b, 3, restart, parts);
        close_tag(b, 2, L"RestartOnFailure");
    }
    if (flag(s, L"StartWhenAvailable", FALSE))
        put_bool(b, 2, L"StartWhenAvailable", TRUE);
    if (flag(s, L"WakeToRun", FALSE))
        put_bool(b, 2, L"WakeToRun", TRUE);
    if (flag(s, L"RunOnlyIfNetworkAvailable", FALSE))
        put_bool(b, 2, L"RunOnlyIfNetworkAvailable", TRUE);
    static const WCHAR *const profile[] = {L"NetworkProfileName", nullptr};
    copy_listed(b, 2, s, profile);
    const TX_NODE *network = TxChild(s, L"NetworkSettings");
    if (network) {
        open_tag(b, 2, L"NetworkSettings");
        static const WCHAR *const parts[] = {L"Name", L"Id", nullptr};
        copy_listed(b, 3, network, parts);
        close_tag(b, 2, L"NetworkSettings");
    }
    if (flag(s, L"RunOnlyIfIdle", FALSE))
        put_bool(b, 2, L"RunOnlyIfIdle", TRUE);
    const TX_NODE *idle = TxChild(s, L"IdleSettings");
    open_tag(b, 2, L"IdleSettings");
    static const WCHAR *const times[] = {L"Duration", L"WaitTimeout", nullptr};
    copy_listed(b, 3, idle, times);
    put_bool(b, 3, L"StopOnIdleEnd", flag(idle, L"StopOnIdleEnd", TRUE));
    put_bool(b, 3, L"RestartOnIdle", flag(idle, L"RestartOnIdle", FALSE));
    close_tag(b, 2, L"IdleSettings");
    if (!enabled)
        put_bool(b, 2, L"Enabled", FALSE);
    BOOL unified = flag(s, L"UseUnifiedSchedulingEngine", unified_default);
    if (unified || TxText(s, L"UseUnifiedSchedulingEngine"))
        put_bool(b, 2, L"UseUnifiedSchedulingEngine", unified);
    if (flag(s, L"DisallowStartOnRemoteAppSession", FALSE))
        put_bool(b, 2, L"DisallowStartOnRemoteAppSession", TRUE);
    const TX_NODE *maintenance = TxChild(s, L"MaintenanceSettings");
    if (maintenance) {
        open_tag(b, 2, L"MaintenanceSettings");
        static const WCHAR *const parts[] = {L"Period", L"Deadline", L"Exclusive", nullptr};
        copy_listed(b, 3, maintenance, parts);
        close_tag(b, 2, L"MaintenanceSettings");
    }
    if (flag(s, L"Volatile", FALSE))
        put_bool(b, 2, L"Volatile", TRUE);
    static const WCHAR *const compat[] = {L"Compatibility", nullptr};
    copy_listed(b, 2, s, compat);
    close_tag(b, 1, L"Settings");
}

/* a version of the schema, 1.2 for one that is not, as a number of tenths. */
static int schema_version(const TX_NODE *task)
{
    const WCHAR *v = TxAttr(task, L"version");
    if (!v || v[0] != L'1' || v[1] != L'.' || v[2] < L'0' || v[2] > L'9' || v[3])
        return 12;
    return 10 + (v[2] - L'0');
}

HRESULT TxWrite(const TX_NODE *task, const TX_PRINCIPAL *who, const WCHAR *uri, BOOL enabled, WCHAR **out)
{
    TX_BUF b = {};
    TxBufPut(&b, L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n<Task version=\"");
    const WCHAR *version = TxAttr(task, L"version");
    TxBufPut(&b, version ? version : L"1.2");
    TxBufPut(&b, L"\" xmlns=\"" TASK_NAMESPACE L"\">\r\n");
    const TX_NODE *registration = TxChild(task, L"RegistrationInfo");
    open_tag(&b, 1, L"RegistrationInfo");
    static const WCHAR *const info[] = {L"Version", L"Date", L"Source", L"Author", L"Description", L"Documentation", nullptr};
    copy_listed(&b, 2, registration, info);
    leaf(&b, 2, L"URI", uri, FALSE);
    static const WCHAR *const sd[] = {L"SecurityDescriptor", nullptr};
    copy_listed(&b, 2, registration, sd);
    close_tag(&b, 1, L"RegistrationInfo");

    open_tag(&b, 1, L"Principals");
    indent(&b, 2);
    TxBufPut(&b, L"<Principal id=\"");
    put_escaped(&b, who->Id, TRUE);
    TxBufPut(&b, L"\">\r\n");
    if (who->GroupId)
        leaf(&b, 3, L"GroupId", who->GroupId, FALSE);
    else if (who->UserId)
        leaf(&b, 3, L"UserId", who->UserId, FALSE);
    if (who->DisplayName)
        leaf(&b, 3, L"DisplayName", who->DisplayName, FALSE);
    if (who->LogonType)
        leaf(&b, 3, L"LogonType", who->LogonType, FALSE);
    if (who->Highest)
        leaf(&b, 3, L"RunLevel", L"HighestAvailable", FALSE);
    close_tag(&b, 2, L"Principal");
    close_tag(&b, 1, L"Principals");

    TX_NODE none = {};
    const TX_NODE *settings = TxChild(task, L"Settings");
    write_settings(&b, settings ? settings : &none, schema_version(task) >= 13, enabled);
    const WCHAR *data = TxText(task, L"Data");
    if (data)
        leaf(&b, 1, L"Data", data, TRUE);
    const TX_NODE *triggers = TxChild(task, L"Triggers");
    if (triggers && triggers->First) {
        open_tag(&b, 1, L"Triggers");
        for (const TX_NODE *t = triggers->First; t; t = t->Next)
            write_trigger(&b, t);
        close_tag(&b, 1, L"Triggers");
    } else {
        empty_tag(&b, 1, L"Triggers");
    }
    indent(&b, 1);
    TxBufPut(&b, L"<Actions Context=\"");
    put_escaped(&b, who->Id, TRUE);
    TxBufPut(&b, L"\">\r\n");
    for (const TX_NODE *a = TxChild(task, L"Actions")->First; a; a = a->Next) {
        open_tag(&b, 2, a->Name);
        if (!wcscmp(a->Name, L"Exec")) {
            static const WCHAR *const parts[] = {L"Command", L"Arguments", L"WorkingDirectory", nullptr};
            copy_listed(&b, 3, a, parts);
        } else {
            static const WCHAR *const id[] = {L"ClassId", nullptr};
            copy_listed(&b, 3, a, id);
            const WCHAR *payload = TxText(a, L"Data");
            if (payload)
                leaf(&b, 3, L"Data", payload, TRUE);
        }
        close_tag(&b, 2, a->Name);
    }
    close_tag(&b, 1, L"Actions");
    TxBufPut(&b, L"</Task>");
    if (b.Failed) {
        SvcFree(b.Text);
        return E_OUTOFMEMORY;
    }
    *out = b.Text;
    return S_OK;
}

/* ------------------------------------------------------------------ the schedule */

#define TICKS_PER_SECOND 10000000ull
#define TICKS_PER_DAY (86400ull * TICKS_PER_SECOND)

BOOLEAN WINAPI SystemFunction036(PVOID buffer, ULONG length);

static ULONGLONG random_below(ULONGLONG bound)
{
    ULONGLONG value = 0;
    if (!bound)
        return 0;
    SystemFunction036(&value, sizeof(value));
    return value % bound;
}

/* the local midnight of a utc filetime and the parts of its local date. */
static BOOL local_parts(ULONGLONG utc, SYSTEMTIME *local)
{
    return TxUtcToLocal(utc, local);
}

static ULONGLONG from_local(const SYSTEMTIME *local)
{
    ULONGLONG utc = 0;
    return TxLocalToUtc(local, &utc) ? utc : 0;
}

/* the same local time of day on the date days after the one of the start. */
static ULONGLONG add_days(ULONGLONG start, LONGLONG days)
{
    SYSTEMTIME local;
    if (!local_parts(start, &local))
        return 0;
    local.wDayOfWeek = 0;
    FILETIME ft;
    SystemTimeToFileTime(&local, &ft);
    ULONGLONG shifted = (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) + (ULONGLONG)days * TICKS_PER_DAY;
    ft.dwLowDateTime = (DWORD)shifted;
    ft.dwHighDateTime = (DWORD)(shifted >> 32);
    FileTimeToSystemTime(&ft, &local);
    return from_local(&local);
}

typedef struct RUNS {
    ULONGLONG From, To;
    ULONGLONG *Times;
    ULONG Count, Room;
} RUNS;

static void add_run(RUNS *r, ULONGLONG when)
{
    if (when < r->From || when > r->To || r->Count >= r->Room)
        return;
    for (ULONG i = 0; i < r->Count; i++)
        if (r->Times[i] == when)
            return;
    r->Times[r->Count++] = when;
}

/* an occurrence of a trigger and the repetitions it starts. */
static void add_occurrence(RUNS *r, const TX_NODE *t, ULONGLONG when, ULONGLONG end)
{
    LONGLONG random = 0;
    TxParseDuration(TxText(t, L"RandomDelay"), &random);
    ULONGLONG delay = random > 0 ? random_below((ULONGLONG)random * TICKS_PER_SECOND) : 0;
    if (end && when > end)
        return;
    add_run(r, when + delay);
    const TX_NODE *repetition = TxChild(t, L"Repetition");
    LONGLONG interval = 0, duration = 0;
    if (!repetition || !TxParseDuration(TxText(repetition, L"Interval"), &interval) || interval <= 0)
        return;
    BOOL bounded = TxParseDuration(TxText(repetition, L"Duration"), &duration) && duration > 0;
    ULONGLONG stop = bounded ? when + (ULONGLONG)duration * TICKS_PER_SECOND : (end ? end : r->To);
    for (ULONGLONG at = when + (ULONGLONG)interval * TICKS_PER_SECOND; at < stop && at <= r->To && r->Count < r->Room;
         at += (ULONGLONG)interval * TICKS_PER_SECOND)
        if (at >= r->From)
            add_run(r, at + delay);
}

static int number(const WCHAR *text, int fallback)
{
    if (!text || !*text)
        return fallback;
    int value = 0;
    for (; *text >= L'0' && *text <= L'9'; text++)
        value = value * 10 + (*text - L'0');
    return value;
}

static ULONG name_bits(const TX_NODE *list, const SCHEMA *names)
{
    ULONG bits = 0;
    for (ULONG i = 0; names[i].Name; i++)
        if (TxChild(list, names[i].Name))
            bits |= 1u << i;
    return bits;
}

static void trigger_runs(RUNS *r, const TX_NODE *t)
{
    ULONGLONG start, end = 0;
    if (!flag(t, L"Enabled", TRUE) || !TxParseTime(TxText(t, L"StartBoundary"), &start))
        return;
    TxParseTime(TxText(t, L"EndBoundary"), &end);
    if (!wcscmp(t->Name, L"TimeTrigger")) {
        add_occurrence(r, t, start, end);
        return;
    }
    if (wcscmp(t->Name, L"CalendarTrigger"))
        return;
    const TX_NODE *by;
    /* a day before the window, so that the repetitions of an occurrence that began then are counted. */
    ULONGLONG from = r->From > TICKS_PER_DAY ? r->From - TICKS_PER_DAY : 0;
    if ((by = TxChild(t, L"ScheduleByDay"))) {
        int interval = number(TxText(by, L"DaysInterval"), 1);
        LONGLONG first = from > start ? (LONGLONG)((from - start) / TICKS_PER_DAY) / interval * interval : 0;
        for (LONGLONG day = first; r->Count < r->Room; day += interval) {
            ULONGLONG when = add_days(start, day);
            if (!when || when > r->To || (end && when > end))
                break;
            add_occurrence(r, t, when, end);
        }
    } else if ((by = TxChild(t, L"ScheduleByWeek"))) {
        int interval = number(TxText(by, L"WeeksInterval"), 1);
        ULONG days = name_bits(TxChild(by, L"DaysOfWeek"), g_days);
        SYSTEMTIME local;
        if (!local_parts(start, &local))
            return;
        /* weeks count from the sunday of the week of the start. */
        ULONGLONG week_start = add_days(start, -(LONGLONG)local.wDayOfWeek);
        for (LONGLONG week = 0; r->Count < r->Room; week += interval) {
            ULONGLONG sunday = add_days(week_start, week * 7);
            if (!sunday || sunday > r->To || (end && sunday > end))
                break;
            for (int d = 0; d < 7; d++) {
                ULONGLONG when = add_days(sunday, d);
                if ((days >> d & 1) && when >= start && (!end || when <= end) && when + TICKS_PER_DAY >= from)
                    add_occurrence(r, t, when, end);
            }
        }
    } else if ((by = TxChild(t, L"ScheduleByMonth")) || (by = TxChild(t, L"ScheduleByMonthDayOfWeek"))) {
        BOOL by_date = !wcscmp(by->Name, L"ScheduleByMonth");
        ULONG months = name_bits(TxChild(by, L"Months"), g_months);
        if (!months)
            months = 0xFFF;
        for (LONGLONG day = 0; day < 366 * 5 && r->Count < r->Room; day++) {
            ULONGLONG when = add_days(start, day);
            SYSTEMTIME local;
            if (!when || when > r->To || (end && when > end) || !local_parts(when, &local))
                break;
            if (!(months >> (local.wMonth - 1) & 1) || when + TICKS_PER_DAY < from)
                continue;
            BOOL hit = FALSE;
            SYSTEMTIME ahead;
            if (by_date) {
                BOOL last_day = local_parts(add_days(when, 1), &ahead) && ahead.wDay == 1;
                const TX_NODE *list = TxChild(by, L"DaysOfMonth");
                for (const TX_NODE *c = list ? list->First : nullptr; c && !hit; c = c->Next)
                    hit = (c->Text && !wcscmp(c->Text, L"Last") && last_day) || number(c->Text, 0) == local.wDay;
            } else {
                ULONG days = name_bits(TxChild(by, L"DaysOfWeek"), g_days);
                int week = (local.wDay - 1) / 7 + 1;
                BOOL last_week = local_parts(add_days(when, 7), &ahead) && ahead.wMonth != local.wMonth;
                const TX_NODE *list = TxChild(by, L"Weeks");
                for (const TX_NODE *c = list ? list->First : nullptr; c && !hit; c = c->Next)
                    hit = (days >> local.wDayOfWeek & 1) &&
                          ((c->Text && !wcscmp(c->Text, L"Last") && last_week) || number(c->Text, 0) == week);
            }
            if (hit)
                add_occurrence(r, t, when, end);
        }
    }
}

static void sort_times(ULONGLONG *times, ULONG count)
{
    for (ULONG i = 1; i < count; i++) {
        ULONGLONG v = times[i];
        ULONG j = i;
        for (; j && times[j - 1] > v; j--)
            times[j] = times[j - 1];
        times[j] = v;
    }
}

ULONG TxRunTimes(const TX_NODE *task, ULONGLONG from, ULONGLONG to, ULONGLONG *times, ULONG room)
{
    const TX_NODE *triggers = TxChild(task, L"Triggers");
    ULONG budget = room * 8 + 64;
    RUNS r = {from, to, SvcAlloc(budget * sizeof(ULONGLONG)), 0, budget};
    if (!r.Times)
        return 0;
    for (const TX_NODE *t = triggers ? triggers->First : nullptr; t; t = t->Next)
        trigger_runs(&r, t);
    sort_times(r.Times, r.Count);
    ULONG n = r.Count < room ? r.Count : room;
    memcpy(times, r.Times, n * sizeof(ULONGLONG));
    SvcFree(r.Times);
    return n;
}

BOOL TxHasTrigger(const TX_NODE *task, const WCHAR *name)
{
    const TX_NODE *triggers = TxChild(task, L"Triggers");
    for (const TX_NODE *t = triggers ? triggers->First : nullptr; t; t = t->Next)
        if (!wcscmp(t->Name, name) && flag(t, L"Enabled", TRUE))
            return TRUE;
    return FALSE;
}

LONGLONG TxTriggerDelay(const TX_NODE *task, const WCHAR *name)
{
    const TX_NODE *triggers = TxChild(task, L"Triggers");
    for (const TX_NODE *t = triggers ? triggers->First : nullptr; t; t = t->Next) {
        LONGLONG delay = 0;
        if (!wcscmp(t->Name, name) && flag(t, L"Enabled", TRUE))
            return TxParseDuration(TxText(t, L"Delay"), &delay) ? delay : 0;
    }
    return 0;
}

BOOL TxSettingsFlag(const TX_NODE *task, const WCHAR *name, BOOL fallback)
{
    return flag(TxChild(task, L"Settings"), name, fallback);
}
