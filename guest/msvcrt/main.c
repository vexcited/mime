/*
 * msvcrt entry point, heap, per thread data, errno, locks and the
 * invalid parameter report.
 */
#include "msvcrt_private.h"

HANDLE MsvcrtHeap;
/* the marker the msvc toolchain wants in modules that use floating point. */
int _fltused = 0x9875;
static DWORD g_tls = TLS_OUT_OF_INDEXES;
static MSVCRT_THREAD_DATA g_fallback_ptd;
static CRITICAL_SECTION g_locks[_TOTAL_LOCKS];

void MsvcrtStartupInit(void);
void MsvcrtProcessDetach(void);
void MsvcrtClockInit(void);

/* the encoded callback of the program for its thread local destructors, run as a thread ends. */
static PVOID g_exe_tls_callback;

DECLSPEC_EXPORT void __cdecl _register_thread_local_exe_atexit_callback(void *callback)
{
    g_exe_tls_callback = callback ? EncodePointer(callback) : nullptr;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        MsvcrtHeap = GetProcessHeap();
        g_tls = TlsAlloc();
        for (int i = 0; i < _TOTAL_LOCKS; i++)
            InitializeCriticalSection(&g_locks[i]);
        g_fallback_ptd.rand_next = 1;
        MsvcrtIoInit();
        MsvcrtStdioInit();
        MsvcrtEnvInit();
        MsvcrtStartupInit();
        MsvcrtClockInit();
        break;
    case DLL_THREAD_DETACH:
        if (g_exe_tls_callback)
            ((void(__cdecl *)(PVOID, DWORD, PVOID))DecodePointer(g_exe_tls_callback))(instance, reason, reserved);
        MsvcrtFreeThreadData();
        break;
    case DLL_PROCESS_DETACH:
        MsvcrtProcessDetach();
        break;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ per thread data */

void *MsvcrtEhThread(void) { return MsvcrtThreadData()->eh; }

MSVCRT_THREAD_DATA *MsvcrtThreadData(void)
{
    if (g_tls == TLS_OUT_OF_INDEXES)
        return &g_fallback_ptd;
    DWORD last = GetLastError();
    MSVCRT_THREAD_DATA *ptd = TlsGetValue(g_tls);
    if (!ptd) {
        ptd = HeapAlloc(MsvcrtHeap, HEAP_ZERO_MEMORY, sizeof(*ptd));
        if (!ptd || !TlsSetValue(g_tls, ptd)) {
            if (ptd)
                HeapFree(MsvcrtHeap, 0, ptd);
            ptd = &g_fallback_ptd;
        } else {
            ptd->rand_next = 1;
        }
    }
    SetLastError(last);
    return ptd;
}

void MsvcrtFreeThreadData(void)
{
    if (g_tls == TLS_OUT_OF_INDEXES)
        return;
    MSVCRT_THREAD_DATA *ptd = TlsGetValue(g_tls);
    if (ptd && ptd != &g_fallback_ptd) {
        TlsSetValue(g_tls, nullptr);
        HeapFree(MsvcrtHeap, 0, ptd);
    }
}

/* ------------------------------------------------------------------ errno */

int *__cdecl _errno(void)
{
    return &MsvcrtThreadData()->err;
}

unsigned long *__cdecl __doserrno(void)
{
    return &MsvcrtThreadData()->doserr;
}

void MsvcrtSetErrno(int e)
{
    MsvcrtThreadData()->err = e;
}

errno_t __cdecl _get_errno(int *value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *value = MsvcrtThreadData()->err;
    return 0;
}

errno_t __cdecl _set_errno(int value)
{
    MsvcrtThreadData()->err = value;
    return 0;
}

errno_t __cdecl _get_doserrno(unsigned long *value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *value = MsvcrtThreadData()->doserr;
    return 0;
}

errno_t __cdecl _set_doserrno(unsigned long value)
{
    MsvcrtThreadData()->doserr = value;
    return 0;
}

/* the win32 error to errno table of msvcrt's _dosmaperr. */
static const unsigned short g_errtable[][2] = {
    {ERROR_INVALID_FUNCTION, EINVAL},  {ERROR_FILE_NOT_FOUND, ENOENT},       {ERROR_PATH_NOT_FOUND, ENOENT},
    {ERROR_TOO_MANY_OPEN_FILES, EMFILE}, {ERROR_ACCESS_DENIED, EACCES},      {ERROR_INVALID_HANDLE, EBADF},
    {7, ENOMEM},                       {ERROR_NOT_ENOUGH_MEMORY, ENOMEM},     {9, ENOMEM},
    {10, E2BIG},                       {11, ENOEXEC},                         {12, EINVAL},
    {ERROR_INVALID_DATA, EINVAL},      {15, ENOENT},                          {16, EACCES},
    {17, EXDEV},                       {ERROR_NO_MORE_FILES, ENOENT},         {33, EACCES},
    {53, ENOENT},                      {65, EACCES},                          {67, ENOENT},
    {ERROR_FILE_EXISTS, EEXIST},       {82, EACCES},                          {83, EACCES},
    {ERROR_INVALID_PARAMETER, EINVAL}, {89, EAGAIN},                          {108, EACCES},
    {ERROR_BROKEN_PIPE, EPIPE},        {ERROR_DISK_FULL, ENOSPC},             {114, EBADF},
    {6, EINVAL},                       {128, ECHILD},                         {129, ECHILD},
    {130, EBADF},                      {131, EINVAL},                         {132, EACCES},
    {ERROR_DIR_NOT_EMPTY, ENOTEMPTY},  {158, EACCES},                         {161, ENOENT},
    {164, EAGAIN},                     {167, EACCES},                         {ERROR_ALREADY_EXISTS, EEXIST},
    {206, ENOENT},                     {215, EAGAIN},                         {1816, ENOMEM},
};

int MsvcrtErrnoFromOsError(unsigned long oserr)
{
    for (size_t i = 0; i < sizeof(g_errtable) / sizeof(g_errtable[0]); i++)
        if (g_errtable[i][0] == oserr)
            return g_errtable[i][1];
    if (oserr >= 19 && oserr <= 36)
        return EACCES;
    if (oserr >= 188 && oserr <= 202)
        return ENOEXEC;
    return EINVAL;
}

void MsvcrtDosMapError(unsigned long oserr)
{
    MSVCRT_THREAD_DATA *ptd = MsvcrtThreadData();
    ptd->doserr = oserr;
    ptd->err = MsvcrtErrnoFromOsError(oserr);
}

/* ------------------------------------------------------------------ invalid parameters */

typedef void(__cdecl *_invalid_parameter_handler)(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int,
                                                  uintptr_t);
static PVOID g_invalid_handler; /* encoded, NULL until a handler is set */

/* the encoding scrambles NULL too, so a handler never set is told apart before decoding. */
static _invalid_parameter_handler invalid_handler(void)
{
    return g_invalid_handler ? (_invalid_parameter_handler)DecodePointer(g_invalid_handler) : nullptr;
}

void __cdecl _invalid_parameter(const wchar_t *expression, const wchar_t *function, const wchar_t *file,
                                unsigned int line, uintptr_t reserved)
{
    MSVCRT_THREAD_DATA *ptd = MsvcrtThreadData();
    _invalid_parameter_handler handler = ptd && ptd->invalid_handler ? (_invalid_parameter_handler)DecodePointer(ptd->invalid_handler)
                                                                     : invalid_handler();
    if (handler) {
        handler(expression, function, file, line, reserved);
        return;
    }
    OutputDebugStringA("Invalid parameter passed to C runtime function.\n");
}

void __cdecl _invalid_parameter_noinfo(void)
{
    _invalid_parameter(nullptr, nullptr, nullptr, 0, 0);
}

void __cdecl _invoke_watson(const wchar_t *expression, const wchar_t *function, const wchar_t *file,
                            unsigned int line, uintptr_t reserved)
{
    OutputDebugStringA("Invalid parameter passed to C runtime function.\n");
}

_invalid_parameter_handler __cdecl _set_invalid_parameter_handler(_invalid_parameter_handler handler)
{
    _invalid_parameter_handler old = invalid_handler();
    g_invalid_handler = handler ? EncodePointer((PVOID)handler) : nullptr;
    return old;
}

_invalid_parameter_handler __cdecl _get_invalid_parameter_handler(void)
{
    return invalid_handler();
}

_invalid_parameter_handler __cdecl _set_thread_local_invalid_parameter_handler(_invalid_parameter_handler handler)
{
    MSVCRT_THREAD_DATA *ptd = MsvcrtThreadData();
    if (!ptd)
        return nullptr;
    _invalid_parameter_handler old = ptd->invalid_handler ? (_invalid_parameter_handler)DecodePointer(ptd->invalid_handler)
                                                          : nullptr;
    ptd->invalid_handler = handler ? EncodePointer((PVOID)handler) : nullptr;
    return old;
}

_invalid_parameter_handler __cdecl _get_thread_local_invalid_parameter_handler(void)
{
    MSVCRT_THREAD_DATA *ptd = MsvcrtThreadData();
    return ptd && ptd->invalid_handler ? (_invalid_parameter_handler)DecodePointer(ptd->invalid_handler) : nullptr;
}

void MsvcrtInvalidParameter(void)
{
    _invalid_parameter(nullptr, nullptr, nullptr, 0, 0);
}

/* ------------------------------------------------------------------ locks */

void __cdecl _lock(int n)
{
    if (n >= 0 && n < _TOTAL_LOCKS)
        EnterCriticalSection(&g_locks[n]);
}

void __cdecl _unlock(int n)
{
    if (n >= 0 && n < _TOTAL_LOCKS)
        LeaveCriticalSection(&g_locks[n]);
}

/* ------------------------------------------------------------------ heap */

static int g_new_mode;
typedef int(__cdecl *_PNH)(size_t);
static _PNH g_new_handler;

int MsvcrtNewMode;

/* the c++ exports _set_new_mode and its kin, in cxx.cpp, call these. */
int MsvcrtSetNewMode(int mode)
{
    int old = g_new_mode;
    g_new_mode = mode;
    return old;
}

int MsvcrtQueryNewMode(void)
{
    return g_new_mode;
}

_PNH MsvcrtSetNewHandler(_PNH handler)
{
    _PNH old = g_new_handler;
    g_new_handler = handler;
    return old;
}

_PNH MsvcrtQueryNewHandler(void)
{
    return g_new_handler;
}

int __cdecl _callnewh(size_t size)
{
    _PNH handler = g_new_handler;
    return handler && handler(size);
}

/* allocates, retrying through the new handler when new mode is set. */
static void *heap_alloc(size_t size, DWORD flags)
{
    if (size > (size_t)-32) {
        MsvcrtSetErrno(ENOMEM);
        return nullptr;
    }
    for (;;) {
        void *p = HeapAlloc(MsvcrtHeap, flags, size ? size : 1);
        if (p)
            return p;
        if (!g_new_mode || !_callnewh(size)) {
            MsvcrtSetErrno(ENOMEM);
            return nullptr;
        }
    }
}

void *__cdecl malloc(size_t size)
{
    return heap_alloc(size, 0);
}

void *__cdecl calloc(size_t count, size_t size)
{
    if (size && count > (size_t)-32 / size) {
        MSVCRT_INVALID(ENOMEM);
        return nullptr;
    }
    return heap_alloc(count * size, HEAP_ZERO_MEMORY);
}

void __cdecl free(void *p)
{
    if (p && !HeapFree(MsvcrtHeap, 0, p))
        MsvcrtDosMapError(GetLastError());
}

void *__cdecl realloc(void *p, size_t size)
{
    if (!p)
        return malloc(size);
    if (!size) {
        free(p);
        return nullptr;
    }
    if (size > (size_t)-32) {
        MsvcrtSetErrno(ENOMEM);
        return nullptr;
    }
    for (;;) {
        void *q = HeapReAlloc(MsvcrtHeap, 0, p, size);
        if (q)
            return q;
        if (!g_new_mode || !_callnewh(size)) {
            MsvcrtSetErrno(ENOMEM);
            return nullptr;
        }
    }
}

void *__cdecl _recalloc(void *p, size_t count, size_t size)
{
    if (size && count > (size_t)-32 / size) {
        MsvcrtSetErrno(ENOMEM);
        return nullptr;
    }
    size_t old = p ? HeapSize(MsvcrtHeap, 0, p) : 0;
    size_t total = count * size;
    void *q = realloc(p, total);
    if (q && total > old)
        memset((char *)q + old, 0, total - old);
    return q;
}

size_t __cdecl _msize(void *p)
{
    if (!p) {
        MSVCRT_INVALID(EINVAL);
        return (size_t)-1;
    }
    return HeapSize(MsvcrtHeap, 0, p);
}

void *__cdecl _expand(void *p, size_t size)
{
    if (!p) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    if (size > (size_t)-32) {
        MsvcrtSetErrno(ENOMEM);
        return nullptr;
    }
    void *q = HeapReAlloc(MsvcrtHeap, HEAP_REALLOC_IN_PLACE_ONLY, p, size ? size : 1);
    if (!q)
        MsvcrtSetErrno(ENOMEM);
    return q;
}

int __cdecl _heapchk(void)
{
    return HeapValidate(MsvcrtHeap, 0, nullptr) ? -2 : -3;
}

int __cdecl _heapmin(void)
{
    return 0;
}

int __cdecl _heapset(unsigned int fill)
{
    return _heapchk();
}

intptr_t __cdecl _get_heap_handle(void)
{
    return (intptr_t)MsvcrtHeap;
}

/* the block before an aligned pointer keeps the original allocation. */
void *__cdecl _aligned_offset_malloc(size_t size, size_t alignment, size_t offset)
{
    if (!alignment || (alignment & (alignment - 1)) || (offset && offset >= size)) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    if (alignment < sizeof(void *))
        alignment = sizeof(void *);
    size_t gap = (0 - offset) & (sizeof(void *) - 1);
    size_t total = size + alignment - 1 + sizeof(void *) + gap;
    if (total < size) {
        MsvcrtSetErrno(ENOMEM);
        return nullptr;
    }
    uintptr_t raw = (uintptr_t)malloc(total);
    if (!raw)
        return nullptr;
    uintptr_t aligned = ((raw + sizeof(void *) + gap + offset + alignment - 1) & ~(alignment - 1)) - offset;
    ((uintptr_t *)((aligned - gap) & ~(sizeof(void *) - 1)))[-1] = raw;
    return (void *)aligned;
}

void *__cdecl _aligned_malloc(size_t size, size_t alignment)
{
    return _aligned_offset_malloc(size, alignment, 0);
}

static uintptr_t aligned_base(void *p)
{
    uintptr_t q = (uintptr_t)p & ~(sizeof(void *) - 1);
    return ((uintptr_t *)q)[-1];
}

void __cdecl _aligned_free(void *p)
{
    if (p)
        free((void *)aligned_base(p));
}

void *__cdecl _aligned_offset_realloc(void *p, size_t size, size_t alignment, size_t offset)
{
    if (!p)
        return _aligned_offset_malloc(size, alignment, offset);
    if (!size) {
        _aligned_free(p);
        return nullptr;
    }
    uintptr_t raw = aligned_base(p);
    size_t old = HeapSize(MsvcrtHeap, 0, (void *)raw) - ((uintptr_t)p - raw);
    void *q = _aligned_offset_malloc(size, alignment, offset);
    if (!q)
        return nullptr;
    memcpy(q, p, old < size ? old : size);
    _aligned_free(p);
    return q;
}

void *__cdecl _aligned_realloc(void *p, size_t size, size_t alignment)
{
    return _aligned_offset_realloc(p, size, alignment, 0);
}

/* ------------------------------------------------------------------ code pages */

UINT MsvcrtCodePage(void)
{
    return CP_ACP;
}

wchar_t *MsvcrtAToW(const char *s)
{
    if (!s)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (n <= 0) {
        MsvcrtDosMapError(GetLastError());
        return nullptr;
    }
    wchar_t *w = malloc(n * sizeof(wchar_t));
    if (w)
        MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
    return w;
}

char *MsvcrtWToA(const wchar_t *s)
{
    if (!s)
        return nullptr;
    int n = WideCharToMultiByte(CP_ACP, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        MsvcrtDosMapError(GetLastError());
        return nullptr;
    }
    char *a = malloc(n);
    if (a)
        WideCharToMultiByte(CP_ACP, 0, s, -1, a, n, nullptr, nullptr);
    return a;
}
