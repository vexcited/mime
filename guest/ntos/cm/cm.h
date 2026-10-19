/*
 * configuration manager internals: the registry is a tree of nodes kept in
 * memory, loaded lazily from the hive files of the c: drive. nonvolatile
 * changes are committed to a separate journal before becoming visible.
 */
#ifndef MIME_CM_H
#define MIME_CM_H

#include "../ntos.h"
#include "journal_format.h"

#define REG_NONE 0
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_LINK 6
#define REG_MULTI_SZ 7
#define REG_QWORD 11

#define REG_OPTION_VOLATILE 0x00000001
#define REG_OPTION_CREATE_LINK 0x00000002
#define REG_CREATED_NEW_KEY 1
#define REG_OPENED_EXISTING_KEY 2

#define STATUS_KEY_DELETED ((NTSTATUS)0xC000017CL)
#define STATUS_CHILD_MUST_BE_VOLATILE ((NTSTATUS)0xC0000181L)
#define STATUS_CANNOT_DELETE ((NTSTATUS)0xC0000121L)
#define STATUS_REGISTRY_CORRUPT ((NTSTATUS)0xC000014CL)
#define STATUS_NOTIFY_CLEANUP ((NTSTATUS)0x0000010BL)
#define STATUS_NOTIFY_ENUM_DIR ((NTSTATUS)0x0000010CL)

#define REG_NOTIFY_CHANGE_NAME 0x00000001
#define REG_NOTIFY_CHANGE_ATTRIBUTES 0x00000002
#define REG_NOTIFY_CHANGE_LAST_SET 0x00000004
#define REG_NOTIFY_CHANGE_SECURITY 0x00000008

/* ------------------------------------------------------------------ hive.c */

/* a regf hive file, read on demand through the file cache. */
typedef struct CM_HIVE {
    PFILE_OBJECT File;
    ULONG64 Size;
    ULONG RootCell;
    ULONG Minor;
} CM_HIVE;

/* the fixed part of a key node cell. */
typedef struct CM_HIVE_KEY {
    USHORT Flags;
    LARGE_INTEGER LastWrite;
    ULONG SubKeyCount;
    ULONG SubKeyList;
    ULONG ValueCount;
    ULONG ValueList;
    USHORT NameLength; /* in characters */
    WCHAR Name[256];
} CM_HIVE_KEY;

#define CM_HIVE_KEY_LINK 0x0010
#define CM_HIVE_NAME_ASCII 0x0020

NTSTATUS CmpOpenHive(PCWSTR path, CM_HIVE **out);
/* the security descriptor the hive keeps for a key node, whole, in a blob. */
NTSTATUS CmpReadSecurity(CM_HIVE *hive, ULONG cell, SD_BLOB **out);
NTSTATUS CmpReadKey(CM_HIVE *hive, ULONG cell, CM_HIVE_KEY *key);
/* calls back for each subkey cell of a key node, in the hive's sorted order. */
NTSTATUS CmpForEachSubKey(CM_HIVE *hive, const CM_HIVE_KEY *key, NTSTATUS (*fn)(PVOID ctx, ULONG cell), PVOID ctx);
/* reads value number index of a key: name, type and a pool copy of its data. */
NTSTATUS CmpReadValue(CM_HIVE *hive, const CM_HIVE_KEY *key, ULONG index, PWCHAR name, PUSHORT name_chars,
                      PULONG type, PUCHAR *data, PULONG data_length);

/* ------------------------------------------------------------------ registry.c */

typedef struct CM_VALUE {
    struct CM_VALUE *Next;
    ULONG Type;
    ULONG DataLength;
    PUCHAR Data;
    USHORT NameLength; /* in characters */
    WCHAR Name[];
} CM_VALUE;

#define CM_NODE_CHILDREN_LOADED 0x01
#define CM_NODE_VALUES_LOADED 0x02
#define CM_NODE_VOLATILE 0x04
#define CM_NODE_DELETED 0x08
#define CM_NODE_LINK 0x10
#define CM_NODE_SD_UNREADABLE 0x20 /* the hive has no usable descriptor for the key */

typedef struct CM_NODE {
    struct CM_NODE *Parent;
    struct CM_NODE *Child; /* first child, children sorted by name */
    struct CM_NODE *Next;
    CM_HIVE *Hive; /* NULL for keys created since boot */
    ULONG Cell;
    ULONG Flags;
    CM_VALUE *Values;
    SD_BLOB *Sd; /* the descriptor of the key: one a program set, or the hive's own once read */
    LARGE_INTEGER LastWrite;
    USHORT NameLength;
    WCHAR Name[];
} CM_NODE;

extern FAST_MUTEX CmpLock;
extern CM_NODE *CmpRoot;
extern POBJECT_TYPE CmKeyType;

/* what an open key handle refers to: the node, and the block of the change notification asked on the handle. */
typedef struct CM_NOTIFY CM_NOTIFY;
typedef struct CM_KEY_BODY {
    CM_NODE *Node;
    CM_NOTIFY *Notify;
} CM_KEY_BODY;

/* answers of change notifications are written once the lock is out, so it is released with this. */
extern volatile LONG CmpNotifyReady;
void CmpNotifyFinish(void);
static inline void CmpUnlock(void)
{
    ExReleaseFastMutex(&CmpLock);
    if (CmpNotifyReady)
        CmpNotifyFinish();
}

/* lock held for all of these. */
CM_NODE *CmpFindChild(CM_NODE *node, const WCHAR *name, ULONG chars);
CM_NODE *CmpCreateChild(CM_NODE *node, const WCHAR *name, ULONG chars, ULONG flags);
CM_NODE *CmpMount(CM_NODE *node, PCWSTR name, CM_HIVE *hive);
CM_NODE *CmpOpenPath(PCWSTR path, BOOLEAN create);
CM_VALUE *CmpFindValue(CM_NODE *node, const WCHAR *name, ULONG chars);
NTSTATUS CmpSetValue(CM_NODE *node, const WCHAR *name, ULONG chars, ULONG type, const void *data, ULONG length);
NTSTATUS CmpDeleteValue(CM_NODE *node, const WCHAR *name, ULONG chars);
NTSTATUS CmpLoadValues(CM_NODE *node);
NTSTATUS CmpLoadChildren(CM_NODE *node);
ULONG CmpFullPath(CM_NODE *node, PWCHAR out, ULONG capacity);
NTSTATUS CmpRestoreJournal(const CmJournalRecord *record, const UCHAR *payload);
/* a referenced body for a key named by the caller, as a handle opened with that access would refer to. */
NTSTATUS CmpReferenceKeyByName(POBJECT_ATTRIBUTES attributes, KPROCESSOR_MODE mode, ACCESS_MASK access,
                               CM_KEY_BODY **body);

/* ------------------------------------------------------------------ notify.c */

void CmpNotifyInitialize(void);
/* lock held: a key's names, values, attributes or security changed. */
void CmpNotifyChange(CM_NODE *node, ULONG filter);
/* lock held: the key was deleted. */
void CmpNotifyKeyDeleted(CM_NODE *node);
/* the last handle of a key closed, or the body is deleted, lock not held. */
void CmpNotifyClose(CM_KEY_BODY *body);
void CmpNotifyDelete(CM_KEY_BODY *body);

/* ------------------------------------------------------------------ journal.c */

void CmpJournalInitialize(void);
void CmpJournalEnable(void);
NTSTATUS CmpJournalAppend(CM_NODE *node, ULONG operation, ULONG type, const WCHAR *name, ULONG chars,
                         const void *data, ULONG length, LARGE_INTEGER last_write);
/* compact.c: the records of a long journal that still count are written again, as the journal. */
PVOID CmpCompactBegin(void);
void CmpCompactNote(PVOID context, ULONG64 offset, const CmJournalRecord *record, const UCHAR *payload);
void CmpCompactFinish(PVOID context, PFILE_OBJECT *journal, ULONG64 *offset, ULONG64 *sequence);
void CmpCompactAbandon(PVOID context);
NTSTATUS CmpJournalReadBytes(void *data, ULONG bytes, ULONG64 offset);
/* starts the worker making appended records durable, once the system can run threads. */
void CmpJournalStart(void);
/* what RegFlushKey asks for: every record appended before the call is on the host disk when it succeeds. */
NTSTATUS CmpJournalFlush(BOOLEAN volatile_key);

/* ------------------------------------------------------------------ boot.c */

void CmpSpecialize(void);

/* ------------------------------------------------------------------ certs.c */

/* writes the roots of the host's certificate bundle into the machine's AuthRoot store, unjournaled. */
void CmpImportHostRoots(void);

#endif
