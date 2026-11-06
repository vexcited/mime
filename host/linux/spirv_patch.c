/*
 * SPIR-V the host reads and rewrites for what GLSL cannot say: the execution modes of geometry and tessellation shaders
 * as OpenGL reports them, and the transform feedback decorations OpenGL puts on varyings by name.
 */
#include "spirv_patch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    OpEntryPoint = 15,
    OpExecutionMode = 16,
    OpCapability = 17,
    OpName = 5,
    OpMemberName = 6,
    OpTypeBool = 20,
    OpTypeInt = 21,
    OpTypeFloat = 22,
    OpTypeVector = 23,
    OpTypeMatrix = 24,
    OpTypeArray = 28,
    OpTypeStruct = 30,
    OpTypePointer = 32,
    OpConstant = 43,
    OpVariable = 59,
    OpDecorate = 71,
    OpMemberDecorate = 72,
    OpExecutionModeId = 331,
};

enum { DecBlock = 2, DecBuiltIn = 11, DecOffset = 35, DecXfbBuffer = 36, DecXfbStride = 37 };

typedef struct Module {
    const uint32_t *w;
    size_t n;
    uint32_t bound;
    const uint32_t **inst; /* the instruction defining each id, null when none. */
    char **name;
    const uint32_t **type_inst;
    uint8_t *block;
} Module;

static const char *literal(const uint32_t *w) { return (const char *)w; }

static bool module_open(Module *m, const uint32_t *w, size_t n)
{
    memset(m, 0, sizeof(*m));
    if (n < 5 || w[0] != 0x07230203)
        return false;
    m->w = w;
    m->n = n;
    m->bound = w[3];
    m->inst = calloc(m->bound + 1, sizeof(*m->inst));
    m->name = calloc(m->bound + 1, sizeof(*m->name));
    m->block = calloc(m->bound + 1, 1);
    for (size_t i = 5; i < n;) {
        uint32_t count = w[i] >> 16, op = w[i] & 0xFFFF;
        if (!count || i + count > n)
            return false;
        const uint32_t *x = w + i;
        switch (op) {
        case OpName:
            if (x[1] <= m->bound)
                m->name[x[1]] = strdup(literal(x + 2));
            break;
        case OpDecorate:
            if (x[1] <= m->bound && x[2] == DecBlock)
                m->block[x[1]] = 1;
            break;
        case OpTypeBool:
        case OpTypeInt:
        case OpTypeFloat:
        case OpTypeVector:
        case OpTypeMatrix:
        case OpTypeArray:
        case OpTypeStruct:
        case OpTypePointer:
            if (x[1] <= m->bound)
                m->inst[x[1]] = x;
            break;
        case OpConstant:
            if (x[2] <= m->bound)
                m->inst[x[2]] = x;
            break;
        case OpVariable:
            if (x[2] <= m->bound)
                m->inst[x[2]] = x;
            break;
        default:
            break;
        }
        i += count;
    }
    return true;
}

static void module_close(Module *m)
{
    for (uint32_t i = 0; i <= m->bound; i++)
        free(m->name[i]);
    free(m->name);
    free(m->inst);
    free(m->block);
}

static const char *member_name(const Module *m, uint32_t type, uint32_t index)
{
    for (size_t i = 5; i < m->n;) {
        uint32_t count = m->w[i] >> 16, op = m->w[i] & 0xFFFF;
        if (op == OpMemberName && m->w[i + 1] == type && m->w[i + 2] == index)
            return literal(m->w + i + 3);
        i += count;
    }
    return "";
}

static bool member_builtin(const Module *m, uint32_t type, uint32_t index)
{
    for (size_t i = 5; i < m->n;) {
        uint32_t count = m->w[i] >> 16, op = m->w[i] & 0xFFFF;
        if (op == OpMemberDecorate && m->w[i + 1] == type && m->w[i + 2] == index && m->w[i + 3] == DecBuiltIn)
            return true;
        i += count;
    }
    return false;
}

/* components of 4 bytes, the GL type of the element and the array size of a type. */
static bool measure(const Module *m, uint32_t id, uint32_t *bytes, uint32_t *gl_type, uint32_t *elements, uint32_t *align)
{
    const uint32_t *t = id <= m->bound ? m->inst[id] : nullptr;
    if (!t)
        return false;
    *elements = 1;
    *align = 4;
    switch (t[0] & 0xFFFF) {
    case OpTypeBool:
        *bytes = 4;
        *gl_type = 0x8B56;
        return true;
    case OpTypeInt:
        *bytes = t[2] / 8;
        *gl_type = t[3] ? 0x1404 : 0x1405;
        return true;
    case OpTypeFloat:
        *bytes = t[2] / 8;
        *gl_type = t[2] == 64 ? 0x140A : 0x1406;
        *align = t[2] == 64 ? 8 : 4;
        return true;
    case OpTypeVector: {
        uint32_t b, g, e, a;
        if (!measure(m, t[2], &b, &g, &e, &a))
            return false;
        *bytes = b * t[3];
        *align = a;
        switch (g) {
        case 0x1406:
            *gl_type = 0x8B50 + t[3] - 2;
            break;
        case 0x1404:
            *gl_type = 0x8B53 + t[3] - 2;
            break;
        case 0x1405:
            *gl_type = 0x8DC6 + t[3] - 2;
            break;
        case 0x140A:
            *gl_type = 0x8FFC + t[3] - 2;
            break;
        default:
            *gl_type = 0x8B57 + t[3] - 2;
            break;
        }
        return true;
    }
    case OpTypeMatrix: {
        const uint32_t *col = m->inst[t[2]];
        uint32_t b, g, e, a;
        if (!col || !measure(m, t[2], &b, &g, &e, &a))
            return false;
        static const uint32_t mats[3][3] = {{0x8B5A, 0x8B65, 0x8B66}, {0x8B67, 0x8B5B, 0x8B68}, {0x8B69, 0x8B6A, 0x8B5C}};
        uint32_t rows = col[3];
        *bytes = b * t[3];
        *align = a;
        *gl_type = t[3] >= 2 && t[3] <= 4 && rows >= 2 && rows <= 4 ? mats[t[3] - 2][rows - 2] : 0x1406;
        return true;
    }
    case OpTypeArray: {
        uint32_t b, g, e, a;
        const uint32_t *len = m->inst[t[3]];
        if (!len || !measure(m, t[2], &b, &g, &e, &a))
            return false;
        *bytes = b * len[3];
        *gl_type = g;
        *elements = len[3];
        *align = a;
        return true;
    }
    default:
        return false;
    }
}

void spirv_modes(const uint32_t *w, size_t n, MimeGpuStageInfo *info)
{
    for (size_t i = 5; i < n;) {
        uint32_t count = w[i] >> 16, op = w[i] & 0xFFFF;
        if (!count || i + count > n)
            break;
        if (op == OpExecutionMode) {
            uint32_t mode = w[i + 2], v = count > 3 ? w[i + 3] : 0;
            switch (mode) {
            case 0:
                info->geometry_invocations = v;
                break;
            case 1:
                info->evaluation_spacing = 0x0202;
                break;
            case 2:
                info->evaluation_spacing = 0x8E7C;
                break;
            case 3:
                info->evaluation_spacing = 0x8E7B;
                break;
            case 5:
                info->evaluation_ccw = 1;
                break;
            case 10:
                info->evaluation_point_mode = 1;
                break;
            case 19:
                info->geometry_input = 0x0000;
                break;
            case 20:
                info->geometry_input = 0x0001;
                break;
            case 21:
                info->geometry_input = 0x000A;
                break;
            case 22:
                info->geometry_input = 0x0004;
                info->evaluation_mode = 0x0004;
                break;
            case 23:
                info->geometry_input = 0x000C;
                break;
            case 24:
                info->evaluation_mode = 0x0007;
                break;
            case 25:
                info->evaluation_mode = 0x8E7A;
                break;
            case 26:
                info->geometry_vertices = info->control_vertices = v;
                break;
            case 27:
                info->geometry_output = 0x0000;
                break;
            case 28:
                info->geometry_output = 0x0003;
                break;
            case 29:
                info->geometry_output = 0x0005;
                break;
            default:
                break;
            }
        }
        i += count;
    }
}

/* names of the form "name", "name[0]" and "block.name". */
static bool name_matches(const char *want, const char *have)
{
    size_t n = strlen(have);
    if (!strncmp(want, have, n) && (!want[n] || !strcmp(want + n, "[0]")))
        return true;
    return false;
}

typedef struct Target {
    uint32_t id, member; /* the variable, or the block type and the member index. */
    bool is_member;
    uint32_t gl_type, elements, bytes, align;
} Target;

static uint32_t g_measure_failed;

static bool find_target(const Module *m, const char *want, Target *out)
{
    for (uint32_t id = 1; id <= m->bound; id++) {
        const uint32_t *v = m->inst[id];
        if (!v || (v[0] & 0xFFFF) != OpVariable || v[3] != 3)
            continue;
        const uint32_t *ptr = m->inst[v[1]];
        if (!ptr || (ptr[0] & 0xFFFF) != OpTypePointer)
            continue;
        uint32_t type = ptr[3];
        const uint32_t *t = m->inst[type];
        if (t && (t[0] & 0xFFFF) == OpTypeArray) {
            /* the per vertex arrays of a block belong to the stages before this one. */
            const uint32_t *e = m->inst[t[2]];
            if (e && (e[0] & 0xFFFF) == OpTypeStruct)
                continue;
        }
        if (t && (t[0] & 0xFFFF) == OpTypeStruct) {
            uint32_t members = (t[0] >> 16) - 2;
            for (uint32_t k = 0; k < members; k++) {
                char full[160];
                const char *mn = member_name(m, type, k);
                if (member_builtin(m, type, k))
                    snprintf(full, sizeof(full), "%s", mn);
                else
                    snprintf(full, sizeof(full), "%s.%s", m->name[type] ? m->name[type] : "", mn);
                if (!name_matches(want, full))
                    continue;
                out->id = type;
                out->member = k;
                out->is_member = true;
                return measure(m, t[2 + k], &out->bytes, &out->gl_type, &out->elements, &out->align);
            }
            continue;
        }
        if (m->name[id] && name_matches(want, m->name[id])) {
            out->id = id;
            out->is_member = false;
            bool ok = measure(m, type, &out->bytes, &out->gl_type, &out->elements, &out->align);
            if (!ok)
                g_measure_failed = t ? (t[0] & 0xFFFF) : 1;
            return ok;
        }
    }
    return false;
}

typedef struct Out {
    uint32_t *w;
    size_t n, cap;
} Out;

static void out_add(Out *o, const uint32_t *words, size_t count)
{
    if (o->n + count > o->cap) {
        o->cap = (o->n + count) * 2;
        o->w = realloc(o->w, o->cap * 4);
    }
    memcpy(o->w + o->n, words, count * 4);
    o->n += count;
}

bool spirv_feedback(const uint32_t *w, size_t n, const char *names, size_t names_len, bool separate, uint32_t **out_w,
                    size_t *out_n, SpirvFeedback *fb, char *error, size_t error_size)
{
    Module m;
    memset(fb, 0, sizeof(*fb));
    if (!module_open(&m, w, n)) {
        module_close(&m);
        snprintf(error, error_size, "transform feedback: bad module");
        return false;
    }
    Out decorations = {};
    uint32_t buffer = 0, offset = 0;
    bool ok = true;
    struct {
        Target t;
        uint32_t buffer, offset;
    } placed[SPIRV_MAX_FEEDBACK];
    uint32_t placed_count = 0;
    const char *p = names, *end = names + names_len;
    while (p < end && ok) {
        const char *name = p;
        p += strlen(p) + 1;
        MimeGpuFeedbackInfo *info = &fb->info[fb->count];
        if (fb->count == SPIRV_MAX_FEEDBACK) {
            snprintf(error, error_size, "too many transform feedback varyings");
            ok = false;
            break;
        }
        snprintf(fb->names[fb->count], sizeof(fb->names[0]), "%s", name);
        if (!strcmp(name, "gl_NextBuffer")) {
            if (!separate) {
                buffer++;
                offset = 0;
            }
            info->buffer = buffer;
            fb->count++;
            continue;
        }
        if (!strncmp(name, "gl_SkipComponents", 17) && name[17] >= '1' && name[17] <= '4') {
            uint32_t c = (uint32_t)(name[17] - '0');
            info->size = c;
            if (separate)
                buffer++;
            info->buffer = buffer;
            if (!separate) {
                offset += c * 4;
                fb->stride[buffer] = offset;
            }
            fb->count++;
            continue;
        }
        Target t;
        if (!find_target(&m, name, &t)) {
            char have[100] = "";
            for (uint32_t id = 1; id <= m.bound; id++) {
                const uint32_t *v = m.inst[id];
                if (v && (v[0] & 0xFFFF) == OpVariable && v[3] == 3 && m.name[id] && strlen(have) + strlen(m.name[id]) < 90)
                    snprintf(have + strlen(have), sizeof(have) - strlen(have), " %s", m.name[id]);
            }
            snprintf(error, error_size, "transform feedback varying %s is not an output of the last stage (%u), which has:%s", name,
                     g_measure_failed, have);
            ok = false;
            break;
        }
        if (separate)
            offset = 0;
        offset = (offset + t.align - 1) & ~(t.align - 1);
        if (buffer >= MIME_GPU_MAX_XFB_BUFFERS) {
            snprintf(error, error_size, "transform feedback needs more than %d buffers", MIME_GPU_MAX_XFB_BUFFERS);
            ok = false;
            break;
        }
        info->gl_type = t.gl_type;
        info->size = t.elements;
        info->buffer = buffer;
        placed[placed_count].t = t;
        placed[placed_count].buffer = buffer;
        placed[placed_count].offset = offset;
        placed_count++;
        offset += t.bytes;
        fb->stride[buffer] = offset;
        if (separate)
            buffer++;
        fb->count++;
    }
    fb->buffers = separate ? buffer : buffer + 1;
    if (ok) {
        for (uint32_t i = 0; i < placed_count; i++) {
            const Target *t = &placed[i].t;
            uint32_t d[3][5];
            uint32_t values[3] = {placed[i].buffer, fb->stride[placed[i].buffer], placed[i].offset};
            static const uint32_t kinds[3] = {DecXfbBuffer, DecXfbStride, DecOffset};
            for (int k = 0; k < 3; k++) {
                if (t->is_member) {
                    d[k][0] = (5u << 16) | OpMemberDecorate;
                    d[k][1] = t->id;
                    d[k][2] = t->member;
                    d[k][3] = kinds[k];
                    d[k][4] = values[k];
                    out_add(&decorations, d[k], 5);
                } else {
                    d[k][0] = (4u << 16) | OpDecorate;
                    d[k][1] = t->id;
                    d[k][2] = kinds[k];
                    d[k][3] = values[k];
                    out_add(&decorations, d[k], 4);
                }
            }
        }
        /* where the new instructions go. */
        size_t cap_at = 5, mode_at = 5, deco_at = n;
        uint32_t entry = 0;
        bool past_types = false;
        for (size_t i = 5; i < n;) {
            uint32_t count = w[i] >> 16, op = w[i] & 0xFFFF;
            if (op == OpCapability)
                cap_at = i + count;
            if (op == OpEntryPoint) {
                if (!entry)
                    entry = w[i + 2];
                mode_at = i + count;
            }
            if (op == OpExecutionMode || op == OpExecutionModeId)
                mode_at = i + count;
            if (!past_types && ((op >= 19 && op <= 39) || (op >= 41 && op <= 52) || op == 54 || op == 59)) {
                deco_at = i;
                past_types = true;
            }
            i += count;
        }
        Out r = {};
        uint32_t cap[2] = {(2u << 16) | OpCapability, 53};
        uint32_t mode[3] = {(3u << 16) | OpExecutionMode, entry, 11};
        out_add(&r, w, cap_at);
        out_add(&r, cap, 2);
        out_add(&r, w + cap_at, mode_at - cap_at);
        out_add(&r, mode, 3);
        out_add(&r, w + mode_at, deco_at - mode_at);
        out_add(&r, decorations.w, decorations.n);
        out_add(&r, w + deco_at, n - deco_at);
        *out_w = r.w;
        *out_n = r.n;
    }
    free(decorations.w);
    module_close(&m);
    return ok;
}

enum { DecLocation = 30, DecIndex = 32 };

/* the locations and indices glBindFragDataLocationIndexed gave to the outputs named in table ("name\0location\0index\0"). */
bool spirv_bind_outputs(const uint32_t *w, size_t n, const char *table, size_t table_len, uint32_t **out_w, size_t *out_n)
{
    Module m;
    if (!module_open(&m, w, n)) {
        module_close(&m);
        return false;
    }
    uint32_t *copy = malloc(n * 4);
    memcpy(copy, w, n * 4);
    Out decorations = {};
    const char *p = table, *end = table + table_len;
    while (p < end) {
        const char *name = p;
        p += strlen(p) + 1;
        if (p >= end)
            break;
        uint32_t location = (uint32_t)atoi(p);
        p += strlen(p) + 1;
        if (p >= end)
            break;
        uint32_t index = (uint32_t)atoi(p);
        p += strlen(p) + 1;
        uint32_t id = 0;
        for (uint32_t v = 1; v <= m.bound && !id; v++) {
            const uint32_t *x = m.inst[v];
            if (x && (x[0] & 0xFFFF) == OpVariable && x[3] == 3 && m.name[v] && !strcmp(m.name[v], name))
                id = v;
        }
        if (!id)
            continue;
        bool has_location = false, has_index = false;
        for (size_t i = 5; i < n;) {
            uint32_t count = w[i] >> 16, op = w[i] & 0xFFFF;
            if (op == OpDecorate && w[i + 1] == id) {
                if (w[i + 2] == DecLocation) {
                    copy[i + 3] = location;
                    has_location = true;
                } else if (w[i + 2] == DecIndex) {
                    copy[i + 3] = index;
                    has_index = true;
                }
            }
            i += count;
        }
        uint32_t d[4] = {(4u << 16) | OpDecorate, id, 0, 0};
        if (!has_location) {
            d[2] = DecLocation;
            d[3] = location;
            out_add(&decorations, d, 4);
        }
        if (!has_index && index) {
            d[2] = DecIndex;
            d[3] = index;
            out_add(&decorations, d, 4);
        }
    }
    size_t deco_at = n;
    for (size_t i = 5; i < n;) {
        uint32_t count = w[i] >> 16, op = w[i] & 0xFFFF;
        if ((op >= 19 && op <= 39) || (op >= 41 && op <= 52) || op == 54 || op == 59) {
            deco_at = i;
            break;
        }
        i += count;
    }
    Out r = {};
    out_add(&r, copy, deco_at);
    if (decorations.n)
        out_add(&r, decorations.w, decorations.n);
    out_add(&r, copy + deco_at, n - deco_at);
    free(copy);
    free(decorations.w);
    module_close(&m);
    *out_w = r.w;
    *out_n = r.n;
    return true;
}
