#ifndef MIME_CBS_H
#define MIME_CBS_H

/*
 * servicing packages: the cabinets Windows installs through its component servicing, and the .NET Framework offline
 * installer that carries them. a package holds component manifests and one payload folder per component; the library
 * lays the files out, makes the links and global assembly cache entries, and writes the registry values the manifests
 * describe. the file system and the registry are reached through a target, so the logic runs on the host in tests.
 */
#include <stddef.h>
#include <stdint.h>

enum {
    CBS_OK = 0,
    CBS_BAD,         /* not a package, or truncated or inconsistent. */
    CBS_UNSUPPORTED, /* a compression or a coder the library does not know. */
    CBS_CORRUPT,     /* data fails its checksum or its hash. */
    CBS_MEMORY,
    CBS_STOPPED,   /* a sink or the target asked to stop. */
    CBS_NOT_FOUND, /* a file the package should hold is not there. */
    CBS_TARGET_FAILED, /* the target failed, see its last error. */
};

/* ------------------------------------------------------------------ memory */

void *CbsAlloc(size_t size);
void *CbsAllocZero(size_t size);
void CbsFree(void *memory);
/* large, lazily committed blocks such as a dictionary window. */
void *CbsAllocLarge(size_t size);
void CbsFreeLarge(void *memory, size_t size);

/* ------------------------------------------------------------------ 7z archives with lzma and bcj2 */

typedef struct CBS_7Z CBS_7Z;

/* data is the archive from its signature on and must outlive the object. */
int Cbs7zOpen(CBS_7Z **archive, const uint8_t *data, size_t size);
void Cbs7zClose(CBS_7Z *archive);
unsigned Cbs7zCount(const CBS_7Z *archive);
/* utf-8 name, size and crc of a file, false when the index is out of range. */
bool Cbs7zInfo(const CBS_7Z *archive, unsigned index, const char **name, uint64_t *size, uint32_t *crc, bool *has_crc);

/* receives the file in order; a non-zero return stops the extraction with CBS_STOPPED. */
typedef int (*CBS_SINK)(void *context, const uint8_t *data, size_t length);
/* decodes the folder holding the file up to its end and gives the file's bytes to the sink. */
int Cbs7zExtract(CBS_7Z *archive, unsigned index, CBS_SINK sink, void *context);

/* ------------------------------------------------------------------ checksums */

uint32_t CbsCrc32(uint32_t crc, const void *data, size_t length);

/* ------------------------------------------------------------------ xml */

typedef enum {
    CBS_XML_ERROR,
    CBS_XML_END,
    CBS_XML_START, /* an element opened; attributes are valid. */
    CBS_XML_CLOSE, /* an element closed, also after an empty one. */
    CBS_XML_TEXT,  /* text that is not only white space, in Text. */
} CBS_XML_EVENT;

#define CBS_XML_MAX_ATTRIBUTES 24

typedef struct CBS_XML {
    const char *Cursor, *End;
    /* names without their prefix, values with the entities decoded. both point into Buffer. */
    const char *Name;
    unsigned AttributeCount;
    const char *AttributeName[CBS_XML_MAX_ATTRIBUTES];
    const char *AttributeValue[CBS_XML_MAX_ATTRIBUTES];
    const char *Text;
    unsigned Depth; /* of the element of the event, the root being 1. */
    unsigned Open;
    bool PendingClose;
    char *Buffer;
    size_t BufferSize;
} CBS_XML;

int CbsXmlOpen(CBS_XML *xml, const char *text, size_t length);
void CbsXmlClose(CBS_XML *xml);
CBS_XML_EVENT CbsXmlNext(CBS_XML *xml);
const char *CbsXmlAttribute(const CBS_XML *xml, const char *name);

/* ------------------------------------------------------------------ manifests */

typedef enum {
    CBS_ARCH_NEUTRAL,
    CBS_ARCH_X86,
    CBS_ARCH_AMD64,
    CBS_ARCH_ARM64,
    CBS_ARCH_WOW64, /* x86 files that a 64-bit system keeps for its 32-bit programs. */
} CBS_ARCH;

typedef enum {
    CBS_GAC_NONE,
    CBS_GAC_MSIL,
    CBS_GAC_X86,
    CBS_GAC_AMD64,
    CBS_GAC_ARM64,
} CBS_GAC;

typedef enum {
    CBS_REG_SZ = 1,
    CBS_REG_EXPAND_SZ = 2,
    CBS_REG_BINARY = 3,
    CBS_REG_DWORD = 4,
    CBS_REG_MULTI_SZ = 7,
} CBS_REG_TYPE;

typedef struct CBS_ID {
    const char *Name, *Version, *Token;
    CBS_ARCH Arch;
} CBS_ID;

typedef struct CBS_FILE_ENTRY {
    const char *Name;        /* relative to the payload folder, with backslashes. */
    const char *Destination; /* the directory, with variables. */
    /* the sha-256 of the content, from the digest of the manifest. its hash attribute is of another form of the file. */
    uint8_t Digest[32];
    bool HasDigest;
    unsigned FirstLink, LinkCount;
    CBS_GAC Gac;
    bool GacCopy; /* the file itself goes into the cache, otherwise only links. */
    const char *GacVersion;
} CBS_FILE_ENTRY;

typedef struct CBS_REG_KEY {
    const char *Name;
    unsigned FirstValue, ValueCount;
} CBS_REG_KEY;

typedef struct CBS_REG_VALUE {
    const char *Name; /* null for the default value. */
    const char *Data; /* as written in the manifest. */
    CBS_REG_TYPE Type;
} CBS_REG_VALUE;

typedef struct CBS_COMPONENT {
    CBS_ID Id;
    const char *Folder; /* the payload folder, the manifest's name without its extension. */
    CBS_ID *Dependencies;
    unsigned DependencyCount;
    CBS_FILE_ENTRY *Files;
    unsigned FileCount;
    const char **Links;
    unsigned LinkCount;
    const char **Directories;
    unsigned DirectoryCount;
    CBS_REG_KEY *Keys;
    unsigned KeyCount;
    CBS_REG_VALUE *Values;
    unsigned ValueCount;
} CBS_COMPONENT;

typedef struct CBS_ARENA CBS_ARENA;
CBS_ARENA *CbsArenaCreate(void);
void CbsArenaDestroy(CBS_ARENA *arena);
void *CbsArenaAlloc(CBS_ARENA *arena, size_t size);
char *CbsArenaString(CBS_ARENA *arena, const char *text, size_t length);

/* parses one component manifest, with its folder name taken from the file name. the strings live in the arena. */
int CbsManifestParse(CBS_ARENA *arena, const char *file_name, const char *text, size_t length,
                     CBS_COMPONENT **component);

/* ------------------------------------------------------------------ the package set */

typedef struct CBS_TARGET CBS_TARGET;

/* the places a component's variables stand for, as utf-8 paths without a trailing backslash. */
typedef struct CBS_LAYOUT {
    const char *Windows;      /* the Windows directory, such as C:\Windows. */
    const char *System32;     /* where 64-bit system files go; the same as SysWow64 on a 32-bit system. */
    const char *SysWow64;     /* where the 32-bit ones go. */
    const char *ProgramData;  /* the data directory of programs, such as C:\ProgramData. */
    const char *ProgramFiles; /* the folder for 32-bit programs. */
    bool Is64Bit;
    CBS_ARCH Native; /* the processor of the system: x86, amd64 or arm64. */
} CBS_LAYOUT;

/* what the library asks of the system. every call returns 0 on success; the target keeps its own last error. */
struct CBS_TARGET {
    void *Context;
    /* creates the directory and its parents. */
    int (*CreateDirectory)(void *context, const char *path);
    /* state: 0 when the file is absent, 1 when it has this size and, if digest is given, this sha-256, 2 otherwise. */
    int (*FileState)(void *context, const char *path, uint64_t size, const uint8_t *digest, int *state);
    /* opens a file for writing in pieces; End closes it and Abort deletes it. */
    int (*Begin)(void *context, const char *path, uint64_t size, void **file);
    int (*Write)(void *context, void *file, const uint8_t *data, size_t length);
    int (*End)(void *context, void *file);
    void (*Abort)(void *context, void *file);
    /* makes path a hard link of existing, or a copy of it when the file system cannot link. */
    int (*Link)(void *context, const char *existing, const char *path);
    /* view is 64 or 32. the key starts below HKEY_LOCAL_MACHINE. string values come as utf-8 with their terminator,
       multi strings as utf-8 with a terminator after each string and one more at the end, a dword as four bytes. */
    int (*CreateKey)(void *context, int view, const char *key);
    int (*SetValue)(void *context, int view, const char *key, const char *name, uint32_t type, const void *data,
                    size_t size);
    /* reads a dword value, used to keep the release number from going down; fails when it is absent. */
    int (*GetDword)(void *context, int view, const char *key, const char *name, uint32_t *value);
    /* optional: a line of progress or a problem for the log. */
    void (*Log)(void *context, const char *message);
};

typedef struct CBS_PROGRESS {
    void (*Report)(void *context, uint64_t done, uint64_t total);
    void *Context;
} CBS_PROGRESS;

/* installs the components that a servicing cabinet holds for the system, see docs/servicing.md. */
int CbsInstallCabinet(const uint8_t *cabinet, size_t size, const CBS_LAYOUT *layout, const CBS_TARGET *target,
                      const CBS_PROGRESS *progress);

/* what the system's own part of the framework registers, which the package does not carry. */
int CbsInstallFrameworkRegistry(const CBS_LAYOUT *layout, const CBS_TARGET *target);

#endif
