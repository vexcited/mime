/*
 * the memory of the engine: every string, object and table of a script is allocated here, and a collector that marks from
 * the roots of the engine and from the stack of the thread frees what nothing reaches. the stack is scanned for words
 * that point into a block, so the native code can keep values in locals without telling the collector.
 */
#include "js.h"

typedef struct GcHdr {
    struct GcHdr *Next;
    ULONG Size;
    BYTE Type, Mark;
    BYTE Pad[2];
#ifndef _WIN64
    ULONG Pad2;
#endif
} GcHdr;

#define HDR(p) ((GcHdr *)((BYTE *)(p) - sizeof(GcHdr)))
#define PAYLOAD(h) ((void *)((BYTE *)(h) + sizeof(GcHdr)))

void *js_alloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void *js_realloc(void *p, SIZE_T size)
{
    if (!p)
        return js_alloc(size);
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p, size ? size : 1);
}

void js_free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

PWSTR js_wcsdup(const WCHAR *s)
{
    SIZE_T n = wcslen(s);
    PWSTR r = js_alloc((n + 1) * sizeof(WCHAR));
    if (r)
        memcpy(r, s, n * sizeof(WCHAR));
    return r;
}

void *gc_alloc(Engine *e, SIZE_T size, int type)
{
    if (!e->NoGc && !e->InGc && e->HeapBytes > e->Threshold)
        gc_collect(e);
    GcHdr *h = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GcHdr) + size);
    if (!h)
        return nullptr;
    h->Size = (ULONG)size;
    h->Type = (BYTE)type;
    h->Next = e->Heap;
    e->Heap = h;
    e->HeapBytes += size + sizeof(GcHdr);
    return PAYLOAD(h);
}

void gc_add_root(Engine *e, void *payload)
{
    if (e->RootCount == e->RootCap) {
        e->RootCap = e->RootCap ? e->RootCap * 2 : 64;
        e->Roots = js_realloc(e->Roots, e->RootCap * sizeof(void *));
    }
    e->Roots[e->RootCount++] = payload;
}

/* ------------------------------------------------------------------ marking */

typedef struct Marker {
    void **Stack;
    ULONG Count, Cap;
} Marker;

static void mark_push(Marker *m, void *payload)
{
    if (!payload)
        return;
    GcHdr *h = HDR(payload);
    if (h->Mark)
        return;
    h->Mark = 1;
    if (h->Type == GC_STR || h->Type == GC_BYTES)
        return;
    if (m->Count == m->Cap) {
        m->Cap = m->Cap ? m->Cap * 2 : 1024;
        m->Stack = js_realloc(m->Stack, m->Cap * sizeof(void *));
    }
    m->Stack[m->Count++] = payload;
}

static void mark_val(Marker *m, const Val *v)
{
    if (v->t == T_STR || v->t == T_OBJ || v->t == T_VAR)
        mark_push(m, v->u.o);
}

static void trace_obj(Marker *m, Obj *o)
{
    mark_push(m, o->proto);
    mark_push(m, o->props);
    mark_push(m, o->index);
    switch (o->cls) {
    case C_STRING:
    case C_NUMBER:
    case C_BOOLEAN:
    case C_DATE:
        mark_val(m, &o->u.prim.prim);
        break;
    case C_FUNCTION:
        mark_push(m, o->u.fn.scope);
        mark_push(m, o->u.fn.home);
        break;
    case C_ARRAY:
        mark_push(m, o->u.arr.items);
        break;
    case C_ACTIVATION:
    case C_WITH:
    case C_ARGS:
        mark_push(m, o->u.scope.outer);
        mark_push(m, o->u.scope.target);
        mark_push(m, o->u.scope.func);
        mark_push(m, o->u.scope.args);
        mark_push(m, o->u.scope.arguments);
        break;
    case C_REGEXP:
        mark_push(m, o->u.re.source);
        break;
    case C_ERROR:
        mark_push(m, o->u.err.vals);
        break;
    case C_HOST:
        mark_push(m, o->u.host.self);
        break;
    default:
        break;
    }
}

static void trace_block(Marker *m, GcHdr *h)
{
    void *p = PAYLOAD(h);
    switch (h->Type) {
    case GC_OBJ:
        trace_obj(m, p);
        break;
    case GC_PROPS: {
        Prop *props = p;
        ULONG n = h->Size / sizeof(Prop);
        for (ULONG i = 0; i < n; i++) {
            mark_push(m, props[i].key);
            mark_val(m, &props[i].v);
        }
        break;
    }
    case GC_VALS: {
        Val *vals = p;
        ULONG n = h->Size / sizeof(Val);
        for (ULONG i = 0; i < n; i++)
            mark_val(m, &vals[i]);
        break;
    }
    case GC_VARBOX:
        break;
    case GC_ITEMS: {
        void **items = p;
        ULONG n = h->Size / sizeof(void *);
        for (ULONG i = 0; i < n; i++)
            mark_push(m, items[i]);
        break;
    }
    default:
        break;
    }
}

static int hdr_compare(const void *a, const void *b)
{
    const BYTE *x = (const BYTE *)*(GcHdr *const *)a, *y = (const BYTE *)*(GcHdr *const *)b;
    return x < y ? -1 : x > y;
}

static GcHdr *find_block(GcHdr **all, ULONG count, SIZE_T word)
{
    if (!count)
        return nullptr;
    ULONG lo = 0, hi = count;
    if (word < (SIZE_T)all[0] || word >= (SIZE_T)all[count - 1] + sizeof(GcHdr) + all[count - 1]->Size)
        return nullptr;
    while (lo + 1 < hi) {
        ULONG mid = (lo + hi) / 2;
        if ((SIZE_T)all[mid] <= word)
            lo = mid;
        else
            hi = mid;
    }
    GcHdr *h = all[lo];
    if (word >= (SIZE_T)PAYLOAD(h) && word < (SIZE_T)PAYLOAD(h) + (h->Size ? h->Size : 1))
        return h;
    /* a pointer to the header itself or one past the payload still belongs to the block. */
    if (word >= (SIZE_T)h && word <= (SIZE_T)PAYLOAD(h) + h->Size)
        return h;
    return nullptr;
}

static void scan_range(Marker *m, GcHdr **all, ULONG count, SIZE_T *from, SIZE_T *to)
{
    for (SIZE_T *p = from; p < to; p++) {
        GcHdr *h = find_block(all, count, *p);
        if (h)
            mark_push(m, PAYLOAD(h));
    }
}

static void mark_engine_roots(Marker *m, Engine *e)
{
    for (ULONG i = 0; i < e->InternCap; i++)
        mark_push(m, e->Interned ? e->Interned[i] : nullptr);
    for (ULONG i = 0; i < e->DispCount; i++)
        mark_push(m, e->DispNames[i]);
    Obj *protos[] = {e->Global, e->ObjectProto, e->FunctionProto, e->ArrayProto, e->StringProto, e->NumberProto,
                     e->BooleanProto, e->DateProto, e->RegExpProto, e->ErrorProto, e->EnumeratorProto, e->VBArrayProto,
                     e->RegExpCtor, e->ArrayCtor, e->ScriptDispatch};
    for (UINT i = 0; i < sizeof(protos) / sizeof(protos[0]); i++)
        mark_push(m, protos[i]);
    for (int i = 0; i < ERR_COUNT; i++) {
        mark_push(m, e->ErrorProtos[i]);
        mark_push(m, e->ErrorCtors[i]);
    }
    mark_val(m, &e->Exception);
    mark_val(m, &e->RetVal);
    mark_val(m, &e->GlobalThis);
    mark_push(m, e->LastInput);
    for (NamedItem *it = e->Items; it; it = it->next) {
        mark_push(m, it->name);
        mark_val(m, &it->value);
    }
    for (CallFrame *f = e->Frames; f; f = f->prev)
        mark_push(m, f->func);
}

void free_heap(Engine *e)
{
    GcHdr *h = e->Heap;
    while (h) {
        GcHdr *next = h->Next;
        if (h->Type == GC_OBJ)
            host_release_obj(e, PAYLOAD(h));
        else if (h->Type == GC_VARBOX)
            varbox_free(e, PAYLOAD(h));
        HeapFree(GetProcessHeap(), 0, h);
        h = next;
    }
    e->Heap = nullptr;
    e->HeapBytes = 0;
}

__attribute__((noinline)) static void collect_with_stack(Engine *e)
{
    Marker m = {};
    ULONG count = 0, cap = 0;
    GcHdr **all = nullptr;
    for (GcHdr *h = e->Heap; h; h = h->Next) {
        if (count == cap) {
            cap = cap ? cap * 2 : 4096;
            all = js_realloc(all, cap * sizeof(GcHdr *));
        }
        all[count++] = h;
    }
    qsort(all, count, sizeof(GcHdr *), hdr_compare);

    /* the stack, from here to the base of the thread. */
    SIZE_T *top = (SIZE_T *)&m;
    SIZE_T *base = (SIZE_T *)e->StackBase;
    if (base && top < base)
        scan_range(&m, all, count, top, base);

    for (ULONG i = 0; i < e->RootCount; i++)
        mark_push(&m, e->Roots[i]);
    mark_engine_roots(&m, e);
    /* objects the host holds references to are alive. */
    for (ULONG i = 0; i < count; i++)
        if (all[i]->Type == GC_OBJ && ((Obj *)PAYLOAD(all[i]))->ext > 0)
            mark_push(&m, PAYLOAD(all[i]));
    while (m.Count)
        trace_block(&m, HDR(m.Stack[--m.Count]));
    js_free(m.Stack);
    js_free(all);

    /* sweep. */
    GcHdr **link = &e->Heap;
    SIZE_T live = 0;
    while (*link) {
        GcHdr *h = *link;
        if (h->Mark) {
            h->Mark = 0;
            live += h->Size + sizeof(GcHdr);
            link = &h->Next;
            continue;
        }
        *link = h->Next;
        if (h->Type == GC_OBJ)
            host_release_obj(e, PAYLOAD(h));
        else if (h->Type == GC_VARBOX)
            varbox_free(e, PAYLOAD(h));
        HeapFree(GetProcessHeap(), 0, h);
    }
    e->HeapBytes = live;
    e->Threshold = live * 2 > 8u * 1024 * 1024 ? live * 2 : 8u * 1024 * 1024;
}

void gc_collect(Engine *e)
{
    if (e->InGc)
        return;
    e->InGc = TRUE;
    /* the registers a function keeps across calls are spilled into this frame. */
    __builtin_unwind_init();
    collect_with_stack(e);
    e->InGc = FALSE;
    /* the objects released while sweeping are let go now, outside of the walk. */
    while (e->DeferredCount) {
        IUnknown *u = e->Deferred[--e->DeferredCount];
        u->lpVtbl->Release(u);
    }
}
