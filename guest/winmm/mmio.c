/*
 * buffered file I/O and RIFF chunks. procedures provide disk, memory or application-defined I/O. mmioGetInfo and
 * mmioAdvance expose the buffer: pchNext is the cursor, pchEndRead ends readable data from lBufOffset, and
 * MMIO_DIRTY marks writes.
 */
#define MIME_BUILD_WINMM
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/mmsystem.h>

typedef struct MMIO {
    MMIOINFO info;
    /* the buffer is mmio's own, freed with it. */
    BOOL own_buffer;
    /* the procedure takes wide file names. */
    BOOL unicode;
} MMIO;

typedef struct IOPROC {
    FOURCC fcc;
    LPMMIOPROC proc;
    BOOL unicode;
} IOPROC;

static IOPROC g_procs[32];
static UINT g_proc_count;
static RTL_SRWLOCK g_procs_lock;

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

static void *mm_alloc(SIZE_T size) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size); }
static void mm_free(void *p) { HeapFree(GetProcessHeap(), 0, p); }

static MMIO *mmio_of(HMMIO h) { return (MMIO *)h; }

static LRESULT send(MMIO *m, UINT message, LPARAM a, LPARAM b)
{
    return m->info.pIOProc((LPSTR)&m->info, message, a, b);
}

/* ------------------------------------------------------------------ the dos and mem procedures */

static LRESULT CALLBACK dos_proc(LPSTR data, UINT message, LPARAM a, LPARAM b)
{
    MMIOINFO *info = (MMIOINFO *)data;
    HANDLE file = (HANDLE)(ULONG_PTR)info->adwInfo[0];
    switch (message) {
    case MMIOM_OPEN: {
        DWORD mode = info->dwFlags & MMIO_RWMODE;
        DWORD access = mode == MMIO_WRITE ? GENERIC_WRITE : mode == MMIO_READWRITE ? GENERIC_READ | GENERIC_WRITE
                                                                                   : GENERIC_READ;
        DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
        switch (info->dwFlags & MMIO_SHAREMODE) {
        case MMIO_EXCLUSIVE:
            share = 0;
            break;
        case MMIO_DENYWRITE:
            share = FILE_SHARE_READ;
            break;
        case MMIO_DENYREAD:
            share = FILE_SHARE_WRITE;
            break;
        }
        DWORD disposition = info->dwFlags & MMIO_CREATE ? CREATE_ALWAYS : OPEN_EXISTING;
        HANDLE h = CreateFileW((LPCWSTR)a, access, share, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            return e == ERROR_FILE_NOT_FOUND ? MMIOERR_FILENOTFOUND
                   : e == ERROR_PATH_NOT_FOUND ? MMIOERR_PATHNOTFOUND
                   : e == ERROR_ACCESS_DENIED ? MMIOERR_ACCESSDENIED
                   : e == ERROR_SHARING_VIOLATION ? MMIOERR_SHARINGVIOLATION
                                                 : MMIOERR_CANNOTOPEN;
        }
        info->adwInfo[0] = (DWORD)(ULONG_PTR)h;
        info->lDiskOffset = 0;
        return MMSYSERR_NOERROR;
    }
    case MMIOM_CLOSE:
        if (!(b & MMIO_FHOPEN) && !CloseHandle(file))
            return MMIOERR_CANNOTCLOSE;
        return MMSYSERR_NOERROR;
    case MMIOM_READ: {
        DWORD done = 0;
        if (!ReadFile(file, (void *)a, (DWORD)b, &done, nullptr))
            return -1;
        info->lDiskOffset += done;
        return done;
    }
    case MMIOM_WRITE:
    case MMIOM_WRITEFLUSH: {
        DWORD done = 0;
        if (!WriteFile(file, (const void *)a, (DWORD)b, &done, nullptr))
            return -1;
        info->lDiskOffset += done;
        if (message == MMIOM_WRITEFLUSH)
            FlushFileBuffers(file);
        return done;
    }
    case MMIOM_SEEK: {
        DWORD pos = SetFilePointer(file, (LONG)a, nullptr, (DWORD)b);
        if (pos == INVALID_SET_FILE_POINTER)
            return -1;
        info->lDiskOffset = (LONG)pos;
        return (LONG)pos;
    }
    case MMIOM_RENAME:
        return MoveFileW((LPCWSTR)a, (LPCWSTR)b) ? MMSYSERR_NOERROR : MMIOERR_FILENOTFOUND;
    }
    return 0;
}

/* a memory file is its buffer: nothing reaches a disk, and it grows only when mmio allocated it. */
static LRESULT CALLBACK mem_proc(LPSTR data, UINT message, LPARAM a, LPARAM b)
{
    switch (message) {
    case MMIOM_OPEN:
    case MMIOM_CLOSE:
        return MMSYSERR_NOERROR;
    case MMIOM_READ:
    case MMIOM_WRITE:
    case MMIOM_WRITEFLUSH:
        return 0;
    case MMIOM_SEEK:
        return -1;
    }
    return 0;
}

static LPMMIOPROC find_proc(FOURCC fcc, BOOL *unicode)
{
    *unicode = TRUE;
    if (fcc == FOURCC_DOS || !fcc)
        return dos_proc;
    if (fcc == FOURCC_MEM)
        return mem_proc;
    LPMMIOPROC proc = nullptr;
    RtlAcquireSRWLockShared(&g_procs_lock);
    for (UINT i = g_proc_count; i-- > 0;)
        if (g_procs[i].fcc == fcc) {
            proc = g_procs[i].proc;
            *unicode = g_procs[i].unicode;
            break;
        }
    RtlReleaseSRWLockShared(&g_procs_lock);
    return proc;
}

static LPMMIOPROC install(FOURCC fcc, LPMMIOPROC proc, DWORD flags, BOOL unicode)
{
    if (flags & MMIO_FINDPROC) {
        BOOL u;
        return find_proc(fcc, &u);
    }
    LPMMIOPROC result = nullptr;
    RtlAcquireSRWLockExclusive(&g_procs_lock);
    if (flags & MMIO_INSTALLPROC) {
        if (proc && g_proc_count < RTL_NUMBER_OF(g_procs)) {
            g_procs[g_proc_count++] = (IOPROC){fcc, proc, unicode || (flags & MMIO_UNICODEPROC)};
            result = proc;
        }
    } else if (flags & MMIO_REMOVEPROC) {
        for (UINT i = g_proc_count; i-- > 0;)
            if (g_procs[i].fcc == fcc && (!proc || g_procs[i].proc == proc)) {
                result = g_procs[i].proc;
                memmove(&g_procs[i], &g_procs[i + 1], (g_proc_count - i - 1) * sizeof(*g_procs));
                g_proc_count--;
                break;
            }
    }
    RtlReleaseSRWLockExclusive(&g_procs_lock);
    return result;
}

WINMMAPI LPMMIOPROC WINAPI mmioInstallIOProcA(FOURCC fcc, LPMMIOPROC proc, DWORD flags)
{
    return install(fcc, proc, flags, FALSE);
}

WINMMAPI LPMMIOPROC WINAPI mmioInstallIOProcW(FOURCC fcc, LPMMIOPROC proc, DWORD flags)
{
    return install(fcc, proc, flags, TRUE);
}

/* packs four characters, padding with spaces and optionally converting to uppercase. */
static FOURCC to_fourcc(const char text[4], int length, UINT flags)
{
    char c[4] = {' ', ' ', ' ', ' '};
    for (int i = 0; i < 4 && i < length && text[i]; i++)
        c[i] = (flags & MMIO_TOUPPER) && text[i] >= 'a' && text[i] <= 'z' ? text[i] - 32 : text[i];
    return mmioFOURCC(c[0], c[1], c[2], c[3]);
}

WINMMAPI FOURCC WINAPI mmioStringToFOURCCA(LPCSTR text, UINT flags)
{
    return text ? to_fourcc(text, 4, flags) : 0;
}

WINMMAPI FOURCC WINAPI mmioStringToFOURCCW(LPCWSTR text, UINT flags)
{
    char c[4] = {};
    for (int i = 0; text && i < 4 && text[i]; i++)
        c[i] = (char)text[i];
    return text ? to_fourcc(c, 4, flags) : 0;
}

/* ------------------------------------------------------------------ opening and closing */

/* a name like file.ext+element selects the installed procedure for its extension. */
static FOURCC proc_of_name(LPCWSTR name)
{
    const WCHAR *plus = name ? wcschr(name, L'+') : nullptr;
    if (!plus)
        return FOURCC_DOS;
    const WCHAR *dot = plus;
    while (dot > name && dot[-1] != L'.' && dot[-1] != L'\\')
        dot--;
    if (dot == name || dot[-1] != L'.')
        return FOURCC_DOS;
    char c[4] = {};
    for (int i = 0; i < 4 && dot + i < plus; i++)
        c[i] = (char)dot[i];
    return to_fourcc(c, 4, MMIO_TOUPPER);
}

static HMMIO open_common(LPWSTR name, LPMMIOINFO user, DWORD flags)
{
    if (flags & MMIO_PARSE) {
        /* the full path, back in the caller's buffer of 128 characters. */
        WCHAR full[MAX_PATH];
        DWORD n = name ? GetFullPathNameW(name, MAX_PATH, full, nullptr) : 0;
        if (!n || n >= 128)
            return nullptr;
        memcpy(name, full, (n + 1) * sizeof(WCHAR));
        return (HMMIO)TRUE;
    }
    if (flags & MMIO_EXIST)
        return GetFileAttributesW(name) != INVALID_FILE_ATTRIBUTES ? (HMMIO)TRUE : nullptr;
    if (flags & MMIO_DELETE)
        return DeleteFileW(name) ? (HMMIO)TRUE : nullptr;
    if (flags & MMIO_GETTEMP) {
        WCHAR dir[MAX_PATH], temp[MAX_PATH];
        if (!GetTempPathW(MAX_PATH, dir) || !GetTempFileNameW(dir, L"sje", 0, temp))
            return nullptr;
        memcpy(name, temp, (wcslen(temp) + 1) * sizeof(WCHAR));
        return (HMMIO)TRUE;
    }
    MMIO *m = mm_alloc(sizeof(*m));
    if (!m) {
        if (user)
            user->wErrorRet = MMIOERR_OUTOFMEMORY;
        return nullptr;
    }
    if (user) {
        m->info.fccIOProc = user->fccIOProc;
        m->info.pIOProc = user->pIOProc;
        m->info.cchBuffer = user->cchBuffer;
        m->info.pchBuffer = user->pchBuffer;
        m->info.adwInfo[0] = user->adwInfo[0];
        m->info.adwInfo[1] = user->adwInfo[1];
        m->info.adwInfo[2] = user->adwInfo[2];
    }
    m->info.dwFlags = flags & ~(MMIO_ALLOCBUF | MMIO_DIRTY);
    m->info.hmmio = (HMMIO)m;
    m->unicode = TRUE;
    if (!m->info.pIOProc) {
        FOURCC fcc = m->info.fccIOProc ? m->info.fccIOProc : proc_of_name(name);
        m->info.fccIOProc = fcc;
        m->info.pIOProc = find_proc(fcc, &m->unicode);
    } else {
        m->unicode = FALSE;
    }
    UINT error = MMSYSERR_NOERROR;
    if (!m->info.pIOProc)
        error = MMIOERR_FILENOTFOUND;
    if (!error && m->info.fccIOProc == FOURCC_MEM && !m->info.pchBuffer && !(flags & MMIO_ALLOCBUF))
        error = MMIOERR_OUTOFMEMORY;
    if (!error && !m->info.pchBuffer && ((flags & MMIO_ALLOCBUF) || m->info.cchBuffer)) {
        if (!m->info.cchBuffer)
            m->info.cchBuffer = MMIO_DEFAULTBUFFER;
        m->info.pchBuffer = mm_alloc(m->info.cchBuffer);
        m->own_buffer = TRUE;
        if (!m->info.pchBuffer)
            error = MMIOERR_OUTOFMEMORY;
    }
    if (!error) {
        m->info.pchNext = m->info.pchBuffer;
        m->info.pchEndWrite = m->info.pchBuffer + m->info.cchBuffer;
        /* a memory file's contents are its buffer; a disk file's buffer starts empty. */
        m->info.pchEndRead = m->info.fccIOProc == FOURCC_MEM ? m->info.pchBuffer + m->info.cchBuffer
                                                             : m->info.pchBuffer;
        if (m->info.fccIOProc == FOURCC_MEM)
            m->info.adwInfo[0] = user ? user->adwInfo[0] : 0;
        LPARAM file = (LPARAM)name;
        char narrow[MAX_PATH] = {};
        if (!m->unicode && name) {
            WideCharToMultiByte(CP_ACP, 0, name, -1, narrow, MAX_PATH, nullptr, nullptr);
            file = (LPARAM)narrow;
        }
        error = (UINT)send(m, MMIOM_OPEN, file, 0);
    }
    if (error) {
        if (m->own_buffer)
            mm_free(m->info.pchBuffer);
        mm_free(m);
        if (user)
            user->wErrorRet = error;
        return nullptr;
    }
    if (user)
        user->wErrorRet = 0;
    return (HMMIO)m;
}

WINMMAPI HMMIO WINAPI mmioOpenW(LPWSTR file, LPMMIOINFO info, DWORD flags) { return open_common(file, info, flags); }

WINMMAPI HMMIO WINAPI mmioOpenA(LPSTR file, LPMMIOINFO info, DWORD flags)
{
    WCHAR wide[MAX_PATH] = {};
    if (file)
        MultiByteToWideChar(CP_ACP, 0, file, -1, wide, MAX_PATH);
    HMMIO h = open_common(file ? wide : nullptr, info, flags);
    if (h && file && (flags & (MMIO_PARSE | MMIO_GETTEMP)))
        WideCharToMultiByte(CP_ACP, 0, wide, -1, file, (flags & MMIO_PARSE) ? 128 : MAX_PATH, nullptr, nullptr);
    return h;
}

/* ------------------------------------------------------------------ the buffer */

static MMRESULT flush_buffer(MMIO *m, BOOL force)
{
    MMIOINFO *i = &m->info;
    if (!i->pchBuffer || !(i->dwFlags & MMIO_DIRTY) || i->fccIOProc == FOURCC_MEM)
        return MMSYSERR_NOERROR;
    LONG length = (LONG)((i->pchNext > i->pchEndRead ? i->pchNext : i->pchEndRead) - i->pchBuffer);
    if (i->lDiskOffset != i->lBufOffset && send(m, MMIOM_SEEK, i->lBufOffset, SEEK_SET) == -1)
        return MMIOERR_CANNOTSEEK;
    if (length && send(m, force ? MMIOM_WRITEFLUSH : MMIOM_WRITE, (LPARAM)i->pchBuffer, length) != length)
        return MMIOERR_CANNOTWRITE;
    i->dwFlags &= ~MMIO_DIRTY;
    return MMSYSERR_NOERROR;
}

/* the logical position is in the buffer, or on disk when unbuffered. */
static LONG position(const MMIO *m)
{
    const MMIOINFO *i = &m->info;
    return i->pchBuffer ? i->lBufOffset + (LONG)(i->pchNext - i->pchBuffer) : i->lDiskOffset;
}

/* empties the buffer and positions the file at the logical cursor. */
static MMRESULT refill_at(MMIO *m, LONG at, BOOL read)
{
    MMIOINFO *i = &m->info;
    MMRESULT r = flush_buffer(m, FALSE);
    if (r)
        return r;
    if (i->lDiskOffset != at && send(m, MMIOM_SEEK, at, SEEK_SET) == -1)
        return MMIOERR_CANNOTSEEK;
    i->lBufOffset = at;
    i->pchNext = i->pchEndRead = i->pchBuffer;
    if (read) {
        LRESULT n = send(m, MMIOM_READ, (LPARAM)i->pchBuffer, i->cchBuffer);
        if (n < 0)
            return MMIOERR_CANNOTREAD;
        i->pchEndRead = i->pchBuffer + n;
    }
    return MMSYSERR_NOERROR;
}

WINMMAPI LONG WINAPI mmioRead(HMMIO h, HPSTR buffer, LONG count)
{
    MMIO *m = mmio_of(h);
    if (!m || count < 0)
        return -1;
    MMIOINFO *i = &m->info;
    if (!i->pchBuffer)
        return (LONG)send(m, MMIOM_READ, (LPARAM)buffer, count);
    LONG done = 0;
    while (done < count) {
        LONG have = (LONG)(i->pchEndRead - i->pchNext);
        if (have <= 0) {
            if (i->fccIOProc == FOURCC_MEM || refill_at(m, position(m), TRUE) || i->pchEndRead == i->pchNext)
                break;
            continue;
        }
        LONG n = have < count - done ? have : count - done;
        memcpy(buffer + done, i->pchNext, n);
        i->pchNext += n;
        done += n;
    }
    return done;
}

/* a memory file grows by its increment, adwInfo[0], when mmio allocated its buffer. */
static BOOL expand(MMIO *m, LONG need)
{
    MMIOINFO *i = &m->info;
    if (!m->own_buffer || !i->adwInfo[0])
        return FALSE;
    LONG size = i->cchBuffer;
    while (size < need)
        size += (LONG)i->adwInfo[0];
    char *grown = mm_alloc(size);
    if (!grown)
        return FALSE;
    memcpy(grown, i->pchBuffer, i->cchBuffer);
    i->pchNext = grown + (i->pchNext - i->pchBuffer);
    i->pchEndRead = grown + (i->pchEndRead - i->pchBuffer);
    mm_free(i->pchBuffer);
    i->pchBuffer = grown;
    i->cchBuffer = size;
    i->pchEndWrite = grown + size;
    return TRUE;
}

WINMMAPI LONG WINAPI mmioWrite(HMMIO h, const char *buffer, LONG count)
{
    MMIO *m = mmio_of(h);
    if (!m || count < 0)
        return -1;
    MMIOINFO *i = &m->info;
    if (!i->pchBuffer)
        return (LONG)send(m, MMIOM_WRITE, (LPARAM)buffer, count);
    LONG done = 0;
    while (done < count) {
        LONG room = (LONG)(i->pchEndWrite - i->pchNext);
        if (room <= 0) {
            if (i->fccIOProc == FOURCC_MEM) {
                if (!expand(m, i->cchBuffer + (count - done)))
                    break;
            } else if (refill_at(m, position(m), FALSE)) {
                break;
            }
            continue;
        }
        LONG n = room < count - done ? room : count - done;
        memcpy(i->pchNext, buffer + done, n);
        i->pchNext += n;
        if (i->pchNext > i->pchEndRead)
            i->pchEndRead = i->pchNext;
        i->dwFlags |= MMIO_DIRTY;
        done += n;
    }
    return done;
}

WINMMAPI LONG WINAPI mmioSeek(HMMIO h, LONG offset, int origin)
{
    MMIO *m = mmio_of(h);
    if (!m)
        return -1;
    MMIOINFO *i = &m->info;
    if (!i->pchBuffer)
        return (LONG)send(m, MMIOM_SEEK, offset, origin);
    LONG target;
    if (origin == SEEK_SET)
        target = offset;
    else if (origin == SEEK_CUR)
        target = position(m) + offset;
    else if (origin == SEEK_END) {
        if (i->fccIOProc == FOURCC_MEM) {
            target = (LONG)(i->pchEndRead - i->pchBuffer) + offset;
        } else {
            if (flush_buffer(m, FALSE))
                return -1;
            LONG end = (LONG)send(m, MMIOM_SEEK, 0, SEEK_END);
            if (end == -1)
                return -1;
            /* the buffer may hold written data past what the disk has. */
            LONG buffered = i->lBufOffset + (LONG)(i->pchEndRead - i->pchBuffer);
            target = (end > buffered ? end : buffered) + offset;
        }
    } else {
        return -1;
    }
    if (target < 0)
        return -1;
    /* within the buffer: only the position moves. */
    if (target >= i->lBufOffset && target <= i->lBufOffset + (LONG)(i->pchEndRead - i->pchBuffer)) {
        i->pchNext = i->pchBuffer + (target - i->lBufOffset);
        return target;
    }
    if (i->fccIOProc == FOURCC_MEM)
        return -1;
    if (refill_at(m, target, FALSE))
        return -1;
    return target;
}

WINMMAPI MMRESULT WINAPI mmioGetInfo(HMMIO h, LPMMIOINFO info, UINT flags)
{
    MMIO *m = mmio_of(h);
    if (!m || !info)
        return MMSYSERR_INVALHANDLE;
    *info = m->info;
    return MMSYSERR_NOERROR;
}

/* accepts the application's buffer position, data end and dirty flag. */
WINMMAPI MMRESULT WINAPI mmioSetInfo(HMMIO h, LPCMMIOINFO info, UINT flags)
{
    MMIO *m = mmio_of(h);
    if (!m || !info)
        return MMSYSERR_INVALHANDLE;
    MMIOINFO *i = &m->info;
    if (info->pchBuffer != i->pchBuffer)
        return MMSYSERR_INVALPARAM;
    i->pchNext = info->pchNext;
    if (info->pchEndRead > i->pchEndRead)
        i->pchEndRead = info->pchEndRead;
    if (info->pchNext > i->pchEndRead)
        i->pchEndRead = info->pchNext;
    i->dwFlags = (i->dwFlags & ~MMIO_DIRTY) | (info->dwFlags & MMIO_DIRTY);
    i->adwInfo[0] = info->adwInfo[0];
    i->adwInfo[1] = info->adwInfo[1];
    i->adwInfo[2] = info->adwInfo[2];
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI mmioSetBuffer(HMMIO h, LPSTR buffer, LONG count, UINT flags)
{
    MMIO *m = mmio_of(h);
    if (!m || count < 0)
        return MMSYSERR_INVALHANDLE;
    MMIOINFO *i = &m->info;
    if (i->fccIOProc == FOURCC_MEM)
        return MMIOERR_CANNOTWRITE;
    LONG at = position(m);
    MMRESULT r = flush_buffer(m, FALSE);
    if (r)
        return r;
    if (m->own_buffer)
        mm_free(i->pchBuffer);
    m->own_buffer = FALSE;
    i->pchBuffer = buffer;
    i->cchBuffer = count;
    if (!buffer && count) {
        if (!(i->pchBuffer = mm_alloc(count))) {
            i->cchBuffer = 0;
            return MMIOERR_OUTOFMEMORY;
        }
        m->own_buffer = TRUE;
    }
    if (!i->pchBuffer)
        i->cchBuffer = 0;
    i->lBufOffset = at;
    i->pchNext = i->pchEndRead = i->pchBuffer;
    i->pchEndWrite = i->pchBuffer + i->cchBuffer;
    if (i->lDiskOffset != at && send(m, MMIOM_SEEK, at, SEEK_SET) == -1)
        return MMIOERR_CANNOTSEEK;
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI mmioFlush(HMMIO h, UINT flags)
{
    MMIO *m = mmio_of(h);
    if (!m)
        return MMSYSERR_INVALHANDLE;
    MMRESULT r = flush_buffer(m, TRUE);
    if (!r && (flags & MMIO_EMPTYBUF) && m->info.fccIOProc != FOURCC_MEM)
        r = refill_at(m, position(m), FALSE);
    return r;
}

/*
 * flushes writes, then prepares the next buffer at the previous one's end. reads fill it; writes start empty.
 * reading at EOF succeeds with pchNext equal to pchEndRead.
 */
WINMMAPI MMRESULT WINAPI mmioAdvance(HMMIO h, LPMMIOINFO info, UINT flags)
{
    MMIO *m = mmio_of(h);
    if (!m)
        return MMSYSERR_INVALHANDLE;
    MMIOINFO *i = &m->info;
    if (!i->pchBuffer)
        return MMIOERR_UNBUFFERED;
    if (info) {
        MMRESULT r = mmioSetInfo(h, info, 0);
        if (r)
            return r;
    }
    MMRESULT r;
    if (i->fccIOProc == FOURCC_MEM) {
        r = flags & MMIO_WRITE ? (expand(m, i->cchBuffer + 1) ? MMSYSERR_NOERROR : MMIOERR_CANNOTEXPAND)
                               : MMSYSERR_NOERROR;
    } else {
        LONG end = i->lBufOffset + (LONG)((i->pchNext > i->pchEndRead ? i->pchNext : i->pchEndRead) - i->pchBuffer);
        r = refill_at(m, flags & MMIO_WRITE ? position(m) : end, !(flags & MMIO_WRITE));
    }
    if (!r && info)
        *info = *i;
    return r;
}

WINMMAPI MMRESULT WINAPI mmioClose(HMMIO h, UINT flags)
{
    MMIO *m = mmio_of(h);
    if (!m)
        return MMSYSERR_INVALHANDLE;
    MMRESULT r = flush_buffer(m, FALSE);
    LRESULT closed = send(m, MMIOM_CLOSE, flags, flags);
    if (!r && closed)
        r = (MMRESULT)closed;
    if (m->own_buffer)
        mm_free(m->info.pchBuffer);
    mm_free(m);
    return r;
}

WINMMAPI LRESULT WINAPI mmioSendMessage(HMMIO h, UINT message, LPARAM param1, LPARAM param2)
{
    MMIO *m = mmio_of(h);
    if (!m || message < MMIOM_USER)
        return MMSYSERR_INVALPARAM;
    return send(m, message, param1, param2);
}

WINMMAPI MMRESULT WINAPI mmioRenameW(LPCWSTR from, LPCWSTR to, LPCMMIOINFO info, DWORD flags)
{
    MMIOINFO temp = {};
    BOOL unicode = TRUE;
    LPMMIOPROC proc = info && info->pIOProc ? info->pIOProc
                                             : find_proc(info && info->fccIOProc ? info->fccIOProc : FOURCC_DOS,
                                                         &unicode);
    if (!proc)
        return MMIOERR_FILENOTFOUND;
    return (MMRESULT)proc((LPSTR)&temp, MMIOM_RENAME, (LPARAM)from, (LPARAM)to);
}

WINMMAPI MMRESULT WINAPI mmioRenameA(LPCSTR from, LPCSTR to, LPCMMIOINFO info, DWORD flags)
{
    WCHAR a[MAX_PATH] = {}, b[MAX_PATH] = {};
    MultiByteToWideChar(CP_ACP, 0, from, -1, a, MAX_PATH);
    MultiByteToWideChar(CP_ACP, 0, to, -1, b, MAX_PATH);
    return mmioRenameW(a, b, info, flags);
}

/* ------------------------------------------------------------------ riff chunks */

/*
 * finds the next matching chunk within the parent, leaving the cursor at its data, past the form type for RIFF and
 * LIST chunks.
 */
WINMMAPI MMRESULT WINAPI mmioDescend(HMMIO h, LPMMCKINFO ck, const MMCKINFO *parent, UINT flags)
{
    MMIO *m = mmio_of(h);
    if (!m || !ck)
        return MMSYSERR_INVALPARAM;
    FOURCC want_id = ck->ckid, want_type = ck->fccType;
    DWORD end = parent ? parent->dwDataOffset + parent->cksize : 0xFFFFFFFF;
    for (;;) {
        LONG start = mmioSeek(h, 0, SEEK_CUR);
        if (start < 0 || (parent && (DWORD)start + 8 > end))
            return MMIOERR_CHUNKNOTFOUND;
        DWORD header[2];
        if (mmioRead(h, (HPSTR)header, 8) != 8)
            return MMIOERR_CHUNKNOTFOUND;
        ck->ckid = header[0];
        ck->cksize = header[1];
        ck->dwDataOffset = (DWORD)start + 8;
        ck->dwFlags = 0;
        ck->fccType = 0;
        BOOL form = ck->ckid == FOURCC_RIFF || ck->ckid == FOURCC_LIST;
        if (form && mmioRead(h, (HPSTR)&ck->fccType, 4) != 4)
            return MMIOERR_CHUNKNOTFOUND;
        BOOL match = TRUE;
        if (flags & MMIO_FINDCHUNK)
            match = ck->ckid == want_id;
        else if (flags & MMIO_FINDRIFF)
            match = ck->ckid == FOURCC_RIFF && ck->fccType == want_type;
        else if (flags & MMIO_FINDLIST)
            match = ck->ckid == FOURCC_LIST && ck->fccType == want_type;
        if (match)
            return MMSYSERR_NOERROR;
        if (mmioSeek(h, (LONG)(ck->dwDataOffset + ck->cksize + (ck->cksize & 1)), SEEK_SET) < 0)
            return MMIOERR_CHUNKNOTFOUND;
    }
}

/* writes a created chunk's size and pad byte, then seeks past its end. */
WINMMAPI MMRESULT WINAPI mmioAscend(HMMIO h, LPMMCKINFO ck, UINT flags)
{
    MMIO *m = mmio_of(h);
    if (!m || !ck)
        return MMSYSERR_INVALPARAM;
    if (ck->dwFlags & MMIO_DIRTY) {
        LONG end = mmioSeek(h, 0, SEEK_CUR);
        if (end < 0)
            return MMIOERR_CANNOTSEEK;
        DWORD size = (DWORD)end - ck->dwDataOffset;
        if (size & 1) {
            char pad = 0;
            if (mmioWrite(h, &pad, 1) != 1)
                return MMIOERR_CANNOTWRITE;
        }
        if (size != ck->cksize) {
            ck->cksize = size;
            if (mmioSeek(h, (LONG)ck->dwDataOffset - 4, SEEK_SET) < 0 || mmioWrite(h, (const char *)&size, 4) != 4)
                return MMIOERR_CANNOTWRITE;
        }
        ck->dwFlags &= ~MMIO_DIRTY;
    }
    if (mmioSeek(h, (LONG)(ck->dwDataOffset + ck->cksize + (ck->cksize & 1)), SEEK_SET) < 0)
        return MMIOERR_CANNOTSEEK;
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI mmioCreateChunk(HMMIO h, LPMMCKINFO ck, UINT flags)
{
    MMIO *m = mmio_of(h);
    if (!m || !ck)
        return MMSYSERR_INVALPARAM;
    if (flags & MMIO_CREATERIFF)
        ck->ckid = FOURCC_RIFF;
    else if (flags & MMIO_CREATELIST)
        ck->ckid = FOURCC_LIST;
    LONG start = mmioSeek(h, 0, SEEK_CUR);
    if (start < 0)
        return MMIOERR_CANNOTSEEK;
    ck->dwDataOffset = (DWORD)start + 8;
    DWORD header[3] = {ck->ckid, ck->cksize, ck->fccType};
    LONG length = (flags & (MMIO_CREATERIFF | MMIO_CREATELIST)) ? 12 : 8;
    if (mmioWrite(h, (const char *)header, length) != length) {
        mmioSeek(h, start, SEEK_SET);
        return MMIOERR_CANNOTWRITE;
    }
    ck->dwFlags = MMIO_DIRTY;
    return MMSYSERR_NOERROR;
}
