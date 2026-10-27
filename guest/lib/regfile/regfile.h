/*
 * the reader and the writer of registry script files (.reg), for regedit.exe and reg.exe.
 *
 * the reader takes the three formats: version 5.00, REGEDIT4 and the old REGEDIT of windows 3.1, as UTF-16 with a
 * byte order mark, UTF-8 with one, or ANSI. it knows nothing of the system: the file, the registry and the memory are
 * a table of functions, so the tests run it on any host. text is 16 bit units, which are wchar_t on windows.
 */
#ifndef MIME_REGFILE_H
#define MIME_REGFILE_H

#include <stddef.h>
#include <stdint.h>

typedef uint16_t MimeRegChar;

/* the root keys, as the index of a root name. */
enum {
    MIME_REG_ROOT_CLASSES = 0,
    MIME_REG_ROOT_USER = 1,
    MIME_REG_ROOT_MACHINE = 2,
    MIME_REG_ROOT_USERS = 3,
    MIME_REG_ROOT_CONFIG = 4,
    MIME_REG_ROOTS = 5,
};

/* how an import ended. a later failure replaces an earlier one, as the programs report only the last. */
typedef enum MimeRegImportStatus {
    MIME_REG_OK = 0,
    MIME_REG_READ_FAILED,     /* the file could not be read. */
    MIME_REG_BAD_HEADER,      /* the first line begins like a header and goes wrong. */
    MIME_REG_NOT_A_SCRIPT,    /* no header of a registry script at all. */
    MIME_REG_WRONG_VERSION,   /* "Windows Registry Editor Version" with a number other than 5.00. */
    MIME_REG_KEY_FAILED,      /* a key of the file could not be created. */
    MIME_REG_VALUE_FAILED,    /* a value could not be written. */
} MimeRegImportStatus;

typedef struct MimeRegImportHost {
    void *Context;
    /* up to size bytes of the file, the number read, 0 at the end and -1 when it fails. */
    long (*Read)(void *context, void *buffer, uint32_t size);
    /* allocation: realloc(NULL, n) allocates, realloc(p, 0) frees. */
    void *(*Realloc)(void *context, void *memory, size_t size);
    /* ANSI text in the code page of the thread to text, the number of units made, 0 when it fails. */
    uint32_t (*AnsiToWide)(void *context, const char *text, uint32_t bytes, MimeRegChar *out, uint32_t capacity);
    /*
     * the key of a root and a subkey, subkey NULL for the root itself. the key is opened for writing and made when it is
     * missing. returns 0 or an error.
     */
    int (*CreateKey)(void *context, int root, const MimeRegChar *subkey, void **key);
    /* the key of a root and a subkey with everything below it; the result does not matter. */
    void (*DeleteKey)(void *context, int root, const MimeRegChar *subkey);
    void (*CloseKey)(void *context, void *key);
    /*
     * a value, the name NULL for the default value. ansi says that the name and the data are in the code page of the
     * thread, which is how the 4.0 format writes the types REG_EXPAND_SZ and REG_MULTI_SZ. returns 0 or an error.
     */
    int (*SetValue)(void *context, void *key, const MimeRegChar *name, uint32_t type, const void *data, uint32_t size,
                    int ansi);
    void (*DeleteValue)(void *context, void *key, const MimeRegChar *name);
    /* the default value of a key under HKEY_CLASSES_ROOT, as a string of the 3.1 format. returns 0 or an error. */
    int (*SetClassDefault)(void *context, const MimeRegChar *subkey, const MimeRegChar *text, uint32_t bytes);
    /* the longest key name of a key line, in units: 10640 in regedit and 2960 in reg. */
    uint32_t KeyNameLimit;
    /* accept the UTF-8 byte order mark, as regedit does and reg does not. */
    int Utf8;
    /* \n and \r in strings, which only the window of regedit allows. */
    int MultiLineStrings;
} MimeRegImportHost;

/* reads the whole file. the file is positioned at its start. */
MimeRegImportStatus MimeRegImport(const MimeRegImportHost *host);

/* ------------------------------------------------------------------ the writer */

typedef struct MimeRegWriter {
    void *Context;
    /* bytes to the file, nonzero when written. */
    int (*Write)(void *context, const void *data, uint32_t bytes);
    /* text to ANSI in the code page of the thread, the number of bytes made, 0 when it fails. */
    uint32_t (*WideToAnsi)(void *context, const MimeRegChar *text, uint32_t count, char *out, uint32_t capacity);
    int Ansi;
    int MultiLineStrings;
    int Failed;
    uint32_t Count, Column;
    MimeRegChar Buffer[0x10000];
    char Bytes[0x20004];
} MimeRegWriter;

/* starts the file: a byte order mark and the 5.00 line, or the REGEDIT4 line, then a blank line. */
void MimeRegWriterBegin(MimeRegWriter *writer, int ansi);
/* "[path]" and the end of the line. */
void MimeRegWriteKey(MimeRegWriter *writer, const MimeRegChar *path);
/* one value of a key. name is NULL or empty for the default value. data of a string needs not end in a NUL. */
void MimeRegWriteValue(MimeRegWriter *writer, const MimeRegChar *name, uint32_t type, const void *data, uint32_t size);
/* the blank line that ends a key. */
void MimeRegWriteEnd(MimeRegWriter *writer);
/* what is left in the buffer, to the file; returns 0 when anything failed to be written. */
int MimeRegWriterFlush(MimeRegWriter *writer);

#endif
