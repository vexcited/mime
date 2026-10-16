/*
 * c++ exceptions as msvc and clang compile them: _CxxThrowException
 * raises 0xE06D7363 with the object and its throw information, and the
 * frame handler of each function with try blocks or objects to destroy
 * finds a catch block for it, unwinds to it and runs it.
 *
 * a function's description comes in three forms, decoded here into one:
 * the FuncInfo of x86, whose fields are addresses and whose state is a
 * variable of the frame; the FuncInfo of x64 and arm64 (__CxxFrameHandler3),
 * whose fields are rvas, its state looked up from the instruction pointer;
 * and the compressed one of __CxxFrameHandler4.
 *
 * on 64-bit windows catch blocks and destructors are funclets, called
 * with the frame of the function they belong to; a catch runs from
 * RtlUnwindEx's consolidation callback once the frames above it are
 * unwound, and returns where to continue. on x86 they are code of the
 * function itself, called with its ebp, and the handler goes on to the
 * continuation itself.
 */
#include "cxx.h"

#define CXX_EXCEPTION 0xE06D7363
#define CXX_MAGIC1 0x19930520
#define CXX_MAGIC3 0x19930522
#define CXX_PURE_MAGIC 0x01994000
#define STATUS_UNWIND_CONSOLIDATE 0x80000029
#define UNWIND_FLAGS (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)
#define EXCEPTION_TARGET_UNWIND 0x20

/* the flags of a function: synchronous exceptions only, and noexcept. */
#define FUNC_EHS 1
#define FUNC_NOEXCEPT 4

/* handler adjectives. */
#define HT_CONST 1
#define HT_VOLATILE 2
#define HT_UNALIGNED 4
#define HT_REFERENCE 8
#define HT_STD_DOTDOT 0x40

/* throw information attributes and catchable type properties. */
#define TI_CONST 1
#define TI_VOLATILE 2
#define TI_UNALIGNED 4
#define CT_SIMPLE 1
#define CT_BY_REFERENCE_ONLY 2
#define CT_VIRTUAL_BASE 4

typedef enum {
    ExceptionContinueExecution,
    ExceptionContinueSearch,
    ExceptionNestedException,
    ExceptionCollidedUnwind,
} EXCEPTION_DISPOSITION;

#ifdef _WIN64
#define CXX_PARAMS 4
#else
#define CXX_PARAMS 3
#endif

/* ------------------------------------------------------------------ throw information */

struct THROW_INFO {
    ULONG attributes;
    int unwind;
    int forward;
    int catchables;
};

struct CATCHABLE_ARRAY {
    int count;
    int types[1];
};

struct CATCHABLE {
    ULONG properties;
    int type;
    RTTI_PMD where;
    int size;
    int copy;
};

/* what the record of a c++ exception names: the object, its throw information, and the image of the rvas. */
struct CXX_THROW {
    void *object;
    const THROW_INFO *info;
    ULONG_PTR base;
};

static bool cxx_throw(const EXCEPTION_RECORD *record, CXX_THROW *out)
{
    if ((ULONG)record->ExceptionCode != CXX_EXCEPTION || record->NumberParameters < CXX_PARAMS - 1)
        return false;
    ULONG_PTR magic = record->ExceptionInformation[0];
    if (magic != CXX_MAGIC1 && magic != CXX_MAGIC1 + 1 && magic != CXX_MAGIC3 && magic != CXX_PURE_MAGIC)
        return false;
    out->object = (void *)record->ExceptionInformation[1];
    out->info = (const THROW_INFO *)record->ExceptionInformation[2];
#ifdef _WIN64
    out->base = record->NumberParameters >= 4 ? record->ExceptionInformation[3] : 0;
#else
    out->base = 0;
#endif
    return out->info != nullptr;
}

#define AT(base, field) ((const void *)((base) + (ULONG_PTR)(LONG_PTR)(field)))

static const CATCHABLE *catchable_at(const CXX_THROW *t, int i)
{
    const CATCHABLE_ARRAY *array = static_cast<const CATCHABLE_ARRAY *>(AT(t->base, t->info->catchables));
    return static_cast<const CATCHABLE *>(AT(t->base, array->types[i]));
}

static int catchable_count(const CXX_THROW *t)
{
    return static_cast<const CATCHABLE_ARRAY *>(AT(t->base, t->info->catchables))->count;
}

/* the subobject of a base class within the object thrown. */
static void *adjust_pointer(void *object, const RTTI_PMD &where)
{
    char *p = static_cast<char *>(object) + where.mdisp;
    if (where.pdisp >= 0) {
        const char *vbtable = *(const char *const *)(static_cast<char *>(object) + where.pdisp);
        p += *(const int *)(vbtable + where.vdisp) + where.pdisp;
    }
    return p;
}

#ifdef __i386__
typedef void(__thiscall *destructor_t)(void *);
typedef void(__thiscall *copy_t)(void *, void *);
typedef void(__thiscall *copy_virtual_t)(void *, void *, int);
#else
typedef void (*destructor_t)(void *);
typedef void (*copy_t)(void *, void *);
typedef void (*copy_virtual_t)(void *, void *, int);
#endif

static void destroy_object(const CXX_THROW *t)
{
    if (t->info->unwind && t->object)
        ((destructor_t)AT(t->base, t->info->unwind))(t->object);
}

/* ------------------------------------------------------------------ functions, decoded */

struct EH_HANDLER {
    ULONG adjectives;
    const type_info *type;
    int catch_object;
    ULONG_PTR code;
    int frame;
    /* __CxxFrameHandler4: where the function goes on, which catch funclets return the index of. */
    int continuations;
    ULONG_PTR continuation[2];
};

struct EH_TRY {
    int low, high, catch_high;
    int count;
    EH_HANDLER *handlers;
};

/* a state's action: a funclet, or, from __CxxFrameHandler4, a destructor of the object at or through an offset. */
enum { ACTION_FUNCLET, ACTION_OBJECT, ACTION_POINTER };

struct EH_UNWIND {
    int to_state;
    ULONG_PTR action;
    int kind;
    ULONG object;
};

struct EH_IP {
    ULONG_PTR ip;
    int state;
};

struct EH_FUNCTION {
    const void *key;
    int max_state;
    EH_UNWIND *unwind;
    int tries;
    EH_TRY *try_blocks;
    int ips;
    EH_IP *ip_map;
    ULONG flags;
    /* a catch funclet's own description, of __CxxFrameHandler4: where its frame keeps its function's. */
    bool is_catch;
    ULONG catch_frame;
};

/* a function's decoded description, kept for its next exception: they are read only and few. */
static SRWLOCK g_cache_lock = SRWLOCK_INIT;
static EH_FUNCTION **g_cache;
static ULONG g_cache_count, g_cache_capacity;

static EH_FUNCTION *cached(const void *key)
{
    EH_FUNCTION *found = nullptr;
    AcquireSRWLockShared(&g_cache_lock);
    if (g_cache_capacity) {
        for (ULONG i = ((ULONG_PTR)key >> 3) & (g_cache_capacity - 1);; i = (i + 1) & (g_cache_capacity - 1)) {
            if (!g_cache[i])
                break;
            if (g_cache[i]->key == key) {
                found = g_cache[i];
                break;
            }
        }
    }
    ReleaseSRWLockShared(&g_cache_lock);
    return found;
}

static EH_FUNCTION *remember(EH_FUNCTION *f)
{
    AcquireSRWLockExclusive(&g_cache_lock);
    if ((g_cache_count + 1) * 2 > g_cache_capacity) {
        ULONG capacity = g_cache_capacity ? g_cache_capacity * 2 : 256;
        EH_FUNCTION **table = static_cast<EH_FUNCTION **>(malloc(capacity * sizeof(*table)));
        if (table) {
            memset(table, 0, capacity * sizeof(*table));
            for (ULONG i = 0; i < g_cache_capacity; i++) {
                if (!g_cache[i])
                    continue;
                ULONG j = ((ULONG_PTR)g_cache[i]->key >> 3) & (capacity - 1);
                while (table[j])
                    j = (j + 1) & (capacity - 1);
                table[j] = g_cache[i];
            }
            free(g_cache);
            g_cache = table;
            g_cache_capacity = capacity;
        }
    }
    if ((g_cache_count + 1) * 2 <= g_cache_capacity) {
        ULONG i = ((ULONG_PTR)f->key >> 3) & (g_cache_capacity - 1);
        while (g_cache[i] && g_cache[i]->key != f->key)
            i = (i + 1) & (g_cache_capacity - 1);
        if (!g_cache[i]) {
            g_cache[i] = f;
            g_cache_count++;
        }
    }
    ReleaseSRWLockExclusive(&g_cache_lock);
    return f;
}

static void *allocate(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (!p)
        abort();
    memset(p, 0, size ? size : 1);
    return p;
}

/* the FuncInfo of __CxxFrameHandler3 and before; base is 0 on x86, where its fields are addresses. */
struct FUNC_INFO3 {
    ULONG magic;
    int max_state;
    int unwind_map;
    ULONG tries;
    int try_map;
    ULONG ips;
    int ip_map;
#ifdef _WIN64
    int unwind_help;
#endif
    int es_types;
    int flags;
};

struct HANDLER3 {
    ULONG adjectives;
    int type;
    int catch_object;
    int code;
#ifdef _WIN64
    int frame;
#endif
};

static EH_FUNCTION *decode3(const FUNC_INFO3 *info, ULONG_PTR base)
{
    EH_FUNCTION *f = cached(info);
    if (f)
        return f;
    f = static_cast<EH_FUNCTION *>(allocate(sizeof(*f)));
    f->key = info;
    f->max_state = info->max_state;
    f->unwind = static_cast<EH_UNWIND *>(allocate(f->max_state * sizeof(EH_UNWIND)));
    const int *unwind = static_cast<const int *>(AT(base, info->unwind_map));
    for (int i = 0; i < f->max_state; i++) {
        f->unwind[i].to_state = unwind[2 * i];
        f->unwind[i].action = unwind[2 * i + 1] ? (ULONG_PTR)AT(base, unwind[2 * i + 1]) : 0;
    }
    f->tries = info->tries;
    f->try_blocks = static_cast<EH_TRY *>(allocate(f->tries * sizeof(EH_TRY)));
    const int *tries = static_cast<const int *>(AT(base, info->try_map));
    for (int i = 0; i < f->tries; i++) {
        const int *t = tries + 5 * i;
        EH_TRY *out = &f->try_blocks[i];
        out->low = t[0];
        out->high = t[1];
        out->catch_high = t[2];
        out->count = t[3];
        out->handlers = static_cast<EH_HANDLER *>(allocate(out->count * sizeof(EH_HANDLER)));
        const HANDLER3 *h = static_cast<const HANDLER3 *>(AT(base, t[4]));
        for (int j = 0; j < out->count; j++) {
            out->handlers[j].adjectives = h[j].adjectives;
            out->handlers[j].type = h[j].type ? static_cast<const type_info *>(AT(base, h[j].type)) : nullptr;
            out->handlers[j].catch_object = h[j].catch_object;
            out->handlers[j].code = (ULONG_PTR)AT(base, h[j].code);
#ifdef _WIN64
            out->handlers[j].frame = h[j].frame;
#endif
        }
    }
#ifdef _WIN64
    f->ips = info->ips;
    f->ip_map = static_cast<EH_IP *>(allocate(f->ips * sizeof(EH_IP)));
    const int *ips = static_cast<const int *>(AT(base, info->ip_map));
    for (int i = 0; i < f->ips; i++) {
        f->ip_map[i].ip = (ULONG_PTR)AT(base, ips[2 * i]);
        f->ip_map[i].state = ips[2 * i + 1];
    }
#endif
    f->flags = (info->magic & 0x1FFFFFFF) >= CXX_MAGIC3 ? info->flags : 0;
    return remember(f);
}

/* ------------------------------------------------------------------ matching */

static bool same_type(const type_info *a, const type_info *b)
{
    return a == b || !strcmp(a->raw_name(), b->raw_name());
}

static bool catch_all(const EH_HANDLER *h) { return !h->type || !h->type->raw_name()[0]; }

static bool type_match(const EH_HANDLER *h, const CATCHABLE *c, const CXX_THROW *t)
{
    if (catch_all(h))
        return true;
    if (!same_type(h->type, static_cast<const type_info *>(AT(t->base, c->type))))
        return false;
    if ((c->properties & CT_BY_REFERENCE_ONLY) && !(h->adjectives & HT_REFERENCE))
        return false;
    if ((t->info->attributes & TI_CONST) && !(h->adjectives & HT_CONST))
        return false;
    if ((t->info->attributes & TI_VOLATILE) && !(h->adjectives & HT_VOLATILE))
        return false;
    if ((t->info->attributes & TI_UNALIGNED) && !(h->adjectives & HT_UNALIGNED))
        return false;
    return true;
}

/* the catchable type of the exception a handler takes, or null. */
static const CATCHABLE *handler_match(const EH_HANDLER *h, const CXX_THROW *t)
{
    for (int i = 0; i < catchable_count(t); i++) {
        const CATCHABLE *c = catchable_at(t, i);
        if (type_match(h, c, t))
            return c;
    }
    return nullptr;
}

extern "C" int MsvcrtIsExceptionTypeof(const type_info *type, const EXCEPTION_RECORD *record)
{
    CXX_THROW t;
    if (!cxx_throw(record, &t))
        return 0;
    for (int i = 0; i < catchable_count(&t); i++)
        if (same_type(type, static_cast<const type_info *>(AT(t.base, catchable_at(&t, i)->type))))
            return 1;
    return 0;
}

/* the catch object: a reference to the object, a copy of it or of a pointer, at frame + offset. */
static void build_catch_object(const EH_HANDLER *h, const CATCHABLE *c, const CXX_THROW *t, ULONG_PTR frame)
{
    if (!c || catch_all(h) || !h->catch_object)
        return;
    void *dest = (void *)(frame + h->catch_object);
    if (h->adjectives & HT_REFERENCE) {
        *(void **)dest = adjust_pointer(t->object, c->where);
    } else if (c->properties & CT_SIMPLE) {
        memmove(dest, t->object, c->size);
        if (c->size == sizeof(void *) && *(void **)dest)
            *(void **)dest = adjust_pointer(*(void **)dest, c->where);
    } else if (c->copy) {
        void *source = adjust_pointer(t->object, c->where);
        if (c->properties & CT_VIRTUAL_BASE)
            ((copy_virtual_t)AT(t->base, c->copy))(dest, source, 1);
        else
            ((copy_t)AT(t->base, c->copy))(dest, source);
    } else {
        memmove(dest, adjust_pointer(t->object, c->where), c->size);
    }
}

/* ------------------------------------------------------------------ catch blocks in progress */

struct MSVCRT_CATCH {
    MSVCRT_CATCH *next;
    CXX_THROW thrown;
    EXCEPTION_RECORD record;
    CONTEXT *context;
    /* x86: the frame and try block of the catch, and the registration around it. */
    const void *frame;
    const void *try_block;
    void *registration;
};

static void push_catch(MSVCRT_CATCH *c, const EXCEPTION_RECORD *record, const CXX_THROW *t, CONTEXT *context)
{
    MSVCRT_EH_THREAD *thread = MsvcrtEhThread();
    memset(c, 0, sizeof(*c));
    if (t)
        c->thrown = *t;
    memcpy(&c->record, record, sizeof(c->record));
    c->context = context;
    c->next = thread->catches;
    thread->catches = c;
    thread->current_record = &c->record;
    thread->current_context = c->context;
    if (thread->throws > 0)
        thread->throws--;
    thread->caught_object = nullptr;
}

/*
 * a catch block ends: its exception object is destroyed unless an outer
 * catch block still has it, or the exception it was rethrown as is being
 * caught.
 */
static void pop_catch(MSVCRT_CATCH *c)
{
    MSVCRT_EH_THREAD *thread = MsvcrtEhThread();
    for (MSVCRT_CATCH **link = &thread->catches; *link; link = &(*link)->next)
        if (*link == c) {
            *link = c->next;
            break;
        }
    thread->current_record = thread->catches ? &thread->catches->record : nullptr;
    thread->current_context = thread->catches ? thread->catches->context : nullptr;
    if (!c->thrown.object || c->thrown.object == thread->caught_object)
        return;
    for (MSVCRT_CATCH *other = thread->catches; other; other = other->next)
        if (other->thrown.object == c->thrown.object)
            return;
    destroy_object(&c->thrown);
}

/* ------------------------------------------------------------------ throwing */

extern "C" __declspec(noreturn) void __stdcall _CxxThrowException(void *object, const THROW_INFO *info)
{
    MSVCRT_EH_THREAD *thread = MsvcrtEhThread();
    if (!info) {
        /* throw; rethrows the exception of the catch block running. */
        MSVCRT_CATCH *c = thread->catches;
        if (!c)
            terminate();
        thread->throws++;
        RaiseException(c->record.ExceptionCode, c->record.ExceptionFlags, c->record.NumberParameters,
                       c->record.ExceptionInformation);
        terminate();
    }
    ULONG_PTR params[CXX_PARAMS];
    params[0] = (info->attributes & 8) ? CXX_PURE_MAGIC : CXX_MAGIC1;
    params[1] = (ULONG_PTR)object;
    params[2] = (ULONG_PTR)info;
#ifdef _WIN64
    void *base = nullptr;
    RtlPcToFileHeader((void *)info, &base);
    params[3] = (ULONG_PTR)base;
#endif
    thread->throws++;
    RaiseException(CXX_EXCEPTION, EXCEPTION_NONCONTINUABLE, CXX_PARAMS, params);
    terminate();
}

/* ------------------------------------------------------------------ unwinding a frame */

static void call_action(ULONG_PTR action, ULONG_PTR frame);

/* the destructors of the states from current down to target, each state left before its action runs. */
static int unwind_to(const EH_FUNCTION *f, ULONG_PTR frame, int current, int target)
{
    while (current > target && current < f->max_state) {
        const EH_UNWIND *u = &f->unwind[current];
        current = u->to_state;
        if (!u->action)
            continue;
        if (u->kind == ACTION_OBJECT)
            ((destructor_t)u->action)((void *)(frame + u->object));
        else if (u->kind == ACTION_POINTER)
            ((destructor_t)u->action)(*(void **)(frame + u->object));
        else
            call_action(u->action, frame);
    }
    return current;
}

#ifdef _WIN64
static int state_from_ip(const EH_FUNCTION *f, ULONG_PTR ip)
{
    int i = 0;
    while (i < f->ips && f->ip_map[i].ip <= ip)
        i++;
    return i ? f->ip_map[i - 1].state : -1;
}
#endif

/* exceptions the function lets through: noexcept ones end the program. */
static void escaped(const EH_FUNCTION *f)
{
    if (f->flags & FUNC_NOEXCEPT)
        terminate();
}

typedef void(__cdecl *translator_t)(unsigned, EXCEPTION_POINTERS *);

/* whether a try block of the function covers a state, and so may catch. */
static bool covered(const EH_FUNCTION *f, int state)
{
    for (int i = 0; i < f->tries; i++)
        if (f->try_blocks[i].low <= state && state <= f->try_blocks[i].high)
            return true;
    return false;
}

#ifdef _WIN64

/* ================================================================== 64-bit windows */

typedef struct _DISPATCHER_CONTEXT {
    ULONG_PTR ControlPc;
    ULONG_PTR ImageBase;
    void *FunctionEntry;
    ULONG_PTR EstablisherFrame;
    ULONG_PTR TargetIp;
    CONTEXT *ContextRecord;
    void *LanguageHandler;
    void *HandlerData;
    void *HistoryTable;
    ULONG ScopeIndex;
    ULONG Fill0;
} DISPATCHER_CONTEXT;


typedef ULONG_PTR (*funclet_t)(ULONG_PTR, ULONG_PTR);

static void call_action(ULONG_PTR action, ULONG_PTR frame) { ((funclet_t)action)(0, frame); }

/*
 * the state of a function while a catch block of it runs, kept per
 * thread: the frame's own instruction pointer is still the throw's, in
 * the try block the catch has unwound.
 */
static bool kept_state(ULONG_PTR frame, int *state)
{
    MSVCRT_EH_THREAD *t = MsvcrtEhThread();
    for (int i = t->kept_count - 1; i >= 0; i--)
        if (t->kept[i][0] == frame) {
            *state = (int)(LONG_PTR)t->kept[i][1];
            return true;
        }
    return false;
}

static void forget_state(ULONG_PTR frame)
{
    MSVCRT_EH_THREAD *t = MsvcrtEhThread();
    for (int i = t->kept_count - 1; i >= 0; i--)
        if (t->kept[i][0] == frame) {
            t->kept[i][0] = t->kept[--t->kept_count][0];
            t->kept[i][1] = t->kept[t->kept_count][1];
        }
}

static void keep_state(ULONG_PTR frame, int state)
{
    MSVCRT_EH_THREAD *t = MsvcrtEhThread();
    forget_state(frame);
    if (t->kept_count < 16) {
        t->kept[t->kept_count][0] = frame;
        t->kept[t->kept_count][1] = (ULONG_PTR)(LONG_PTR)state;
        t->kept_count++;
    }
}

/* where a frame's function is: a catch funclet's frame keeps the frame of the function it belongs to. */
struct EH_WHERE {
    ULONG_PTR frame;
    const EH_TRY *catch_try;
    int state;
};

static void locate(const EH_FUNCTION *f, ULONG_PTR establisher, const DISPATCHER_CONTEXT *dc, EH_WHERE *w)
{
    w->frame = establisher;
    w->catch_try = nullptr;
    w->state = state_from_ip(f, dc->ControlPc);
    if (f->is_catch) {
        /* __CxxFrameHandler4 describes the funclet itself: the innermost catch its state is in. */
        w->frame = *(const ULONG_PTR *)(establisher + f->catch_frame);
        for (int i = 0; i < f->tries; i++) {
            const EH_TRY *t = &f->try_blocks[i];
            if (w->state > t->high && w->state <= t->catch_high && (!w->catch_try || t->low > w->catch_try->low))
                w->catch_try = t;
        }
        if (w->catch_try)
            return;
        w->frame = establisher;
    }
    for (int i = f->tries - 1; i >= 0; i--) {
        const EH_TRY *t = &f->try_blocks[i];
        if (w->state <= t->high || w->state > t->catch_high)
            continue;
        for (int j = 0; j < t->count; j++) {
            ULONG_PTR base;
            if (RtlLookupFunctionEntry(t->handlers[j].code, &base, nullptr) != dc->FunctionEntry)
                continue;
            w->frame = *(const ULONG_PTR *)(establisher + t->handlers[j].frame);
            w->catch_try = t;
            return;
        }
    }
    int kept;
    if (kept_state(establisher, &kept))
        w->state = kept;
}

/* the parameters of the consolidation record a catch unwinds with. */
enum {
    CATCH_CALLBACK,
    CATCH_FRAME,
    CATCH_CODE,
    CATCH_STATE,
    CATCH_RECORD,
    CATCH_FUNCTION,
    CATCH_CONTEXT,
    /* the try block is in a catch funclet: the funclet goes on, the function keeps its state. */
    CATCH_IN_FUNCLET,
    CATCH_HANDLER,
    CATCH_PARAMS
};

/* a catch block in progress whose frame an unwind leaves, as a new exception goes past it. */
struct CATCH_GUARD {
    MSVCRT_CATCH *c;
    bool done;
    ~CATCH_GUARD()
    {
        if (!done)
            pop_catch(c);
    }
};

/*
 * the consolidation callback: runs the catch funclet, with the frames
 * above its function unwound, and returns where the function goes on.
 */
static void *__stdcall call_catch(EXCEPTION_RECORD *consolidate)
{
    ULONG_PTR *p = consolidate->ExceptionInformation;
    const EXCEPTION_RECORD *record = (const EXCEPTION_RECORD *)p[CATCH_RECORD];
    const EH_HANDLER *h = (const EH_HANDLER *)p[CATCH_HANDLER];
    ULONG_PTR frame = p[CATCH_FRAME];
    CXX_THROW t;
    bool cxx = cxx_throw(record, &t);
    MSVCRT_CATCH c;
    push_catch(&c, record, cxx ? &t : nullptr, (CONTEXT *)p[CATCH_CONTEXT]);
    CATCH_GUARD guard = {&c, false};
    ULONG_PTR continuation = ((funclet_t)p[CATCH_CODE])(0, frame);
    guard.done = true;
    pop_catch(&c);
    if (!p[CATCH_IN_FUNCLET])
        forget_state(frame);
    if (continuation < (ULONG_PTR)h->continuations)
        continuation = h->continuation[continuation];
    return (void *)continuation;
}

static __declspec(noreturn) void catch_it(EXCEPTION_RECORD *record, ULONG_PTR establisher, CONTEXT *context,
                                          DISPATCHER_CONTEXT *dc, const EH_FUNCTION *f, const EH_WHERE *w,
                                          const EH_TRY *t, const EH_HANDLER *h, const CATCHABLE *c,
                                          const CXX_THROW *thrown)
{
    if (thrown)
        build_catch_object(h, c, thrown, w->frame);
    MsvcrtEhThread()->caught_object = thrown ? thrown->object : nullptr;
    EXCEPTION_RECORD consolidate;
    memset(&consolidate, 0, sizeof(consolidate));
    consolidate.ExceptionCode = STATUS_UNWIND_CONSOLIDATE;
    consolidate.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    consolidate.NumberParameters = CATCH_PARAMS;
    ULONG_PTR *p = consolidate.ExceptionInformation;
    p[CATCH_CALLBACK] = (ULONG_PTR)call_catch;
    p[CATCH_FRAME] = w->frame;
    p[CATCH_CODE] = h->code;
    p[CATCH_STATE] = (ULONG_PTR)(LONG_PTR)(t->low - 1);
    p[CATCH_RECORD] = (ULONG_PTR)record;
    p[CATCH_FUNCTION] = (ULONG_PTR)f;
    p[CATCH_CONTEXT] = (ULONG_PTR)context;
    p[CATCH_HANDLER] = (ULONG_PTR)h;
    /* a try block within the catch funclet running goes on in that funclet, else in its function. */
    bool in_funclet = w->catch_try && t->low > w->catch_try->high && t->high <= w->catch_try->catch_high;
    p[CATCH_IN_FUNCLET] = in_funclet;
    RtlUnwindEx((void *)(in_funclet ? establisher : w->frame), (void *)dc->ControlPc, &consolidate, nullptr, context,
                dc->HistoryTable);
    terminate();
}

static EXCEPTION_DISPOSITION frame_handler(EXCEPTION_RECORD *record, ULONG_PTR establisher, CONTEXT *context,
                                           DISPATCHER_CONTEXT *dc, const EH_FUNCTION *f)
{
    EH_WHERE w;
    locate(f, establisher, dc, &w);
    if (record->ExceptionFlags & UNWIND_FLAGS) {
        bool target = (record->ExceptionFlags & EXCEPTION_TARGET_UNWIND) &&
                      (ULONG)record->ExceptionCode == STATUS_UNWIND_CONSOLIDATE &&
                      record->ExceptionInformation[CATCH_CALLBACK] == (ULONG_PTR)call_catch;
        if (target) {
            int state = (int)(LONG_PTR)record->ExceptionInformation[CATCH_STATE];
            unwind_to(f, w.frame, w.state, state);
            if (!record->ExceptionInformation[CATCH_IN_FUNCLET])
                keep_state(w.frame, state);
        } else if (w.catch_try) {
            unwind_to(f, w.frame, w.state, w.catch_try->low - 1);
        } else {
            unwind_to(f, w.frame, w.state, -1);
            forget_state(w.frame);
        }
        return ExceptionContinueSearch;
    }
    CXX_THROW thrown;
    bool cxx = cxx_throw(record, &thrown);
    if (!cxx) {
        MSVCRT_EH_THREAD *thread = MsvcrtEhThread();
        if (thread->translator && covered(f, w.state)) {
            /* the translator throws a c++ exception of its own, which this dispatch then finds. */
            EXCEPTION_POINTERS pointers = {record, context};
            ((translator_t)thread->translator)(record->ExceptionCode, &pointers);
        }
    }
    for (int i = 0; i < f->tries; i++) {
        const EH_TRY *t = &f->try_blocks[i];
        if (w.state < t->low || w.state > t->high)
            continue;
        for (int j = 0; j < t->count; j++) {
            const EH_HANDLER *h = &t->handlers[j];
            if (cxx) {
                const CATCHABLE *c = handler_match(h, &thrown);
                if (c)
                    catch_it(record, establisher, context, dc, f, &w, t, h, c, &thrown);
            } else if (catch_all(h) && !(f->flags & FUNC_EHS)) {
                catch_it(record, establisher, context, dc, f, &w, t, h, nullptr, nullptr);
            }
        }
    }
    escaped(f);
    return ExceptionContinueSearch;
}

extern "C" EXCEPTION_DISPOSITION __cdecl __CxxFrameHandler3(EXCEPTION_RECORD *record, ULONG_PTR establisher,
                                                            CONTEXT *context, DISPATCHER_CONTEXT *dc)
{
    const FUNC_INFO3 *info = static_cast<const FUNC_INFO3 *>(AT(dc->ImageBase, *(const ULONG *)dc->HandlerData));
    return frame_handler(record, establisher, context, dc, decode3(info, dc->ImageBase));
}

/* ------------------------------------------------------------------ __CxxFrameHandler4 */

/* the compressed integers: the low bits say the length, 1 to 4 bytes, or 5 with the value in the last four. */
struct FH4_READER {
    const BYTE *p;
    ULONG unsigned_value()
    {
        const BYTE *b = p;
        ULONG v;
        if (!(b[0] & 1)) {
            v = b[0] >> 1;
            p += 1;
        } else if (!(b[0] & 2)) {
            v = (b[0] | b[1] << 8) >> 2;
            p += 2;
        } else if (!(b[0] & 4)) {
            v = (b[0] | b[1] << 8 | b[2] << 16) >> 3;
            p += 3;
        } else if (!(b[0] & 8)) {
            v = (b[0] | b[1] << 8 | b[2] << 16 | (ULONG)b[3] << 24) >> 4;
            p += 4;
        } else {
            v = b[1] | b[2] << 8 | b[3] << 16 | (ULONG)b[4] << 24;
            p += 5;
        }
        return v;
    }
    int int32()
    {
        int v;
        memcpy(&v, p, 4);
        p += 4;
        return v;
    }
    BYTE byte() { return *p++; }
};

#define FH4_CATCH 0x01
#define FH4_SEPARATED 0x02
#define FH4_BBT 0x04
#define FH4_UNWIND_MAP 0x08
#define FH4_TRY_MAP 0x10
#define FH4_EHS 0x20
#define FH4_NOEXCEPT 0x40

/* the first instruction of the function a frame is in, its chained unwind information followed back. */
static ULONG_PTR function_start(const DISPATCHER_CONTEXT *dc)
{
    const ULONG *entry = static_cast<const ULONG *>(dc->FunctionEntry);
#ifdef __x86_64__
    for (;;) {
        const BYTE *unwind = (const BYTE *)(dc->ImageBase + entry[2]);
        if (!((unwind[0] >> 3) & 4))
            break;
        entry = (const ULONG *)(unwind + 4 + ((unwind[2] + 1) & ~1) * 2);
    }
#endif
    return dc->ImageBase + entry[0];
}

static void decode_ip_map4(EH_IP *out, int *count, ULONG_PTR base, int rva, ULONG_PTR start)
{
    FH4_READER r = {(const BYTE *)AT(base, rva)};
    ULONG n = r.unsigned_value();
    ULONG_PTR ip = start;
    for (ULONG i = 0; i < n; i++) {
        ip += r.unsigned_value();
        out[*count].ip = ip;
        out[*count].state = (int)r.unsigned_value() - 1;
        (*count)++;
    }
}

static ULONG ip_map4_count(ULONG_PTR base, int rva)
{
    FH4_READER r = {(const BYTE *)AT(base, rva)};
    return r.unsigned_value();
}

static EH_FUNCTION *decode4(const BYTE *info, const DISPATCHER_CONTEXT *dc)
{
    EH_FUNCTION *f = cached(info);
    if (f)
        return f;
    ULONG_PTR base = dc->ImageBase;
    f = static_cast<EH_FUNCTION *>(allocate(sizeof(*f)));
    f->key = info;
    FH4_READER r = {info};
    BYTE header = r.byte();
    if (header & FH4_BBT)
        r.unsigned_value();
    int unwind_map = header & FH4_UNWIND_MAP ? r.int32() : 0;
    int try_map = header & FH4_TRY_MAP ? r.int32() : 0;
    int ip_map = r.int32();
    if (header & FH4_CATCH) {
        f->is_catch = true;
        f->catch_frame = r.unsigned_value();
    }
    f->flags = (header & FH4_EHS ? FUNC_EHS : 0) | (header & FH4_NOEXCEPT ? FUNC_NOEXCEPT : 0);
    ULONG_PTR start = function_start(dc);

    /* the unwind map: entries in state order, each linked back to the one it unwinds to, or to the count for none. */
    if (unwind_map) {
        FH4_READER u = {(const BYTE *)AT(base, unwind_map)};
        const BYTE *map_start = u.p;
        f->max_state = u.unsigned_value();
        f->unwind = static_cast<EH_UNWIND *>(allocate(f->max_state * sizeof(EH_UNWIND)));
        const BYTE **offsets = static_cast<const BYTE **>(allocate(f->max_state * sizeof(*offsets)));
        for (int i = 0; i < f->max_state; i++) {
            offsets[i] = u.p;
            ULONG v = u.unsigned_value();
            ULONG type = v & 3, next = v >> 2;
            EH_UNWIND *e = &f->unwind[i];
            e->to_state = -1;
            if (next && offsets[i] - next != map_start)
                for (int j = 0; j < i; j++)
                    if (offsets[j] == offsets[i] - next)
                        e->to_state = j;
            if (type == 1 || type == 2) {
                e->action = (ULONG_PTR)AT(base, u.int32());
                e->object = u.unsigned_value();
                e->kind = type == 1 ? ACTION_OBJECT : ACTION_POINTER;
            } else if (type == 3) {
                e->action = (ULONG_PTR)AT(base, u.int32());
                e->kind = ACTION_FUNCLET;
            }
        }
        free(offsets);
    }

    if (try_map) {
        FH4_READER t = {(const BYTE *)AT(base, try_map)};
        f->tries = t.unsigned_value();
        f->try_blocks = static_cast<EH_TRY *>(allocate(f->tries * sizeof(EH_TRY)));
        for (int i = 0; i < f->tries; i++) {
            EH_TRY *out = &f->try_blocks[i];
            out->low = t.unsigned_value();
            out->high = t.unsigned_value();
            out->catch_high = t.unsigned_value();
            FH4_READER h = {(const BYTE *)AT(base, t.int32())};
            out->count = h.unsigned_value();
            out->handlers = static_cast<EH_HANDLER *>(allocate(out->count * sizeof(EH_HANDLER)));
            for (int j = 0; j < out->count; j++) {
                EH_HANDLER *x = &out->handlers[j];
                BYTE flags = h.byte();
                if (flags & 1)
                    x->adjectives = h.unsigned_value();
                if (flags & 2)
                    x->type = static_cast<const type_info *>(AT(base, h.int32()));
                if (flags & 4)
                    x->catch_object = h.unsigned_value();
                x->code = (ULONG_PTR)AT(base, h.int32());
                x->continuations = (flags >> 4) & 3;
                for (int k = 0; k < x->continuations; k++)
                    x->continuation[k] = flags & 8 ? (ULONG_PTR)AT(base, h.int32()) : start + h.unsigned_value();
            }
        }
    }

    /* the instruction pointer map: offsets from the function's start, or one map per separated part of it. */
    if (header & FH4_SEPARATED) {
        FH4_READER sep = {(const BYTE *)AT(base, ip_map)};
        ULONG parts = sep.unsigned_value();
        FH4_READER count = sep;
        ULONG total = 0;
        for (ULONG i = 0; i < parts; i++) {
            count.int32();
            total += ip_map4_count(base, count.int32());
        }
        f->ip_map = static_cast<EH_IP *>(allocate(total * sizeof(EH_IP)));
        for (ULONG i = 0; i < parts; i++) {
            int part_start = sep.int32();
            decode_ip_map4(f->ip_map, &f->ips, base, sep.int32(), (ULONG_PTR)AT(base, part_start));
        }
        /* the parts may be anywhere: their entries in address order. */
        for (int i = 1; i < f->ips; i++)
            for (int j = i; j > 0 && f->ip_map[j - 1].ip > f->ip_map[j].ip; j--) {
                EH_IP swap = f->ip_map[j];
                f->ip_map[j] = f->ip_map[j - 1];
                f->ip_map[j - 1] = swap;
            }
    } else {
        f->ip_map = static_cast<EH_IP *>(allocate(ip_map4_count(base, ip_map) * sizeof(EH_IP)));
        decode_ip_map4(f->ip_map, &f->ips, base, ip_map, start);
    }
    return remember(f);
}

extern "C" EXCEPTION_DISPOSITION __cdecl __CxxFrameHandler4(EXCEPTION_RECORD *record, ULONG_PTR establisher,
                                                            CONTEXT *context, DISPATCHER_CONTEXT *dc)
{
    const BYTE *info = static_cast<const BYTE *>(AT(dc->ImageBase, *(const ULONG *)dc->HandlerData));
    return frame_handler(record, establisher, context, dc, decode4(info, dc));
}

extern "C" EXCEPTION_DISPOSITION __cdecl __CxxFrameHandler2(EXCEPTION_RECORD *record, ULONG_PTR establisher,
                                                            CONTEXT *context, DISPATCHER_CONTEXT *dc)
{
    return __CxxFrameHandler3(record, establisher, context, dc);
}

extern "C" EXCEPTION_DISPOSITION __cdecl __CxxFrameHandler(EXCEPTION_RECORD *record, ULONG_PTR establisher,
                                                           CONTEXT *context, DISPATCHER_CONTEXT *dc)
{
    return __CxxFrameHandler3(record, establisher, context, dc);
}

#else

/* ================================================================== x86 */

/* a c++ frame's registration, 0xC below its ebp, the stack pointer of the function 4 below it. */
struct EH_NODE {
    EH_NODE *next;
    void *handler;
    int state;
};

extern "C" ULONG MsvcrtCallWithFrame(ULONG_PTR code, ULONG_PTR ebp);
extern "C" __declspec(noreturn) void MsvcrtContinueCatch(ULONG_PTR continuation, EH_NODE *node, void *registration);

static ULONG_PTR node_ebp(const EH_NODE *node) { return (ULONG_PTR)node + 0xC; }

static void call_action(ULONG_PTR action, ULONG_PTR ebp) { MsvcrtCallWithFrame(action, ebp); }

static void *read_fs0()
{
    void *r;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(r));
    return r;
}

static void write_fs0(void *r) { __asm__ volatile("movl %0, %%fs:0" : : "r"(r) : "memory"); }

/* the registration around a catch block: when an unwind leaves it, the catch has ended. */
struct CATCH_REGISTRATION {
    void *next;
    void *handler;
    MSVCRT_CATCH *c;
    bool done;
    /* the frame's saved stack pointer before the catch block put its own there. */
    EH_NODE *node;
    ULONG saved_esp;
};

static EXCEPTION_DISPOSITION __cdecl catch_guard(EXCEPTION_RECORD *record, CATCH_REGISTRATION *frame,
                                                 CONTEXT *context, void *dispatcher)
{
    if ((record->ExceptionFlags & UNWIND_FLAGS) && !frame->done) {
        frame->done = true;
        ((ULONG *)frame->node)[-1] = frame->saved_esp;
        pop_catch(frame->c);
    }
    return ExceptionContinueSearch;
}

static __declspec(noreturn) void catch_it(EXCEPTION_RECORD *record, EH_NODE *node, CONTEXT *context,
                                          const EH_FUNCTION *f, const EH_TRY *t, const EH_HANDLER *h,
                                          const CATCHABLE *c, const CXX_THROW *thrown)
{
    if (thrown)
        build_catch_object(h, c, thrown, node_ebp(node));
    MsvcrtEhThread()->caught_object = thrown ? thrown->object : nullptr;
    /*
     * a try block within a catch block of this frame still running unwinds
     * no further than that catch's registration, which stays, as the catch
     * block goes on after this one.
     */
    void *target = node;
    for (MSVCRT_CATCH *outer = MsvcrtEhThread()->catches; outer; outer = outer->next) {
        const EH_TRY *o = static_cast<const EH_TRY *>(outer->try_block);
        if (outer->frame == node && t->low > o->high && t->high <= o->catch_high) {
            target = outer->registration;
            break;
        }
    }
    RtlUnwind(target, nullptr, record, nullptr);
    node->state = unwind_to(f, node_ebp(node), node->state, t->low - 1);
    node->state = t->low - 1;
    MSVCRT_CATCH entry;
    push_catch(&entry, record, thrown, context);
    /* catch blocks keep their own stack pointer where the function's is: the continuation needs the one before. */
    CATCH_REGISTRATION guard = {read_fs0(), (void *)catch_guard, &entry, false, node, ((ULONG *)node)[-1]};
    entry.frame = node;
    entry.try_block = t;
    entry.registration = &guard;
    write_fs0(&guard);
    ULONG_PTR continuation = MsvcrtCallWithFrame(h->code, node_ebp(node));
    ((ULONG *)node)[-1] = guard.saved_esp;
    write_fs0(guard.next);
    guard.done = true;
    pop_catch(&entry);
    MsvcrtContinueCatch(continuation, node, target);
}

extern "C" EXCEPTION_DISPOSITION __cdecl MsvcrtCxxFrameHandler32(EXCEPTION_RECORD *record, EH_NODE *node,
                                                                 CONTEXT *context, void *dispatcher,
                                                                 const FUNC_INFO3 *info)
{
    const EH_FUNCTION *f = decode3(info, 0);
    if (record->ExceptionFlags & UNWIND_FLAGS) {
        node->state = unwind_to(f, node_ebp(node), node->state, -1);
        return ExceptionContinueSearch;
    }
    CXX_THROW thrown;
    bool cxx = cxx_throw(record, &thrown);
    if (!cxx) {
        MSVCRT_EH_THREAD *thread = MsvcrtEhThread();
        if (thread->translator && covered(f, node->state)) {
            EXCEPTION_POINTERS pointers = {record, context};
            ((translator_t)thread->translator)(record->ExceptionCode, &pointers);
        }
    }
    for (int i = 0; i < f->tries; i++) {
        const EH_TRY *t = &f->try_blocks[i];
        if (node->state < t->low || node->state > t->high)
            continue;
        for (int j = 0; j < t->count; j++) {
            const EH_HANDLER *h = &t->handlers[j];
            if (cxx) {
                const CATCHABLE *c = handler_match(h, &thrown);
                if (c)
                    catch_it(record, node, context, f, t, h, c, &thrown);
            } else if (catch_all(h) && !(f->flags & FUNC_EHS)) {
                catch_it(record, node, context, f, t, h, nullptr, nullptr);
            }
        }
    }
    escaped(f);
    return ExceptionContinueSearch;
}

/* the jump buffer of _setjmp3, as x86/seh.c has it. */
struct JUMP_BUFFER {
    ULONG Ebp, Ebx, Edi, Esi, Esp, Eip;
    EH_NODE *Registration;
    int TryLevel;
};

/* longjmp out of a c++ frame: its destructors down to the level setjmp saw; the handler's thunk gives FuncInfo. */
extern "C" void __stdcall __CxxLongjmpUnwind(JUMP_BUFFER *buf)
{
    EH_NODE *node = buf->Registration;
    const unsigned char *thunk = static_cast<const unsigned char *>(node->handler);
    if (thunk[0] != 0xB8)
        return;
    const FUNC_INFO3 *info = *(const FUNC_INFO3 *const *)(thunk + 1);
    node->state = unwind_to(decode3(info, 0), node_ebp(node), node->state, buf->TryLevel);
}

extern "C" void __stdcall __CxxCallUnwindDtor(destructor_t destructor, void *object) { destructor(object); }

typedef void(__thiscall *vector_destructor_t)(void *);
extern "C" void __stdcall __CxxCallUnwindVecDtor(void(__stdcall *destructor)(void *, size_t, int, vector_destructor_t),
                                                 void *array, size_t size, int count, vector_destructor_t element)
{
    destructor(array, size, count, element);
}

extern "C" void __stdcall __CxxCallUnwindDelDtor(void(__cdecl *destructor)(void *), void *object)
{
    destructor(object);
}

#endif

/* ------------------------------------------------------------------ helpers of other runtimes */

extern "C" int __cdecl __CxxDetectRethrow(EXCEPTION_POINTERS *pointers)
{
    const EXCEPTION_RECORD *r = pointers ? pointers->ExceptionRecord : nullptr;
    return r && (ULONG)r->ExceptionCode == CXX_EXCEPTION && r->NumberParameters >= 3 && !r->ExceptionInformation[2];
}

/* catch blocks of managed code: whether the exception is of a type, with no catch object built here. */
extern "C" int __cdecl __CxxExceptionFilter(EXCEPTION_POINTERS *pointers, const type_info *type, int adjectives,
                                            void *build)
{
    if (!pointers)
        return 0;
    if (!type)
        return 1;
    return MsvcrtIsExceptionTypeof(type, pointers->ExceptionRecord);
}

extern "C" int __cdecl __CxxQueryExceptionSize(void) { return sizeof(MSVCRT_CATCH); }

extern "C" int __cdecl __CxxRegisterExceptionObject(EXCEPTION_POINTERS *pointers, void *storage)
{
    CXX_THROW t;
    MSVCRT_CATCH *c = static_cast<MSVCRT_CATCH *>(storage);
    push_catch(c, pointers->ExceptionRecord, cxx_throw(pointers->ExceptionRecord, &t) ? &t : nullptr,
               pointers->ContextRecord);
    return 1;
}

extern "C" int __cdecl __CxxUnregisterExceptionObject(void *storage, int rethrow)
{
    MSVCRT_CATCH *c = static_cast<MSVCRT_CATCH *>(storage);
    if (rethrow)
        MsvcrtEhThread()->caught_object = c->thrown.object;
    pop_catch(c);
    return 0;
}

/* the exception the newest catch block handles, as vcruntime's helpers of current compilers ask for it. */
extern "C" void **__cdecl __current_exception(void) { return &MsvcrtEhThread()->current_record; }
extern "C" void **__cdecl __current_exception_context(void) { return &MsvcrtEhThread()->current_context; }
extern "C" int *__cdecl __processing_throw(void) { return &MsvcrtEhThread()->throws; }
