/*
 * what tasklist and taskkill share: the processes of the machine with the properties their filters and columns ask
 * for, and the filters themselves.
 */
#ifndef MIME_PROCUTIL_H
#define MIME_PROCUTIL_H

#include "tool.h"

typedef struct PROC_ENTRY {
    DWORD Pid;
    DWORD Parent;
    DWORD Session;
    DWORD Threads;
    WCHAR *Name;
    ULONGLONG MemoryKb;
    ULONGLONG CpuSeconds;
    /* null until the matching load. */
    WCHAR *User;
    WCHAR *Title;
    WCHAR *Services;
    BOOL HasWindow;
    BOOL Hung;
} PROC_ENTRY;

typedef struct PROC_LIST {
    PROC_ENTRY *Items;
    DWORD Count;
    BOOL UsersLoaded;
    BOOL WindowsLoaded;
    BOOL ServicesLoaded;
} PROC_LIST;

BOOL proc_list_load(PROC_LIST *list);
void proc_list_free(PROC_LIST *list);
void proc_load_users(PROC_LIST *list);
void proc_load_windows(PROC_LIST *list);
void proc_load_services(PROC_LIST *list);
PROC_ENTRY *proc_find(PROC_LIST *list, DWORD pid);
/* the names of the modules a process has loaded, separated by commas, or null when they cannot be read. */
WCHAR *proc_modules(DWORD pid);
/* some name of the comma separated list fits the pattern. */
BOOL proc_list_matches(const WCHAR *list, const WCHAR *pattern);
/* the session's name in the form the tools print it. */
const WCHAR *proc_session_name(DWORD session);

#define FILTER_STATUS 0
#define FILTER_IMAGENAME 1
#define FILTER_PID 2
#define FILTER_SESSION 3
#define FILTER_SESSIONNAME 4
#define FILTER_CPUTIME 5
#define FILTER_MEMUSAGE 6
#define FILTER_USERNAME 7
#define FILTER_SERVICES 8
#define FILTER_WINDOWTITLE 9
#define FILTER_MODULES 10

#define OP_EQ 0
#define OP_NE 1
#define OP_GT 2
#define OP_LT 3
#define OP_GE 4
#define OP_LE 5

typedef struct PROC_FILTER {
    int Field;
    int Op;
    WCHAR *Value;
    ULONGLONG Number;
} PROC_FILTER;

typedef struct PROC_FILTERS {
    PROC_FILTER *Items;
    int Count;
} PROC_FILTERS;

/* adds a filter written as `name op value`; false with the text of the problem in error. */
BOOL proc_filter_add(PROC_FILTERS *filters, const WCHAR *text, TU_STR *error);
BOOL proc_filter_needs(const PROC_FILTERS *filters, int field);
BOOL proc_filter_match(const PROC_FILTERS *filters, PROC_ENTRY *process);
/* the image name matches with a wildcard. */
BOOL proc_name_match(const WCHAR *pattern, const WCHAR *name);
const WCHAR *proc_status_name(const PROC_ENTRY *process);

#endif
