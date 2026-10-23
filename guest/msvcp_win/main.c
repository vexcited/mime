/*
 * msvcp_win.dll, the C++ runtime of Windows' own dlls: std::mutex, the errors the standard library throws, and the
 * parallel algorithm threads, which run serially here.
 */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

/*
 * a mutex in the 80 bytes the caller reserves: its type first, as the library's inline constructor writes it, then
 * the lock, the owner and the recursion depth. a zeroed mutex is unlocked.
 */
typedef struct MUTEX {
    int type;
    int pad;
    void *unused;
    SRWLOCK lock;
    BYTE reserved[48];
    LONG owner;
    int count;
} MUTEX;

#define MTX_RECURSIVE 0x100

DECLSPEC_EXPORT void __cdecl _Mtx_init_in_situ(MUTEX *mutex, int type)
{
    memset(mutex, 0, sizeof(*mutex));
    mutex->type = type;
    InitializeSRWLock(&mutex->lock);
}

DECLSPEC_EXPORT void __cdecl _Mtx_destroy_in_situ(MUTEX *mutex) {}

DECLSPEC_EXPORT int __cdecl _Mtx_lock(MUTEX *mutex)
{
    LONG self = (LONG)GetCurrentThreadId();
    if ((mutex->type & MTX_RECURSIVE) && mutex->owner == self) {
        mutex->count++;
        return 0;
    }
    AcquireSRWLockExclusive(&mutex->lock);
    mutex->owner = self;
    mutex->count = 1;
    return 0;
}

DECLSPEC_EXPORT int __cdecl _Mtx_unlock(MUTEX *mutex)
{
    if (--mutex->count)
        return 0;
    mutex->owner = 0;
    ReleaseSRWLockExclusive(&mutex->lock);
    return 0;
}

/* library errors end the process like an unhandled exception would. */
void __cdecl throw_cpp_error(int code)
{
    OutputDebugStringA("msvcp_win: std::system_error thrown\n");
    TerminateProcess(GetCurrentProcess(), 3);
}

void __cdecl xlength_error(const char *message)
{
    OutputDebugStringA("msvcp_win: std::length_error thrown\n");
    TerminateProcess(GetCurrentProcess(), 3);
}

/* one thread, so the parallel algorithms take their serial path and never ask for work items. */
DECLSPEC_EXPORT DWORD __stdcall __std_parallel_algorithms_hw_threads(void) { return 1; }

DECLSPEC_EXPORT void *__stdcall __std_create_threadpool_work(void *callback, void *context, void *environment)
{
    return nullptr;
}

DECLSPEC_EXPORT void __stdcall __std_submit_threadpool_work(void *work) {}

DECLSPEC_EXPORT void __stdcall __std_bulk_submit_threadpool_work(void *work, SIZE_T count) {}

DECLSPEC_EXPORT void __stdcall __std_close_threadpool_work(void *work) {}

DECLSPEC_EXPORT void __stdcall __std_wait_for_threadpool_work_callbacks(void *work, BOOL cancel) {}
