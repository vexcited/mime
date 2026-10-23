/* the small runtime of an import free dll: memory from the kernel, a lock, and debug output. */
#include "opengl32.h"

#include <stdarg.h>

/*
 * small blocks come from size classes carved out of 64K chunks, big ones straight from NtAllocateVirtualMemory. each
 * block has a 16 byte header holding its class, or its size for big blocks.
 */
#define CLASS_COUNT 9 /* 32 to 8192 bytes. */
#define CHUNK (64 * 1024)

typedef struct FreeBlock {
    struct FreeBlock *next;
} FreeBlock;

static FreeBlock *g_free[CLASS_COUNT];
static GL_LOCK g_heap_lock;

static void *virtual_alloc(SIZE_T size)
{
    PVOID base = nullptr;
    SIZE_T n = size;
    if (!NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)))
        return nullptr;
    return base;
}

static void virtual_free(void *p)
{
    PVOID base = p;
    SIZE_T n = 0;
    NtFreeVirtualMemory(NtCurrentProcess(), &base, &n, MEM_RELEASE);
}

static int class_of(SIZE_T size)
{
    SIZE_T c = 32;
    for (int i = 0; i < CLASS_COUNT; i++, c <<= 1)
        if (size <= c)
            return i;
    return -1;
}

void *gl_alloc(SIZE_T size)
{
    SIZE_T total = size + 16;
    int k = class_of(total);
    uint8_t *p;
    if (k < 0) {
        p = virtual_alloc(total);
        if (!p)
            return nullptr;
        *(SIZE_T *)p = total;
        *(int *)(p + 8) = -1;
        return p + 16;
    }
    gl_lock(&g_heap_lock);
    if (!g_free[k]) {
        uint8_t *chunk = virtual_alloc(CHUNK);
        SIZE_T bs = (SIZE_T)32 << k;
        for (SIZE_T off = 0; chunk && off + bs <= CHUNK; off += bs) {
            FreeBlock *b = (FreeBlock *)(chunk + off);
            b->next = g_free[k];
            g_free[k] = b;
        }
    }
    p = (uint8_t *)g_free[k];
    if (p)
        g_free[k] = ((FreeBlock *)p)->next;
    gl_unlock(&g_heap_lock);
    if (!p)
        return nullptr;
    memset(p, 0, (SIZE_T)32 << k);
    *(int *)(p + 8) = k;
    return p + 16;
}

void gl_free(void *ptr)
{
    if (!ptr)
        return;
    uint8_t *p = (uint8_t *)ptr - 16;
    int k = *(int *)(p + 8);
    if (k < 0) {
        virtual_free(p);
        return;
    }
    gl_lock(&g_heap_lock);
    ((FreeBlock *)p)->next = g_free[k];
    g_free[k] = (FreeBlock *)p;
    gl_unlock(&g_heap_lock);
}

void *gl_realloc(void *ptr, SIZE_T size)
{
    if (!ptr)
        return gl_alloc(size);
    uint8_t *p = (uint8_t *)ptr - 16;
    int k = *(int *)(p + 8);
    SIZE_T old = k < 0 ? *(SIZE_T *)p - 16 : ((SIZE_T)32 << k) - 16;
    if (size <= old)
        return ptr;
    void *n = gl_alloc(size);
    if (n)
        memcpy(n, ptr, old);
    gl_free(ptr);
    return n;
}

char *gl_strdup(const char *s)
{
    SIZE_T n = strlen(s);
    char *d = gl_alloc(n + 1);
    if (d)
        memcpy(d, s, n + 1);
    return d;
}

/* a recursive lock: contention is rare, so waiters just yield. */
void gl_lock(GL_LOCK *l)
{
    HANDLE self = NtCurrentTeb()->ClientId.UniqueThread;
    if (l->Owner == self) {
        l->Depth++;
        return;
    }
    while (__atomic_exchange_n(&l->Locked, 1, __ATOMIC_ACQUIRE))
        NtYieldExecution();
    l->Owner = self;
    l->Depth = 1;
}

void gl_unlock(GL_LOCK *l)
{
    if (--l->Depth)
        return;
    l->Owner = nullptr;
    __atomic_store_n(&l->Locked, 0, __ATOMIC_RELEASE);
}

void gl_log(const char *fmt, ...)
{
    char text[512];
    WCHAR wide[512];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(text, sizeof(text) - 2, fmt, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof(text) - 2)
        n = (int)sizeof(text) - 2;
    text[n++] = '\n';
    int len = 0;
    const char *prefix = "opengl32: ";
    for (const char *p = prefix; *p; p++)
        wide[len++] = (WCHAR)*p;
    for (int i = 0; i < n && len < 511; i++)
        wide[len++] = (WCHAR)(unsigned char)text[i];
    UNICODE_STRING s = {(USHORT)(len * sizeof(WCHAR)), (USHORT)(len * sizeof(WCHAR)), wide};
    NtDisplayString(&s);
}

uint64_t gl_hash(const void *data, SIZE_T size, uint64_t seed)
{
    const uint8_t *p = data;
    uint64_t h = 0xCBF29CE484222325ull ^ seed;
    for (SIZE_T i = 0; i < size; i++) {
        h ^= p[i];
        h *= 0x100000001B3ull;
    }
    return h;
}

void *gl_name_get(GlNames *n, GLuint name)
{
    return name && name < n->count ? n->items[name] : nullptr;
}

void gl_name_set(GlNames *n, GLuint name, void *object)
{
    if (name >= n->count) {
        GLuint count = n->count ? n->count : 64;
        while (count <= name)
            count *= 2;
        void **items = gl_realloc(n->items, count * sizeof(void *));
        if (!items)
            return;
        memset(items + n->count, 0, (count - n->count) * sizeof(void *));
        n->items = items;
        n->count = count;
    }
    n->items[name] = object;
}

/* the lowest free name; the slot keeps a marker until the object is made. */

GLuint gl_name_new(GlNames *n)
{
    for (GLuint i = 1; i < n->count; i++) {
        if (!n->items[i]) {
            n->items[i] = GL_RESERVED;
            return i;
        }
    }
    GLuint name = n->count ? n->count : 1;
    gl_name_set(n, name, GL_RESERVED);
    return name;
}

/* the runtime library allocates its strings through these. */
PVOID MimeRtlAllocate(SIZE_T size)
{
    return gl_alloc(size);
}

void MimeRtlFree(PVOID p)
{
    gl_free(p);
}
