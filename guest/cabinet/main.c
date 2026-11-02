/*
 * cabinet.dll's FDI reader. the cabinet is read into memory through the caller's file callbacks, so cabinets kept
 * in memory or in a custom archive work like plain files, then unpacked by guest/lib/cabinet: stored, MSZIP and LZX.
 *
 * not done: cabinet sets, so a file or folder continued in another cabinet is skipped and one begun in an earlier
 * cabinet is only announced, Quantum, the decryption callback, truncation, FCI and the compression api.
 */
#define MIME_BUILD_CABINET
#include "../lib/cabinet/cabinet.h"
#include <mime/win32/fdi.h>
#include <mime/win32/winbase.h>
#include <rtl.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

#define OPEN_BINARY_READ 0x8000
#define OPEN_MODE 0x180
#define CFHEADER_SIZE 36
#define CAB_FLAG_PREV 1
#define CAB_FLAG_NEXT 2
#define CAB_FLAG_RESERVE 4
#define FOLDER_CONTINUED_FROM_PREV 0xFFFD
#define CABINET_PATH_MAX 0x100
#define NAME_MAX_ 0x100

typedef struct FDI {
    PERF Erf;
    PFNFREE Free;
    PFNALLOC Alloc;
    PFNOPEN Open;
    PFNREAD Read;
    PFNWRITE Write;
    PFNCLOSE Close;
    PFNSEEK Seek;
    int Cpu;
} FDI;

static void fail(FDI *fdi, int operation, int type)
{
    fdi->Erf->erfOper = operation;
    fdi->Erf->erfType = type;
    fdi->Erf->fError = TRUE;
}

static USHORT word(const BYTE *p)
{
    return (USHORT)(p[0] | p[1] << 8);
}

static ULONG dword(const BYTE *p)
{
    return (ULONG)p[0] | (ULONG)p[1] << 8 | (ULONG)p[2] << 16 | (ULONG)p[3] << 24;
}

HFDI DIAMONDAPI FDICreate(PFNALLOC pfnalloc, PFNFREE pfnfree, PFNOPEN pfnopen, PFNREAD pfnread, PFNWRITE pfnwrite,
                          PFNCLOSE pfnclose, PFNSEEK pfnseek, int cpuType, PERF perf)
{
    if (!perf)
        return nullptr;
    perf->erfOper = FDIERROR_NONE;
    perf->erfType = 0;
    perf->fError = FALSE;
    FDI *fdi = pfnalloc(sizeof(FDI));
    if (!fdi) {
        perf->erfOper = FDIERROR_ALLOC_FAIL;
        perf->erfType = 0;
        perf->fError = TRUE;
        return nullptr;
    }
    fdi->Erf = perf;
    fdi->Free = pfnfree;
    fdi->Alloc = pfnalloc;
    fdi->Open = pfnopen;
    fdi->Read = pfnread;
    fdi->Write = pfnwrite;
    fdi->Close = pfnclose;
    fdi->Seek = pfnseek;
    fdi->Cpu = cpuType;
    return fdi;
}

BOOL DIAMONDAPI FDIDestroy(HFDI hfdi)
{
    FDI *fdi = hfdi;
    if (!fdi)
        return FALSE;
    fdi->Free(fdi);
    return TRUE;
}

/* reads the header at the file's position and checks this reader supports it. */
static BOOL read_header(FDI *fdi, INT_PTR file, BYTE header[CFHEADER_SIZE])
{
    if (fdi->Read(file, header, CFHEADER_SIZE) != CFHEADER_SIZE || dword(header) != 0x4643534D)
        return FALSE;
    return TRUE;
}

BOOL DIAMONDAPI FDIIsCabinet(HFDI hfdi, INT_PTR hf, PFDICABINETINFO pfdici)
{
    FDI *fdi = hfdi;
    BYTE header[CFHEADER_SIZE] = {};
    if (!fdi || !read_header(fdi, hf, header))
        return FALSE;
    /* minor byte then major; 1.3 is the only version in use. */
    if (word(header + 24) != 0x0103) {
        fail(fdi, FDIERROR_UNKNOWN_CABINET_VERSION, word(header + 24));
        return FALSE;
    }
    if (pfdici) {
        USHORT flags = word(header + 30);
        pfdici->cbCabinet = (long)dword(header + 8);
        pfdici->cFolders = word(header + 26);
        pfdici->cFiles = word(header + 28);
        pfdici->setID = word(header + 32);
        pfdici->iCabinet = word(header + 34);
        pfdici->fReserve = (flags & CAB_FLAG_RESERVE) != 0;
        pfdici->hasprev = (flags & CAB_FLAG_PREV) != 0;
        pfdici->hasnext = (flags & CAB_FLAG_NEXT) != 0;
    }
    return TRUE;
}

BOOL DIAMONDAPI FDITruncateCabinet(HFDI hfdi, char *pszCabinetName, USHORT iFolderToDelete)
{
    FDI *fdi = hfdi;
    if (!fdi || !pszCabinetName)
        return FALSE;
    fail(fdi, FDIERROR_MDI_FAIL, 0);
    return FALSE;
}

/* ------------------------------------------------------------------ the cabinet in memory */

typedef struct CABINET {
    FDI *Fdi;
    BYTE *Data;
    ULONG Size;
    MIME_CAB *Cab;
    /* empty when the header has none. */
    char Previous[NAME_MAX_], PreviousDisk[NAME_MAX_], Next[NAME_MAX_], NextDisk[NAME_MAX_];
} CABINET;

/* the reader's own memory comes from the process heap, not the caller's allocator. */
static void *cab_allocate(size_t size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

static void cab_release(void *memory)
{
    HeapFree(GetProcessHeap(), 0, memory);
}

static void copy_name(char *out, const BYTE *data, ULONG size, ULONG at)
{
    ULONG i = 0;
    out[0] = 0;
    for (; at + i < size && i < NAME_MAX_ - 1 && data[at + i]; i++)
        out[i] = (char)data[at + i];
    out[i] = 0;
}

/* the previous and next cabinet names follow the header's reserved bytes. */
static void read_names(CABINET *cabinet)
{
    const BYTE *p = cabinet->Data;
    USHORT flags = word(p + 30);
    ULONG at = CFHEADER_SIZE;
    cabinet->Previous[0] = cabinet->PreviousDisk[0] = cabinet->Next[0] = cabinet->NextDisk[0] = 0;
    if (flags & CAB_FLAG_RESERVE)
        at += 4 + word(p + at);
    char *names[4] = {cabinet->Previous, cabinet->PreviousDisk, cabinet->Next, cabinet->NextDisk};
    for (int i = 0; i < 4; i++) {
        bool present = i < 2 ? (flags & CAB_FLAG_PREV) : (flags & CAB_FLAG_NEXT);
        if (!present)
            continue;
        copy_name(names[i], p, cabinet->Size, at);
        while (at < cabinet->Size && p[at])
            at++;
        at++;
    }
}

/* reads as far as the header says the cabinet goes. */
static BOOL load(FDI *fdi, INT_PTR file, CABINET *cabinet)
{
    BYTE header[CFHEADER_SIZE];
    if (!read_header(fdi, file, header)) {
        fail(fdi, FDIERROR_NOT_A_CABINET, 0);
        return FALSE;
    }
    if (word(header + 24) != 0x0103) {
        fail(fdi, FDIERROR_UNKNOWN_CABINET_VERSION, word(header + 24));
        return FALSE;
    }
    ULONG size = dword(header + 8);
    if (size < CFHEADER_SIZE) {
        fail(fdi, FDIERROR_CORRUPT_CABINET, 0);
        return FALSE;
    }
    BYTE *data = fdi->Alloc(size);
    if (!data) {
        fail(fdi, FDIERROR_ALLOC_FAIL, 0);
        return FALSE;
    }
    memcpy(data, header, CFHEADER_SIZE);
    ULONG done = CFHEADER_SIZE;
    while (done < size) {
        UINT want = size - done > 0x100000 ? 0x100000 : size - done;
        UINT got = fdi->Read(file, data + done, want);
        if (got != want) {
            fdi->Free(data);
            fail(fdi, FDIERROR_CORRUPT_CABINET, 0);
            return FALSE;
        }
        done += got;
    }
    cabinet->Fdi = fdi;
    cabinet->Data = data;
    cabinet->Size = size;
    read_names(cabinet);
    return TRUE;
}

/* ------------------------------------------------------------------ the files */

typedef struct COPY {
    CABINET *Cabinet;
    PFNFDINOTIFY Notify;
    void *User;
    FDINOTIFICATION Notification;
    bool Aborted;
    bool Limited; /* the caller cut the files from Limit on. */
    unsigned Limit;
} COPY;

typedef struct COPY_FILE_STATE {
    INT_PTR Handle;
    bool Failed;
} COPY_FILE_STATE;

static void fill_file(COPY *copy, const MIME_CAB_FILE *file, char *name)
{
    FDINOTIFICATION *n = &copy->Notification;
    memset(n, 0, sizeof(*n));
    n->cb = (long)file->Size;
    n->psz1 = name;
    n->psz2 = copy->Cabinet->Previous;
    n->psz3 = copy->Cabinet->PreviousDisk;
    n->pv = copy->User;
    n->date = file->Date;
    n->time = file->Time;
    n->attribs = file->Attributes;
    n->setID = word(copy->Cabinet->Data + 32);
    n->iCabinet = word(copy->Cabinet->Data + 34);
    n->iFolder = file->Folder;
}

static void *file_begin(void *context, const MIME_CAB_FILE *file)
{
    COPY *copy = context;
    FDI *fdi = copy->Cabinet->Fdi;
    if (copy->Limited) {
        bool listed = false;
        MIME_CAB_FILE other;
        for (unsigned i = 0; i < copy->Limit && !listed; i++)
            listed = MimeCabFile(copy->Cabinet->Cab, i, &other) == MIME_CAB_OK && other.Name == file->Name;
        if (!listed)
            return nullptr;
    }
    COPY_FILE_STATE *state = fdi->Alloc(sizeof(*state));
    if (!state) {
        copy->Aborted = true;
        fail(fdi, FDIERROR_ALLOC_FAIL, 0);
        return nullptr;
    }
    state->Handle = -1;
    state->Failed = false;
    char name[NAME_MAX_ * 2];
    ULONG length = 0;
    for (; file->Name[length] && length < sizeof(name) - 1; length++)
        name[length] = file->Name[length];
    name[length] = 0;
    fill_file(copy, file, name);
    INT_PTR handle = copy->Notify(fdintCOPY_FILE, &copy->Notification);
    if (handle == -1) {
        copy->Aborted = true;
        fail(fdi, FDIERROR_USER_ABORT, 0);
        state->Failed = true;
        return state;
    }
    if (!handle) {
        fdi->Free(state);
        return nullptr;
    }
    state->Handle = handle;
    return state;
}

static int file_write(void *context, void *file_context, const BYTE *data, size_t length)
{
    COPY *copy = context;
    COPY_FILE_STATE *state = file_context;
    FDI *fdi = copy->Cabinet->Fdi;
    if (state->Failed)
        return 1;
    while (length) {
        UINT chunk = length > 0x40000000 ? 0x40000000 : (UINT)length;
        if (fdi->Write(state->Handle, (void *)data, chunk) != chunk) {
            fdi->Close(state->Handle);
            state->Handle = -1;
            state->Failed = true;
            copy->Aborted = true;
            fail(fdi, FDIERROR_TARGET_FILE, 0);
            return 1;
        }
        data += chunk;
        length -= chunk;
    }
    return 0;
}

static int file_end(void *context, void *file_context, const MIME_CAB_FILE *file)
{
    COPY *copy = context;
    COPY_FILE_STATE *state = file_context;
    FDI *fdi = copy->Cabinet->Fdi;
    if (state->Failed) {
        fdi->Free(state);
        return 1;
    }
    char name[NAME_MAX_ * 2];
    ULONG length = 0;
    for (; file->Name[length] && length < sizeof(name) - 1; length++)
        name[length] = file->Name[length];
    name[length] = 0;
    fill_file(copy, file, name);
    FDINOTIFICATION *n = &copy->Notification;
    n->hf = state->Handle;
    n->cb = 0;
    /* the executable attribute is reported apart from the others. */
    if (file->Attributes & _A_EXEC) {
        n->cb = 1;
        n->attribs = file->Attributes & ~_A_EXEC;
    }
    n->iFolder = file->Folder;
    INT_PTR result = copy->Notify(fdintCLOSE_FILE_INFO, n);
    fdi->Free(state);
    if (result == -1) {
        copy->Aborted = true;
        fail(fdi, FDIERROR_USER_ABORT, 0);
        return 1;
    }
    if (!result) {
        copy->Aborted = true;
        fail(fdi, FDIERROR_TARGET_FILE, 0);
        return 1;
    }
    return 0;
}

BOOL DIAMONDAPI FDICopy(HFDI hfdi, char *pszCabinet, char *pszCabPath, int flags, PFNFDINOTIFY pfnfdin,
                        PFNFDIDECRYPT pfnfdid, void *pvUser)
{
    FDI *fdi = hfdi;
    if (!fdi || !pfnfdin)
        return FALSE;
    char path[CABINET_PATH_MAX + 1], full[CABINET_PATH_MAX * 2 + 2];
    ULONG length = 0;
    for (; pszCabPath && pszCabPath[length] && length < CABINET_PATH_MAX; length++)
        path[length] = pszCabPath[length];
    path[length] = 0;
    memcpy(full, path, length);
    ULONG name_length = 0;
    for (; pszCabinet && pszCabinet[name_length] && name_length < CABINET_PATH_MAX; name_length++)
        full[length + name_length] = pszCabinet[name_length];
    full[length + name_length] = 0;

    INT_PTR file = fdi->Open(full, OPEN_BINARY_READ, OPEN_MODE);
    if (file == -1) {
        fail(fdi, FDIERROR_CABINET_NOT_FOUND, 0);
        return FALSE;
    }
    CABINET cabinet = {};
    BOOL loaded = load(fdi, file, &cabinet);
    fdi->Close(file);
    if (!loaded)
        return FALSE;

    BOOL done = FALSE;
    MIME_CAB *cab = nullptr;
    int result = MimeCabOpen(&cab, cabinet.Data, cabinet.Size, cab_allocate, cab_release);
    if (result != MIME_CAB_OK) {
        fail(fdi, result == MIME_CAB_MEMORY ? FDIERROR_ALLOC_FAIL : FDIERROR_CORRUPT_CABINET, 0);
        fdi->Free(cabinet.Data);
        return FALSE;
    }
    MimeCabSkipContinued(cab);
    MimeCabOneAtATime(cab);
    cabinet.Cab = cab;

    COPY copy = {&cabinet, pfnfdin, pvUser};
    FDINOTIFICATION *n = &copy.Notification;
    USHORT set = word(cabinet.Data + 32), index = word(cabinet.Data + 34);

    memset(n, 0, sizeof(*n));
    n->psz1 = cabinet.Next;
    n->psz2 = cabinet.NextDisk;
    n->psz3 = path;
    n->pv = pvUser;
    n->setID = set;
    n->iCabinet = index;
    if (pfnfdin(fdintCABINET_INFO, n) == -1) {
        fail(fdi, FDIERROR_USER_ABORT, 0);
        goto finish;
    }

    /* the caller may cut the file count here. */
    memset(n, 0, sizeof(*n));
    n->cb = (long)dword(cabinet.Data + 16);
    n->pv = pvUser;
    n->setID = set;
    n->iCabinet = index;
    n->iFolder = word(cabinet.Data + 28);
    if (pfnfdin(fdintENUMERATE, n) == -1) {
        fail(fdi, FDIERROR_USER_ABORT, 0);
        goto finish;
    }
    unsigned count = n->iFolder;
    if (count < MimeCabFileCount(cab)) {
        copy.Limited = true;
        copy.Limit = count;
    }

    /* files begun in an earlier cabinet are only announced, the rest follow with their folder. */
    for (unsigned i = 0; i < count; i++) {
        MIME_CAB_FILE entry;
        if (MimeCabFile(cab, i, &entry) != MIME_CAB_OK)
            break;
        if ((entry.Folder & 0xFFFD) == 0xFFFD) {
            char name[NAME_MAX_ * 2];
            ULONG k = 0;
            for (; entry.Name[k] && k < sizeof(name) - 1; k++)
                name[k] = entry.Name[k];
            name[k] = 0;
            fill_file(&copy, &entry, name);
            if (pfnfdin(fdintPARTIAL_FILE, n) == -1) {
                fail(fdi, FDIERROR_USER_ABORT, 0);
                goto finish;
            }
        }
    }
    {
        static const MIME_CAB_FILE_SINK sink = {file_begin, file_write, file_end};
        result = MimeCabExtract(cab, &sink, &copy);
    }
    if (copy.Aborted)
        goto finish;
    switch (result) {
    case MIME_CAB_OK:
        done = TRUE;
        break;
    case MIME_CAB_MEMORY:
        fail(fdi, FDIERROR_ALLOC_FAIL, 0);
        break;
    case MIME_CAB_UNSUPPORTED:
        fail(fdi, FDIERROR_BAD_COMPR_TYPE, 0);
        break;
    default:
        fail(fdi, FDIERROR_CORRUPT_CABINET, 0);
        break;
    }
finish:
    MimeCabClose(cab);
    fdi->Free(cabinet.Data);
    return done;
}

/* ------------------------------------------------------------------ the version */

typedef struct {
    DWORD cbSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformID;
} CABINET_DLLVERSIONINFO;

FDIAPI const char *WINAPI GetDllVersion(void)
{
    return "5.00";
}

FDIAPI HRESULT WINAPI DllGetVersion(CABINET_DLLVERSIONINFO *info)
{
    if (info && info->cbSize >= sizeof(*info)) {
        info->dwMajorVersion = 5;
        info->dwMinorVersion = 0;
        info->dwBuildNumber = 26100;
        info->dwPlatformID = 2;
    }
    return 0;
}
