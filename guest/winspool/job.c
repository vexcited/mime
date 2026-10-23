/*
 * jobs run in the process that starts them; there is no spooler service. Microsoft Print to PDF converts GDI bitmap
 * pages to PDF and copies RAW data unchanged. output comes from the document's path, MIME_PRINT_DIR plus its name,
 * or a save dialog.
 */
#include "winspool_private.h"
#include "pdf.h"

struct SpoolJob {
    SpoolJob *Next;
    DWORD Id;
    int Printer;
    WCHAR Document[260];
    WCHAR Output[MAX_PATH];
    WCHAR Datatype[32];
    DWORD Status;
    DWORD PagesPrinted;
    DWORD Size;
    SYSTEMTIME Submitted;
    BOOL Bitmap;
    BOOL InPage;
    BOOL Cancelled;
    HANDLE File;
    PdfWriter *Pdf;
    BYTE *Page;
    SIZE_T PageSize, PageCapacity;
};

static SRWLOCK g_lock = SRWLOCK_INIT;
static SpoolJob *g_jobs;
static LONG g_next_job;

/* ------------------------------------------------------------------ the output file */

/* sanitizes a document name for use as a filename, including trailing dots and spaces. */
static void file_name_of(LPCWSTR document, WCHAR *name, SIZE_T chars)
{
    SIZE_T n = 0;
    for (; *document && n + 1 < chars; document++) {
        WCHAR c = *document;
        if (c < 32 || wcschr(L"\\/:*?\"<>|", c))
            c = L'_';
        name[n++] = c;
    }
    while (n && (name[n - 1] == L' ' || name[n - 1] == L'.'))
        n--;
    name[n] = 0;
    if (!n)
        wcscpy(name, L"Document");
}

/* the first name free in a folder, "Name.pdf", "Name (2).pdf" and so on. */
static BOOL free_path(LPCWSTR directory, LPCWSTR base, WCHAR *path)
{
    for (int i = 1; i < 1000; i++) {
        if (i == 1)
            _snwprintf(path, MAX_PATH, L"%ls\\%ls.pdf", directory, base);
        else
            _snwprintf(path, MAX_PATH, L"%ls\\%ls (%d).pdf", directory, base, i);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
            return TRUE;
    }
    return FALSE;
}

typedef BOOL(WINAPI *GET_SAVE_FILE_NAME)(void *ofn);

/* loads comdlg32's save dialog by name to avoid a static import. */
static PWSTR ask_user(LPCWSTR document, LPCWSTR directory)
{
    HMODULE comdlg = LoadLibraryW(L"comdlg32.dll");
    GET_SAVE_FILE_NAME save = comdlg ? (GET_SAVE_FILE_NAME)GetProcAddress(comdlg, "GetSaveFileNameW") : nullptr;
    if (!save)
        return nullptr;
    WCHAR base[MAX_PATH], path[MAX_PATH];
    file_name_of(document, base, ARRAYSIZE(base));
    _snwprintf(path, ARRAYSIZE(path), L"%ls.pdf", base);
    static const WCHAR filter[] = L"PDF Document (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
    /* OPENFILENAMEW layout without the comdlg32 header dependency. */
    struct {
        DWORD lStructSize;
        HWND hwndOwner;
        HINSTANCE hInstance;
        LPCWSTR lpstrFilter;
        LPWSTR lpstrCustomFilter;
        DWORD nMaxCustFilter, nFilterIndex;
        LPWSTR lpstrFile;
        DWORD nMaxFile;
        LPWSTR lpstrFileTitle;
        DWORD nMaxFileTitle;
        LPCWSTR lpstrInitialDir, lpstrTitle;
        DWORD Flags;
        WORD nFileOffset, nFileExtension;
        LPCWSTR lpstrDefExt;
        LPARAM lCustData;
        void *lpfnHook;
        LPCWSTR lpTemplateName;
        void *pvReserved;
        DWORD dwReserved, FlagsEx;
    } ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = path;
    ofn.nMaxFile = ARRAYSIZE(path);
    ofn.lpstrInitialDir = directory;
    ofn.lpstrTitle = L"Save Print Output As";
    ofn.lpstrDefExt = L"pdf";
    /* OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_HIDEREADONLY. */
    ofn.Flags = 0x2 | 0x800 | 0x8 | 0x80000 | 0x4;
    if (!save(&ofn))
        return nullptr;
    SIZE_T chars = wcslen(path) + 1;
    PWSTR result = SpoolAlloc(chars * sizeof(WCHAR));
    if (result)
        memcpy(result, path, chars * sizeof(WCHAR));
    return result;
}

PWSTR SpoolAskOutput(LPCWSTR document, int printer)
{
    WCHAR folder[MAX_PATH], base[MAX_PATH], path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"MIME_PRINT_DIR", folder, ARRAYSIZE(folder));
    if (n && n < ARRAYSIZE(folder)) {
        file_name_of(document, base, ARRAYSIZE(base));
        CreateDirectoryW(folder, nullptr);
        if (!free_path(folder, base, path))
            return nullptr;
        PWSTR result = SpoolAlloc((wcslen(path) + 1) * sizeof(WCHAR));
        if (result)
            wcscpy(result, path);
        return result;
    }
    n = GetEnvironmentVariableW(L"USERPROFILE", folder, ARRAYSIZE(folder));
    if (n && n + 12 < ARRAYSIZE(folder))
        wcscat(folder, L"\\Documents");
    else
        folder[0] = 0;
    (void)printer;
    return ask_user(document, folder[0] ? folder : nullptr);
}

/* ------------------------------------------------------------------ the list */

static SpoolJob *job_of(SpoolHandle *h)
{
    return h->job;
}

static void unlink_job(SpoolJob *job)
{
    AcquireSRWLockExclusive(&g_lock);
    for (SpoolJob **link = &g_jobs; *link; link = &(*link)->Next) {
        if (*link == job) {
            *link = job->Next;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

static void free_job(SpoolJob *job)
{
    SpoolFree(job->Page);
    SpoolFree(job);
}

DWORD SpoolActiveJobs(int printer)
{
    DWORD count = 0;
    AcquireSRWLockShared(&g_lock);
    for (SpoolJob *j = g_jobs; j; j = j->Next)
        if (j->Printer == printer)
            count++;
    ReleaseSRWLockShared(&g_lock);
    return count;
}

/* ------------------------------------------------------------------ starting and ending */

static BOOL known_datatype(LPCWSTR type, BOOL *bitmap)
{
    *bitmap = FALSE;
    if (!_wcsicmp(type, MIME_DATATYPE_BITMAP)) {
        *bitmap = TRUE;
        return TRUE;
    }
    return !_wcsicmp(type, L"RAW") || !_wcsicmp(type, L"RAW [FF appended]") || !_wcsicmp(type, L"RAW [FF auto]");
}

DWORD SpoolStartDoc(SpoolHandle *h, const DOC_INFO_1W *info)
{
    if (h->job) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    BOOL bitmap;
    LPCWSTR type = info->pDatatype && *info->pDatatype ? info->pDatatype : (h->datatype[0] ? h->datatype : L"RAW");
    if (!known_datatype(type, &bitmap)) {
        SetLastError(ERROR_INVALID_DATATYPE);
        return 0;
    }
    SpoolJob *job = SpoolAlloc(sizeof(*job));
    if (!job) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    job->Printer = h->printer;
    job->File = INVALID_HANDLE_VALUE;
    job->Bitmap = bitmap;
    _snwprintf(job->Document, ARRAYSIZE(job->Document), L"%ls", info->pDocName ? info->pDocName : L"Untitled");
    _snwprintf(job->Datatype, ARRAYSIZE(job->Datatype), L"%ls", type);
    PWSTR asked = nullptr;
    if (info->pOutputFile && *info->pOutputFile && _wcsicmp(info->pOutputFile, L"FILE:") &&
        _wcsicmp(info->pOutputFile, L"PORTPROMPT:")) {
        DWORD n = GetFullPathNameW(info->pOutputFile, ARRAYSIZE(job->Output), job->Output, nullptr);
        if (!n || n >= ARRAYSIZE(job->Output)) {
            free_job(job);
            SetLastError(ERROR_INVALID_NAME);
            return 0;
        }
    } else {
        asked = SpoolAskOutput(job->Document, h->printer);
        if (!asked) {
            free_job(job);
            SetLastError(ERROR_PRINT_CANCELLED);
            return 0;
        }
        _snwprintf(job->Output, ARRAYSIZE(job->Output), L"%ls", asked);
        SpoolFree(asked);
    }
    job->File = CreateFileW(job->Output, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
    if (job->File == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        free_job(job);
        SetLastError(error);
        return 0;
    }
    if (bitmap) {
        job->Pdf = PdfCreate(job->File, job->Document);
        if (!job->Pdf) {
            CloseHandle(job->File);
            DeleteFileW(job->Output);
            free_job(job);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
    }
    GetLocalTime(&job->Submitted);
    job->Status = JOB_STATUS_SPOOLING;
    job->Id = (DWORD)InterlockedIncrement(&g_next_job);
    AcquireSRWLockExclusive(&g_lock);
    job->Next = g_jobs;
    g_jobs = job;
    ReleaseSRWLockExclusive(&g_lock);
    h->job = job;
    return job->Id;
}

/* closes a completed document or removes an abandoned one. */
static BOOL finish(SpoolHandle *h, BOOL keep)
{
    SpoolJob *job = job_of(h);
    if (!job)
        return SpoolFail(ERROR_SPL_NO_STARTDOC);
    BOOL ok = TRUE;
    if (job->Pdf) {
        if (keep)
            ok = PdfFinish(job->Pdf);
        else
            PdfAbort(job->Pdf);
        job->Pdf = nullptr;
    }
    CloseHandle(job->File);
    if (!keep || job->Cancelled)
        DeleteFileW(job->Output);
    h->job = nullptr;
    unlink_job(job);
    free_job(job);
    if (!ok)
        SetLastError(ERROR_WRITE_FAULT);
    return ok;
}

void SpoolAbortJob(SpoolHandle *h)
{
    if (h->job)
        finish(h, FALSE);
}

/* ------------------------------------------------------------------ the data */

static BOOL page_end(SpoolJob *job)
{
    const MIME_PRINT_PAGE *header = (const MIME_PRINT_PAGE *)job->Page;
    if (job->PageSize < sizeof(*header) || header->Magic != MIME_PRINT_PAGE_MAGIC || !header->Width || !header->Height ||
        !header->XDpi || !header->YDpi || header->Stride < header->Width * 4u ||
        job->PageSize < sizeof(*header) + (SIZE_T)header->Stride * header->Height)
        return SpoolFail(ERROR_INVALID_DATA);
    if (!PdfAddImagePage(job->Pdf, job->Page + sizeof(*header), (int)header->Width, (int)header->Height,
                         (int)header->Stride, (int)header->XDpi, (int)header->YDpi, (header->Reserved & 1) != 0))
        return SpoolFail(ERROR_WRITE_FAULT);
    job->PagesPrinted++;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI StartPagePrinter(HANDLE printer)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return FALSE;
    SpoolJob *job = job_of(h);
    if (!job)
        return SpoolFail(ERROR_SPL_NO_STARTDOC);
    if (job->InPage)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    job->InPage = TRUE;
    job->PageSize = 0;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI EndPagePrinter(HANDLE printer)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return FALSE;
    SpoolJob *job = job_of(h);
    if (!job)
        return SpoolFail(ERROR_SPL_NO_STARTDOC);
    if (job->Cancelled)
        return SpoolFail(ERROR_PRINT_CANCELLED);
    /* a page that is not started is still a page for a job of raw data. */
    if (job->Bitmap && job->InPage) {
        job->InPage = FALSE;
        return page_end(job);
    }
    job->InPage = FALSE;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI WritePrinter(HANDLE printer, LPVOID buffer, DWORD size, LPDWORD written)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return FALSE;
    SpoolJob *job = job_of(h);
    if (!job)
        return SpoolFail(ERROR_SPL_NO_STARTDOC);
    if (!written)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    *written = 0;
    if (job->Cancelled)
        return SpoolFail(ERROR_PRINT_CANCELLED);
    if (job->Bitmap) {
        if (!job->InPage)
            return SpoolFail(ERROR_SPL_NO_STARTDOC);
        if (job->PageSize + size > job->PageCapacity) {
            SIZE_T cap = job->PageCapacity ? job->PageCapacity : 4096;
            while (cap < job->PageSize + size)
                cap *= 2;
            BYTE *grown = job->Page ? HeapReAlloc(GetProcessHeap(), 0, job->Page, cap)
                                    : HeapAlloc(GetProcessHeap(), 0, cap);
            if (!grown)
                return SpoolFail(ERROR_NOT_ENOUGH_MEMORY);
            job->Page = grown;
            job->PageCapacity = cap;
        }
        memcpy(job->Page + job->PageSize, buffer, size);
        job->PageSize += size;
        job->Size += size;
        *written = size;
        return TRUE;
    }
    DWORD done = 0;
    if (size && (!WriteFile(job->File, buffer, size, &done, nullptr) || done != size))
        return SpoolFail(ERROR_WRITE_FAULT);
    job->Size += size;
    *written = size;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI FlushPrinter(HANDLE printer, LPVOID buffer, DWORD size, LPDWORD written, DWORD sleep)
{
    return WritePrinter(printer, buffer, size, written);
}

DECLSPEC_EXPORT BOOL WINAPI EndDocPrinter(HANDLE printer)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return FALSE;
    return finish(h, TRUE);
}

DECLSPEC_EXPORT BOOL WINAPI AbortPrinter(HANDLE printer)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return FALSE;
    if (!h->job)
        return SpoolFail(ERROR_SPL_NO_STARTDOC);
    return finish(h, FALSE);
}

DECLSPEC_EXPORT DWORD WINAPI StartDocPrinterW(HANDLE printer, DWORD level, LPBYTE info)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return 0;
    if (level < 1 || level > 3) {
        SetLastError(ERROR_INVALID_LEVEL);
        return 0;
    }
    if (!info) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return SpoolStartDoc(h, (const DOC_INFO_1W *)info);
}

DECLSPEC_EXPORT DWORD WINAPI StartDocPrinterA(HANDLE printer, DWORD level, LPBYTE info)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return 0;
    if (level < 1 || level > 3) {
        SetLastError(ERROR_INVALID_LEVEL);
        return 0;
    }
    if (!info) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    const DOC_INFO_1A *a = (const DOC_INFO_1A *)info;
    DOC_INFO_1W w = {SpoolToWide(a->pDocName), SpoolToWide(a->pOutputFile), SpoolToWide(a->pDatatype)};
    DWORD id = SpoolStartDoc(h, &w);
    DWORD error = GetLastError();
    SpoolFree(w.pDocName);
    SpoolFree(w.pOutputFile);
    SpoolFree(w.pDatatype);
    SetLastError(error);
    return id;
}

/* ------------------------------------------------------------------ asking about jobs */

static void build_job(SpoolPack *p, const SpoolJob *j, DWORD level, void **result)
{
    WCHAR machine[MAX_COMPUTERNAME_LENGTH + 3] = L"\\\\", user[64] = L"user";
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(machine + 2, &size);
    const SpoolPrinter *pr = SpoolPrinterAt(j->Printer);
    JOB_INFO_1W *i = SpoolPackStruct(p, level == 2 ? sizeof(JOB_INFO_2W) : sizeof(JOB_INFO_1W));
    LPWSTR printer = SpoolPackString(p, pr->Name), host = SpoolPackString(p, machine), who = SpoolPackString(p, user),
           document = SpoolPackString(p, j->Document), datatype = SpoolPackString(p, j->Datatype);
    LPWSTR processor = nullptr, driver = nullptr;
    if (level == 2) {
        processor = SpoolPackString(p, L"winprint");
        driver = SpoolPackString(p, pr->Driver);
    }
    if (i) {
        i->JobId = j->Id;
        i->pPrinterName = printer;
        i->pMachineName = host;
        i->pUserName = who;
        i->pDocument = document;
        if (level == 1) {
            i->pDatatype = datatype;
            i->Status = j->Status;
            i->Priority = 1;
            i->Position = 1;
            i->TotalPages = j->PagesPrinted;
            i->PagesPrinted = j->PagesPrinted;
            i->Submitted = j->Submitted;
        } else {
            JOB_INFO_2W *i2 = (JOB_INFO_2W *)i;
            i2->pNotifyName = who;
            i2->pDatatype = datatype;
            i2->pPrintProcessor = processor;
            i2->pDriverName = driver;
            i2->Status = j->Status;
            i2->Priority = 1;
            i2->Position = 1;
            i2->TotalPages = j->PagesPrinted;
            i2->Size = j->Size;
            i2->Submitted = j->Submitted;
            i2->PagesPrinted = j->PagesPrinted;
        }
    }
    *result = i;
}

BOOL SpoolJobsEnum(int printer, DWORD level, DWORD first, DWORD count, LPBYTE buffer, DWORD size, LPDWORD needed,
                   LPDWORD returned, BOOL ansi)
{
    SpoolPack p;
    void *r;
    SpoolJob *list[64];
    DWORD n = 0, index = 0;
    AcquireSRWLockShared(&g_lock);
    for (SpoolJob *j = g_jobs; j && n < ARRAYSIZE(list); j = j->Next) {
        if (j->Printer != printer)
            continue;
        if (index++ >= first && n < count)
            list[n++] = j;
    }
    SpoolPackBegin(&p, nullptr, 0, TRUE, ansi);
    for (DWORD i = 0; i < n; i++)
        build_job(&p, list[i], level, &r);
    SIZE_T total = SpoolPackNeeded(&p);
    *needed = (DWORD)total;
    *returned = 0;
    BOOL ok = TRUE;
    if (n) {
        if (!buffer || size < total) {
            ok = FALSE;
        } else {
            memset(buffer, 0, size);
            SpoolPackBegin(&p, buffer, total, FALSE, ansi);
            for (DWORD i = 0; i < n; i++)
                build_job(&p, list[i], level, &r);
            *returned = n;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return ok ? TRUE : SpoolFail(ERROR_INSUFFICIENT_BUFFER);
}

BOOL SpoolJobGet(int printer, DWORD id, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, BOOL ansi)
{
    BOOL ok = FALSE, found = FALSE;
    AcquireSRWLockShared(&g_lock);
    for (SpoolJob *j = g_jobs; j; j = j->Next) {
        if (j->Printer != printer || j->Id != id)
            continue;
        found = TRUE;
        SpoolPack p;
        void *r;
        SpoolPackBegin(&p, nullptr, 0, TRUE, ansi);
        build_job(&p, j, level, &r);
        SIZE_T total = SpoolPackNeeded(&p);
        *needed = (DWORD)total;
        if (buffer && size >= total) {
            memset(buffer, 0, size);
            SpoolPackBegin(&p, buffer, total, FALSE, ansi);
            build_job(&p, j, level, &r);
            ok = TRUE;
        }
        break;
    }
    ReleaseSRWLockShared(&g_lock);
    if (!found)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    return ok ? TRUE : SpoolFail(ERROR_INSUFFICIENT_BUFFER);
}

BOOL SpoolJobSet(int printer, DWORD id, DWORD command)
{
    BOOL found = FALSE;
    AcquireSRWLockExclusive(&g_lock);
    for (SpoolJob *j = g_jobs; j; j = j->Next) {
        if (j->Printer != printer || j->Id != id)
            continue;
        found = TRUE;
        if (command == JOB_CONTROL_CANCEL || command == JOB_CONTROL_DELETE) {
            j->Cancelled = TRUE;
            j->Status |= JOB_STATUS_DELETING;
        } else if (command == JOB_CONTROL_PAUSE) {
            j->Status |= JOB_STATUS_PAUSED;
        } else if (command == JOB_CONTROL_RESUME) {
            j->Status &= ~JOB_STATUS_PAUSED;
        }
        break;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return found ? TRUE : SpoolFail(ERROR_INVALID_PARAMETER);
}
