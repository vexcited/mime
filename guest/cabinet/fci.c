/*
 * cabinet.dll's FCI writer. the files given to FCIAddFile are collected into folders in memory and a cabinet is
 * written through the caller's file callbacks when it is flushed. every folder is stored without compression whatever
 * type was asked for, and a cabinet that would pass the size limit of its description is refused instead of continued
 * on another one.
 */
#define MIME_BUILD_CABINET
#include <mime/win32/fci.h>
#include <rtl.h>

#define FCI_MAGIC 0x49434643 /* "CFCI". */
#define BLOCK_SIZE 0x8000
#define CFHEADER_SIZE 36
#define CFFOLDER_SIZE 8
#define CFFILE_SIZE 16
#define CFDATA_SIZE 8
#define CAB_FLAG_RESERVE 4
#define A_MASK 0x27
#define A_EXEC 0x40

#define O_BINARY_ 0x8000
#define O_CREAT_ 0x100
#define O_TRUNC_ 0x200
#define O_RDWR_ 0x2
#define S_IREAD_ 0x100
#define S_IWRITE_ 0x80

typedef struct FCI_FILE {
    char *Name;
    ULONG Size, Offset;
    USHORT Folder, Date, Time, Attributes;
} FCI_FILE;

typedef struct FCI_FOLDER {
    ULONG Start, Length;
    USHORT Type;
} FCI_FOLDER;

typedef struct FCI {
    ULONG Magic;
    PERF Erf;
    PFNFCIFILEPLACED Placed;
    PFNFCIALLOC Alloc;
    PFNFCIFREE Free;
    PFNFCIOPEN Open;
    PFNFCIREAD Read;
    PFNFCIWRITE Write;
    PFNFCICLOSE Close;
    PFNFCISEEK Seek;
    PFNFCIDELETE Delete;
    PFNFCIGETTEMPFILE TempFile;
    CCAB Cab;
    void *Pv;
    BYTE *Data;
    ULONG DataSize, DataCapacity;
    FCI_FOLDER *Folders;
    ULONG FolderCount, FolderCapacity;
    FCI_FILE *Files;
    ULONG FileCount, FileCapacity;
    BOOL FolderOpen;
} FCI;

static BOOL fail(FCI *fci, int oper, int type)
{
    fci->Erf->erfOper = oper;
    fci->Erf->erfType = type;
    fci->Erf->fError = TRUE;
    return FALSE;
}

static void put16(BYTE *p, USHORT v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
}

static void put32(BYTE *p, ULONG v)
{
    put16(p, (USHORT)v);
    put16(p + 2, (USHORT)(v >> 16));
}

/* the checksum of a data block: the bytes in little endian words, xored, and the tail padded with zeros. */
static ULONG checksum(const BYTE *p, UINT size, ULONG seed)
{
    ULONG sum = seed;
    for (UINT words = size / 4; words; words--, p += 4)
        sum ^= (ULONG)p[0] | (ULONG)p[1] << 8 | (ULONG)p[2] << 16 | (ULONG)p[3] << 24;
    ULONG tail = 0;
    switch (size % 4) {
    case 3:
        tail |= (ULONG)*p++ << 16;
        [[fallthrough]];
    case 2:
        tail |= (ULONG)*p++ << 8;
        [[fallthrough]];
    case 1:
        tail |= *p;
    }
    return sum ^ tail;
}

/* grows a table, keeping its contents, with the memory of the caller. */
static BOOL grow(FCI *fci, void **table, ULONG *capacity, ULONG needed, ULONG element)
{
    if (needed <= *capacity)
        return TRUE;
    ULONG wanted = *capacity ? *capacity : 16;
    while (wanted < needed) {
        if (wanted > 0x7FFFFFFF / 2 / (element ? element : 1))
            return FALSE;
        wanted *= 2;
    }
    void *memory = fci->Alloc(wanted * element);
    if (!memory)
        return FALSE;
    if (*table) {
        memcpy(memory, *table, *capacity * element);
        fci->Free(*table);
    }
    *table = memory;
    *capacity = wanted;
    return TRUE;
}

static void reset(FCI *fci)
{
    for (ULONG i = 0; i < fci->FileCount; i++)
        fci->Free(fci->Files[i].Name);
    fci->FileCount = fci->FolderCount = fci->DataSize = 0;
    fci->FolderOpen = FALSE;
}

HFCI DIAMONDAPI FCICreate(PERF perf, PFNFCIFILEPLACED placed, PFNFCIALLOC alloc, PFNFCIFREE free_memory, PFNFCIOPEN open,
                          PFNFCIREAD read, PFNFCIWRITE write, PFNFCICLOSE close, PFNFCISEEK seek,
                          PFNFCIDELETE remove_file, PFNFCIGETTEMPFILE temp_file, PCCAB cab, void *pv)
{
    if (!perf) {
        SetLastError(ERROR_BAD_ARGUMENTS);
        return nullptr;
    }
    if (!placed || !alloc || !free_memory || !open || !read || !write || !close || !seek || !remove_file ||
        !temp_file || !cab) {
        perf->erfOper = FCIERR_NONE;
        perf->erfType = ERROR_BAD_ARGUMENTS;
        perf->fError = TRUE;
        SetLastError(ERROR_BAD_ARGUMENTS);
        return nullptr;
    }
    FCI *fci = alloc(sizeof(*fci));
    if (!fci) {
        perf->erfOper = FCIERR_ALLOC_FAIL;
        perf->erfType = ERROR_NOT_ENOUGH_MEMORY;
        perf->fError = TRUE;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    memset(fci, 0, sizeof(*fci));
    fci->Magic = FCI_MAGIC;
    fci->Erf = perf;
    fci->Placed = placed;
    fci->Alloc = alloc;
    fci->Free = free_memory;
    fci->Open = open;
    fci->Read = read;
    fci->Write = write;
    fci->Close = close;
    fci->Seek = seek;
    fci->Delete = remove_file;
    fci->TempFile = temp_file;
    fci->Cab = *cab;
    fci->Pv = pv;
    perf->erfOper = FCIERR_NONE;
    perf->erfType = 0;
    perf->fError = FALSE;
    return fci;
}

static FCI *check(HFCI handle)
{
    FCI *fci = handle;
    if (!fci || fci->Magic != FCI_MAGIC) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    return fci;
}

static void close_folder(FCI *fci)
{
    fci->FolderOpen = FALSE;
}

BOOL DIAMONDAPI FCIAddFile(HFCI handle, char *source, char *name, BOOL execute, PFNFCIGETNEXTCABINET next_cabinet,
                           PFNFCISTATUS status, PFNFCIGETOPENINFO open_info, TCOMP compression)
{
    FCI *fci = check(handle);
    if (!fci)
        return FALSE;
    if (!source || !name || !open_info) {
        SetLastError(ERROR_BAD_ARGUMENTS);
        return fail(fci, FCIERR_NONE, ERROR_BAD_ARGUMENTS);
    }
    if ((compression & tcompMASK_TYPE) > tcompTYPE_LZX)
        return fail(fci, FCIERR_BAD_COMPR_TYPE, 0);

    int err = 0;
    USHORT date = 0, time = 0, attributes = 0;
    INT_PTR file = open_info(source, &date, &time, &attributes, &err, fci->Pv);
    if (file == -1)
        return fail(fci, FCIERR_OPEN_SRC, err);

    ULONG start = fci->DataSize;
    BOOL ok = TRUE;
    for (;;) {
        if (!grow(fci, (void **)&fci->Data, &fci->DataCapacity, fci->DataSize + BLOCK_SIZE, 1)) {
            fail(fci, FCIERR_ALLOC_FAIL, ERROR_NOT_ENOUGH_MEMORY);
            ok = FALSE;
            break;
        }
        UINT got = fci->Read(file, fci->Data + fci->DataSize, BLOCK_SIZE, &err, fci->Pv);
        if (got == (UINT)-1) {
            fail(fci, FCIERR_READ_SRC, err);
            ok = FALSE;
            break;
        }
        if (!got)
            break;
        fci->DataSize += got;
        if (status && status(statusFile, got, got, fci->Pv) == -1) {
            fail(fci, FCIERR_USER_ABORT, 0);
            ok = FALSE;
            break;
        }
    }
    fci->Close(file, &err, fci->Pv);
    if (!ok) {
        fci->DataSize = start;
        return FALSE;
    }

    /*
     * the files of a folder share a compression type, so a new type is a new folder. an empty file has no data to
     * compress and joins the last folder, as a folder with no data would end the cabinet.
     */
    ULONG size = fci->DataSize - start;
    BOOL join = size == 0 && fci->FolderCount > 0;
    if (!join && fci->FolderOpen && fci->Folders[fci->FolderCount - 1].Type != (compression & tcompMASK_TYPE))
        close_folder(fci);
    if (!join && !fci->FolderOpen) {
        if (!grow(fci, (void **)&fci->Folders, &fci->FolderCapacity, fci->FolderCount + 1, sizeof(FCI_FOLDER)))
            return fail(fci, FCIERR_ALLOC_FAIL, ERROR_NOT_ENOUGH_MEMORY);
        FCI_FOLDER *created = &fci->Folders[fci->FolderCount++];
        created->Start = start;
        created->Length = 0;
        created->Type = compression & tcompMASK_TYPE;
        fci->FolderOpen = TRUE;
    }
    FCI_FOLDER *folder = &fci->Folders[fci->FolderCount - 1];
    if (!grow(fci, (void **)&fci->Files, &fci->FileCapacity, fci->FileCount + 1, sizeof(FCI_FILE)))
        return fail(fci, FCIERR_ALLOC_FAIL, ERROR_NOT_ENOUGH_MEMORY);
    ULONG length = (ULONG)strlen(name);
    char *copy = fci->Alloc(length + 1);
    if (!copy)
        return fail(fci, FCIERR_ALLOC_FAIL, ERROR_NOT_ENOUGH_MEMORY);
    memcpy(copy, name, length + 1);
    FCI_FILE *record = &fci->Files[fci->FileCount++];
    record->Name = copy;
    record->Size = size;
    record->Offset = start - folder->Start;
    record->Folder = (USHORT)(fci->FolderCount - 1);
    record->Date = date;
    record->Time = time;
    record->Attributes = (attributes & A_MASK) | (execute ? A_EXEC : 0);
    folder->Length = fci->DataSize - folder->Start;
    if (!join && fci->Cab.cbFolderThresh && folder->Length >= fci->Cab.cbFolderThresh)
        close_folder(fci);
    return TRUE;
}

BOOL DIAMONDAPI FCIFlushFolder(HFCI handle, PFNFCIGETNEXTCABINET next_cabinet, PFNFCISTATUS status)
{
    FCI *fci = check(handle);
    if (!fci)
        return FALSE;
    if (fci->FolderOpen) {
        ULONG length = fci->Folders[fci->FolderCount - 1].Length;
        close_folder(fci);
        if (status && status(statusFolder, length, length, fci->Pv) == -1)
            return fail(fci, FCIERR_USER_ABORT, 0);
    }
    return TRUE;
}

static BOOL write_all(FCI *fci, INT_PTR file, const void *bytes, ULONG size)
{
    int err = 0;
    if (size && fci->Write(file, (void *)bytes, size, &err, fci->Pv) != size)
        return fail(fci, FCIERR_CAB_FILE, err);
    return TRUE;
}

/* a folder of empty files has one empty block, so that its start is inside the cabinet. */
static ULONG blocks_of(ULONG length)
{
    return length ? (length + BLOCK_SIZE - 1) / BLOCK_SIZE : 1;
}

BOOL DIAMONDAPI FCIFlushCabinet(HFCI handle, BOOL get_next, PFNFCIGETNEXTCABINET next_cabinet, PFNFCISTATUS status)
{
    FCI *fci = check(handle);
    if (!fci)
        return FALSE;
    close_folder(fci);

    CCAB *cab = &fci->Cab;
    BOOL reserve = cab->cbReserveCFHeader || cab->cbReserveCFFolder || cab->cbReserveCFData;
    if (cab->cbReserveCFHeader > 60000 || cab->cbReserveCFFolder > 255 || cab->cbReserveCFData > 255)
        return fail(fci, FCIERR_CAB_FORMAT_LIMIT, 0);
    ULONG header_size = CFHEADER_SIZE + (reserve ? 4 + cab->cbReserveCFHeader : 0);
    ULONG folder_size = CFFOLDER_SIZE + (reserve ? cab->cbReserveCFFolder : 0);
    ULONG data_size = CFDATA_SIZE + (reserve ? cab->cbReserveCFData : 0);

    ULONG files_size = 0, blocks = 0;
    for (ULONG i = 0; i < fci->FileCount; i++)
        files_size += CFFILE_SIZE + (ULONG)strlen(fci->Files[i].Name) + 1;
    for (ULONG i = 0; i < fci->FolderCount; i++)
        blocks += blocks_of(fci->Folders[i].Length);
    ULONG files_at = header_size + fci->FolderCount * folder_size;
    ULONG data_at = files_at + files_size;
    ULONG total = data_at + blocks * data_size + fci->DataSize;
    if (fci->FolderCount > 0xFFFF || fci->FileCount > 0xFFFF || total < data_at || (cab->cb && total > cab->cb))
        return fail(fci, FCIERR_CAB_FORMAT_LIMIT, 0);

    char path[CB_MAX_CAB_PATH + CB_MAX_CABINET_NAME];
    ULONG path_length = (ULONG)strlen(cab->szCabPath);
    memcpy(path, cab->szCabPath, path_length);
    strcpy(path + path_length, cab->szCab);

    int err = 0;
    INT_PTR file = fci->Open(path, O_BINARY_ | O_CREAT_ | O_TRUNC_ | O_RDWR_, S_IREAD_ | S_IWRITE_, &err, fci->Pv);
    if (file == -1)
        return fail(fci, FCIERR_CAB_FILE, err);

    BYTE *head = fci->Alloc(files_at ? files_at : 1);
    BYTE *block = fci->Alloc(data_size + BLOCK_SIZE);
    if (!head || !block) {
        if (head)
            fci->Free(head);
        if (block)
            fci->Free(block);
        fci->Close(file, &err, fci->Pv);
        return fail(fci, FCIERR_ALLOC_FAIL, ERROR_NOT_ENOUGH_MEMORY);
    }
    memset(head, 0, files_at);
    memcpy(head, "MSCF", 4);
    put32(head + 8, total);
    put32(head + 16, files_at);
    head[24] = 3;
    head[25] = 1;
    put16(head + 26, (USHORT)fci->FolderCount);
    put16(head + 28, (USHORT)fci->FileCount);
    put16(head + 30, reserve ? CAB_FLAG_RESERVE : 0);
    put16(head + 32, cab->setID);
    put16(head + 34, (USHORT)cab->iCab);
    if (reserve) {
        put16(head + CFHEADER_SIZE, (USHORT)cab->cbReserveCFHeader);
        head[CFHEADER_SIZE + 2] = (BYTE)cab->cbReserveCFFolder;
        head[CFHEADER_SIZE + 3] = (BYTE)cab->cbReserveCFData;
    }
    ULONG at = data_at;
    for (ULONG i = 0; i < fci->FolderCount; i++) {
        BYTE *entry = head + header_size + i * folder_size;
        put32(entry, at);
        put16(entry + 4, (USHORT)blocks_of(fci->Folders[i].Length));
        put16(entry + 6, 0);
        at += blocks_of(fci->Folders[i].Length) * data_size + fci->Folders[i].Length;
    }
    BOOL ok = write_all(fci, file, head, files_at);

    for (ULONG i = 0; ok && i < fci->FileCount; i++) {
        FCI_FILE *record = &fci->Files[i];
        ULONG name_length = (ULONG)strlen(record->Name) + 1;
        BYTE entry[CFFILE_SIZE];
        put32(entry, record->Size);
        put32(entry + 4, record->Offset);
        put16(entry + 8, record->Folder);
        put16(entry + 10, record->Date);
        put16(entry + 12, record->Time);
        put16(entry + 14, record->Attributes);
        ok = write_all(fci, file, entry, CFFILE_SIZE) && write_all(fci, file, record->Name, name_length);
    }

    for (ULONG i = 0; ok && i < fci->FolderCount; i++) {
        const FCI_FOLDER *folder = &fci->Folders[i];
        for (ULONG done = 0; ok && (done < folder->Length || !done); done += BLOCK_SIZE) {
            UINT size = folder->Length - done < BLOCK_SIZE ? folder->Length - done : BLOCK_SIZE;
            const BYTE *data = fci->Data + folder->Start + done;
            memset(block, 0, data_size);
            put16(block + 4, (USHORT)size);
            put16(block + 6, (USHORT)size);
            memcpy(block + data_size, data, size);
            put32(block, checksum(block + 4, 4, checksum(data, size, 0)));
            ok = write_all(fci, file, block, data_size + size);
        }
    }
    fci->Free(head);
    fci->Free(block);
    if (fci->Close(file, &err, fci->Pv) != 0 && ok)
        ok = fail(fci, FCIERR_CAB_FILE, err);
    if (!ok)
        return FALSE;

    for (ULONG i = 0; i < fci->FileCount; i++)
        if (fci->Placed(cab, fci->Files[i].Name, (long)fci->Files[i].Size, FALSE, fci->Pv) == -1)
            return fail(fci, FCIERR_USER_ABORT, 0);
    if (status && status(statusCabinet, total, total, fci->Pv) == -1)
        return fail(fci, FCIERR_USER_ABORT, 0);

    reset(fci);
    if (get_next) {
        cab->iCab++;
        if (!next_cabinet || !next_cabinet(cab, total, fci->Pv))
            return fail(fci, FCIERR_USER_ABORT, 0);
    }
    return TRUE;
}

BOOL DIAMONDAPI FCIDestroy(HFCI handle)
{
    FCI *fci = check(handle);
    if (!fci)
        return FALSE;
    reset(fci);
    fci->Magic = 0;
    if (fci->Data)
        fci->Free(fci->Data);
    if (fci->Folders)
        fci->Free(fci->Folders);
    if (fci->Files)
        fci->Free(fci->Files);
    fci->Free(fci);
    return TRUE;
}
