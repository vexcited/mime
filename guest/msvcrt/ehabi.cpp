/*
 * the c++ exception helpers the universal crt exports for vcruntime and for code built with its headers: frame
 * information of catch blocks, the throw and image bases of the 64-bit tables, the catch object builders and the
 * checks of the exception records. they read the records as windows lays them out.
 */
#include "cxx.h"

#define CXX_EXCEPTION 0xE06D7363
#define CLR_EXCEPTION 0xE0434352
#define CLR_EXCEPTION_EXCEPTION 0xE0434F4D
#define CXX_MAGIC1 0x19930520
#define CXX_MAGIC3 0x19930522
#define FAST_FAIL_STACK_COOKIE_CHECK_FAILURE 2
#define PF_FASTFAIL_AVAILABLE 23
#define STATUS_STACK_BUFFER_OVERRUN_CODE 0xC0000409

#ifdef _WIN64
#define CXX_PARAMS 4
#else
#define CXX_PARAMS 3
#endif

static_assert(sizeof(MSVCRT_EH_THREAD) <= 48 * sizeof(void *), "the thread data holds the exception state");

/* ------------------------------------------------------------------ records */

/* handler, catchable type and throw information, with rvas on 64-bit Windows and addresses on x86. */
struct HANDLER_TYPE {
    ULONG adjectives;
    int type;
    int catch_object;
    int handler;
};

struct CATCHABLE_TYPE {
    ULONG properties;
    int type;
    RTTI_PMD where;
    int size;
    int copy;
};

struct THROW_INFO_ABI {
    ULONG attributes;
    int unwind;
    int forward;
    int catchables;
};

#define HT_COMPLUS 0x80000000
#define HT_WINRT 0x80
#define HT_REFERENCE 8
#define HT_CONST 1
#define HT_VOLATILE 2
#define HT_UNALIGNED 4
#define TI_CONST 1
#define TI_VOLATILE 2
#define TI_UNALIGNED 4
#define TI_WINRT 0x10
#define CT_SIMPLE 1
#define CT_BY_REFERENCE_ONLY 2
#define CT_VIRTUAL_BASE 4
#define CT_WINRT 0x10

static bool is_cxx_record(const EXCEPTION_RECORD *record)
{
    return record && (ULONG)record->ExceptionCode == CXX_EXCEPTION && record->NumberParameters == CXX_PARAMS &&
           (ULONG)record->ExceptionInformation[0] - CXX_MAGIC1 <= CXX_MAGIC3 - CXX_MAGIC1;
}

/* the address an rva or address in a throw record refers to. */
static const void *throw_at(const EXCEPTION_RECORD *record, int field)
{
#ifdef _WIN64
    return (const void *)(record->ExceptionInformation[3] + (ULONG_PTR)(LONG_PTR)field);
#else
    return (const void *)(LONG_PTR)field;
#endif
}

static const THROW_INFO_ABI *throw_info_of(const EXCEPTION_RECORD *record)
{
    return (const THROW_INFO_ABI *)record->ExceptionInformation[2];
}

/* ------------------------------------------------------------------ frame information */

struct FRAME_INFO {
    void *object;
    FRAME_INFO *next;
};

extern "C" DECLSPEC_EXPORT FRAME_INFO *__cdecl _CreateFrameInfo(FRAME_INFO *frame, void *object)
{
    MSVCRT_EH_THREAD *thread = MsvcrtEhThread();
    frame->object = object;
#ifdef _WIN64
    /* a frame above the newest one starts a new chain. */
    frame->next = (ULONG_PTR)frame >= (ULONG_PTR)thread->frame_info ? nullptr : static_cast<FRAME_INFO *>(thread->frame_info);
#else
    frame->next = static_cast<FRAME_INFO *>(thread->frame_info);
#endif
    thread->frame_info = frame;
    return frame;
}

extern "C" DECLSPEC_EXPORT void __cdecl _FindAndUnlinkFrame(FRAME_INFO *frame)
{
    MSVCRT_EH_THREAD *thread = MsvcrtEhThread();
#ifdef _WIN64
    if (frame != thread->frame_info || !thread->frame_info)
        abort();
    thread->frame_info = frame->next;
#else
    if (frame == thread->frame_info) {
        thread->frame_info = frame->next;
        return;
    }
    FRAME_INFO **link = thread->frame_info ? &static_cast<FRAME_INFO *>(thread->frame_info)->next : nullptr;
    for (;;) {
        FRAME_INFO *found = link ? *link : nullptr;
        if (!found)
            abort();
        if (found == frame)
            break;
        link = &found->next;
    }
    *link = frame->next;
#endif
}

extern "C" DECLSPEC_EXPORT int __cdecl _IsExceptionObjectToBeDestroyed(const void *object)
{
    for (FRAME_INFO *frame = static_cast<FRAME_INFO *>(MsvcrtEhThread()->frame_info); frame; frame = frame->next)
        if (frame->object == object)
            return 0;
    return 1;
}

#ifdef _WIN64
extern "C" DECLSPEC_EXPORT ULONG_PTR __cdecl _GetImageBase(void) { return MsvcrtEhThread()->image_base; }
extern "C" DECLSPEC_EXPORT ULONG_PTR __cdecl _GetThrowImageBase(void) { return MsvcrtEhThread()->throw_image_base; }
extern "C" DECLSPEC_EXPORT void __cdecl _SetImageBase(ULONG_PTR base) { MsvcrtEhThread()->image_base = base; }
extern "C" DECLSPEC_EXPORT void __cdecl _SetThrowImageBase(ULONG_PTR base) { MsvcrtEhThread()->throw_image_base = base; }
#endif

/* ------------------------------------------------------------------ exception objects */

extern "C" DECLSPEC_EXPORT void *__cdecl __AdjustPointer(void *object, const RTTI_PMD *where)
{
    char *p = static_cast<char *>(object) + where->mdisp;
    if (where->pdisp >= 0) {
        const char *vbtable = *(const char *const *)(static_cast<char *>(object) + where->pdisp);
        p += where->pdisp + *(const int *)(vbtable + where->vdisp);
    }
    return p;
}

#ifdef __i386__
typedef void(__thiscall *destructor_fn)(void *);
typedef void(__thiscall *copy_fn)(void *, void *);
typedef void(__thiscall *copy_virtual_fn)(void *, void *, int);
#else
typedef void (*destructor_fn)(void *);
typedef void (*copy_fn)(void *, void *);
typedef void (*copy_virtual_fn)(void *, void *, int);
#endif

/* destroys the object a c++ exception record carries, or releases a WinRT one. */
extern "C" DECLSPEC_EXPORT void __cdecl __DestructExceptionObject(const EXCEPTION_RECORD *record)
{
    if (!is_cxx_record(record) || !throw_info_of(record))
        return;
    const THROW_INFO_ABI *info = throw_info_of(record);
    void *object = (void *)record->ExceptionInformation[1];
    if (info->unwind) {
        ((destructor_fn)throw_at(record, info->unwind))(object);
    } else if (info->attributes & TI_WINRT) {
        void **instance = *(void ***)object;
        if (instance)
            (*(void(__stdcall **)(void *))(*(char **)instance + 2 * sizeof(void *)))(instance);
    }
}

/* the type whose name tells a bad_alloc, compared by name as every catchable type is. */
static const struct {
    const void *vtable;
    void *cache;
    char name[24];
} g_bad_alloc = {nullptr, nullptr, ".?AVbad_alloc@std@@"};

static bool record_has_type(const EXCEPTION_RECORD *record, const char *name)
{
    const THROW_INFO_ABI *info = throw_info_of(record);
    const int *array = (const int *)throw_at(record, info->catchables);
    for (int i = 0; i < array[0]; i++) {
        const CATCHABLE_TYPE *c = (const CATCHABLE_TYPE *)throw_at(record, array[1 + i]);
        const char *candidate = (const char *)throw_at(record, c->type) + 2 * sizeof(void *);
        if (!strcmp(name, candidate))
            return true;
    }
    return false;
}

/* aborts unless the pointers carry a c++ exception, as windows does. */
extern "C" DECLSPEC_EXPORT int __cdecl _is_exception_typeof(const type_info *type, EXCEPTION_POINTERS *pointers)
{
    if (!pointers || !is_cxx_record(pointers->ExceptionRecord))
        abort();
    return record_has_type(pointers->ExceptionRecord, type->raw_name());
}

extern "C" DECLSPEC_EXPORT void *__cdecl __GetPlatformExceptionInfo(int *is_bad_alloc)
{
    *is_bad_alloc = 0;
    const EXCEPTION_RECORD *record = static_cast<const EXCEPTION_RECORD *>(MsvcrtEhThread()->current_record);
    if (!record)
        return nullptr;
    const THROW_INFO_ABI *info = throw_info_of(record);
    if (!info)
        return nullptr;
    if (info->attributes & TI_WINRT) {
        /* the information sits before the vtable of the thrown object. */
        void **vtable = **(void ****)&record->ExceptionInformation[1];
        return vtable[-1];
    }
    EXCEPTION_POINTERS pointers = {const_cast<EXCEPTION_RECORD *>(record), nullptr};
    *is_bad_alloc = _is_exception_typeof(reinterpret_cast<const type_info *>(&g_bad_alloc), &pointers);
    return nullptr;
}

/* an unwinding exception that no frame handles: managed ones count down, c++ ones end the program. */
extern "C" DECLSPEC_EXPORT int __cdecl __FrameUnwindFilter(EXCEPTION_POINTERS *pointers)
{
    MSVCRT_EH_THREAD *thread = MsvcrtEhThread();
    const EXCEPTION_RECORD *record = pointers->ExceptionRecord;
    if ((ULONG)record->ExceptionCode == CLR_EXCEPTION || (ULONG)record->ExceptionCode == CLR_EXCEPTION_EXCEPTION) {
        if (thread->throws > 0)
            thread->throws--;
    } else if ((ULONG)record->ExceptionCode == CXX_EXCEPTION) {
        thread->current_record = const_cast<EXCEPTION_RECORD *>(record);
        thread->current_context = pointers->ContextRecord;
        terminate();
    }
    return 0;
}

/* ------------------------------------------------------------------ WinRT */

typedef void *(__stdcall *winrt_oom_callback)(void);
static winrt_oom_callback g_winrt_oom;

#ifdef __i386__
extern "C" DECLSPEC_EXPORT winrt_oom_callback __cdecl _SetWinRTOutOfMemoryExceptionCallback(winrt_oom_callback callback)
{
    g_winrt_oom = callback;
    return callback;
}
#else
extern "C" DECLSPEC_EXPORT void __cdecl _SetWinRTOutOfMemoryExceptionCallback(winrt_oom_callback callback)
{
    g_winrt_oom = callback;
}
#endif

/* ------------------------------------------------------------------ catch objects */

/* where the catch object of a handler sits: its frame is the registration node plus 12 on x86. */
static void *catch_object_slot(const void *node, const HANDLER_TYPE *handler)
{
    if (handler->adjectives & HT_COMPLUS)
        return const_cast<void *>(node);
#ifdef _WIN64
    return (void *)(*(const ULONG_PTR *)node + handler->catch_object);
#else
    return (char *)node + handler->catch_object + 12;
#endif
}

static const char *handler_type_name(const HANDLER_TYPE *handler)
{
    if (!handler->type)
        return nullptr;
#ifdef _WIN64
    const char *type = (const char *)(MsvcrtEhThread()->image_base + (ULONG_PTR)(LONG_PTR)handler->type);
#else
    const char *type = (const char *)(LONG_PTR)handler->type;
#endif
    return type + 2 * sizeof(void *);
}

/*
 * copies the exception into the catch object where that takes no call: 0 when it did, 1 or 2 when the copy
 * constructor has to run, with or without its virtual base argument.
 */
extern "C" DECLSPEC_EXPORT int __cdecl __BuildCatchObjectHelper(const EXCEPTION_RECORD *record, void *node,
                                                                const HANDLER_TYPE *handler,
                                                                const CATCHABLE_TYPE *catchable)
{
    const char *name = handler_type_name(handler);
    if (!name || !name[0] || (!handler->catch_object && !(handler->adjectives & HT_COMPLUS)))
        return 0;
    void *slot = catch_object_slot(node, handler);
    void *object = (void *)record->ExceptionInformation[1];
    if ((handler->adjectives & HT_WINRT) && (catchable->properties & CT_WINRT) && g_winrt_oom) {
        void *oom = g_winrt_oom();
        if (!oom || !slot)
            abort();
        *(void **)slot = oom;
        *(void **)slot = __AdjustPointer(oom, &catchable->where);
        return 0;
    }
    if (handler->adjectives & HT_REFERENCE) {
        if (!object || !slot)
            abort();
        *(void **)slot = object;
        *(void **)slot = __AdjustPointer(object, &catchable->where);
        return 0;
    }
    if (!(catchable->properties & CT_SIMPLE)) {
        if (!object || !slot)
            abort();
        if (catchable->copy)
            return (catchable->properties & CT_VIRTUAL_BASE) ? 2 : 1;
        memmove(slot, __AdjustPointer(object, &catchable->where), catchable->size);
        return 0;
    }
    if (!object || !slot)
        abort();
    memmove(slot, object, catchable->size);
    if (catchable->size != sizeof(void *) || !*(void **)slot)
        return 0;
    *(void **)slot = __AdjustPointer(*(void **)slot, &catchable->where);
    return 0;
}

static const void *image_at(int field)
{
#ifdef _WIN64
    return (const void *)(MsvcrtEhThread()->throw_image_base + (ULONG_PTR)(LONG_PTR)field);
#else
    return (const void *)(LONG_PTR)field;
#endif
}

/* builds the catch object, running the copy constructor of a class the helper left to it. */
extern "C" DECLSPEC_EXPORT void __cdecl __BuildCatchObject(const EXCEPTION_RECORD *record, void *node,
                                                           const HANDLER_TYPE *handler,
                                                           const CATCHABLE_TYPE *catchable)
{
    void *slot = catch_object_slot(node, handler);
    int copy = __BuildCatchObjectHelper(record, node, handler, catchable);
    if (!copy)
        return;
    void *source = __AdjustPointer((void *)record->ExceptionInformation[1], &catchable->where);
    const void *constructor = catchable->copy ? image_at(catchable->copy) : nullptr;
    if (copy == 1)
        ((copy_fn)constructor)(slot, source);
    else
        ((copy_virtual_fn)constructor)(slot, source, 1);
}

/* whether a handler takes the catchable type of a throw. */
extern "C" DECLSPEC_EXPORT int __cdecl __TypeMatch(const HANDLER_TYPE *handler, const CATCHABLE_TYPE *catchable,
                                                   const THROW_INFO_ABI *info)
{
    const char *name = handler_type_name(handler);
    if (!name)
        return 1;
    if (!name[0] || ((handler->adjectives & HT_WINRT) && (catchable->properties & CT_WINRT)))
        return 1;
    const char *thrown = (const char *)image_at(catchable->type) + 2 * sizeof(void *);
    if (name != thrown && strcmp(name, thrown))
        return 0;
    ULONG adjectives = handler->adjectives;
    return (!(catchable->properties & CT_BY_REFERENCE_ONLY) || (adjectives & HT_REFERENCE)) &&
           (!(info->attributes & TI_CONST) || (adjectives & HT_CONST)) &&
#ifdef _WIN64
           (!(info->attributes & TI_UNALIGNED) || (adjectives & HT_UNALIGNED)) &&
#endif
           (!(info->attributes & TI_VOLATILE) || (adjectives & HT_VOLATILE));
}

#ifdef _WIN64
/* ------------------------------------------------------------------ 64-bit only */

typedef enum {
    ExceptionContinueExecution,
    ExceptionContinueSearch,
    ExceptionNestedException,
    ExceptionCollidedUnwind,
} EXCEPTION_DISPOSITION;

extern "C" EXCEPTION_DISPOSITION NTAPI __C_specific_handler(EXCEPTION_RECORD *record, void *frame, CONTEXT *context,
                                                           void *dispatcher);

/* the handler of functions that cannot throw: a c++ exception it finds no handler for ends the program. */
extern "C" DECLSPEC_EXPORT EXCEPTION_DISPOSITION NTAPI __C_specific_handler_noexcept(EXCEPTION_RECORD *record,
                                                                                    void *frame, CONTEXT *context,
                                                                                    void *dispatcher)
{
    EXCEPTION_DISPOSITION result = __C_specific_handler(record, frame, context, dispatcher);
    if (!(record->ExceptionFlags & EXCEPTION_UNWIND) &&
        (ULONG)record->ExceptionCode == CXX_EXCEPTION && result == ExceptionContinueSearch) {
        MSVCRT_EH_THREAD *thread = MsvcrtEhThread();
        thread->current_record = record;
        thread->current_context = context;
        terminate();
    }
    return result;
}

extern "C" DECLSPEC_EXPORT void __cdecl _local_unwind(void *frame, void *target)
{
    RtlUnwind(frame, target, nullptr, nullptr);
}

/* the non-local goto notifications debuggers look for do nothing where the unwind tables carry the frames. */
extern "C" DECLSPEC_EXPORT void __cdecl __NLG_Dispatch2(void) {}
extern "C" DECLSPEC_EXPORT void __cdecl __NLG_Return2(void) {}
#endif
