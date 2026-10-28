/*
 * zipfldr.dll, the compressed folders of the shell: a zip file read as a folder. The namespace extension is the class
 * CLSID_CompressedFolder, which the type .zip names in the registry as the class of its files, so the folders of the shell
 * open a zip file like a directory. Only reading is done: listing, binding to a folder inside and reading a file as a
 * stream. The stored and deflated entries are read, with zip64 sizes.
 */
#ifndef MIME_ZIPFLDR_PRIVATE_H
#define MIME_ZIPFLDR_PRIVATE_H

#include <mime/win32/shlfolder.h>
#include <rtl.h>

#ifndef ERROR_FILE_CORRUPT
#define ERROR_FILE_CORRUPT 1392L
#endif

typedef struct _ZIP_ENTRY {
    /* the path below the root of the archive, separated by backslashes. */
    LPWSTR Name;
    SIZE_T Length;
    FILETIME Time;
    ULONGLONG CompressedSize, Size, Offset;
    DWORD Crc;
    WORD Method;
    BOOL Folder, Encrypted;
} ZIP_ENTRY;

typedef struct _ZIP_ARCHIVE {
    LONG Refs;
    WCHAR Path[MAX_PATH];
    ZIP_ENTRY *Entries;
    ULONG Count;
    /* the entries by name, so that the entries below a folder are next to each other. */
    ULONG *Order;
} ZIP_ARCHIVE;

/* zip.c */
void ZipInit(void);
HRESULT ZipOpen(LPCWSTR path, ZIP_ARCHIVE **archive);
void ZipAddRef(ZIP_ARCHIVE *archive);
void ZipRelease(ZIP_ARCHIVE *archive);
int ZipCompare(LPCWSTR a, SIZE_T a_length, LPCWSTR b, SIZE_T b_length);
/* the position in the order of the first entry named the prefix or after it. */
ULONG ZipLowerBound(const ZIP_ARCHIVE *archive, LPCWSTR prefix, SIZE_T length);
BOOL ZipHasPrefix(LPCWSTR name, LPCWSTR prefix, SIZE_T length);
LONG ZipFind(const ZIP_ARCHIVE *archive, LPCWSTR name, SIZE_T length);
HRESULT ZipOpenStream(ZIP_ARCHIVE *archive, ULONG entry, IStream **stream);

/* folder.c */
HRESULT ZipFolderCreate(REFIID riid, void **object);

#endif
