#include "parse.h"

typedef struct Specifiers Specifiers;
HlFunc *parse_function(Parser *p, Specifiers *s, HlToken *name, HlAttribute *attrs, uint32_t attr_count,
                       HlStruct *owner);
static bool contains_break(const HlStmt *s, int depth);
void parse_typedef_or_struct(Parser *p);

/* ------------------------------------------------------------------ tokens */

void syntax_error(Parser *p, HlToken *t)
{
    hl_error(p->h, t->loc, 3000, "syntax error: unexpected %s", t->kind == TK_EOF ? "end of file" :
             hl_sprintf(p->h, "token '%s'", hl_token_text(p->h, t)));
}

bool expect_punct(Parser *p, int c)
{
    if (accept_punct(p, c))
        return true;
    HlToken *t = peek(p);
    char want[4] = {(char)c, 0};
    if (c == P_COLONCOLON)
        want[0] = ':', want[1] = ':';
    hl_error(p->h, t->loc, 3000, "syntax error: unexpected %s, expected '%s'",
             t->kind == TK_EOF ? "end of file" : hl_sprintf(p->h, "token '%s'", hl_token_text(p->h, t)), want);
    return false;
}

void recover(Parser *p)
{
    int level = 0;
    while (peek(p)->kind != TK_EOF) {
        HlToken *t = peek(p);
        if (hl_is_punct(t, '{') || hl_is_punct(t, '(') || hl_is_punct(t, '['))
            level++;
        else if (hl_is_punct(t, '}') || hl_is_punct(t, ')') || hl_is_punct(t, ']')) {
            if (!level)
                return;
            level--;
            if (!level && hl_is_punct(t, '}')) {
                next(p);
                accept_punct(p, ';');
                return;
            }
        } else if (hl_is_punct(t, ';') && !level) {
            next(p);
            return;
        }
        next(p);
    }
}

/* skips a balanced group from the open bracket at the cursor. */
static void skip_group(Parser *p, int open, int close)
{
    int level = 0;
    while (peek(p)->kind != TK_EOF) {
        HlToken *t = next(p);
        if (hl_is_punct(t, open))
            level++;
        else if (hl_is_punct(t, close) && --level <= 0)
            return;
    }
}

/* ------------------------------------------------------------------ symbols */

static Bucket **bucket_of(Parser *p, const char *name)
{
    uint32_t at = (uint32_t)(((uintptr_t)name >> 4) * 2654435761u) & (p->bucket_count - 1);
    return &p->buckets[at];
}

Symbol *lookup(Parser *p, const char *name)
{
    for (Bucket *b = *bucket_of(p, name); b; b = b->next)
        if (b->name == name)
            return b->sym;
    return nullptr;
}

Symbol *declare(Parser *p, const char *name, uint8_t kind)
{
    Bucket **head = bucket_of(p, name), *b;
    for (b = *head; b; b = b->next)
        if (b->name == name)
            break;
    if (!b) {
        b = hl_alloc(p->h, sizeof(Bucket));
        b->name = name;
        b->next = *head;
        *head = b;
    }
    Symbol *s = hl_alloc(p->h, sizeof(Symbol));
    s->name = name;
    s->kind = kind;
    s->depth = p->depth;
    s->shadowed = b->sym;
    b->sym = s;
    s->scope_next = p->scope_syms;
    p->scope_syms = s;
    return s;
}

void push_scope(Parser *p) { p->depth++; }

void pop_scope(Parser *p)
{
    while (p->scope_syms && p->scope_syms->depth == p->depth) {
        Symbol *s = p->scope_syms;
        Bucket **head = bucket_of(p, s->name);
        for (Bucket *b = *head; b; b = b->next)
            if (b->name == s->name) {
                b->sym = s->shadowed;
                break;
            }
        p->scope_syms = s->scope_next;
    }
    p->depth--;
}

/* ------------------------------------------------------------------ types */

static const struct {
    const char *name;
    uint8_t base;
} scalar_names[] = {
    {"bool", BT_BOOL},         {"int", BT_INT},           {"uint", BT_UINT},           {"dword", BT_UINT},
    {"half", BT_HALF},         {"float", BT_FLOAT},       {"double", BT_DOUBLE},       {"min16float", BT_MIN16FLOAT},
    {"min10float", BT_MIN10FLOAT}, {"min16int", BT_MIN16INT}, {"min12int", BT_MIN12INT}, {"min16uint", BT_MIN16UINT},
};

static const struct {
    const char *name;
    uint8_t object;
    uint8_t modifiers;
} object_names[] = {
    {"SamplerState", OBJ_SAMPLER},
    {"sampler", OBJ_SAMPLER},
    {"sampler_state", OBJ_SAMPLER},
    {"SamplerComparisonState", OBJ_SAMPLER_COMPARISON},
    {"sampler1D", OBJ_SAMPLER1D},
    {"sampler2D", OBJ_SAMPLER2D},
    {"sampler3D", OBJ_SAMPLER3D},
    {"samplerCUBE", OBJ_SAMPLERCUBE},
    {"texture", OBJ_TEXTURE},
    {"Texture", OBJ_TEXTURE},
    {"texture1D", OBJ_TEXTURE1D},
    {"texture2D", OBJ_TEXTURE2D},
    {"texture3D", OBJ_TEXTURE3D},
    {"textureCUBE", OBJ_TEXTURECUBE},
    {"Buffer", OBJ_BUFFER},
    {"Texture1D", OBJ_TEXTURE1D},
    {"Texture1DArray", OBJ_TEXTURE1DARRAY},
    {"Texture2D", OBJ_TEXTURE2D},
    {"Texture2DArray", OBJ_TEXTURE2DARRAY},
    {"Texture2DMS", OBJ_TEXTURE2DMS},
    {"Texture2DMSArray", OBJ_TEXTURE2DMSARRAY},
    {"Texture3D", OBJ_TEXTURE3D},
    {"TextureCube", OBJ_TEXTURECUBE},
    {"TextureCubeArray", OBJ_TEXTURECUBEARRAY},
    {"StructuredBuffer", OBJ_STRUCTURED_BUFFER},
    {"ByteAddressBuffer", OBJ_BYTE_ADDRESS_BUFFER},
    {"RWBuffer", OBJ_RWBUFFER},
    {"RWTexture1D", OBJ_RWTEXTURE1D},
    {"RWTexture1DArray", OBJ_RWTEXTURE1DARRAY},
    {"RWTexture2D", OBJ_RWTEXTURE2D},
    {"RWTexture2DArray", OBJ_RWTEXTURE2DARRAY},
    {"RWTexture3D", OBJ_RWTEXTURE3D},
    {"RWStructuredBuffer", OBJ_RWSTRUCTURED_BUFFER},
    {"RWByteAddressBuffer", OBJ_RWBYTE_ADDRESS_BUFFER},
    {"AppendStructuredBuffer", OBJ_APPEND_STRUCTURED_BUFFER},
    {"ConsumeStructuredBuffer", OBJ_CONSUME_STRUCTURED_BUFFER},
    {"RasterizerOrderedBuffer", OBJ_RWBUFFER, TM_ROV},
    {"RasterizerOrderedTexture1D", OBJ_RWTEXTURE1D, TM_ROV},
    {"RasterizerOrderedTexture1DArray", OBJ_RWTEXTURE1DARRAY, TM_ROV},
    {"RasterizerOrderedTexture2D", OBJ_RWTEXTURE2D, TM_ROV},
    {"RasterizerOrderedTexture2DArray", OBJ_RWTEXTURE2DARRAY, TM_ROV},
    {"RasterizerOrderedTexture3D", OBJ_RWTEXTURE3D, TM_ROV},
    {"RasterizerOrderedStructuredBuffer", OBJ_RWSTRUCTURED_BUFFER, TM_ROV},
    {"RasterizerOrderedByteAddressBuffer", OBJ_RWBYTE_ADDRESS_BUFFER, TM_ROV},
    {"InputPatch", OBJ_INPUT_PATCH},
    {"OutputPatch", OBJ_OUTPUT_PATCH},
    {"PointStream", OBJ_POINT_STREAM},
    {"LineStream", OBJ_LINE_STREAM},
    {"TriangleStream", OBJ_TRIANGLE_STREAM},
    {"ConstantBuffer", OBJ_CONSTANT_BUFFER},
    {"TextureBuffer", OBJ_TEXTURE_BUFFER},
    {"BlendState", OBJ_STATE},
    {"DepthStencilState", OBJ_STATE},
    {"RasterizerState", OBJ_STATE},
    {"VertexShader", OBJ_STATE},
    {"PixelShader", OBJ_STATE},
    {"GeometryShader", OBJ_STATE},
    {"HullShader", OBJ_STATE},
    {"DomainShader", OBJ_STATE},
    {"ComputeShader", OBJ_STATE},
    {"StateBlock", OBJ_STATE},
    {"pixelshader", OBJ_STATE},
    {"vertexshader", OBJ_STATE},
    {"pixelfragment", OBJ_STATE},
    {"vertexfragment", OBJ_STATE},
    {"RenderTargetView", OBJ_STATE},
    {"DepthStencilView", OBJ_STATE},
};

HlType *builtin_type(Parser *p, const char *name, uint8_t *object)
{
    Hlsl *h = p->h;
    *object = OBJ_NONE;
    for (size_t i = 0; i < HL_ARRAY_SIZE(scalar_names); i++) {
        size_t n = __builtin_strlen(scalar_names[i].name);
        if (__builtin_strncmp(name, scalar_names[i].name, n))
            continue;
        const char *s = name + n;
        if (!*s)
            return hl_type_scalar(h, scalar_names[i].base);
        if (s[0] >= '1' && s[0] <= '4' && !s[1])
            return hl_type_vector(h, scalar_names[i].base, (uint8_t)(s[0] - '0'));
        if (s[0] >= '1' && s[0] <= '4' && s[1] == 'x' && s[2] >= '1' && s[2] <= '4' && !s[3])
            return hl_type_matrix(h, scalar_names[i].base, (uint8_t)(s[0] - '0'), (uint8_t)(s[2] - '0'));
    }
    if (name == p->k.k_vector)
        return hl_type_vector(h, BT_FLOAT, 4);
    if (name == p->k.k_matrix)
        return hl_type_matrix(h, BT_FLOAT, 4, 4);
    if (name == p->k.k_void)
        return hl_type_void(h);
    if (name == p->k.k_string)
        return hl_type_string(h);
    for (size_t i = 0; i < HL_ARRAY_SIZE(object_names); i++)
        if (hl_streq(name, object_names[i].name)) {
            *object = object_names[i].object;
            HlType *t = hl_type_object(h, object_names[i].object, nullptr, 0);
            t->modifiers = object_names[i].modifiers;
            return t;
        }
    return nullptr;
}

bool at_type(Parser *p)
{
    HlToken *t = peek(p);
    if (t->kind != TK_IDENT)
        return false;
    if (t->ident == p->k.k_unsigned || t->ident == p->k.k_struct)
        return true;
    uint8_t object;
    if (builtin_type(p, t->ident, &object))
        return true;
    Symbol *s = lookup(p, t->ident);
    return s && (s->kind == SYM_TYPE || s->kind == SYM_STRUCT);
}

static HlStruct *parse_struct_body(Parser *p, const char *name, HlLoc loc, bool klass, bool interface);

static HlType *object_template(Parser *p, HlType *t, HlToken *at)
{
    Hlsl *h = p->h;
    uint8_t object = t->object;
    HlType *elem = nullptr;
    uint32_t samples = 0;
    bool needs_count = object == OBJ_INPUT_PATCH || object == OBJ_OUTPUT_PATCH;
    bool needs_type = object == OBJ_STRUCTURED_BUFFER || object == OBJ_RWSTRUCTURED_BUFFER ||
                      object == OBJ_APPEND_STRUCTURED_BUFFER || object == OBJ_CONSUME_STRUCTURED_BUFFER ||
                      needs_count || object == OBJ_POINT_STREAM || object == OBJ_LINE_STREAM ||
                      object == OBJ_TRIANGLE_STREAM || object == OBJ_CONSTANT_BUFFER || object == OBJ_TEXTURE_BUFFER;
    if (accept_punct(p, '<')) {
        uint8_t modifiers = 0;
        for (;;) {
            if (accept_ident(p, p->k.k_snorm))
                modifiers |= TM_SNORM;
            else if (accept_ident(p, p->k.k_unorm))
                modifiers |= TM_UNORM;
            else
                break;
        }
        elem = parse_type(p, true);
        if (!elem)
            return hl_type_error(h);
        if (modifiers)
            elem = hl_type_with_base(h, elem, elem->base), elem->modifiers |= 0;
        if (accept_punct(p, ',')) {
            HlExpr *n = parse_template_arg(p);
            if (!const_uint(p, n, &samples, "template argument"))
                samples = 1;
        }
        expect_punct(p, '>');
    } else if (needs_type) {
        hl_error(h, at->loc, 3000, "syntax error: '%s' needs a template argument", hl_object_name(object));
        return hl_type_error(h);
    }
    if (needs_count && !samples) {
        hl_error(h, at->loc, 3000, "syntax error: '%s' needs a point count", hl_object_name(object));
        return hl_type_error(h);
    }
    if (!elem && object != OBJ_BYTE_ADDRESS_BUFFER && object != OBJ_RWBYTE_ADDRESS_BUFFER &&
        !hl_object_is_sampler(object) && object != OBJ_STATE)
        elem = hl_type_vector(h, BT_FLOAT, 4);
    if (elem && elem->kind != TY_ERROR && (hl_object_is_texture(object) || object == OBJ_RWBUFFER ||
                                           (object >= OBJ_RWTEXTURE1D && object <= OBJ_RWTEXTURE3D))) {
        if (!hl_type_numeric_p(elem) || elem->kind == TY_MATRIX || hl_type_components(elem) > 4) {
            if (object != OBJ_STRUCTURED_BUFFER) {
                hl_error(h, at->loc, 3000, "syntax error: '%s' cannot hold '%s'", hl_object_name(object),
                         hl_type_name(h, elem));
                return hl_type_error(h);
            }
        }
    }
    HlType *r = hl_type_object(h, object, elem, samples);
    r->modifiers = t->modifiers;
    return r;
}

HlType *parse_type(Parser *p, bool required)
{
    Hlsl *h = p->h;
    HlToken *t = peek(p);
    if (t->kind != TK_IDENT) {
        if (required)
            syntax_error(p, t);
        return required ? hl_type_error(h) : nullptr;
    }
    if (t->ident == p->k.k_unsigned) {
        next(p);
        HlToken *u = peek(p);
        if (u->kind == TK_IDENT && (hl_streq(u->ident, "int") || hl_streq(u->ident, "dword")))
            next(p);
        return hl_type_scalar(h, BT_UINT);
    }
    if (t->ident == p->k.k_struct) {
        next(p);
        const char *name = nullptr;
        HlLoc loc = t->loc;
        if (peek(p)->kind == TK_IDENT)
            name = next(p)->ident;
        if (!at_punct(p, '{') && !at_punct(p, ':')) {
            Symbol *s = name ? lookup(p, name) : nullptr;
            if (s && s->kind == SYM_STRUCT)
                return s->type;
            hl_error(h, loc, 3000, "syntax error: unknown struct '%s'", name ? name : "");
            return hl_type_error(h);
        }
        HlStruct *st = parse_struct_body(p, name, loc, false, false);
        HlType *type = hl_type_struct(h, st);
        if (name) {
            Symbol *s = declare(p, name, SYM_STRUCT);
            s->type = type;
        }
        return type;
    }
    uint8_t object;
    HlType *b = builtin_type(p, t->ident, &object);
    if (b) {
        next(p);
        if (t->ident == p->k.k_vector && accept_punct(p, '<')) {
            HlType *e = parse_type(p, true);
            uint32_t n = 4;
            if (expect_punct(p, ',')) {
                HlExpr *x = parse_template_arg(p);
                const_uint(p, x, &n, "vector size");
            }
            expect_punct(p, '>');
            if (e->kind != TY_SCALAR || n < 1 || n > 4) {
                hl_error(h, t->loc, 3052, "vector dimension must be between 1 and 4");
                return hl_type_error(h);
            }
            return hl_type_vector(h, e->base, (uint8_t)n);
        }
        if (t->ident == p->k.k_matrix && accept_punct(p, '<')) {
            HlType *e = parse_type(p, true);
            uint32_t r = 4, c = 4;
            if (expect_punct(p, ','))
                const_uint(p, parse_template_arg(p), &r, "matrix size");
            if (expect_punct(p, ','))
                const_uint(p, parse_template_arg(p), &c, "matrix size");
            expect_punct(p, '>');
            if (e->kind != TY_SCALAR || r < 1 || r > 4 || c < 1 || c > 4) {
                hl_error(h, t->loc, 3053, "matrix dimensions must be between 1 and 4");
                return hl_type_error(h);
            }
            return hl_type_matrix(h, e->base, (uint8_t)r, (uint8_t)c);
        }
        if (b->kind == TY_OBJECT)
            return object_template(p, b, t);
        return b;
    }
    Symbol *s = lookup(p, t->ident);
    if (s && (s->kind == SYM_TYPE || s->kind == SYM_STRUCT)) {
        next(p);
        return s->type;
    }
    if (required) {
        hl_error(h, t->loc, 3000, "syntax error: unexpected token '%s', expected a type", hl_token_text(h, t));
        return hl_type_error(h);
    }
    return nullptr;
}

/* ------------------------------------------------------------------ attributes */

void parse_attributes(Parser *p, HlAttribute **attrs, uint32_t *count)
{
    uint32_t capacity = *count;
    while (at_punct(p, '[')) {
        HlToken *open = next(p);
        HlToken *name = next(p);
        if (name->kind != TK_IDENT) {
            syntax_error(p, name);
            recover(p);
            return;
        }
        HlAttribute a = {.name = name->ident, .loc = open->loc};
        if (accept_punct(p, '(')) {
            uint32_t arg_capacity = 0;
            while (!at_punct(p, ')') && peek(p)->kind != TK_EOF) {
                HlExpr *e = parse_assignment(p);
                HL_PUSH(p->h, a.args, a.arg_count, arg_capacity, e);
                if (!accept_punct(p, ','))
                    break;
            }
            expect_punct(p, ')');
        }
        expect_punct(p, ']');
        HL_PUSH(p->h, *attrs, *count, capacity, a);
    }
}

/* ------------------------------------------------------------------ declarations */

struct Specifiers {
    uint32_t storage;
    uint32_t interp;
    uint8_t modifiers;
    HlType *type;
    HlLoc loc;
};

/* storage, interpolation and type modifiers; true when one was read. */
static bool parse_modifier(Parser *p, Specifiers *s)
{
    HlToken *t = peek(p);
    if (t->kind != TK_IDENT)
        return false;
    const char *k = t->ident;
    static const struct {
        uint32_t offset;
        uint32_t storage, interp, modifiers;
        bool contextual;
    } table[] = {
#define K(name) offsetof(Parser, k.k_##name)
        {K(static), ST_STATIC},
        {K(const), ST_CONST},
        {K(uniform), ST_UNIFORM},
        {K(extern), ST_EXTERN},
        {K(shared), ST_SHARED},
        {K(groupshared), ST_GROUPSHARED},
        {K(volatile), ST_VOLATILE},
        {K(precise), ST_PRECISE},
        {K(inline), ST_INLINE},
        {K(in), ST_IN},
        {K(out), ST_OUT},
        {K(inout), ST_IN | ST_OUT},
        {K(nointerpolation), 0, IM_NOINTERPOLATION},
        {K(linear), 0, IM_LINEAR, 0, true},
        {K(centroid), 0, IM_CENTROID, 0, true},
        {K(noperspective), 0, IM_NOPERSPECTIVE},
        {K(sample), 0, IM_SAMPLE, 0, true},
        {K(row_major), 0, 0, TM_ROW_MAJOR},
        {K(column_major), 0, 0, TM_COLUMN_MAJOR},
        {K(snorm), 0, 0, TM_SNORM},
        {K(unorm), 0, 0, TM_UNORM},
        {K(globallycoherent), 0, 0, TM_GLOBALLY_COHERENT},
        {K(point), ST_POINT, 0, 0, true},
        {K(line), ST_LINE, 0, 0, true},
        {K(triangle), ST_TRIANGLE, 0, 0, true},
        {K(lineadj), ST_LINEADJ, 0, 0, true},
        {K(triangleadj), ST_TRIANGLEADJ, 0, 0, true},
        {K(indices), ST_INDICES, 0, 0, true},
        {K(vertices), ST_VERTICES, 0, 0, true},
        {K(primitives), ST_PRIMITIVES, 0, 0, true},
        {K(payload), ST_PAYLOAD, 0, 0, true},
#undef K
    };
    for (size_t i = 0; i < HL_ARRAY_SIZE(table); i++) {
        if (*(const char **)((const char *)p + table[i].offset) != k)
            continue;
        if (table[i].contextual) {
            /* "sample" and the primitive names are identifiers unless a type or modifier follows. */
            HlToken *n = peek_at(p, 1);
            if (n->kind != TK_IDENT)
                return false;
            uint32_t save = p->pos;
            p->pos++;
            bool type_follows = at_type(p);
            p->pos = save;
            if (!type_follows) {
                bool modifier_follows = false;
                for (size_t j = 0; j < HL_ARRAY_SIZE(table); j++)
                    if (*(const char **)((const char *)p + table[j].offset) == n->ident)
                        modifier_follows = true;
                if (!modifier_follows)
                    return false;
            }
        }
        next(p);
        s->storage |= table[i].storage;
        s->interp |= table[i].interp;
        s->modifiers |= (uint8_t)table[i].modifiers;
        return true;
    }
    return false;
}

static void parse_specifiers(Parser *p, Specifiers *s)
{
    s->loc = peek(p)->loc;
    while (parse_modifier(p, s))
        ;
    s->type = parse_type(p, true);
    while (parse_modifier(p, s))
        ;
}

static HlType *apply_modifiers(Parser *p, HlType *t, uint8_t modifiers)
{
    if (t->kind == TY_ARRAY)
        return hl_type_array(p->h, apply_modifiers(p, t->elem, modifiers), t->count);
    if (t->kind == TY_MATRIX && (modifiers & (TM_ROW_MAJOR | TM_COLUMN_MAJOR)))
        return hl_type_majored(p->h, t, modifiers);
    if (t->kind == TY_OBJECT && (modifiers & TM_GLOBALLY_COHERENT)) {
        HlType *c = hl_alloc(p->h, sizeof(HlType));
        *c = *t;
        c->modifiers |= TM_GLOBALLY_COHERENT;
        return c;
    }
    return t;
}

/* matrices without row_major or column_major take the default in force. */
static HlType *default_major(Parser *p, HlType *t)
{
    if (t->kind == TY_ARRAY) {
        HlType *e = default_major(p, t->elem);
        return e == t->elem ? t : hl_type_array(p->h, e, t->count);
    }
    if (t->kind == TY_MATRIX && !(t->modifiers & (TM_ROW_MAJOR | TM_COLUMN_MAJOR)))
        return hl_type_majored(p->h, t, p->pack_default);
    return t;
}

static HlType *parse_array_dims(Parser *p, HlType *t, bool allow_unsized)
{
    Hlsl *h = p->h;
    uint32_t dims[8], count = 0;
    while (at_punct(p, '[')) {
        HlToken *open = next(p);
        uint32_t n = 0;
        if (at_punct(p, ']') && allow_unsized) {
            n = 0;
        } else {
            HlExpr *e = parse_expr(p);
            if (const_uint(p, e, &n, "array dimension") && (n < 1 || n > 65536)) {
                hl_error(h, open->loc, 3059, "array dimension must be between 1 and 65536");
                n = 1;
            }
        }
        expect_punct(p, ']');
        if (count < 8)
            dims[count++] = n;
    }
    for (uint32_t i = count; i > 0; i--)
        t = hl_type_array(h, t, dims[i - 1]);
    return t;
}

static void parse_register(Parser *p, HlRegister **regs, uint32_t *count, HlLoc loc)
{
    Hlsl *h = p->h;
    expect_punct(p, '(');
    HlRegister r = {.loc = loc};
    HlToken *t = next(p);
    /* register(ps_5_0, t0) names a profile first. */
    if (t->kind == TK_IDENT && hl_is_punct(peek(p), ',') && peek_at(p, 1)->kind == TK_IDENT &&
        hl_strnicmp(peek_at(p, 1)->ident, "space", 5)) {
        r.profile = t->ident;
        next(p);
        t = next(p);
    }
    if (t->kind == TK_IDENT && t->length >= 1) {
        const char *s = t->ident;
        r.kind = (char)(s[0] | 0x20);
        uint32_t n = 0;
        bool digits = false;
        for (s++; *s >= '0' && *s <= '9'; s++)
            n = n * 10 + (uint32_t)(*s - '0'), digits = true;
        if (*s || !digits) {
            if (hl_strnicmp(t->ident, "space", 5) == 0)
                goto space;
            hl_error(h, t->loc, 3000, "syntax error: invalid register '%s'", t->ident);
        }
        r.index = n;
        if (accept_punct(p, '[')) {
            uint32_t extra = 0;
            const_uint(p, parse_expr(p), &extra, "register offset");
            r.index += extra;
            expect_punct(p, ']');
        }
    } else {
        syntax_error(p, t);
    }
    if (accept_punct(p, ',')) {
        t = next(p);
    space:
        if (t->kind == TK_IDENT && !hl_strnicmp(t->ident, "space", 5)) {
            uint32_t n = 0;
            const char *s = t->ident + 5;
            for (; *s >= '0' && *s <= '9'; s++)
                n = n * 10 + (uint32_t)(*s - '0');
            r.space = n;
            r.has_space = true;
        } else {
            syntax_error(p, t);
        }
    }
    expect_punct(p, ')');
    uint32_t capacity = *count;
    HL_PUSH(h, *regs, *count, capacity, r);
}

static void parse_packoffset(Parser *p, HlVar *v)
{
    Hlsl *h = p->h;
    expect_punct(p, '(');
    HlToken *t = next(p);
    if (t->kind != TK_IDENT || (t->ident[0] | 0x20) != 'c') {
        syntax_error(p, t);
        recover(p);
        return;
    }
    uint32_t n = 0;
    const char *s = t->ident + 1;
    for (; *s >= '0' && *s <= '9'; s++)
        n = n * 10 + (uint32_t)(*s - '0');
    uint32_t comp = 0;
    if (*s) {
        hl_error(h, t->loc, 3000, "syntax error: invalid packoffset '%s'", t->ident);
    } else if (accept_punct(p, '.')) {
        HlToken *c = next(p);
        if (c->kind == TK_IDENT && c->length == 1) {
            const char *xyzw = "xyzw", *rgba = "rgba";
            for (uint32_t i = 0; i < 4; i++)
                if (c->ident[0] == xyzw[i] || c->ident[0] == rgba[i])
                    comp = i;
        } else {
            syntax_error(p, c);
        }
    }
    expect_punct(p, ')');
    v->has_packoffset = true;
    v->packoffset = n * 4 + comp;
}

/* ": SEMANTIC", ": register(...)" and ": packoffset(...)" after a declarator. */
static void parse_semantics(Parser *p, HlVar *v, const char **semantic, HlRegister **regs, uint32_t *reg_count)
{
    while (at_punct(p, ':')) {
        HlToken *colon = next(p);
        HlToken *t = peek(p);
        if (t->kind != TK_IDENT) {
            syntax_error(p, t);
            return;
        }
        next(p);
        if (t->ident == p->k.k_register) {
            if (regs)
                parse_register(p, regs, reg_count, colon->loc);
            else
                skip_group(p, '(', ')');
        } else if (t->ident == p->k.k_packoffset) {
            if (v)
                parse_packoffset(p, v);
            else
                skip_group(p, '(', ')');
        } else if (semantic) {
            *semantic = t->ident;
        }
    }
}

static void skip_annotations(Parser *p)
{
    if (at_punct(p, '<'))
        skip_group(p, '<', '>');
}

/* sampler_state { ... } and state blocks in effects. */
static void skip_state_block(Parser *p)
{
    if (at_punct(p, '{'))
        skip_group(p, '{', '}');
}

static HlVar *new_var(Parser *p, const char *name, HlType *type, HlLoc loc)
{
    HlVar *v = hl_alloc(p->h, sizeof(HlVar));
    v->name = name;
    v->type = type;
    v->loc = loc;
    v->id = ++p->prog->var_ids;
    return v;
}

static void redefinition(Parser *p, const char *name, HlLoc loc)
{
    hl_error(p->h, loc, 3003, "redefinition of '%s'", name);
}

static bool can_declare(Parser *p, const char *name, HlLoc loc, bool shadow_ok)
{
    Symbol *s = lookup(p, name);
    if (s && s->depth == p->depth && !shadow_ok) {
        if (s->kind == SYM_FUNC && p->depth == 0)
            ;
        redefinition(p, name, loc);
        return false;
    }
    return true;
}

static HlBuffer *globals_buffer(Parser *p)
{
    HlProgram *prog = p->prog;
    if (!prog->globals_buffer) {
        HlBuffer *b = hl_alloc(p->h, sizeof(HlBuffer));
        b->name = "$Globals";
        prog->globals_buffer = b;
        HL_PUSH(p->h, prog->buffers, prog->buffer_count, prog->buffer_capacity, b);
    }
    return prog->globals_buffer;
}

static void add_to_buffer(Parser *p, HlBuffer *b, HlVar *v)
{
    v->buffer = b;
    HL_PUSH(p->h, b->vars, b->var_count, b->var_capacity, v);
}

/* the declarators after the specifiers, at global scope, in a function or a cbuffer. */
static void parse_declarators(Parser *p, Specifiers *s, HlToken *first_name, HlBuffer *buffer, HlStmt *decl)
{
    Hlsl *h = p->h;
    bool global = p->depth == 0;
    HlToken *name = first_name;
    for (;;) {
        if (!name) {
            name = next(p);
            if (name->kind != TK_IDENT) {
                syntax_error(p, name);
                recover(p);
                return;
            }
        }
        HlType *type = apply_modifiers(p, parse_array_dims(p, s->type, true), s->modifiers);
        HlVar *v = new_var(p, name->ident, type, name->loc);
        v->storage = s->storage;
        v->interp = s->interp;
        v->global = global || buffer;
        parse_semantics(p, v, &v->semantic, &v->registers, &v->register_count);
        skip_annotations(p);
        if (accept_punct(p, '=')) {
            HlLoc loc = peek(p)->loc;
            if (at_ident(p, p->k.k_sampler_state)) {
                next(p);
                skip_state_block(p);
            } else if (at_ident(p, p->k.k_compile)) {
                while (!at_punct(p, ';') && !at_punct(p, ',') && peek(p)->kind != TK_EOF)
                    next(p);
            } else if (type->kind == TY_OBJECT && type->object == OBJ_STATE) {
                if (at_punct(p, '{'))
                    skip_group(p, '{', '}');
                else
                    while (!at_punct(p, ';') && !at_punct(p, ',') && peek(p)->kind != TK_EOF)
                        next(p);
            } else {
                HlExpr *init = at_punct(p, '{') ? init_list(p, loc) : parse_assignment(p);
                v->init = check_initializer(p, init, &v->type, loc);
                type = v->type;
            }
        } else if (at_punct(p, '{') && (type->kind == TY_OBJECT || type->kind == TY_ARRAY)) {
            skip_state_block(p);
        }
        if (type->kind == TY_ARRAY && !type->count) {
            hl_error(h, name->loc, 3074, "'%s': implicit array missing initial value", v->name);
            v->type = type = hl_type_array(h, type->elem, 1);
        }
        if (type->kind == TY_VOID) {
            hl_error(h, name->loc, 3038, "'%s': variables cannot be declared 'void'", v->name);
            v->type = hl_type_error(h);
        }
        if (global || buffer)
            v->type = default_major(p, v->type);
        bool is_object = hl_type_has_object(v->type);
        if (buffer) {
            if (is_object) {
                /* objects in a cbuffer are globals of their own, as fxc takes them. */
                v->global = true;
                HL_PUSH(h, p->prog->globals, p->prog->global_count, p->prog->global_capacity, v);
            } else {
                add_to_buffer(p, buffer, v);
            }
        } else if (global) {
            if ((v->storage & ST_STATIC) || (v->storage & ST_GROUPSHARED) || is_object || v->type->kind == TY_STRING ||
                v->type->kind == TY_ERROR) {
            } else {
                if ((v->storage & ST_CONST) && v->init)
                    hl_warning(h, name->loc, 3207, "Initializer used on a global 'const' variable. This requires setting an external constant. If a literal is desired, use 'static const' instead.");
                v->storage |= ST_UNIFORM;
                add_to_buffer(p, globals_buffer(p), v);
            }
            HL_PUSH(h, p->prog->globals, p->prog->global_count, p->prog->global_capacity, v);
        } else {
            if (v->storage & (ST_GROUPSHARED | ST_UNIFORM | ST_EXTERN))
                hl_error(h, name->loc, 3047, "'%s': local variables cannot be declared 'uniform', 'extern' or 'groupshared'", v->name);
            if (v->type->kind == TY_OBJECT && !v->init && v->type->object != OBJ_STATE)
                ;
        }
        if ((v->storage & ST_CONST) && !v->init && !global && !buffer && !v->param)
            hl_error(h, name->loc, 3012, "'%s': missing initial value", v->name);
        if (can_declare(p, v->name, name->loc, decl && decl->kind == S_FOR)) {
            Symbol *sym = declare(p, v->name, SYM_VAR);
            sym->var = v;
        }
        if (decl) {
            uint32_t capacity = decl->var_count;
            HL_PUSH(h, decl->vars, decl->var_count, capacity, v);
        }
        name = nullptr;
        if (!accept_punct(p, ','))
            break;
    }
    expect_punct(p, ';');
}

static HlStruct *parse_struct_body(Parser *p, const char *name, HlLoc loc, bool klass, bool interface)
{
    Hlsl *h = p->h;
    HlStruct *st = hl_alloc(h, sizeof(HlStruct));
    st->name = name;
    st->loc = loc;
    st->klass = klass;
    st->interface = interface;
    if (accept_punct(p, ':')) {
        do {
            HlToken *b = next(p);
            Symbol *s = b->kind == TK_IDENT ? lookup(p, b->ident) : nullptr;
            if (!s || s->kind != SYM_STRUCT) {
                hl_error(h, b->loc, 3000, "syntax error: unknown base type '%s'", hl_token_text(h, b));
                continue;
            }
            if (s->type->st->interface)
                continue;
            st->base = s->type->st;
        } while (accept_punct(p, ','));
    }
    if (!expect_punct(p, '{')) {
        recover(p);
        return st;
    }
    /* a struct's own name is a type inside it, for methods. */
    HlStruct *outer = p->klass;
    if (name) {
        Symbol *s = declare(p, name, SYM_STRUCT);
        s->type = hl_type_struct(h, st);
    }
    p->klass = st;
    while (!at_punct(p, '}') && peek(p)->kind != TK_EOF) {
        if (accept_punct(p, ';'))
            continue;
        Specifiers s = {};
        HlAttribute *attrs = nullptr;
        uint32_t attr_count = 0;
        parse_attributes(p, &attrs, &attr_count);
        parse_specifiers(p, &s);
        if (s.type->kind == TY_ERROR) {
            recover(p);
            continue;
        }
        for (;;) {
            HlToken *fname = next(p);
            if (fname->kind != TK_IDENT) {
                syntax_error(p, fname);
                recover(p);
                break;
            }
            if (at_punct(p, '(')) {
                /* a method. */
                HlFunc *f = parse_function(p, &s, fname, attrs, attr_count, st);
                if (f)
                    HL_PUSH(h, st->methods, st->method_count, st->method_capacity, f);
                break;
            }
            HlType *type = apply_modifiers(p, parse_array_dims(p, s.type, false), s.modifiers);
            HlField f = {.name = fname->ident, .type = type, .interp = s.interp, .loc = fname->loc};
            parse_semantics(p, nullptr, &f.semantic, nullptr, nullptr);
            skip_annotations(p);
            if (type->kind == TY_VOID)
                hl_error(h, fname->loc, 3038, "'%s': variables cannot be declared 'void'", f.name);
            for (uint32_t i = 0; i < st->field_count; i++)
                if (st->fields[i].name == f.name)
                    redefinition(p, f.name, fname->loc);
            HL_PUSH(h, st->fields, st->field_count, st->field_capacity, f);
            if (accept_punct(p, ','))
                continue;
            expect_punct(p, ';');
            break;
        }
    }
    expect_punct(p, '}');
    p->klass = outer;
    HL_PUSH(h, p->prog->structs, p->prog->struct_count, p->prog->struct_capacity, st);
    return st;
}

static void parse_cbuffer(Parser *p, bool tbuffer)
{
    Hlsl *h = p->h;
    HlToken *kw = next(p);
    HlToken *name = next(p);
    if (name->kind != TK_IDENT) {
        syntax_error(p, name);
        recover(p);
        return;
    }
    HlBuffer *b = hl_alloc(h, sizeof(HlBuffer));
    b->name = name->ident;
    b->tbuffer = tbuffer;
    b->loc = kw->loc;
    parse_semantics(p, nullptr, nullptr, &b->registers, &b->register_count);
    if (!expect_punct(p, '{')) {
        recover(p);
        return;
    }
    for (uint32_t i = 0; i < p->prog->buffer_count; i++)
        if (p->prog->buffers[i]->name == b->name)
            redefinition(p, b->name, name->loc);
    HL_PUSH(h, p->prog->buffers, p->prog->buffer_count, p->prog->buffer_capacity, b);
    while (!at_punct(p, '}') && peek(p)->kind != TK_EOF) {
        if (accept_punct(p, ';'))
            continue;
        uint32_t before = p->pos;
        Specifiers s = {};
        parse_specifiers(p, &s);
        if (s.type->kind == TY_ERROR) {
            recover(p);
            if (p->pos == before)
                next(p);
            continue;
        }
        if (s.storage & ST_STATIC) {
            hl_error(h, s.loc, 3047, "static variables cannot be declared in a constant buffer");
        }
        parse_declarators(p, &s, nullptr, b, nullptr);
    }
    expect_punct(p, '}');
    accept_punct(p, ';');
}

/* ------------------------------------------------------------------ functions */

static bool same_signature(const HlFunc *a, const HlFunc *b)
{
    if (a->param_count != b->param_count)
        return false;
    for (uint32_t i = 0; i < a->param_count; i++)
        if (!hl_type_equal(a->params[i]->type, b->params[i]->type))
            return false;
    return true;
}

static bool parse_params(Parser *p, HlFunc *f)
{
    Hlsl *h = p->h;
    uint32_t capacity = 0;
    expect_punct(p, '(');
    if (at_ident(p, p->k.k_void) && hl_is_punct(peek_at(p, 1), ')'))
        next(p);
    while (!at_punct(p, ')') && peek(p)->kind != TK_EOF) {
        Specifiers s = {};
        parse_specifiers(p, &s);
        if (s.type->kind == TY_ERROR) {
            while (!at_punct(p, ')') && !at_punct(p, ',') && peek(p)->kind != TK_EOF)
                next(p);
            if (!accept_punct(p, ','))
                break;
            continue;
        }
        HlToken *name = peek(p);
        const char *pname = nullptr;
        if (name->kind == TK_IDENT) {
            next(p);
            pname = name->ident;
        }
        HlType *type = apply_modifiers(p, parse_array_dims(p, s.type, false), s.modifiers);
        HlVar *v = new_var(p, pname ? pname : "", type, name->loc);
        v->param = true;
        v->storage = s.storage;
        v->interp = s.interp;
        if (!(v->storage & (ST_IN | ST_OUT)))
            v->storage |= ST_IN;
        parse_semantics(p, nullptr, &v->semantic, &v->registers, &v->register_count);
        if (accept_punct(p, '=')) {
            HlExpr *init = parse_assignment(p);
            v->init = check_initializer(p, init, &v->type, init->loc);
        }
        if (type->kind == TY_VOID)
            hl_error(h, name->loc, 3038, "'%s': function parameters cannot be declared 'void'", v->name);
        HL_PUSH(h, f->params, f->param_count, capacity, v);
        if (!accept_punct(p, ','))
            break;
    }
    return expect_punct(p, ')');
}

/* whether control can reach the end of a statement. */
static bool falls_through(const HlStmt *s)
{
    if (!s)
        return true;
    switch (s->kind) {
    case S_RETURN:
    case S_DISCARD:
        return false;
    case S_BLOCK:
        for (uint32_t i = 0; i < s->stmt_count; i++)
            if (!falls_through(s->stmts[i]))
                return false;
        return true;
    case S_IF:
        return !s->other || falls_through(s->body) || falls_through(s->other);
    case S_WHILE:
    case S_FOR: {
        /* an endless loop ends only through a break. */
        bool endless = !s->cond || (s->cond->kind == E_CONST && s->cond->values[0].u);
        if (!endless)
            return true;
        return contains_break(s->body, 0);
    }
    case S_DO: {
        if (!falls_through(s->body)) {
                return contains_break(s->body, 0);
        }
        return true;
    }
    case S_SWITCH: {
        /* a switch with a default whose groups all return ends there. */
        bool has_default = false;
        const HlStmt *b = s->body;
        if (!b)
            return true;
        for (uint32_t i = 0; i < b->stmt_count; i++)
            if (b->stmts[i]->kind == S_DEFAULT)
                has_default = true;
        if (!has_default)
            return true;
        if (contains_break(b, 0))
            return true;
        return b->stmt_count && falls_through(b->stmts[b->stmt_count - 1]);
    }
    default:
        return true;
    }
}

static bool contains_break(const HlStmt *s, int depth)
{
    if (!s)
        return false;
    switch (s->kind) {
    case S_BREAK:
        return depth == 0;
    case S_BLOCK:
        for (uint32_t i = 0; i < s->stmt_count; i++)
            if (contains_break(s->stmts[i], depth))
                return true;
        return false;
    case S_IF:
        return contains_break(s->body, depth) || contains_break(s->other, depth);
    case S_FOR:
    case S_WHILE:
    case S_DO:
    case S_SWITCH:
        return contains_break(s->body, depth + 1);
    default:
        return false;
    }
}

HlFunc *parse_function(Parser *p, Specifiers *s, HlToken *name, HlAttribute *attrs, uint32_t attr_count,
                       HlStruct *owner)
{
    Hlsl *h = p->h;
    HlFunc *f = hl_alloc(h, sizeof(HlFunc));
    f->name = name->ident;
    f->ret = default_major(p, apply_modifiers(p, s->type, s->modifiers));
    f->loc = name->loc;
    f->attrs = attrs;
    f->attr_count = attr_count;
    f->owner = owner;
    f->storage = s->storage;
    f->ret_interp = s->interp;
    push_scope(p);
    bool ok = parse_params(p, f);
    pop_scope(p);
    if (!ok) {
        recover(p);
        return nullptr;
    }
    parse_semantics(p, nullptr, &f->semantic, nullptr, nullptr);
    skip_annotations(p);
    /* the same function declared before. */
    Symbol *sym = owner ? nullptr : lookup(p, f->name);
    if (sym && sym->kind != SYM_FUNC && sym->depth == p->depth) {
        redefinition(p, f->name, name->loc);
        sym = nullptr;
    }
    HlFunc *previous = nullptr;
    if (sym && sym->kind == SYM_FUNC)
        for (uint32_t i = 0; i < sym->func_count; i++)
            if (same_signature(sym->funcs[i], f))
                previous = sym->funcs[i];
    if (owner)
        for (uint32_t i = 0; i < owner->method_count; i++)
            if (owner->methods[i]->name == f->name && same_signature(owner->methods[i], f))
                previous = owner->methods[i];
    if (!owner) {
        if (!sym || sym->kind != SYM_FUNC) {
            sym = declare(p, f->name, SYM_FUNC);
        }
        if (!previous)
            HL_PUSH(h, sym->funcs, sym->func_count, sym->func_capacity, f);
        HL_PUSH(h, p->prog->funcs, p->prog->func_count, p->prog->func_capacity, f);
    }
    if (accept_punct(p, ';')) {
        if (previous)
            f->decl = previous;
        return f;
    }
    if (!at_punct(p, '{')) {
        syntax_error(p, peek(p));
        recover(p);
        return f;
    }
    if (previous) {
        if (previous->body || previous->definition) {
            redefinition(p, f->name, name->loc);
        } else {
            previous->definition = f;
            f->decl = previous;
            /* semantics and attributes of the prototype hold for the definition. */
            if (!f->semantic)
                f->semantic = previous->semantic;
            if (!f->attr_count) {
                f->attrs = previous->attrs;
                f->attr_count = previous->attr_count;
            }
        }
    }
    HlFunc *outer = p->func;
    uint32_t loops = p->loops, switches = p->switches;
    p->func = f;
    p->loops = p->switches = 0;
    push_scope(p);
    if (owner && (f->storage & ST_STATIC)) {
        /* static methods see the other methods but have no this. */
        push_scope(p);
    } else if (owner) {
        /* this, and the fields as names that read through it. */
        HlVar *self = new_var(p, p->k.k_this, hl_type_struct(h, owner), name->loc);
        self->param = true;
        self->storage = ST_IN | ST_OUT;
        f->this_var = self;
        if (previous)
            previous->this_var = self;
        Symbol *ts = declare(p, self->name, SYM_VAR);
        ts->var = self;
        for (uint32_t i = 0; i < owner->field_count; i++) {
            HlVar *fv = new_var(p, owner->fields[i].name, owner->fields[i].type, owner->fields[i].loc);
            fv->this_var = self;
            fv->this_field = i;
            Symbol *fs = declare(p, fv->name, SYM_VAR);
            fs->var = fv;
        }
        for (uint32_t i = 0; i < owner->method_count; i++) {
            Symbol *ms = lookup(p, owner->methods[i]->name);
            if (!ms || ms->depth != p->depth || ms->kind != SYM_FUNC)
                ms = declare(p, owner->methods[i]->name, SYM_FUNC);
            HL_PUSH(h, ms->funcs, ms->func_count, ms->func_capacity, owner->methods[i]);
        }
        /* parameters may share names with fields. */
        push_scope(p);
    }
    for (uint32_t i = 0; i < f->param_count; i++)
        if (f->params[i]->name[0]) {
            if (can_declare(p, f->params[i]->name, f->params[i]->loc, false)) {
                Symbol *ps = declare(p, f->params[i]->name, SYM_VAR);
                ps->var = f->params[i];
            }
        }
    f->body = parse_block(p);
    if (owner)
        pop_scope(p);
    pop_scope(p);
    p->func = outer;
    p->loops = loops;
    p->switches = switches;
    if (f->ret->kind != TY_VOID && f->ret->kind != TY_ERROR && falls_through(f->body))
        hl_error(h, name->loc, 3507, "'%s': Not all control paths return a value", f->name);
    return f;
}

/* ------------------------------------------------------------------ statements */

static HlStmt *new_stmt(Parser *p, uint8_t kind, HlLoc loc)
{
    HlStmt *s = hl_alloc(p->h, sizeof(HlStmt));
    s->kind = kind;
    s->loc = loc;
    return s;
}

static HlStmt *parse_statement(Parser *p);

static bool at_declaration(Parser *p)
{
    HlToken *t = peek(p);
    if (t->kind != TK_IDENT)
        return false;
    Specifiers s = {};
    uint32_t save = p->pos;
    bool modifier = parse_modifier(p, &s);
    bool result = modifier || at_type(p);
    if (result && !modifier) {
        /* float4(1, 2, 3, 4).x is an expression; float4 x a declaration. */
        HlType *type = parse_type(p, false);
        result = type && (peek(p)->kind == TK_IDENT || at_punct(p, '<'));
    }
    p->pos = save;
    return result;
}

static HlExpr *condition(Parser *p, HlExpr *e, const char *what)
{
    Hlsl *h = p->h;
    if (e->kind == E_ERROR || e->type->kind == TY_ERROR)
        return e;
    if (!hl_type_numeric_p(e->type) || hl_type_components(e->type) != 1) {
        if (hl_type_numeric_p(e->type) && hl_type_components(e->type) > 1) {
            hl_error(h, e->loc, 3019, "%s statement conditional expressions must evaluate to a scalar", what);
        } else {
            hl_error(h, e->loc, 3019, "%s statement conditional expressions must evaluate to a scalar", what);
        }
        return error_expr(p, e->loc);
    }
    return convert(p, e, hl_type_scalar(h, BT_BOOL), e->loc, false);
}

static HlStmt *parse_decl_statement(Parser *p, bool for_init)
{
    HlStmt *s = new_stmt(p, for_init ? S_FOR : S_DECL, peek(p)->loc);
    Specifiers spec = {};
    parse_specifiers(p, &spec);
    if (spec.type->kind == TY_ERROR) {
        recover(p);
        s->kind = S_EMPTY;
        return s;
    }
    if (at_punct(p, ';')) {
        next(p);
        s->kind = S_EMPTY;
        return s;
    }
    parse_declarators(p, &spec, nullptr, nullptr, s);
    s->kind = S_DECL;
    return s;
}

HlStmt *parse_block(Parser *p)
{
    Hlsl *h = p->h;
    HlStmt *b = new_stmt(p, S_BLOCK, peek(p)->loc);
    if (!expect_punct(p, '{')) {
        recover(p);
        return b;
    }
    uint32_t capacity = 0;
    while (!at_punct(p, '}') && peek(p)->kind != TK_EOF) {
        uint32_t before = p->pos;
        HlStmt *s = parse_statement(p);
        HL_PUSH(h, b->stmts, b->stmt_count, capacity, s);
        if (p->pos == before)
            next(p);
        if (h->errors >= 1000)
            break;
    }
    expect_punct(p, '}');
    return b;
}

static HlStmt *parse_statement(Parser *p)
{
    Hlsl *h = p->h;
    HlToken *t = peek(p);
    HlLoc loc = t->loc;
    if (++p->nesting > 200) {
        hl_error(h, loc, 3000, "statements are nested too deeply");
        p->nesting--;
        recover(p);
        return new_stmt(p, S_EMPTY, loc);
    }
    HlStmt *result = nullptr;
    HlAttribute *attrs = nullptr;
    uint32_t attr_count = 0;
    parse_attributes(p, &attrs, &attr_count);
    t = peek(p);
    if (accept_punct(p, ';')) {
        result = new_stmt(p, S_EMPTY, loc);
    } else if (at_punct(p, '{')) {
        push_scope(p);
        result = parse_block(p);
        pop_scope(p);
    } else if (t->kind == TK_PRAGMA) {
        next(p);
        if (t->pragma && t->pragma->kind == TK_IDENT && hl_streq(t->pragma->ident, "pack_matrix")) {
            HlToken *u = t->pragma->next;
            if (hl_is_punct(u, '(') && u->next->kind == TK_IDENT) {
                if (hl_streq(u->next->ident, "row_major"))
                    p->pack_default = TM_ROW_MAJOR;
                else if (hl_streq(u->next->ident, "column_major"))
                    p->pack_default = TM_COLUMN_MAJOR;
            }
        }
        result = new_stmt(p, S_EMPTY, loc);
    } else if (accept_ident(p, p->k.k_if)) {
        HlStmt *s = new_stmt(p, S_IF, loc);
        expect_punct(p, '(');
        s->cond = condition(p, parse_expr(p), "if");
        expect_punct(p, ')');
        push_scope(p);
        s->body = parse_statement(p);
        pop_scope(p);
        if (accept_ident(p, p->k.k_else)) {
            push_scope(p);
            s->other = parse_statement(p);
            pop_scope(p);
        }
        result = s;
    } else if (accept_ident(p, p->k.k_for)) {
        HlStmt *s = new_stmt(p, S_FOR, loc);
        expect_punct(p, '(');
        /* the init's variables live on after the loop, as fxc keeps them. */
        if (!accept_punct(p, ';')) {
            if (at_declaration(p)) {
                s->init = parse_decl_statement(p, true);
            } else {
                s->init = new_stmt(p, S_EXPR, peek(p)->loc);
                s->init->expr = parse_expr(p);
                expect_punct(p, ';');
            }
        }
        if (!at_punct(p, ';'))
            s->cond = condition(p, parse_expr(p), "for");
        expect_punct(p, ';');
        if (!at_punct(p, ')'))
            s->iter = parse_expr(p);
        expect_punct(p, ')');
        p->loops++;
        push_scope(p);
        s->body = parse_statement(p);
        pop_scope(p);
        p->loops--;
        result = s;
    } else if (accept_ident(p, p->k.k_while)) {
        HlStmt *s = new_stmt(p, S_WHILE, loc);
        expect_punct(p, '(');
        s->cond = condition(p, parse_expr(p), "while");
        expect_punct(p, ')');
        p->loops++;
        push_scope(p);
        s->body = parse_statement(p);
        pop_scope(p);
        p->loops--;
        result = s;
    } else if (accept_ident(p, p->k.k_do)) {
        HlStmt *s = new_stmt(p, S_DO, loc);
        p->loops++;
        push_scope(p);
        s->body = parse_statement(p);
        pop_scope(p);
        p->loops--;
        if (!accept_ident(p, p->k.k_while))
            syntax_error(p, peek(p));
        expect_punct(p, '(');
        s->cond = condition(p, parse_expr(p), "do");
        expect_punct(p, ')');
        expect_punct(p, ';');
        result = s;
    } else if (accept_ident(p, p->k.k_switch)) {
        HlStmt *s = new_stmt(p, S_SWITCH, loc);
        expect_punct(p, '(');
        HlExpr *e = parse_expr(p);
        expect_punct(p, ')');
        if (e->kind != E_ERROR && (!hl_type_numeric_p(e->type) || e->type->kind != TY_SCALAR ||
                                   hl_base_float(e->type->base))) {
            hl_error(h, e->loc, 3019, "switch statement conditional expressions must evaluate to a scalar integer");
            e = error_expr(p, e->loc);
        } else if (e->kind != E_ERROR) {
            e = convert(p, e, hl_type_scalar(h, hl_base_unsigned(e->type->base) ? BT_UINT : BT_INT), e->loc, false);
        }
        s->cond = e;
        p->switches++;
        push_scope(p);
        s->body = parse_statement(p);
        pop_scope(p);
        p->switches--;
        result = s;
    } else if (at_ident(p, p->k.k_case)) {
        next(p);
        HlStmt *s = new_stmt(p, S_CASE, loc);
        HlExpr *e = parse_expr(p);
        uint32_t v = 0;
        const_uint(p, e, &v, "case expression");
        s->value.u = v;
        expect_punct(p, ':');
        if (!p->switches)
            hl_error(h, loc, 3000, "syntax error: 'case' outside of a switch");
        result = s;
    } else if (at_ident(p, p->k.k_default) && hl_is_punct(peek_at(p, 1), ':')) {
        next(p);
        next(p);
        if (!p->switches)
            hl_error(h, loc, 3000, "syntax error: 'default' outside of a switch");
        result = new_stmt(p, S_DEFAULT, loc);
    } else if (accept_ident(p, p->k.k_break)) {
        if (!p->loops && !p->switches)
            hl_error(h, loc, 3518, "break must be inside a loop or switch");
        expect_punct(p, ';');
        result = new_stmt(p, S_BREAK, loc);
    } else if (accept_ident(p, p->k.k_continue)) {
        if (!p->loops)
            hl_error(h, loc, 3519, "continue must be inside a loop");
        expect_punct(p, ';');
        result = new_stmt(p, S_CONTINUE, loc);
    } else if (accept_ident(p, p->k.k_return)) {
        HlStmt *s = new_stmt(p, S_RETURN, loc);
        HlType *ret = p->func ? p->func->ret : hl_type_void(h);
        if (!at_punct(p, ';')) {
            HlExpr *e = parse_expr(p);
            if (ret->kind == TY_VOID) {
                if (e->kind != E_ERROR && e->type->kind != TY_VOID)
                    hl_error(h, loc, 3079, "void functions cannot return a value");
            } else {
                HlType *tt = ret;
                s->expr = e->kind == E_INIT_LIST ? check_initializer(p, e, &tt, e->loc) : convert(p, e, ret, e->loc, false);
            }
        } else if (ret->kind != TY_VOID && ret->kind != TY_ERROR) {
            hl_error(h, loc, 3080, "'%s': function must return a value", p->func ? p->func->name : "");
        }
        expect_punct(p, ';');
        result = s;
    } else if (accept_ident(p, p->k.k_discard)) {
        expect_punct(p, ';');
        result = new_stmt(p, S_DISCARD, loc);
    } else if (at_declaration(p)) {
        result = parse_decl_statement(p, false);
    } else if (at_ident(p, p->k.k_typedef) || at_ident(p, p->k.k_struct)) {
        parse_typedef_or_struct(p);
        result = new_stmt(p, S_EMPTY, loc);
    } else {
        HlStmt *s = new_stmt(p, S_EXPR, loc);
        s->expr = parse_expr(p);
        if (!expect_punct(p, ';'))
            recover(p);
        result = s;
    }
    result->attrs = attrs;
    result->attr_count = attr_count;
    p->nesting--;
    return result;
}

/* ------------------------------------------------------------------ top level */

void parse_typedef_or_struct(Parser *p)
{
    Hlsl *h = p->h;
    if (accept_ident(p, p->k.k_typedef)) {
        Specifiers s = {};
        parse_specifiers(p, &s);
        if (s.type->kind == TY_ERROR) {
            recover(p);
            return;
        }
        for (;;) {
            HlToken *name = next(p);
            if (name->kind != TK_IDENT) {
                syntax_error(p, name);
                recover(p);
                return;
            }
            HlType *t = apply_modifiers(p, parse_array_dims(p, s.type, false), s.modifiers);
            HlType *named = hl_alloc(h, sizeof(HlType));
            *named = *t;
            named->name = name->ident;
            Symbol *existing = lookup(p, name->ident);
            if (existing && existing->depth == p->depth &&
                !(existing->kind == SYM_TYPE && hl_type_equal(existing->type, named)))
                redefinition(p, name->ident, name->loc);
            Symbol *sym = declare(p, name->ident, SYM_TYPE);
            sym->type = t->kind == TY_STRUCT || t->kind == TY_OBJECT ? t : named;
            if (!accept_punct(p, ','))
                break;
        }
        expect_punct(p, ';');
        return;
    }
    /* struct S { ... } [declarators]; */
    Specifiers s = {};
    parse_specifiers(p, &s);
    if (accept_punct(p, ';'))
        return;
    if (s.type->kind == TY_ERROR) {
        recover(p);
        return;
    }
    HlToken *name = next(p);
    if (name->kind != TK_IDENT) {
        syntax_error(p, name);
        recover(p);
        return;
    }
    parse_declarators(p, &s, name, nullptr, nullptr);
}

static void parse_class(Parser *p, bool interface)
{
    Hlsl *h = p->h;
    HlToken *kw = next(p);
    HlToken *name = next(p);
    if (name->kind != TK_IDENT) {
        syntax_error(p, name);
        recover(p);
        return;
    }
    HlStruct *st = parse_struct_body(p, name->ident, kw->loc, !interface, interface);
    Symbol *sym = declare(p, name->ident, SYM_STRUCT);
    sym->type = hl_type_struct(h, st);
    if (accept_punct(p, ';'))
        return;
    Specifiers s = {.type = sym->type};
    parse_declarators(p, &s, nullptr, nullptr, nullptr);
}

static void parse_global(Parser *p)
{
    Hlsl *h = p->h;
    HlToken *t = peek(p);
    if (t->kind == TK_PRAGMA) {
        next(p);
        if (t->pragma && t->pragma->kind == TK_IDENT && hl_streq(t->pragma->ident, "pack_matrix")) {
            HlToken *u = t->pragma->next;
            if (hl_is_punct(u, '(') && u->next->kind == TK_IDENT) {
                if (hl_streq(u->next->ident, "row_major"))
                    p->pack_default = TM_ROW_MAJOR;
                else if (hl_streq(u->next->ident, "column_major"))
                    p->pack_default = TM_COLUMN_MAJOR;
                else
                    hl_warning(h, t->loc, 3568, "'%s' : unknown pack_matrix value", u->next->ident);
            }
        }
        return;
    }
    if (accept_punct(p, ';'))
        return;
    if (t->kind != TK_IDENT && !hl_is_punct(t, '[')) {
        syntax_error(p, t);
        recover(p);
        return;
    }
    if (at_ident(p, p->k.k_cbuffer) || at_ident(p, p->k.k_tbuffer)) {
        parse_cbuffer(p, t->ident == p->k.k_tbuffer);
        return;
    }
    if (at_ident(p, p->k.k_typedef)) {
        parse_typedef_or_struct(p);
        return;
    }
    if (at_ident(p, p->k.k_interface) || at_ident(p, p->k.k_class)) {
        parse_class(p, t->ident == p->k.k_interface);
        return;
    }
    if (at_ident(p, p->k.k_technique) || at_ident(p, p->k.k_technique10) || at_ident(p, p->k.k_technique11) ||
        at_ident(p, p->k.k_fxgroup)) {
        next(p);
        while (!at_punct(p, '{') && peek(p)->kind != TK_EOF)
            next(p);
        skip_group(p, '{', '}');
        accept_punct(p, ';');
        return;
    }
    if (at_ident(p, p->k.k_namespace)) {
        hl_error(h, t->loc, 3000, "syntax error: namespaces are not supported");
        next(p);
        if (peek(p)->kind == TK_IDENT)
            next(p);
        skip_group(p, '{', '}');
        return;
    }
    HlAttribute *attrs = nullptr;
    uint32_t attr_count = 0;
    parse_attributes(p, &attrs, &attr_count);
    Specifiers s = {};
    uint32_t before = p->pos;
    parse_specifiers(p, &s);
    if (s.type->kind == TY_ERROR) {
        recover(p);
        if (p->pos == before)
            next(p);
        return;
    }
    if (accept_punct(p, ';'))
        return;
    HlToken *name = next(p);
    if (name->kind != TK_IDENT) {
        syntax_error(p, name);
        recover(p);
        return;
    }
    if (accept_punct(p, P_COLONCOLON)) {
        /* Struct::Method definitions of methods the struct declares. */
        Symbol *st = lookup(p, name->ident);
        HlToken *method = next(p);
        if (!st || st->kind != SYM_STRUCT || st->type->kind != TY_STRUCT || method->kind != TK_IDENT ||
            !at_punct(p, '(')) {
            hl_error(h, name->loc, 3000, "syntax error: '%s' is not a struct with methods", name->ident);
            recover(p);
            return;
        }
        HlStruct *owner = st->type->st;
        bool declared = false;
        for (uint32_t i = 0; i < owner->method_count; i++)
            if (owner->methods[i]->name == method->ident)
                declared = true;
        if (!declared) {
            hl_error(h, method->loc, 3000, "'%s': no method of that name in '%s'", method->ident, name->ident);
            recover(p);
            return;
        }
        parse_function(p, &s, method, attrs, attr_count, owner);
        return;
    }
    if (at_punct(p, '(')) {
        parse_function(p, &s, name, attrs, attr_count, nullptr);
        return;
    }
    parse_declarators(p, &s, name, nullptr, nullptr);
}

static void init_keywords(Parser *p)
{
    Hlsl *h = p->h;
#define K(name) p->k.k_##name = hl_intern_z(h, #name)
    K(struct); K(class); K(interface); K(cbuffer); K(tbuffer); K(typedef); K(static); K(const); K(uniform);
    K(extern); K(shared); K(groupshared); K(volatile); K(precise); K(inline); K(in); K(out); K(inout);
    K(nointerpolation); K(linear); K(centroid); K(noperspective); K(sample); K(row_major); K(column_major);
    K(snorm); K(unorm); K(globallycoherent); K(if); K(else); K(for); K(while); K(do); K(switch); K(case);
    K(default); K(break); K(continue); K(return); K(discard); K(true); K(false); K(register); K(packoffset);
    K(technique); K(technique10); K(technique11); K(pass); K(compile); K(sampler_state); K(point); K(line);
    K(triangle); K(lineadj); K(triangleadj); K(vector); K(matrix); K(void); K(string); K(unsigned); K(fxgroup);
    K(stateblock_state); K(compile_fragment); K(indices); K(vertices); K(primitives); K(payload); K(namespace);
    K(NULL); K(this); K(sizeof);
#undef K
}

void parse_program(Parser *p)
{
    Hlsl *h = p->h;
    init_keywords(p);
    p->bucket_count = 1024;
    p->buckets = hl_alloc(h, p->bucket_count * sizeof(Bucket *));
    p->tokens = h->tokens;
    p->count = h->token_count;
    while (peek(p)->kind != TK_EOF) {
        uint32_t before = p->pos;
        parse_global(p);
        if (p->pos == before)
            next(p);
        if (h->errors >= 1000)
            break;
    }
}

/* ------------------------------------------------------------------ overloads */

uint32_t conversion_cost(Parser *p, const HlType *from, const HlType *to)
{
    if (from->kind == TY_ERROR || to->kind == TY_ERROR)
        return 0;
    if (hl_type_numeric_p(from) && hl_type_numeric_p(to)) {
        uint32_t cost = 0;
        if (from->base != to->base) {
            bool literal = from->base == BT_LIT_INT || from->base == BT_LIT_FLOAT;
            cost = literal ? 1 : hl_base_float(from->base) == hl_base_float(to->base) ? 2 : 3;
        }
        if (from->kind == to->kind && from->rows == to->rows && from->cols == to->cols)
            return cost;
        if (from->kind == TY_SCALAR)
            return cost + 8;
        if (!can_convert(p, from, to, false))
            return UINT32_MAX;
        if (hl_type_components(from) == hl_type_components(to))
            return cost + 4;
        return cost + 16;
    }
    if (hl_type_equal(from, to))
        return 0;
    if (from->kind == TY_OBJECT && to->kind == TY_OBJECT && can_convert(p, from, to, false))
        return 0;
    if (from->kind == TY_ARRAY && to->kind == TY_ARRAY && from->count == to->count)
        return conversion_cost(p, from->elem, to->elem) == 0 ? 0 : UINT32_MAX;
    return UINT32_MAX;
}

HlExpr *resolve_call(Parser *p, Symbol *s, HlExpr **args, uint32_t count, HlLoc loc, HlExpr *object)
{
    Hlsl *h = p->h;
    HlFunc *best = nullptr;
    uint64_t best_cost = UINT64_MAX;
    bool ambiguous = false;
    for (uint32_t i = 0; i < s->func_count; i++) {
        HlFunc *f = s->funcs[i];
        if (count > f->param_count)
            continue;
        bool ok = true;
        for (uint32_t j = count; j < f->param_count && ok; j++)
            ok = f->params[j]->init != nullptr;
        uint64_t cost = 0;
        for (uint32_t j = 0; j < count && ok; j++) {
            HlVar *param = f->params[j];
            uint32_t c = conversion_cost(p, args[j]->type, param->type);
            if (c == UINT32_MAX)
                ok = false;
            if ((param->storage & ST_OUT) && c != UINT32_MAX &&
                conversion_cost(p, param->type, args[j]->type) == UINT32_MAX)
                ok = false;
            cost += c;
        }
        if (!ok)
            continue;
        if (cost < best_cost) {
            best = f;
            best_cost = cost;
            ambiguous = false;
        } else if (cost == best_cost) {
            ambiguous = true;
        }
    }
    if (!best) {
        hl_error(h, loc, 3013, "'%s': no matching %u parameter function", s->name ? s->name : s->funcs[0]->name, count);
        return error_expr(p, loc);
    }
    if (ambiguous) {
        hl_error(h, loc, 3067, "'%s': ambiguous function call", best->name);
        return error_expr(p, loc);
    }
    HlFunc *f = best;
    HlExpr *e = new_expr(p, E_CALL, f->ret, loc);
    e->func = f;
    e->a = object;
    e->args = hl_alloc(h, (f->param_count + 1) * sizeof(HlExpr *));
    e->arg_count = f->param_count;
    for (uint32_t j = 0; j < f->param_count; j++) {
        HlVar *param = f->params[j];
        if (j >= count) {
            e->args[j] = param->init;
            continue;
        }
        if (param->storage & ST_OUT) {
            if (!args[j]->lvalue) {
                hl_error(h, args[j]->loc, 3025, "out parameters require l-value arguments");
                return error_expr(p, loc);
            }
            mark_assigned(p, args[j]);
            if (!hl_type_equal(args[j]->type, param->type) && hl_type_numeric_p(args[j]->type) &&
                hl_type_components(args[j]->type) < hl_type_components(param->type))
                hl_warning(h, args[j]->loc, 3206, "implicit truncation of vector type");
            e->args[j] = args[j];
            continue;
        }
        if (param->type->kind == TY_OBJECT) {
            e->args[j] = args[j];
            continue;
        }
        e->args[j] = convert(p, args[j], param->type, args[j]->loc, false);
    }
    return e;
}

/* ------------------------------------------------------------------ the profile */

static bool parse_profile(const char *target, HlProfile *out)
{
    static const struct {
        const char *prefix;
        uint8_t stage;
    } stages[] = {
        {"ps_", STAGE_PIXEL}, {"vs_", STAGE_VERTEX}, {"gs_", STAGE_GEOMETRY}, {"hs_", STAGE_HULL},
        {"ds_", STAGE_DOMAIN}, {"cs_", STAGE_COMPUTE}, {"lib_", STAGE_LIBRARY}, {"fx_", STAGE_EFFECT},
    };
    if (!target)
        return false;
    for (size_t i = 0; i < HL_ARRAY_SIZE(stages); i++) {
        size_t n = __builtin_strlen(stages[i].prefix);
        if (__builtin_strncmp(target, stages[i].prefix, n))
            continue;
        const char *s = target + n;
        out->stage = stages[i].stage;
        out->name = target;
        out->level9 = 0;
        if (s[0] < '1' || s[0] > '5' || s[1] != '_')
            return false;
        out->major = (uint8_t)(s[0] - '0');
        s += 2;
        if (s[0] == 'x' || s[0] == 's' || s[0] == 'w') {
            out->minor = 0;
            s++;
        } else if (s[0] >= '0' && s[0] <= '1') {
            out->minor = (uint8_t)(s[0] - '0');
            s++;
        } else {
            return false;
        }
        if (!__builtin_strncmp(s, "_level_9_", 9) && out->major == 4 && out->minor == 0) {
            if (s[9] < '1' || s[9] > '3' || s[10])
                return false;
            out->level9 = (uint8_t)(90 + s[9] - '0');
            return out->stage == STAGE_PIXEL || out->stage == STAGE_VERTEX || out->stage == STAGE_LIBRARY;
        }
        if (*s)
            return false;
        switch (out->stage) {
        case STAGE_PIXEL:
        case STAGE_VERTEX:
            return (out->major == 2 && out->minor == 0) || (out->major == 3 && out->minor == 0) ||
                   (out->major == 4 && out->minor <= 1) || (out->major == 5 && out->minor <= 1) ||
                   (out->major == 1 && out->stage == STAGE_PIXEL) || (out->major == 1 && out->minor == 1);
        case STAGE_GEOMETRY:
            return (out->major == 4 && out->minor <= 1) || (out->major == 5 && out->minor <= 1);
        case STAGE_HULL:
        case STAGE_DOMAIN:
            return out->major == 5 && out->minor <= 1;
        case STAGE_COMPUTE:
            return (out->major == 4 && out->minor <= 1) || (out->major == 5 && out->minor <= 1);
        case STAGE_LIBRARY:
            return (out->major == 4 && out->minor <= 1) || (out->major == 5 && out->minor == 0);
        case STAGE_EFFECT:
            return true;
        }
    }
    return false;
}

int hl_backend(Hlsl *h, HlProgram *prog, HlFunc *entry);

int hl_compile(Hlsl *h)
{
    const HlslOptions *o = h->options;
    HlProgram *prog = hl_alloc(h, sizeof(HlProgram));
    HlLoc start = {0, 1, 1};
    if (!parse_profile(o->target, &prog->profile)) {
        hl_error(h, start, 3530, "invalid target '%s'", o->target ? o->target : "");
        return -1;
    }
    Parser p_ = {.h = h, .prog = prog}, *p = &p_;
    p->pack_default = (o->flags & HLSL_PACK_MATRIX_ROW_MAJOR) ? TM_ROW_MAJOR : TM_COLUMN_MAJOR;
    parse_program(p);
    if (h->errors)
        return -1;
    if (prog->profile.stage == STAGE_EFFECT) {
        hl_error(h, start, 3530, "effect targets are not supported");
        return -1;
    }
    HlFunc *entry = nullptr;
    const char *entry_name = o->entry ? hl_intern_z(h, o->entry) : nullptr;
    if (prog->profile.stage != STAGE_LIBRARY) {
        Symbol *s = entry_name ? lookup(p, entry_name) : nullptr;
        if (!s || s->kind != SYM_FUNC) {
            hl_error(h, start, 3501, "'%s': entrypoint not found", o->entry ? o->entry : "");
            return -1;
        }
        for (uint32_t i = 0; i < s->func_count; i++) {
            HlFunc *f = s->funcs[i];
            HlFunc *def = f->body ? f : f->definition;
            if (!def)
                continue;
            if (entry) {
                hl_error(h, f->loc, 3501, "'%s': ambiguous entrypoint", f->name);
                return -1;
            }
            entry = def;
        }
        if (!entry) {
            hl_error(h, start, 3501, "'%s': entrypoint not found", o->entry);
            return -1;
        }
    }
    return hl_backend(h, prog, entry);
}
