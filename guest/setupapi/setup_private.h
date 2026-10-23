#ifndef MIME_SETUP_PRIVATE_H
#define MIME_SETUP_PRIVATE_H

#define MIME_BUILD_SETUPAPI
#include <mime/win32/setupapi.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winver.h>
#include <rtl.h>

#define SETUPAPI WINSETUPAPI

/* truncates to fit, always zero terminated. */
static inline void SetupCopyText(PWSTR out, PCWSTR text, SIZE_T chars)
{
    SIZE_T n = wcslen(text);
    if (n >= chars)
        n = chars - 1;
    memcpy(out, text, n * sizeof(WCHAR));
    out[n] = 0;
}

#define INF_MAGIC 0x464e4953 /* 'SINF' */
#define QUEUE_MAGIC 0x51454c46

typedef struct INF_LINE {
    ULONG First;   /* index into the file's fields. */
    ULONG Count;   /* the key, when there is one, is the first field. */
    BOOLEAN HasKey;
} INF_LINE;

typedef struct INF_SECTION {
    PWSTR Name;
    ULONG FirstLine, LineCount;
} INF_SECTION;

typedef struct INF_USER_DIRECTORY {
    DWORD Id;
    PWSTR Path;
} INF_USER_DIRECTORY;

typedef struct INF_FILE {
    ULONG Magic;
    struct INF_FILE *Next; /* files appended to this one. */
    WCHAR Path[MAX_PATH];
    INF_SECTION *Sections;
    ULONG SectionCount, SectionCapacity;
    INF_LINE *Lines;
    ULONG LineCount, LineCapacity;
    PWSTR *Fields;
    ULONG FieldCount, FieldCapacity;
    INF_USER_DIRECTORY *Directories;
    ULONG DirectoryCount;
} INF_FILE;

void *SetupAlloc(SIZE_T size);
void SetupFree(void *memory);
PWSTR SetupDuplicate(PCWSTR text);
PWSTR SetupToWide(PCSTR text);
PSTR SetupToAnsi(PCWSTR text);

/* NULL with the last error set for a bad handle. */
INF_FILE *SetupInfOf(HINF handle);
/* the section's index, or -1. */
LONG SetupInfFindSection(INF_FILE *file, PCWSTR name);
/* the key, when there is one, is field 0. expanded fields are the caller's to free. */
PCWSTR SetupInfRawField(INF_FILE *file, const INF_LINE *line, ULONG index);
PWSTR SetupInfExpand(INF_FILE *file, PCWSTR raw);
/* by index, from the file that has the section, or NULL. */
const INF_LINE *SetupInfLine(INF_FILE *file, ULONG section, ULONG index);
/* field 0 is the key. expanded, or NULL when absent. */
PWSTR SetupContextField(const INFCONTEXT *context, DWORD field);
BOOL SetupFirstLine(HINF handle, PCWSTR section, PCWSTR key, INFCONTEXT *context);
BOOL SetupNextLine(const INFCONTEXT *in, INFCONTEXT *out);
const INF_LINE *SetupContextLine(const INFCONTEXT *context, INF_FILE **file);
/* adds this platform's decoration when the file has such a section. */
BOOL SetupDecoratedSection(INF_FILE *head, PCWSTR name, PWSTR out, SIZE_T chars);

/* resolves a directory id, including the inf's user directories, or takes the absolute path. */
BOOL SetupDirectoryPath(INF_FILE *file, UINT id, PCWSTR absolute, PWSTR out, SIZE_T chars);
/* replaces %number% directory marks, as found in fields that name files. */
void SetupExpandDirectories(INF_FILE *file, PCWSTR text, PWSTR out, SIZE_T chars);

typedef enum QUEUE_KIND { QUEUE_COPY, QUEUE_DELETE, QUEUE_RENAME } QUEUE_KIND;

typedef struct QUEUE_ITEM {
    QUEUE_KIND Kind;
    PWSTR SourceDirectory, SourceName; /* the copy or rename source, or the file deleted. */
    PWSTR TargetDirectory, TargetName;
    PWSTR Description, TagFile;
    DWORD Style;
} QUEUE_ITEM;

typedef struct FILE_QUEUE {
    ULONG Magic;
    QUEUE_ITEM *Items;
    ULONG Count, Capacity;
} FILE_QUEUE;

FILE_QUEUE *SetupQueueOf(HSPFILEQ queue);
BOOL SetupQueueAdd(FILE_QUEUE *queue, QUEUE_KIND kind, PCWSTR source_directory, PCWSTR source_name, PCWSTR target_directory,
                   PCWSTR target_name, PCWSTR description, PCWSTR tag_file, DWORD style);
/* queues the files of copy, delete or rename sections. */
BOOL SetupQueueSections(HSPFILEQ queue, INF_FILE *file, INF_FILE *layout, PCWSTR section, PCWSTR source_root, DWORD style,
                        BOOL copies, BOOL deletes, BOOL renames);
/* commits with a wide handler, or with the default one. */
BOOL SetupCommit(FILE_QUEUE *queue, PSP_FILE_CALLBACK_W handler, PVOID context, BOOL ansi_handler, PVOID raw_handler);

BOOL SetupInstallInfSection(HWND owner, HINF handle, PCWSTR section, UINT flags, HKEY key_root, PCWSTR source_root,
                            UINT copy_flags, PVOID handler, PVOID context, BOOL ansi);

#endif
