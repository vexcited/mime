/*
 * the registry of the machine for the reader and the writer of .reg files, shared by regedit.exe and reg.exe.
 */
#ifndef MIME_REGSYS_H
#define MIME_REGSYS_H

#include "regfile.h"
#include <mime/win32/winbase.h>

typedef struct MimeRegSysOptions {
    /* KEY_WOW64_32KEY or KEY_WOW64_64KEY, or 0 for the view of the process. */
    DWORD View;
    /* the access a key is created with. */
    DWORD CreateAccess;
    /* reg.exe takes the shorter key names and does not know the UTF-8 mark. */
    BOOL RegExe;
} MimeRegSysOptions;

/* the key of a root, and the root named by the first characters of a name, case ignored, or -1. */
HKEY MimeRegSysRootKey(int root);
int MimeRegSysFindRoot(const WCHAR *name, SIZE_T length);

/* the name of a root key in capitals. */
const WCHAR *MimeRegSysRootName(int root);

/* a key and all of its subkeys. */
LSTATUS MimeRegSysDeleteTree(HKEY root, const WCHAR *subkey, DWORD view);

/* reads a .reg file, which is positioned at its start. */
MimeRegImportStatus MimeRegSysImport(HANDLE file, const MimeRegSysOptions *options);

/* a writer that writes to a file. */
void MimeRegSysInitWriter(MimeRegWriter *writer, HANDLE file);

/*
 * the text of a key and everything below it, path being the name written for the key. the values that cannot be read are
 * left out, and so are the subkeys that cannot be opened. returns FALSE once a write has failed.
 */
BOOL MimeRegSysExportKey(MimeRegWriter *writer, HKEY key, const WCHAR *path, DWORD view);

#endif
