#ifndef MIME_SETUP_PRIVATE_H
#define MIME_SETUP_PRIVATE_H

#define MIME_BUILD_SETUPAPI
#include <mime/win32/setupapi.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winver.h>
#include <rtl.h>

#define SETUPAPI WINSETUPAPI

/* the characters that fit, with the zero. */
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

/* an inf file read into memory: sections of lines of fields, with the strings of its Strings section at hand. */
typedef struct INF_LINE {
    ULONG First;   /* in the fields of the file */
    ULONG Count;   /* fields, with the key as the first one when there is one */
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
    struct INF_FILE *Next; /* the files appended to this one */
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

/* the inf and context behind a handle and a context, or null with the last error set. */
INF_FILE *SetupInfOf(HINF handle);
/* the number of a section of a file, or -1. */
LONG SetupInfFindSection(INF_FILE *file, PCWSTR name);
/* the fields of a line, expanded, each in memory of the caller's to free; the key is field 0 when the line has one. */
PCWSTR SetupInfRawField(INF_FILE *file, const INF_LINE *line, ULONG index);
PWSTR SetupInfExpand(INF_FILE *file, PCWSTR raw);
/* a line of a section as the file with that section has it, by index, or null. */
const INF_LINE *SetupInfLine(INF_FILE *file, ULONG section, ULONG index);
/* a field of a context: 0 is the key, 1 and on the fields after it; expanded, null when there is none. */
PWSTR SetupContextField(const INFCONTEXT *context, DWORD field);
/* the context of the line a section has, for the lookups of the installer. */
BOOL SetupFirstLine(HINF handle, PCWSTR section, PCWSTR key, INFCONTEXT *context);
BOOL SetupNextLine(const INFCONTEXT *in, INFCONTEXT *out);
const INF_LINE *SetupContextLine(const INFCONTEXT *context, INF_FILE **file);
/* the section name with the platform's decoration, when the file has one. */
BOOL SetupDecoratedSection(INF_FILE *head, PCWSTR name, PWSTR out, SIZE_T chars);

/* the directory a number names, with the user's directories of the inf, or an absolute path. */
BOOL SetupDirectoryPath(INF_FILE *file, UINT id, PCWSTR absolute, PWSTR out, SIZE_T chars);
/* the text with the %number% directory marks replaced, as the fields that name files have them. */
void SetupExpandDirectories(INF_FILE *file, PCWSTR text, PWSTR out, SIZE_T chars);

typedef enum QUEUE_KIND { QUEUE_COPY, QUEUE_DELETE, QUEUE_RENAME } QUEUE_KIND;

typedef struct QUEUE_ITEM {
    QUEUE_KIND Kind;
    PWSTR SourceDirectory, SourceName; /* what a copy or rename is from, what is deleted */
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
/* the files of a copy, delete or rename section into a queue. */
BOOL SetupQueueSections(HSPFILEQ queue, INF_FILE *file, INF_FILE *layout, PCWSTR section, PCWSTR source_root, DWORD style,
                        BOOL copies, BOOL deletes, BOOL renames);
/* commits with a handler of wide structures, or the default one. */
BOOL SetupCommit(FILE_QUEUE *queue, PSP_FILE_CALLBACK_W handler, PVOID context, BOOL ansi_handler, PVOID raw_handler);

BOOL SetupInstallInfSection(HWND owner, HINF handle, PCWSTR section, UINT flags, HKEY key_root, PCWSTR source_root,
                            UINT copy_flags, PVOID handler, PVOID context, BOOL ansi);

#endif
