/* the xml of scheduled tasks, see taskxml.c. */
#ifndef MIME_SERVICES_TASKXML_H
#define MIME_SERVICES_TASKXML_H

#define SCHED_E_UNEXPECTEDNODE_ ((HRESULT)0x80041316L)
#define SCHED_E_NAMESPACE_ ((HRESULT)0x80041317L)
#define SCHED_E_INVALIDVALUE_ ((HRESULT)0x80041318L)
#define SCHED_E_MISSINGNODE_ ((HRESULT)0x80041319L)
#define SCHED_E_MALFORMEDXML_ ((HRESULT)0x8004131AL)
#define SCHED_E_DEPRECATED_FEATURE_USED_ ((HRESULT)0x80041330L)

typedef struct TX_ATTR {
    struct TX_ATTR *Next;
    WCHAR *Name, *Value;
    ULONG Line, Column;
} TX_ATTR;

typedef struct TX_NODE {
    struct TX_NODE *Next, *First, *Parent;
    WCHAR *Name, *Text;
    TX_ATTR *Attrs;
    ULONG Line, Column;       /* where the name of the start tag is, as windows tells it. */
    ULONG EndLine, EndColumn; /* the same for the end tag. */
    BOOL Cdata;
} TX_NODE;

/* where windows says a task is wrong: the line, the column, the name of the node and its value. */
typedef struct TX_ERROR {
    ULONG Line, Column;
    WCHAR *Node, *Value;
} TX_ERROR;

/* the principal a task is kept with. */
typedef struct TX_PRINCIPAL {
    const WCHAR *Id;
    const WCHAR *UserId, *GroupId, *DisplayName;
    const WCHAR *LogonType;
    BOOL Highest;
} TX_PRINCIPAL;

typedef struct TX_BUF {
    WCHAR *Text;
    SIZE_T Used, Room;
    BOOL Failed;
} TX_BUF;

HRESULT TxRead(const WCHAR *text, TX_NODE **task, TX_ERROR *info);
void TxFreeError(TX_ERROR *info);
void TxFree(TX_NODE *n);
HRESULT TxWrite(const TX_NODE *task, const TX_PRINCIPAL *who, const WCHAR *uri, BOOL enabled, WCHAR **out);
void TxBufPut(TX_BUF *b, const WCHAR *text);

TX_NODE *TxChild(const TX_NODE *n, const WCHAR *name);
const WCHAR *TxText(const TX_NODE *n, const WCHAR *name);
const WCHAR *TxAttr(const TX_NODE *n, const WCHAR *name);
BOOL TxParseBool(const WCHAR *text, BOOL *out);
BOOL TxParseDuration(const WCHAR *text, LONGLONG *seconds);
BOOL TxParseTime(const WCHAR *text, ULONGLONG *filetime);
BOOL TxLocalToUtc(const SYSTEMTIME *local, ULONGLONG *utc);
BOOL TxUtcToLocal(ULONGLONG utc, SYSTEMTIME *local);

/* the times in [from, to] the time and calendar triggers start the task, sorted, as utc filetimes. */
ULONG TxRunTimes(const TX_NODE *task, ULONGLONG from, ULONGLONG to, ULONGLONG *times, ULONG room);
BOOL TxHasTrigger(const TX_NODE *task, const WCHAR *name);
LONGLONG TxTriggerDelay(const TX_NODE *task, const WCHAR *name);
BOOL TxSettingsFlag(const TX_NODE *task, const WCHAR *name, BOOL fallback);

#endif
