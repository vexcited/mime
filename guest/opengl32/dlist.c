#include "dlist.h"
#include "immediate.h"

#define MAX_NESTING 64
#define MAX_STREAM (1u << 30)
/* vertices of one buffer for the primitives that stand alone, a multiple of 12 so every mode ends on a whole primitive. */
#define CHUNK_VERTICES 393216u
#define MAX_WHOLE_VERTICES 500000u

struct GlList {
    GLuint name;
    LONG refs;
    uint8_t *ops;
    size_t size;
    GlDlBatch **batches;
    uint32_t batch_count;
};

struct GlDlBuilder {
    uint8_t *ops;
    size_t size, cap;
    GlDlBatch **batches;
    uint32_t batch_count, batch_cap;
    BOOL failed;

    /* the glBegin being compiled. its vertices are kept as glBegin collects them, and made into a batch at glEnd. */
    GLenum begin_mode;
    GlFfVertex *vertices;
    uint32_t vertex_count, vertex_cap;

    /* what the list knows of the current color, normal and texture coordinate: known has a bit for each one it set, and
       unknown counts the vertices of the batch that were made before it did. */
    uint32_t known;
    uint32_t unknown[3];
    uint32_t set; /* the ones set since glBegin. */
    GLfloat current[3][4];

    /* a glBegin whose glEnd came and that is not made a batch yet: the next one of the same kind goes on in the same
       vertices, so a list of many small shapes is a few draws. what is set meanwhile is not recorded, the vertices that
       follow have it and a batch that ends has it current. */
    BOOL pending;
};

typedef struct OpHeader {
    uint32_t op, size;
} OpHeader;

/* ---------------------------------------------------------------- the names of lists */

static uint32_t hash_name(GLuint name)
{
    return name * 2654435761u;
}

/* the entry of a name that is a list; NULL when it is none. */
static GlListEntry *table_find(GlListTable *t, GLuint name)
{
    if (!t->cap || !name)
        return nullptr;
    for (uint32_t i = hash_name(name) & (t->cap - 1);; i = (i + 1) & (t->cap - 1)) {
        GlListEntry *e = &t->entries[i];
        if (!e->name)
            return nullptr;
        if (e->name == name)
            return e->list ? e : nullptr;
    }
}

static BOOL table_grow(GlListTable *t)
{
    uint32_t cap = t->cap ? t->cap : 64;
    while ((t->count + 1) * 2 > cap)
        cap *= 2;
    GlListEntry *entries = gl_alloc((SIZE_T)cap * sizeof(GlListEntry));
    if (!entries)
        return FALSE;
    for (uint32_t i = 0; i < t->cap; i++) {
        GlListEntry *e = &t->entries[i];
        if (!e->name || !e->list)
            continue;
        uint32_t j = hash_name(e->name) & (cap - 1);
        while (entries[j].name)
            j = (j + 1) & (cap - 1);
        entries[j] = *e;
    }
    gl_free(t->entries);
    t->entries = entries;
    t->cap = cap;
    t->used = t->count;
    return TRUE;
}

/* sets the list of a name and returns the one it replaced. FALSE when the table is out of memory. */
static BOOL table_set(GlListTable *t, GLuint name, GlList *list, GlList **old)
{
    *old = nullptr;
    GlListEntry *e = table_find(t, name);
    if (e) {
        *old = e->list;
        e->list = list;
        return TRUE;
    }
    if ((t->used + 1) * 2 > t->cap && !table_grow(t))
        return FALSE;
    uint32_t i = hash_name(name) & (t->cap - 1);
    while (t->entries[i].name && t->entries[i].list)
        i = (i + 1) & (t->cap - 1);
    if (!t->entries[i].name)
        t->used++;
    t->entries[i].name = name;
    t->entries[i].list = list;
    t->count++;
    return TRUE;
}

static void list_free(GlList *l)
{
    for (uint32_t i = 0; i < l->batch_count; i++)
        gl_batch_free(l->batches[i]);
    gl_free(l->batches);
    gl_free(l->ops);
    gl_free(l);
}

static void list_release(GlList *l)
{
    if (__atomic_sub_fetch(&l->refs, 1, __ATOMIC_ACQ_REL) == 0)
        list_free(l);
}

static GlList *list_acquire(GlShared *s, GLuint name)
{
    gl_lock(&s->lists.lock);
    GlListEntry *e = table_find(&s->lists, name);
    GlList *l = e ? e->list : nullptr;
    if (l)
        __atomic_fetch_add(&l->refs, 1, __ATOMIC_ACQ_REL);
    gl_unlock(&s->lists.lock);
    return l;
}

/* the list a name gets, and the one it had is let go. */
static BOOL list_install(GlShared *s, GLuint name, GlList *list)
{
    GlList *old;
    gl_lock(&s->lists.lock);
    BOOL ok = table_set(&s->lists, name, list, &old);
    gl_unlock(&s->lists.lock);
    if (ok && old)
        list_release(old);
    return ok;
}

void gl_dl_shared_destroy(GlShared *s)
{
    for (uint32_t i = 0; i < s->lists.cap; i++)
        if (s->lists.entries[i].name && s->lists.entries[i].list)
            list_release(s->lists.entries[i].list);
    gl_free(s->lists.entries);
    memset(&s->lists, 0, sizeof(s->lists));
}

/* ---------------------------------------------------------------- the stream of a list being compiled */

static void builder_free(GlDlBuilder *b)
{
    if (!b)
        return;
    for (uint32_t i = 0; i < b->batch_count; i++)
        gl_batch_free(b->batches[i]);
    gl_free(b->batches);
    gl_free(b->ops);
    gl_free(b->vertices);
    gl_free(b);
}

void gl_dl_destroy(GlContext *ctx)
{
    builder_free(ctx->dl.b);
    ctx->dl.b = nullptr;
    ctx->dl.mode = 0;
}

static void flush_pending(GlContext *ctx);

uint8_t *gl_dl_emit(GlContext *ctx, uint32_t op, size_t payload)
{
    GlDlBuilder *b = ctx->dl.b;
    if (b->pending && !ctx->dl.in_begin)
        flush_pending(ctx);
    size_t total = sizeof(OpHeader) + ((payload + 7) & ~(size_t)7);
    if (b->failed || total > MAX_STREAM || b->size + total > MAX_STREAM) {
        b->failed = TRUE;
        return nullptr;
    }
    if (b->size + total > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->size + total)
            cap *= 2;
        uint8_t *ops = gl_realloc(b->ops, cap);
        if (!ops) {
            b->failed = TRUE;
            return nullptr;
        }
        b->ops = ops;
        b->cap = cap;
    }
    OpHeader *h = (OpHeader *)(b->ops + b->size);
    h->op = op;
    h->size = (uint32_t)total;
    memset(h + 1, 0, total - sizeof(OpHeader));
    b->size += total;
    return (uint8_t *)(h + 1);
}

void gl_dl_error(GlContext *ctx, GLenum error)
{
    uint8_t *p = gl_dl_emit(ctx, GL_DL_OP_ERROR, 8);
    if (p)
        gl_dl_put_i(p, 0, error);
}

/* ---------------------------------------------------------------- how many values a vector command reads */

int gl_dl_fog_count(GLenum pname)
{
    return pname == GL_FOG_COLOR ? 4 : 1;
}

int gl_dl_light_count(GLenum pname)
{
    switch (pname) {
    case GL_AMBIENT:
    case GL_DIFFUSE:
    case GL_SPECULAR:
    case GL_POSITION:
        return 4;
    case GL_SPOT_DIRECTION:
        return 3;
    default:
        return 1;
    }
}

int gl_dl_light_model_count(GLenum pname)
{
    return pname == GL_LIGHT_MODEL_AMBIENT ? 4 : 1;
}

int gl_dl_material_count(GLenum pname)
{
    switch (pname) {
    case GL_AMBIENT:
    case GL_DIFFUSE:
    case GL_SPECULAR:
    case GL_EMISSION:
    case GL_AMBIENT_AND_DIFFUSE:
        return 4;
    case GL_COLOR_INDEXES:
        return 3;
    default:
        return 1;
    }
}

int gl_dl_point_parameter_count(GLenum pname)
{
    return pname == GL_POINT_DISTANCE_ATTENUATION ? 3 : 1;
}

int gl_dl_tex_env_count(GLenum pname)
{
    return pname == GL_TEXTURE_ENV_COLOR ? 4 : 1;
}

int gl_dl_tex_gen_count(GLenum pname)
{
    return pname == GL_OBJECT_PLANE || pname == GL_EYE_PLANE ? 4 : 1;
}

int gl_dl_tex_parameter_count(GLenum pname)
{
    return pname == GL_TEXTURE_BORDER_COLOR ? 4 : 1;
}

/* ---------------------------------------------------------------- vertices */

static BOOL batch_grow(GlDlBuilder *b)
{
    uint32_t cap = b->vertex_cap ? b->vertex_cap * 2 : 256;
    if (cap < b->vertex_cap || cap > (0x60000000u / sizeof(GlFfVertex)))
        return FALSE;
    GlFfVertex *v = gl_realloc(b->vertices, (SIZE_T)cap * sizeof(GlFfVertex));
    if (!v)
        return FALSE;
    b->vertices = v;
    b->vertex_cap = cap;
    return TRUE;
}

BOOL gl_dl_vertex(GlContext *ctx, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    GlDlBuilder *b = ctx->dl.b;
    if (ctx->dl.in_begin) {
        if (b->vertex_count == b->vertex_cap && !batch_grow(b)) {
            b->failed = TRUE;
        } else if (!b->failed) {
            GlFfVertex *v = &b->vertices[b->vertex_count++];
            v->position[0] = x;
            v->position[1] = y;
            v->position[2] = z;
            v->position[3] = w;
            memcpy(v->color, b->current, sizeof(b->current));
            for (int a = 0; a < 3; a++)
                b->unknown[a] += !((b->known >> a) & 1);
        }
    } else {
        uint8_t *p = gl_dl_emit(ctx, GL_DL_OP_VERTEX, 32);
        if (p) {
            gl_dl_put_f(p, 0, x);
            gl_dl_put_f(p, 1, y);
            gl_dl_put_f(p, 2, z);
            gl_dl_put_f(p, 3, w);
        }
    }
    return ctx->dl.mode == GL_COMPILE;
}

static void put4(uint8_t *p, int first, GLfloat a, GLfloat b, GLfloat c, GLfloat d)
{
    gl_dl_put_f(p, first, a);
    gl_dl_put_f(p, first + 1, b);
    gl_dl_put_f(p, first + 2, c);
    gl_dl_put_f(p, first + 3, d);
}

BOOL gl_dl_color(GlContext *ctx, GLfloat r, GLfloat g, GLfloat b_, GLfloat a)
{
    GlDlBuilder *b = ctx->dl.b;
    b->current[0][0] = r;
    b->current[0][1] = g;
    b->current[0][2] = b_;
    b->current[0][3] = a;
    b->known |= 1;
    if (ctx->dl.in_begin || b->pending) {
        b->set |= 1;
    } else {
        uint8_t *p = gl_dl_emit(ctx, GL_DL_OP_COLOR, 32);
        if (p)
            put4(p, 0, r, g, b_, a);
    }
    return ctx->dl.mode == GL_COMPILE;
}

BOOL gl_dl_normal(GlContext *ctx, GLfloat x, GLfloat y, GLfloat z)
{
    GlDlBuilder *b = ctx->dl.b;
    b->current[1][0] = x;
    b->current[1][1] = y;
    b->current[1][2] = z;
    b->known |= 2;
    if (ctx->dl.in_begin || b->pending) {
        b->set |= 2;
    } else {
        uint8_t *p = gl_dl_emit(ctx, GL_DL_OP_NORMAL, 32);
        if (p)
            put4(p, 0, x, y, z, 0);
    }
    return ctx->dl.mode == GL_COMPILE;
}

BOOL gl_dl_texcoord(GlContext *ctx, GLuint unit, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    GlDlBuilder *b = ctx->dl.b;
    if (unit == 0) {
        b->current[2][0] = s;
        b->current[2][1] = t;
        b->current[2][2] = r;
        b->current[2][3] = q;
        b->known |= 4;
        if (ctx->dl.in_begin || b->pending) {
            b->set |= 4;
            return ctx->dl.mode == GL_COMPILE;
        }
    }
    /* the other units are not drawn with, so a change of one is kept for what it leaves current. */
    uint8_t *p = gl_dl_emit(ctx, GL_DL_OP_TEXCOORD, 40);
    if (p) {
        gl_dl_put_i(p, 0, unit);
        put4(p, 1, s, t, r, q);
    }
    return ctx->dl.mode == GL_COMPILE;
}

BOOL gl_dl_rasterpos(GlContext *ctx, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    if (ctx->dl.in_begin) {
        gl_dl_error(ctx, GL_INVALID_OPERATION);
    } else {
        uint8_t *p = gl_dl_emit(ctx, GL_DL_OP_RASTERPOS, 32);
        if (p)
            put4(p, 0, x, y, z, w);
    }
    return ctx->dl.mode == GL_COMPILE;
}

/* ---------------------------------------------------------------- batches */

static BOOL independent(GLenum mode)
{
    return mode == GL_POINTS || mode == GL_LINES || mode == GL_TRIANGLES || mode == GL_QUADS;
}

static BOOL attribute_differs(const GlFfVertex *v, uint32_t count, int a)
{
    for (uint32_t i = 1; i < count; i++) {
        const GLfloat *x = a == 0 ? v[i].color : a == 1 ? v[i].normal : v[i].texcoord;
        const GLfloat *y = a == 0 ? v[0].color : a == 1 ? v[0].normal : v[0].texcoord;
        if (memcmp(x, y, 16))
            return TRUE;
    }
    return FALSE;
}

static BOOL builder_add_batch(GlDlBuilder *b, GlDlBatch *batch)
{
    if (b->batch_count == b->batch_cap) {
        uint32_t cap = b->batch_cap ? b->batch_cap * 2 : 64;
        GlDlBatch **grown = gl_realloc(b->batches, (SIZE_T)cap * sizeof(GlDlBatch *));
        if (!grown)
            return FALSE;
        b->batches = grown;
        b->batch_cap = cap;
    }
    b->batches[b->batch_count++] = batch;
    return TRUE;
}

/* a batch of count vertices, the first one being vertex first of the glBegin. */
static GlDlBatch *make_batch(GlDlBuilder *b, uint32_t first, uint32_t count, BOOL last)
{
    const GlFfVertex *v = b->vertices + first;
    GlDlBatch *batch = gl_alloc(sizeof(GlDlBatch));
    if (!batch)
        return nullptr;
    batch->mode = b->begin_mode;
    batch->count = count;
    for (int a = 0; a < 3; a++) {
        /* how many of its vertices were made before the list knew the attribute. */
        uint32_t unknown = b->unknown[a] > first ? b->unknown[a] - first : 0;
        if (unknown > count)
            unknown = count;
        if (unknown == 0) {
        } else if (unknown == count) {
            batch->dep |= 1u << a;
        } else {
            batch->mixed |= 1u << a;
            batch->prefix[a] = unknown;
        }
    }
    if (last) {
        batch->set = b->set;
        memcpy(batch->final, b->current, sizeof(batch->final));
    }
    if (!count)
        return batch;
    if (batch->mixed) {
        batch->cpu = gl_alloc((SIZE_T)count * sizeof(GlFfVertex));
        if (!batch->cpu) {
            gl_free(batch);
            return nullptr;
        }
        memcpy(batch->cpu, v, (SIZE_T)count * sizeof(GlFfVertex));
        batch->varying = 7 & ~batch->dep;
        return batch;
    }
    for (int a = 0; a < 3; a++) {
        if (!((batch->dep >> a) & 1) && attribute_differs(v, count, a))
            batch->varying |= 1u << a;
        const GLfloat *first = a == 0 ? v[0].color : a == 1 ? v[0].normal : v[0].texcoord;
        memcpy(batch->constant[a], first, 16);
    }
    uint32_t stride = 16;
    for (int a = 0; a < 3; a++) {
        if ((batch->varying >> a) & 1) {
            batch->offset[a] = stride;
            stride += 16;
        }
    }
    batch->stride = stride;
    if (!gl_heap_alloc((uint64_t)count * stride, &batch->vb)) {
        /* the buffer is not there: the vertices stay where the cpu has them. */
        batch->cpu = gl_alloc((SIZE_T)count * sizeof(GlFfVertex));
        if (!batch->cpu) {
            gl_free(batch);
            return nullptr;
        }
        memcpy(batch->cpu, v, (SIZE_T)count * sizeof(GlFfVertex));
        return batch;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *d = batch->vb.ptr + (uint64_t)i * stride;
        memcpy(d, v[i].position, 16);
        if (batch->offset[0])
            memcpy(d + batch->offset[0], v[i].color, 16);
        if (batch->offset[1])
            memcpy(d + batch->offset[1], v[i].normal, 16);
        if (batch->offset[2])
            memcpy(d + batch->offset[2], v[i].texcoord, 16);
    }
    return batch;
}

static void emit_batch(GlContext *ctx, GlDlBatch *batch)
{
    GlDlBuilder *b = ctx->dl.b;
    uint8_t *p = batch ? gl_dl_emit(ctx, GL_DL_OP_BATCH, sizeof(void *)) : nullptr;
    if (!p || !builder_add_batch(b, batch)) {
        gl_batch_free(batch);
        b->failed = TRUE;
        return;
    }
    memcpy(p, &batch, sizeof(batch));
}

static void begin_batch(GlContext *ctx, GLenum mode)
{
    GlDlBuilder *b = ctx->dl.b;
    if (b->pending) {
        if (b->begin_mode == mode && independent(mode) && b->vertex_count < (1u << 21)) {
            b->pending = FALSE;
            ctx->dl.in_begin = TRUE;
            return;
        }
        flush_pending(ctx);
    }
    ctx->dl.in_begin = TRUE;
    b->begin_mode = mode;
    b->vertex_count = 0;
    b->set = 0;
    b->unknown[0] = b->unknown[1] = b->unknown[2] = 0;
}

/* the pending glBegin made into the batches that draw it. */
static void bake_pending(GlContext *ctx)
{
    GlDlBuilder *b = ctx->dl.b;
    if (b->failed)
        return;
    uint32_t count = gl_whole_primitives(b->begin_mode, b->vertex_count);
    if (independent(b->begin_mode) && count > CHUNK_VERTICES) {
        for (uint32_t first = 0; first < count; first += CHUNK_VERTICES) {
            uint32_t n = count - first < CHUNK_VERTICES ? count - first : CHUNK_VERTICES;
            emit_batch(ctx, make_batch(b, first, n, first + n == count));
        }
        return;
    }
    if (count > MAX_WHOLE_VERTICES) {
        /* a strip or a fan that large is drawn from the cpu's copy of it. */
        GlDlBatch *batch = gl_alloc(sizeof(GlDlBatch));
        if (batch) {
            batch->mode = b->begin_mode;
            batch->count = count;
            batch->cpu = gl_alloc((SIZE_T)count * sizeof(GlFfVertex));
            if (!batch->cpu) {
                gl_free(batch);
                batch = nullptr;
            } else {
                memcpy(batch->cpu, b->vertices, (SIZE_T)count * sizeof(GlFfVertex));
                for (int a = 0; a < 3; a++) {
                    if (b->unknown[a] >= b->vertex_count)
                        batch->dep |= 1u << a;
                    else if (b->unknown[a])
                        batch->mixed |= 1u << a, batch->prefix[a] = b->unknown[a];
                }
                batch->varying = 7 & ~batch->dep;
                batch->set = b->set;
                memcpy(batch->final, b->current, sizeof(batch->final));
            }
        }
        emit_batch(ctx, batch);
        return;
    }
    emit_batch(ctx, make_batch(b, 0, count, TRUE));
}

static void flush_pending(GlContext *ctx)
{
    GlDlBuilder *b = ctx->dl.b;
    if (!b->pending)
        return;
    b->pending = FALSE;
    bake_pending(ctx);
    b->vertex_count = 0;
    b->set = 0;
    b->unknown[0] = b->unknown[1] = b->unknown[2] = 0;
}

static void end_batch(GlContext *ctx)
{
    GlDlBuilder *b = ctx->dl.b;
    ctx->dl.in_begin = FALSE;
    /* a part of a primitive at the end is dropped now, so the next glBegin of the kind starts on one. */
    uint32_t whole = gl_whole_primitives(b->begin_mode, b->vertex_count);
    if (whole < b->vertex_count) {
        b->vertex_count = whole;
        for (int a = 0; a < 3; a++)
            if (b->unknown[a] > whole)
                b->unknown[a] = whole;
    }
    b->pending = TRUE;
}

/* ---------------------------------------------------------------- the commands recorded by hand */

#define RUN(ctx) ((ctx)->dl.mode == GL_COMPILE_AND_EXECUTE)

BOOL gl_dl_custom_glBegin(GlContext *ctx, GLenum mode)
{
    if (mode > GL_POLYGON)
        gl_dl_error(ctx, GL_INVALID_ENUM);
    else
        begin_batch(ctx, mode);
    return RUN(ctx);
}

BOOL gl_dl_custom_glEnd(GlContext *ctx)
{
    if (!ctx->dl.in_begin)
        gl_dl_error(ctx, GL_INVALID_OPERATION);
    else
        end_batch(ctx);
    return RUN(ctx);
}

BOOL gl_dl_custom_glEdgeFlag(GlContext *ctx, GLboolean flag)
{
    uint8_t *p = gl_dl_emit(ctx, GL_DL_OP_EDGEFLAG, 8);
    if (p)
        gl_dl_put_i(p, 0, flag);
    return RUN(ctx);
}

BOOL gl_dl_custom_glEdgeFlagv(GlContext *ctx, const GLboolean *flag)
{
    return gl_dl_custom_glEdgeFlag(ctx, *flag);
}

/* the attributes and the position of vertex i of the client arrays, as the commands a glBegin would have got. */
static void record_element(GlContext *ctx, GLint i)
{
    const GlVertexArray *vao = ctx->vao;
    GLfloat v[4];
    int n = gl_read_array(ctx, &vao->legacy[1], i, v);
    if (n)
        gl_dl_color(ctx, v[0], v[1], v[2], n > 3 ? v[3] : 1);
    n = gl_read_array(ctx, &vao->legacy[2], i, v);
    if (n)
        gl_dl_normal(ctx, v[0], v[1], v[2]);
    for (GLuint unit = 0; unit < FF_UNITS; unit++) {
        GLfloat t[4] = {0, 0, 0, 1};
        n = gl_read_array(ctx, &vao->legacy[3 + unit], i, t);
        if (n)
            gl_dl_texcoord(ctx, unit, t[0], n > 1 ? t[1] : 0, n > 2 ? t[2] : 0, n > 3 ? t[3] : 1);
    }
    GLfloat p[4] = {0, 0, 0, 1};
    n = gl_read_array(ctx, &vao->legacy[0], i, p);
    if (n)
        gl_dl_vertex(ctx, p[0], p[1], n > 2 ? p[2] : 0, n > 3 ? p[3] : 1);
}

BOOL gl_dl_custom_glArrayElement(GlContext *ctx, GLint i)
{
    record_element(ctx, i);
    return RUN(ctx);
}

static BOOL draws_arrays(const GlContext *ctx)
{
    return ctx->vao->legacy[0].enabled && !ctx->current_program;
}

static void record_arrays(GlContext *ctx, GLenum mode, GLint first, GLsizei count)
{
    begin_batch(ctx, mode);
    for (GLsizei i = 0; i < count && !ctx->dl.b->failed; i++)
        record_element(ctx, first + i);
    end_batch(ctx);
}

BOOL gl_dl_custom_glDrawArrays(GlContext *ctx, GLenum mode, GLint first, GLsizei count)
{
    if (first < 0 || count < 0)
        gl_dl_error(ctx, GL_INVALID_VALUE);
    else if (mode > GL_POLYGON)
        gl_dl_error(ctx, GL_INVALID_ENUM);
    else if (count && draws_arrays(ctx))
        record_arrays(ctx, mode, first, count);
    return RUN(ctx);
}

BOOL gl_dl_custom_glMultiDrawArrays(GlContext *ctx, GLenum mode, const GLint *first, const GLsizei *count, GLsizei n)
{
    for (GLsizei i = 0; i < n; i++)
        gl_dl_custom_glDrawArrays(ctx, mode, first[i], count[i]);
    return RUN(ctx);
}

static uint32_t read_index(const void *indices, GLenum type, uint32_t i)
{
    switch (type) {
    case GL_UNSIGNED_BYTE:
        return ((const uint8_t *)indices)[i];
    case GL_UNSIGNED_SHORT:
        return ((const uint16_t *)indices)[i];
    default:
        return ((const uint32_t *)indices)[i];
    }
}

static void record_elements(GlContext *ctx, GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    /* the indices are read now, from the buffer when one is bound. */
    uint32_t size = type == GL_UNSIGNED_BYTE ? 1 : type == GL_UNSIGNED_SHORT ? 2 : 4;
    const uint8_t *data = indices;
    GlBuffer *eb = ctx->vao->element_buffer ? gl_buffer(ctx, ctx->vao->element_buffer) : nullptr;
    if (eb) {
        if (!eb->mem.ptr || (uint64_t)(ULONG_PTR)indices + (uint64_t)count * size > eb->size)
            return;
        data = eb->mem.ptr + (ULONG_PTR)indices;
    }
    if (!data)
        return;
    begin_batch(ctx, mode);
    for (GLsizei i = 0; i < count && !ctx->dl.b->failed; i++)
        record_element(ctx, (GLint)read_index(data, type, (uint32_t)i));
    end_batch(ctx);
}

BOOL gl_dl_custom_glDrawElements(GlContext *ctx, GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    if (count < 0)
        gl_dl_error(ctx, GL_INVALID_VALUE);
    else if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT)
        gl_dl_error(ctx, GL_INVALID_ENUM);
    else if (mode > GL_POLYGON)
        gl_dl_error(ctx, GL_INVALID_ENUM);
    else if (count && draws_arrays(ctx))
        record_elements(ctx, mode, count, type, indices);
    return RUN(ctx);
}

BOOL gl_dl_custom_glDrawRangeElements(GlContext *ctx, GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                                      const void *indices)
{
    return gl_dl_custom_glDrawElements(ctx, mode, count, type, indices);
}

BOOL gl_dl_custom_glMultiDrawElements(GlContext *ctx, GLenum mode, const GLsizei *count, GLenum type,
                                      const void *const *indices, GLsizei n)
{
    for (GLsizei i = 0; i < n; i++)
        gl_dl_custom_glDrawElements(ctx, mode, count[i], type, indices[i]);
    return RUN(ctx);
}

/* a rectangle is a polygon of four vertices. */
static BOOL record_rect(GlContext *ctx, GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    begin_batch(ctx, GL_POLYGON);
    gl_dl_vertex(ctx, x1, y1, 0, 1);
    gl_dl_vertex(ctx, x2, y1, 0, 1);
    gl_dl_vertex(ctx, x2, y2, 0, 1);
    gl_dl_vertex(ctx, x1, y2, 0, 1);
    end_batch(ctx);
    return RUN(ctx);
}

BOOL gl_dl_custom_glRectd(GlContext *ctx, GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2)
{
    return record_rect(ctx, (GLfloat)x1, (GLfloat)y1, (GLfloat)x2, (GLfloat)y2);
}

BOOL gl_dl_custom_glRectdv(GlContext *ctx, const GLdouble *v1, const GLdouble *v2)
{
    return record_rect(ctx, (GLfloat)v1[0], (GLfloat)v1[1], (GLfloat)v2[0], (GLfloat)v2[1]);
}

BOOL gl_dl_custom_glRectf(GlContext *ctx, GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    return record_rect(ctx, x1, y1, x2, y2);
}

BOOL gl_dl_custom_glRectfv(GlContext *ctx, const GLfloat *v1, const GLfloat *v2)
{
    return record_rect(ctx, v1[0], v1[1], v2[0], v2[1]);
}

BOOL gl_dl_custom_glRecti(GlContext *ctx, GLint x1, GLint y1, GLint x2, GLint y2)
{
    return record_rect(ctx, (GLfloat)x1, (GLfloat)y1, (GLfloat)x2, (GLfloat)y2);
}

BOOL gl_dl_custom_glRectiv(GlContext *ctx, const GLint *v1, const GLint *v2)
{
    return record_rect(ctx, (GLfloat)v1[0], (GLfloat)v1[1], (GLfloat)v2[0], (GLfloat)v2[1]);
}

BOOL gl_dl_custom_glRects(GlContext *ctx, GLshort x1, GLshort y1, GLshort x2, GLshort y2)
{
    return record_rect(ctx, (GLfloat)x1, (GLfloat)y1, (GLfloat)x2, (GLfloat)y2);
}

BOOL gl_dl_custom_glRectsv(GlContext *ctx, const GLshort *v1, const GLshort *v2)
{
    return record_rect(ctx, (GLfloat)v1[0], (GLfloat)v1[1], (GLfloat)v2[0], (GLfloat)v2[1]);
}

/* calling a list inside one keeps the call, and the called list may set any attribute. */
static void forget_attributes(GlDlBuilder *b)
{
    b->known = 0;
}

BOOL gl_dl_custom_glCallList(GlContext *ctx, GLuint list)
{
    uint8_t *p = gl_dl_emit(ctx, GL_DL_OP_CALL, 8);
    if (p)
        gl_dl_put_i(p, 0, list);
    forget_attributes(ctx->dl.b);
    return RUN(ctx);
}

static size_t list_id_size(GLenum type)
{
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        return 1;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
    case GL_2_BYTES:
        return 2;
    case GL_3_BYTES:
        return 3;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
    case GL_4_BYTES:
        return 4;
    default:
        return 0;
    }
}

BOOL gl_dl_custom_glCallLists(GlContext *ctx, GLsizei n, GLenum type, const GLvoid *lists)
{
    size_t bytes = lists ? gl_dl_bytes(n, list_id_size(type)) : 0;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_OP_CALLS, 24 + bytes);
    if (p) {
        gl_dl_put_i(p, 0, (uint64_t)(int64_t)n);
        gl_dl_put_i(p, 1, type);
        gl_dl_put_i(p, 2, bytes ? 1 : 0);
        if (bytes)
            memcpy(p + 24, lists, bytes);
    }
    forget_attributes(ctx->dl.b);
    return RUN(ctx);
}

BOOL gl_dl_custom_glPopAttrib(GlContext *ctx)
{
    /* the current values come back from the stack. */
    gl_dl_emit(ctx, GL_DL_OP_POPATTRIB, 0);
    forget_attributes(ctx->dl.b);
    return RUN(ctx);
}

/* ---------------------------------------------------------------- pixels */

enum {
    IMG_TEXIMAGE1D,
    IMG_TEXIMAGE2D,
    IMG_TEXIMAGE3D,
    IMG_TEXSUBIMAGE1D,
    IMG_TEXSUBIMAGE2D,
    IMG_TEXSUBIMAGE3D,
    IMG_COMPRESSED2D,
    IMG_COMPRESSED_SUB2D,
    IMG_DRAWPIXELS,
    IMG_BITMAP,
    IMG_STIPPLE,
};

/* slots of an image operation: what it is, the pixel store flags, where the data is, and its arguments. */
#define IMG_ARGS 12
#define IMG_FIRST_ARG 3

static const uint8_t *unpack_source(GlContext *ctx, const void *pixels, uint64_t needed)
{
    if (ctx->pixel_unpack) {
        GlBuffer *b = gl_buffer(ctx, ctx->pixel_unpack);
        uint64_t offset = (uint64_t)(ULONG_PTR)pixels;
        if (!b || !b->mem.ptr || offset > b->size || needed > b->size - offset)
            return nullptr;
        return b->mem.ptr + offset;
    }
    return pixels;
}

/* the bytes of w by h by d pixels once they are tightly packed; 0 when the format and type are not known. */
static uint64_t packed_bytes(GLsizei w, GLsizei h, GLsizei d, GLenum format, GLenum type, uint32_t *pixel)
{
    uint32_t bpp = gl_client_pixel_bytes(format, type);
    if (pixel)
        *pixel = bpp;
    if (!bpp || w <= 0 || h <= 0 || d <= 0)
        return 0;
    return (uint64_t)w * (uint64_t)h * (uint64_t)d * bpp;
}

/* tightly packed copies of the pixels as the pixel store of the context reads them now. */
static void pack_pixels(GlContext *ctx, GLsizei w, GLsizei h, GLsizei d, uint32_t bpp, const uint8_t *src, uint8_t *dst)
{
    uint64_t row_length = ctx->unpack_row_length > 0 ? (uint64_t)ctx->unpack_row_length : (uint64_t)w;
    uint64_t align = ctx->unpack_alignment > 0 ? (uint64_t)ctx->unpack_alignment : 1;
    uint64_t stride = (row_length * bpp + align - 1) / align * align;
    uint64_t rows = ctx->unpack_image_height > 0 ? (uint64_t)ctx->unpack_image_height : (uint64_t)h;
    src += (uint64_t)(ctx->unpack_skip_images > 0 ? ctx->unpack_skip_images : 0) * rows * stride +
           (uint64_t)(ctx->unpack_skip_rows > 0 ? ctx->unpack_skip_rows : 0) * stride +
           (uint64_t)(ctx->unpack_skip_pixels > 0 ? ctx->unpack_skip_pixels : 0) * bpp;
    for (GLsizei k = 0; k < d; k++)
        for (GLsizei r = 0; r < h; r++)
            memcpy(dst + ((uint64_t)k * (uint64_t)h + (uint64_t)r) * (uint64_t)w * bpp,
                   src + (uint64_t)k * rows * stride + (uint64_t)r * stride, (size_t)w * bpp);
}

/* the bits of a bitmap read with the pixel store, most significant bit first and rows of whole bytes. */
static void pack_bits(GlContext *ctx, GLsizei w, GLsizei h, const uint8_t *src, uint8_t *dst)
{
    uint64_t row_length = ctx->unpack_row_length > 0 ? (uint64_t)ctx->unpack_row_length : (uint64_t)w;
    uint64_t align = ctx->unpack_alignment > 0 ? (uint64_t)ctx->unpack_alignment : 1;
    uint64_t stride = ((row_length + 7) / 8 + align - 1) / align * align;
    src += (uint64_t)(ctx->unpack_skip_rows > 0 ? ctx->unpack_skip_rows : 0) * stride;
    uint64_t skip = ctx->unpack_skip_pixels > 0 ? (uint64_t)ctx->unpack_skip_pixels : 0;
    size_t out_stride = ((size_t)w + 7) / 8;
    for (GLsizei y = 0; y < h; y++) {
        const uint8_t *row = src + (uint64_t)y * stride;
        uint8_t *out = dst + (size_t)y * out_stride;
        for (GLsizei x = 0; x < w; x++) {
            uint64_t bit = skip + (uint64_t)x;
            unsigned shift = ctx->unpack_lsb_first ? (unsigned)(bit & 7) : 7 - (unsigned)(bit & 7);
            if ((row[bit >> 3] >> shift) & 1)
                out[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
        }
    }
}

static uint8_t *emit_image(GlContext *ctx, int fn, uint64_t data_bytes, const int64_t *args, int count, uint64_t *offset)
{
    size_t header = (IMG_FIRST_ARG + IMG_ARGS) * 8;
    uint8_t *p = gl_dl_emit(ctx, GL_DL_OP_IMAGE, header + gl_dl_blob((size_t)data_bytes));
    if (!p)
        return nullptr;
    gl_dl_put_i(p, 0, (uint64_t)fn);
    gl_dl_put_i(p, 1, ctx->unpack_swap_bytes ? 1 : 0);
    for (int i = 0; i < count; i++)
        gl_dl_put_i(p, IMG_FIRST_ARG + i, (uint64_t)args[i]);
    *offset = header;
    return p;
}

/* a command with an image: the pixels are read now and copied, and the command runs on the copy when the list does. */
static BOOL record_image(GlContext *ctx, int fn, GLsizei w, GLsizei h, GLsizei d, GLenum format, GLenum type,
                         const void *pixels, const int64_t *args, int count)
{
    uint32_t bpp;
    uint64_t bytes = packed_bytes(w, h, d, format, type, &bpp);
    const uint8_t *src = pixels && bytes ? unpack_source(ctx, pixels, bytes) : nullptr;
    uint64_t at;
    uint8_t *p = emit_image(ctx, fn, src ? bytes : 0, args, count, &at);
    if (p && src) {
        gl_dl_put_i(p, 2, at);
        pack_pixels(ctx, w, h, d, bpp, src, p + at);
    }
    return RUN(ctx);
}

static BOOL proxy_target(GLenum target)
{
    return target == GL_PROXY_TEXTURE_1D || target == GL_PROXY_TEXTURE_2D || target == GL_PROXY_TEXTURE_3D ||
           target == GL_PROXY_TEXTURE_CUBE_MAP;
}

BOOL gl_dl_custom_glTexImage1D(GlContext *ctx, GLenum target, GLint level, GLint internal, GLsizei w, GLint border,
                               GLenum format, GLenum type, const void *pixels)
{
    if (proxy_target(target))
        return TRUE;
    const int64_t a[] = {target, level, internal, w, border, format, type};
    return record_image(ctx, IMG_TEXIMAGE1D, w, 1, 1, format, type, pixels, a, 7);
}

BOOL gl_dl_custom_glTexImage2D(GlContext *ctx, GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                               GLint border, GLenum format, GLenum type, const void *pixels)
{
    if (proxy_target(target))
        return TRUE;
    const int64_t a[] = {target, level, internal, w, h, border, format, type};
    return record_image(ctx, IMG_TEXIMAGE2D, w, h, 1, format, type, pixels, a, 8);
}

BOOL gl_dl_custom_glTexImage3D(GlContext *ctx, GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d,
                               GLint border, GLenum format, GLenum type, const void *pixels)
{
    if (proxy_target(target))
        return TRUE;
    const int64_t a[] = {target, level, internal, w, h, d, border, format, type};
    return record_image(ctx, IMG_TEXIMAGE3D, w, h, d, format, type, pixels, a, 9);
}

BOOL gl_dl_custom_glTexSubImage1D(GlContext *ctx, GLenum target, GLint level, GLint x, GLsizei w, GLenum format,
                                  GLenum type, const void *pixels)
{
    const int64_t a[] = {target, level, x, w, format, type};
    return record_image(ctx, IMG_TEXSUBIMAGE1D, w, 1, 1, format, type, pixels, a, 6);
}

BOOL gl_dl_custom_glTexSubImage2D(GlContext *ctx, GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                  GLenum format, GLenum type, const void *pixels)
{
    const int64_t a[] = {target, level, x, y, w, h, format, type};
    return record_image(ctx, IMG_TEXSUBIMAGE2D, w, h, 1, format, type, pixels, a, 8);
}

BOOL gl_dl_custom_glTexSubImage3D(GlContext *ctx, GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w,
                                  GLsizei h, GLsizei d, GLenum format, GLenum type, const void *pixels)
{
    const int64_t a[] = {target, level, x, y, z, w, h, d, format, type};
    return record_image(ctx, IMG_TEXSUBIMAGE3D, w, h, d, format, type, pixels, a, 10);
}

BOOL gl_dl_custom_glDrawPixels(GlContext *ctx, GLsizei width, GLsizei height, GLenum format, GLenum type,
                               const GLvoid *pixels)
{
    const int64_t a[] = {width, height, format, type};
    return record_image(ctx, IMG_DRAWPIXELS, width, height, 1, format, type, pixels, a, 4);
}

static BOOL record_compressed(GlContext *ctx, int fn, GLsizei size, const void *data, const int64_t *args, int count)
{
    uint64_t bytes = size > 0 ? (uint64_t)size : 0;
    const uint8_t *src = data && bytes ? unpack_source(ctx, data, bytes) : nullptr;
    uint64_t at;
    uint8_t *p = emit_image(ctx, fn, src ? bytes : 0, args, count, &at);
    if (p && src) {
        gl_dl_put_i(p, 2, at);
        memcpy(p + at, src, (size_t)bytes);
    }
    return RUN(ctx);
}

BOOL gl_dl_custom_glCompressedTexImage2D(GlContext *ctx, GLenum target, GLint level, GLenum internal, GLsizei w,
                                         GLsizei h, GLint border, GLsizei size, const void *data)
{
    if (proxy_target(target))
        return TRUE;
    const int64_t a[] = {target, level, internal, w, h, border, size};
    return record_compressed(ctx, IMG_COMPRESSED2D, size, data, a, 7);
}

BOOL gl_dl_custom_glCompressedTexSubImage2D(GlContext *ctx, GLenum target, GLint level, GLint x, GLint y, GLsizei w,
                                            GLsizei h, GLenum format, GLsizei size, const void *data)
{
    const int64_t a[] = {target, level, x, y, w, h, format, size};
    return record_compressed(ctx, IMG_COMPRESSED_SUB2D, size, data, a, 8);
}

static BOOL record_bits(GlContext *ctx, int fn, GLsizei w, GLsizei h, const GLubyte *bits, const int64_t *args, int count)
{
    uint64_t bytes = w > 0 && h > 0 ? (uint64_t)(((uint64_t)w + 7) / 8) * (uint64_t)h : 0;
    uint64_t needed = bytes ? (((uint64_t)w + 7) / 8 + 3) * (uint64_t)h + 4096 : 0;
    /* the source is read the way the pixel store says, so the buffer is only checked for being there. */
    const uint8_t *src = bits && bytes ? unpack_source(ctx, bits, 1) : nullptr;
    (void)needed;
    uint64_t at;
    uint8_t *p = emit_image(ctx, fn, src ? bytes : 0, args, count, &at);
    if (p && src) {
        gl_dl_put_i(p, 2, at);
        pack_bits(ctx, w, h, src, p + at);
    }
    return RUN(ctx);
}

static int64_t float_bits(GLfloat f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

BOOL gl_dl_custom_glBitmap(GlContext *ctx, GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove,
                           GLfloat ymove, const GLubyte *bitmap)
{
    const int64_t a[] = {width, height, float_bits(xorig), float_bits(yorig), float_bits(xmove), float_bits(ymove)};
    return record_bits(ctx, IMG_BITMAP, width, height, bitmap, a, 6);
}

BOOL gl_dl_custom_glPolygonStipple(GlContext *ctx, const GLubyte *mask)
{
    return record_bits(ctx, IMG_STIPPLE, 32, 32, mask, nullptr, 0);
}

/* runs an image operation with the pixel store the copy was made for. */
static void run_image(const uint8_t *p)
{
    GlContext *ctx = gl_current();
    if (!ctx)
        return;
    int fn = (int)gl_dl_get_i(p, 0);
    uint64_t flags = gl_dl_get_i(p, 1), at = gl_dl_get_i(p, 2);
    const void *data = at ? p + at : nullptr;
    int64_t a[IMG_ARGS];
    for (int i = 0; i < IMG_ARGS; i++)
        a[i] = (int64_t)gl_dl_get_i(p, IMG_FIRST_ARG + i);

    GLint saved[8] = {ctx->unpack_alignment, ctx->unpack_row_length, ctx->unpack_skip_rows, ctx->unpack_skip_pixels,
                      ctx->unpack_image_height, ctx->unpack_skip_images, (GLint)ctx->pixel_unpack,
                      ctx->unpack_lsb_first | (ctx->unpack_swap_bytes << 1)};
    ctx->unpack_alignment = 1;
    ctx->unpack_row_length = ctx->unpack_skip_rows = ctx->unpack_skip_pixels = 0;
    ctx->unpack_image_height = ctx->unpack_skip_images = 0;
    ctx->pixel_unpack = 0;
    ctx->unpack_lsb_first = FALSE;
    ctx->unpack_swap_bytes = (flags & 1) != 0;
    switch (fn) {
    case IMG_TEXIMAGE1D:
        glTexImage1D((GLenum)a[0], (GLint)a[1], (GLint)a[2], (GLsizei)a[3], (GLint)a[4], (GLenum)a[5], (GLenum)a[6], data);
        break;
    case IMG_TEXIMAGE2D:
        glTexImage2D((GLenum)a[0], (GLint)a[1], (GLint)a[2], (GLsizei)a[3], (GLsizei)a[4], (GLint)a[5], (GLenum)a[6],
                     (GLenum)a[7], data);
        break;
    case IMG_TEXIMAGE3D:
        glTexImage3D((GLenum)a[0], (GLint)a[1], (GLint)a[2], (GLsizei)a[3], (GLsizei)a[4], (GLsizei)a[5], (GLint)a[6],
                     (GLenum)a[7], (GLenum)a[8], data);
        break;
    case IMG_TEXSUBIMAGE1D:
        glTexSubImage1D((GLenum)a[0], (GLint)a[1], (GLint)a[2], (GLsizei)a[3], (GLenum)a[4], (GLenum)a[5], data);
        break;
    case IMG_TEXSUBIMAGE2D:
        glTexSubImage2D((GLenum)a[0], (GLint)a[1], (GLint)a[2], (GLint)a[3], (GLsizei)a[4], (GLsizei)a[5], (GLenum)a[6],
                        (GLenum)a[7], data);
        break;
    case IMG_TEXSUBIMAGE3D:
        glTexSubImage3D((GLenum)a[0], (GLint)a[1], (GLint)a[2], (GLint)a[3], (GLint)a[4], (GLsizei)a[5], (GLsizei)a[6],
                        (GLsizei)a[7], (GLenum)a[8], (GLenum)a[9], data);
        break;
    case IMG_COMPRESSED2D:
        glCompressedTexImage2D((GLenum)a[0], (GLint)a[1], (GLenum)a[2], (GLsizei)a[3], (GLsizei)a[4], (GLint)a[5],
                               (GLsizei)a[6], data);
        break;
    case IMG_COMPRESSED_SUB2D:
        glCompressedTexSubImage2D((GLenum)a[0], (GLint)a[1], (GLint)a[2], (GLint)a[3], (GLsizei)a[4], (GLsizei)a[5],
                                  (GLenum)a[6], (GLsizei)a[7], data);
        break;
    case IMG_DRAWPIXELS:
        glDrawPixels((GLsizei)a[0], (GLsizei)a[1], (GLenum)a[2], (GLenum)a[3], data);
        break;
    case IMG_BITMAP: {
        float f[4];
        for (int i = 0; i < 4; i++) {
            uint32_t u = (uint32_t)a[2 + i];
            memcpy(&f[i], &u, 4);
        }
        glBitmap((GLsizei)a[0], (GLsizei)a[1], f[0], f[1], f[2], f[3], data);
        break;
    }
    case IMG_STIPPLE:
        glPolygonStipple(data);
        break;
    default:
        break;
    }
    ctx->unpack_alignment = saved[0];
    ctx->unpack_row_length = saved[1];
    ctx->unpack_skip_rows = saved[2];
    ctx->unpack_skip_pixels = saved[3];
    ctx->unpack_image_height = saved[4];
    ctx->unpack_skip_images = saved[5];
    ctx->pixel_unpack = (GLuint)saved[6];
    ctx->unpack_lsb_first = saved[7] & 1;
    ctx->unpack_swap_bytes = (saved[7] & 2) != 0;
}

/* ---------------------------------------------------------------- running a list */

static void apply_final(GlContext *ctx, const GlDlBatch *b)
{
    if (b->set & 1)
        gl_ff_color(ctx, b->final[0][0], b->final[0][1], b->final[0][2], b->final[0][3]);
    if (b->set & 2)
        gl_ff_normal(ctx, b->final[1][0], b->final[1][1], b->final[1][2]);
    if (b->set & 4)
        gl_ff_texcoord(ctx, 0, b->final[2][0], b->final[2][1], b->final[2][2], b->final[2][3]);
}

static void run_list(GlContext *ctx, const GlList *l)
{
    const uint8_t *p = l->ops, *end = l->ops + l->size;
    while (p < end) {
        OpHeader h;
        memcpy(&h, p, sizeof(h));
        const uint8_t *payload = p + sizeof(h);
        switch (h.op) {
        case GL_DL_OP_ERROR:
            gl_error(ctx, (GLenum)gl_dl_get_i(payload, 0));
            break;
        case GL_DL_OP_CALL:
            glCallList((GLuint)gl_dl_get_i(payload, 0));
            break;
        case GL_DL_OP_CALLS:
            glCallLists((GLsizei)(int64_t)gl_dl_get_i(payload, 0), (GLenum)gl_dl_get_i(payload, 1),
                        gl_dl_get_i(payload, 2) ? payload + 24 : nullptr);
            break;
        case GL_DL_OP_BATCH: {
            GlDlBatch *b;
            memcpy(&b, payload, sizeof(b));
            if (ctx->ff.in_begin) {
                gl_error(ctx, GL_INVALID_OPERATION);
            } else {
                gl_draw_batch(ctx, b);
                apply_final(ctx, b);
            }
            break;
        }
        case GL_DL_OP_VERTEX:
            gl_ff_vertex(ctx, gl_dl_get_f(payload, 0), gl_dl_get_f(payload, 1), gl_dl_get_f(payload, 2),
                         gl_dl_get_f(payload, 3));
            break;
        case GL_DL_OP_COLOR:
            gl_ff_color(ctx, gl_dl_get_f(payload, 0), gl_dl_get_f(payload, 1), gl_dl_get_f(payload, 2),
                        gl_dl_get_f(payload, 3));
            break;
        case GL_DL_OP_NORMAL:
            gl_ff_normal(ctx, gl_dl_get_f(payload, 0), gl_dl_get_f(payload, 1), gl_dl_get_f(payload, 2));
            break;
        case GL_DL_OP_TEXCOORD:
            gl_ff_texcoord(ctx, (GLuint)gl_dl_get_i(payload, 0), gl_dl_get_f(payload, 1), gl_dl_get_f(payload, 2),
                           gl_dl_get_f(payload, 3), gl_dl_get_f(payload, 4));
            break;
        case GL_DL_OP_EDGEFLAG:
            ctx->ff.edge_flag = (GLboolean)gl_dl_get_i(payload, 0);
            break;
        case GL_DL_OP_RASTERPOS:
            if (ctx->ff.in_begin)
                gl_error(ctx, GL_INVALID_OPERATION);
            else
                gl_raster_position(ctx, gl_dl_get_f(payload, 0), gl_dl_get_f(payload, 1), gl_dl_get_f(payload, 2),
                                   gl_dl_get_f(payload, 3));
            break;
        case GL_DL_OP_IMAGE:
            run_image(payload);
            break;
        case GL_DL_OP_POPATTRIB:
            glPopAttrib();
            break;
        default:
            if (h.op >= GL_DL_FIRST_GENERATED && h.op - GL_DL_FIRST_GENERATED < gl_dl_run_count)
                gl_dl_run[h.op - GL_DL_FIRST_GENERATED](payload);
            break;
        }
        p += h.size;
    }
}

static void call_list(GlContext *ctx, GLuint name)
{
    GlList *l = list_acquire(ctx->shared, name);
    if (!l)
        return;
    if (ctx->dl.depth < MAX_NESTING) {
        ctx->dl.depth++;
        run_list(ctx, l);
        ctx->dl.depth--;
    }
    list_release(l);
}

/* ---------------------------------------------------------------- the commands of lists */

GLAPI GLuint APIENTRY glGenLists(GLsizei range)
{
    GL_CONTEXT_OR_RETURN(0);
    if (range < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return 0;
    }
    if (!range)
        return 0;
    GlListTable *t = &ctx->shared->lists;
    gl_lock(&t->lock);
    uint64_t start = t->next ? t->next : 1;
    BOOL wrapped = FALSE;
    GLuint result = 0;
    for (;;) {
        if (start + (uint64_t)range - 1 > 0xFFFFFFFFull) {
            if (wrapped)
                break;
            wrapped = TRUE;
            start = 1;
            continue;
        }
        uint64_t taken = 0;
        for (GLsizei i = 0; i < range && !taken; i++)
            if (table_find(t, (GLuint)(start + (uint64_t)i)))
                taken = (uint64_t)i + 1;
        if (!taken) {
            result = (GLuint)start;
            break;
        }
        start += taken;
    }
    BOOL ok = result != 0;
    for (GLsizei i = 0; ok && i < range; i++) {
        GlList *l = gl_alloc(sizeof(GlList)), *old;
        if (l) {
            l->refs = 1;
            l->name = result + (GLuint)i;
        }
        if (!l || !table_set(t, result + (GLuint)i, l, &old)) {
            gl_free(l);
            ok = FALSE;
        }
    }
    if (ok)
        t->next = result + (GLuint)range;
    gl_unlock(&t->lock);
    return ok ? result : 0;
}

GLAPI GLboolean APIENTRY glIsList(GLuint list)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    GlListTable *t = &ctx->shared->lists;
    gl_lock(&t->lock);
    BOOL found = table_find(t, list) != nullptr;
    gl_unlock(&t->lock);
    return found ? GL_TRUE : GL_FALSE;
}

GLAPI void APIENTRY glDeleteLists(GLuint list, GLsizei range)
{
    GL_CONTEXT_OR_RETURN();
    if (range < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GlListTable *t = &ctx->shared->lists;
    uint64_t last = (uint64_t)list + (uint64_t)range; /* one past the range. */
    gl_lock(&t->lock);
    if ((uint64_t)range > t->count) {
        for (uint32_t i = 0; i < t->cap; i++) {
            GlListEntry *e = &t->entries[i];
            if (e->name && e->list && e->name >= list && (uint64_t)e->name < last) {
                GlList *old = e->list;
                e->list = nullptr;
                t->count--;
                list_release(old);
            }
        }
    } else {
        for (GLsizei i = 0; i < range; i++) {
            GlListEntry *e = table_find(t, list + (GLuint)i);
            if (!e)
                continue;
            GlList *old = e->list;
            e->list = nullptr;
            t->count--;
            list_release(old);
        }
    }
    gl_unlock(&t->lock);
}

GLAPI void APIENTRY glNewList(GLuint list, GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    if (list == 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (mode != GL_COMPILE && mode != GL_COMPILE_AND_EXECUTE) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (ctx->dl.mode) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    GlDlBuilder *b = gl_alloc(sizeof(GlDlBuilder));
    if (!b) {
        gl_error(ctx, GL_OUT_OF_MEMORY);
        return;
    }
    ctx->dl.b = b;
    ctx->dl.list = list;
    ctx->dl.mode = mode;
    ctx->dl.in_begin = FALSE;
}

GLAPI void APIENTRY glEndList(void)
{
    GL_CONTEXT_OR_RETURN();
    GlDlBuilder *b = ctx->dl.b;
    if (!ctx->dl.mode) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    GLuint name = ctx->dl.list;
    flush_pending(ctx);
    ctx->dl.mode = 0;
    ctx->dl.b = nullptr;
    ctx->dl.list = 0;
    ctx->dl.in_begin = FALSE;
    /* a glBegin without its glEnd is dropped. */
    GlList *l = b->failed ? nullptr : gl_alloc(sizeof(GlList));
    if (l) {
        l->name = name;
        l->refs = 1;
        l->ops = b->ops;
        l->size = b->size;
        l->batches = b->batches;
        l->batch_count = b->batch_count;
        b->ops = nullptr;
        b->batches = nullptr;
        b->batch_count = 0;
        if (!list_install(ctx->shared, name, l)) {
            list_free(l);
            l = nullptr;
        }
    }
    builder_free(b);
    if (!l)
        gl_error(ctx, GL_OUT_OF_MEMORY);
}

GLAPI void APIENTRY glListBase(GLuint base)
{
    GL_CONTEXT_OR_RETURN();
    ctx->dl.base = base;
}

GLAPI void APIENTRY glCallList(GLuint list)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (list == 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    call_list(ctx, list);
}

GLAPI void APIENTRY glCallLists(GLsizei n, GLenum type, const GLvoid *lists)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    if (n < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    size_t size = list_id_size(type);
    if (!size) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (!lists)
        return;
    const uint8_t *p = lists;
    for (GLsizei i = 0; i < n; i++, p += size) {
        GLuint offset;
        switch (type) {
        case GL_BYTE:
            offset = (GLuint)(GLint)*(const int8_t *)p;
            break;
        case GL_UNSIGNED_BYTE:
            offset = *p;
            break;
        case GL_SHORT: {
            int16_t v;
            memcpy(&v, p, 2);
            offset = (GLuint)(GLint)v;
            break;
        }
        case GL_UNSIGNED_SHORT: {
            uint16_t v;
            memcpy(&v, p, 2);
            offset = v;
            break;
        }
        case GL_INT:
        case GL_UNSIGNED_INT: {
            memcpy(&offset, p, 4);
            break;
        }
        case GL_FLOAT: {
            float v;
            memcpy(&v, p, 4);
            offset = (GLuint)(GLint)v;
            break;
        }
        default: {
            /* two, three and four bytes, the first one the most significant. */
            offset = 0;
            for (size_t k = 0; k < size; k++)
                offset = (offset << 8) | p[k];
            break;
        }
        }
        GLuint name = ctx->dl.base + offset;
        if (name)
            call_list(ctx, name);
    }
}
