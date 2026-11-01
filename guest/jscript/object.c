/*
 * objects: property tables, arrays, the prototype chain, calls, and the errors a script raises.
 */
#include "js.h"

BOOL is_js_space(WCHAR c);

static const struct {
    int Code;
    PCWSTR Text;
} g_errors[] = {
#include "errors.inc"
};

PCWSTR error_text(int code)
{
    for (UINT i = 0; i < sizeof(g_errors) / sizeof(g_errors[0]); i++)
        if (g_errors[i].Code == code)
            return g_errors[i].Text;
    return L"Unknown runtime error";
}


/* ------------------------------------------------------------------ property tables */

Obj *obj_new(Engine *e, int cls, Obj *proto)
{
    Obj *o = gc_alloc(e, sizeof(Obj), GC_OBJ);
    o->cls = (BYTE)cls;
    o->flags = OF_EXTENSIBLE;
    o->proto = proto;
    return o;
}

static BOOL key_equal(Str *a, Str *b)
{
    if (a == b)
        return TRUE;
    if (a->len != b->len)
        return FALSE;
    if (a->hash && b->hash && a->hash != b->hash)
        return FALSE;
    return !memcmp(a->s, b->s, a->len * sizeof(WCHAR));
}

Prop *obj_find_own(Obj *o, Str *key)
{
    if (o->icap) {
        ULONG h = str_hash(key);
        ULONG i = h & (o->icap - 1);
        while (o->index[i]) {
            Prop *p = &o->props[o->index[i] - 1];
            if (p->key && (p->key == key || (p->key->hash == h && key_equal(p->key, key))))
                return p;
            i = (i + 1) & (o->icap - 1);
        }
        return nullptr;
    }
    for (ULONG i = 0; i < o->pcount; i++) {
        Prop *p = &o->props[i];
        if (p->key == key)
            return p;
    }
    for (ULONG i = 0; i < o->pcount; i++) {
        Prop *p = &o->props[i];
        if (p->key && key_equal(p->key, key))
            return p;
    }
    return nullptr;
}

static void rebuild_index(Engine *e, Obj *o)
{
    ULONG cap = 16;
    while (cap < o->pcount * 2 + 2)
        cap *= 2;
    o->index = gc_alloc(e, cap * sizeof(ULONG), GC_BYTES);
    o->icap = cap;
    for (ULONG i = 0; i < o->pcount; i++) {
        if (!o->props[i].key)
            continue;
        ULONG k = str_hash(o->props[i].key) & (cap - 1);
        while (o->index[k])
            k = (k + 1) & (cap - 1);
        o->index[k] = i + 1;
    }
}

static void compact_props(Engine *e, Obj *o)
{
    ULONG n = 0;
    for (ULONG i = 0; i < o->pcount; i++)
        if (o->props[i].key)
            o->props[n++] = o->props[i];
    for (ULONG i = n; i < o->pcount; i++)
        memset(&o->props[i], 0, sizeof(Prop));
    o->pcount = n;
    o->ndead = 0;
    if (o->icap)
        rebuild_index(e, o);
}

Prop *obj_add_prop(Engine *e, Obj *o, Str *key, Val v, ULONG flags)
{
    if (o->ndead > 16 && o->ndead > o->pcount / 2)
        compact_props(e, o);
    if (o->pcount == o->pcap) {
        ULONG cap = o->pcap ? o->pcap * 2 : 4;
        Prop *np = gc_alloc(e, cap * sizeof(Prop), GC_PROPS);
        if (o->pcount)
            memcpy(np, o->props, o->pcount * sizeof(Prop));
        o->props = np;
        o->pcap = cap;
    }
    Prop *p = &o->props[o->pcount++];
    p->key = key;
    p->v = v;
    p->flags = flags;
    if (o->icap && o->pcount * 2 + 2 > o->icap)
        rebuild_index(e, o);
    else if (o->icap) {
        ULONG k = str_hash(key) & (o->icap - 1);
        while (o->index[k])
            k = (k + 1) & (o->icap - 1);
        o->index[k] = o->pcount;
    } else if (o->pcount > 8)
        rebuild_index(e, o);
    return p;
}

static void remove_prop(Obj *o, Prop *p)
{
    p->key = nullptr;
    p->v = UNDEF;
    o->ndead++;
}

/* ------------------------------------------------------------------ arrays */

static BOOL array_grow(Engine *e, Obj *a, ULONG need)
{
    if (need <= a->u.arr.cap)
        return TRUE;
    ULONG cap = a->u.arr.cap ? a->u.arr.cap : 4;
    while (cap < need)
        cap = cap + cap / 2 + 4;
    Val *items = gc_alloc(e, (SIZE_T)cap * sizeof(Val), GC_VALS);
    if (!items)
        return FALSE;
    if (a->u.arr.len)
        memcpy(items, a->u.arr.items, (SIZE_T)(a->u.arr.len < a->u.arr.cap ? a->u.arr.len : a->u.arr.cap) * sizeof(Val));
    for (ULONG i = a->u.arr.cap; i < cap; i++)
        items[i].t = T_HOLE;
    a->u.arr.items = items;
    a->u.arr.cap = cap;
    return TRUE;
}

Obj *array_new(Engine *e, ULONG len)
{
    Obj *a = obj_new(e, C_ARRAY, e->ArrayProto);
    if (len && len < (1u << 24)) {
        array_grow(e, a, len);
    }
    a->u.arr.len = len;
    return a;
}

int array_set_length(Engine *e, Obj *a, double len)
{
    ULONG n = d_to_uint32(len);
    if ((double)n != len)
        return throw_error(e, ERR_RANGE, 5030, nullptr);
    if (n < a->u.arr.len) {
        ULONG top = a->u.arr.len < a->u.arr.cap ? a->u.arr.len : a->u.arr.cap;
        for (ULONG i = n; i < top; i++)
            a->u.arr.items[i].t = T_HOLE;
        /* names that are indexes past the new length go too. */
        for (ULONG i = 0; i < a->pcount; i++) {
            ULONG idx;
            if (a->props[i].key && str_to_index(a->props[i].key, &idx) && idx >= n)
                remove_prop(a, &a->props[i]);
        }
    }
    a->u.arr.len = n;
    return RC_OK;
}

void array_push(Engine *e, Obj *a, Val v)
{
    ULONG n = a->u.arr.len;
    array_grow(e, a, n + 1);
    a->u.arr.items[n] = v;
    a->u.arr.len = n + 1;
}

#define SPARSE_LIMIT (1u << 24)

static BOOL array_put_index(Engine *e, Obj *a, ULONG i, Val *v)
{
    if (i < a->u.arr.cap || (i < SPARSE_LIMIT && i <= a->u.arr.cap * 2 + 1024)) {
        if (!array_grow(e, a, i + 1))
            return FALSE;
        a->u.arr.items[i] = *v;
        if (i >= a->u.arr.len)
            a->u.arr.len = i + 1;
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ get, put, has, delete */

static BOOL own_lookup(Engine *e, Obj *o, Str *key, Val *out, int *rc)
{
    *rc = RC_OK;
    ULONG idx;
    switch (o->cls) {
    case C_ARRAY:
        if (key->len && key->s[0] >= L'0' && key->s[0] <= L'9' && str_to_index(key, &idx)) {
            if (idx < o->u.arr.cap && idx < o->u.arr.len && o->u.arr.items[idx].t != T_HOLE) {
                *out = o->u.arr.items[idx];
                return TRUE;
            }
            break;
        }
        if (key->len == 6 && !wcscmp(key->s, L"length")) {
            *out = v_num(o->u.arr.len);
            return TRUE;
        }
        break;
    case C_STRING:
        if (key->len == 6 && !wcscmp(key->s, L"length")) {
            *out = v_num(o->u.prim.prim.u.s->len);
            return TRUE;
        }
        break;
    case C_ARGS:
        if (key->len && key->s[0] >= L'0' && key->s[0] <= L'9' && str_to_index(key, &idx)) {
            if ((int)idx < o->u.scope.argc && o->u.scope.args[idx].t != T_HOLE) {
                if ((int)idx < o->u.scope.mapped) {
                    Obj *act = o->u.scope.outer;
                    Prop *p = obj_find_own(act, o->u.scope.func->u.fn.node->params[idx]);
                    if (p) {
                        *out = p->v;
                        return TRUE;
                    }
                }
                *out = o->u.scope.args[idx];
                return TRUE;
            }
        }
        break;
    case C_HOST: {
        BOOL found = FALSE;
        *rc = host_get(e, o, key, out, &found);
        return *rc || found;
    }
    case C_REGEXP_STATIC:
        break;
    default:
        break;
    }
    if (o == e->RegExpCtor) {
        extern BOOL regexp_static_get(Engine * e, Str * key, Val * out);
        if (regexp_static_get(e, key, out))
            return TRUE;
    }
    Prop *p = obj_find_own(o, key);
    if (p) {
        *out = p->v;
        return TRUE;
    }
    if (o->cls == C_FUNCTION && o->u.fn.node && ((key->len == 6 && !wcscmp(key->s, L"caller")) || (key->len == 9 && !wcscmp(key->s, L"arguments")))) {
        *out = v_null();
        return TRUE;
    }
    if (o->cls == C_FUNCTION && o->u.fn.node && key->len == 9 && !wcscmp(key->s, L"prototype")) {
        /* the prototype of a function is made when it is first asked for. */
        Obj *proto = obj_new(e, C_OBJECT, e->ObjectProto);
        obj_define(e, proto, str_intern_ascii(e, "constructor"), v_obj(o), PF_DONTENUM);
        obj_define(e, o, key, v_obj(proto), PF_DONTENUM | PF_DONTDELETE);
        *out = v_obj(proto);
        return TRUE;
    }
    return FALSE;
}

int obj_get_this(Engine *e, Obj *o, Str *key, Val *self, Val *out)
{
    int rc;
    for (Obj *cur = o; cur; cur = cur->proto) {
        if (own_lookup(e, cur, key, out, &rc))
            return rc;
    }
    *out = UNDEF;
    return RC_OK;
}

int obj_get(Engine *e, Obj *o, Str *key, Val *out)
{
    return obj_get_this(e, o, key, nullptr, out);
}

BOOL obj_has_own(Engine *e, Obj *o, Str *key)
{
    ULONG idx;
    switch (o->cls) {
    case C_ARRAY:
        if (str_to_index(key, &idx))
            return idx < o->u.arr.cap && idx < o->u.arr.len && o->u.arr.items[idx].t != T_HOLE;
        if (key->len == 6 && !wcscmp(key->s, L"length"))
            return TRUE;
        break;
    case C_STRING:
        if (key->len == 6 && !wcscmp(key->s, L"length"))
            return TRUE;
        break;
    case C_ARGS:
        if (str_to_index(key, &idx) && (int)idx < o->u.scope.argc)
            return o->u.scope.args[idx].t != T_HOLE;
        break;
    case C_HOST:
        return host_has(e, o, key);
    default:
        break;
    }
    return obj_find_own(o, key) != nullptr;
}

BOOL obj_has(Engine *e, Obj *o, Str *key)
{
    for (Obj *cur = o; cur; cur = cur->proto)
        if (obj_has_own(e, cur, key))
            return TRUE;
    return FALSE;
}

int obj_put(Engine *e, Obj *o, Str *key, Val *v)
{
    ULONG idx;
    switch (o->cls) {
    case C_ARRAY:
        if (key->len && key->s[0] >= L'0' && key->s[0] <= L'9' && str_to_index(key, &idx)) {
            if (array_put_index(e, o, idx, v))
                return RC_OK;
            if (idx >= o->u.arr.len)
                o->u.arr.len = idx + 1;
            break;
        }
        if (key->len == 6 && !wcscmp(key->s, L"length")) {
            double d;
            int rc = to_number(e, v, &d);
            if (rc)
                return rc;
            return array_set_length(e, o, d);
        }
        break;
    case C_STRING:
        if (key->len == 6 && !wcscmp(key->s, L"length"))
            return RC_OK;
        break;
    case C_ARGS:
        if (str_to_index(key, &idx) && (int)idx < o->u.scope.argc && o->u.scope.args[idx].t != T_HOLE) {
            o->u.scope.args[idx] = *v;
            if ((int)idx < o->u.scope.mapped) {
                Prop *p = obj_find_own(o->u.scope.outer, o->u.scope.func->u.fn.node->params[idx]);
                if (p)
                    p->v = *v;
            }
            return RC_OK;
        }
        break;
    case C_HOST:
        return host_put(e, o, key, v);
    default:
        break;
    }
    if (o == e->RegExpCtor) {
        extern BOOL regexp_static_put(Engine * e, Str * key, Val * v);
        if (regexp_static_put(e, key, v))
            return RC_OK;
    }
    Prop *p = obj_find_own(o, key);
    if (p) {
        if (!(p->flags & PF_READONLY))
            p->v = *v;
        return RC_OK;
    }
    /* a read only property of the chain keeps the object from getting one of the same name. */
    for (Obj *cur = o->proto; cur; cur = cur->proto) {
        Prop *q = cur->cls == C_HOST ? nullptr : obj_find_own(cur, key);
        if (q) {
            if (q->flags & PF_READONLY)
                return RC_OK;
            break;
        }
    }
    if (!(o->flags & OF_EXTENSIBLE))
        return RC_OK;
    obj_add_prop(e, o, key, *v, 0);
    return RC_OK;
}

void obj_define(Engine *e, Obj *o, Str *key, Val v, ULONG flags)
{
    Prop *p = obj_find_own(o, key);
    if (p) {
        p->v = v;
        p->flags = flags;
        return;
    }
    obj_add_prop(e, o, key, v, flags);
}

void obj_define_z(Engine *e, Obj *o, const char *name, Val v, ULONG flags)
{
    obj_define(e, o, str_intern_ascii(e, name), v, flags);
}

int obj_delete(Engine *e, Obj *o, Str *key, BOOL *result)
{
    ULONG idx;
    *result = TRUE;
    switch (o->cls) {
    case C_ARRAY:
        if (str_to_index(key, &idx)) {
            if (idx < o->u.arr.cap && idx < o->u.arr.len)
                o->u.arr.items[idx].t = T_HOLE;
            return RC_OK;
        }
        if (key->len == 6 && !wcscmp(key->s, L"length")) {
            *result = FALSE;
            return RC_OK;
        }
        break;
    case C_STRING:
        if (key->len == 6 && !wcscmp(key->s, L"length")) {
            *result = FALSE;
            return RC_OK;
        }
        break;
    case C_ARGS:
        if (str_to_index(key, &idx) && (int)idx < o->u.scope.argc) {
            o->u.scope.args[idx].t = T_HOLE;
            return RC_OK;
        }
        break;
    case C_HOST:
        return host_delete(e, o, key, result);
    default:
        break;
    }
    Prop *p = obj_find_own(o, key);
    if (!p)
        return RC_OK;
    if (p->flags & PF_DONTDELETE) {
        *result = FALSE;
        return RC_OK;
    }
    remove_prop(o, p);
    return RC_OK;
}

int obj_get_index(Engine *e, Obj *o, ULONG i, Val *out)
{
    if (o->cls == C_ARRAY && i < o->u.arr.cap && i < o->u.arr.len && o->u.arr.items[i].t != T_HOLE) {
        *out = o->u.arr.items[i];
        return RC_OK;
    }
    Str *k = index_to_str(e, i);
    return obj_get(e, o, k, out);
}

int obj_put_index(Engine *e, Obj *o, ULONG i, Val *v)
{
    if (o->cls == C_ARRAY && array_put_index(e, o, i, v))
        return RC_OK;
    return obj_put(e, o, index_to_str(e, i), v);
}

/* ------------------------------------------------------------------ enumeration */

int obj_enum_keys(Engine *e, Obj *o, Obj **out_array, BOOL include_dontenum)
{
    Obj *result = array_new(e, 0);
    *out_array = result;
    Obj *seen = obj_new(e, C_OBJECT, nullptr);
    /* the properties of the prototypes come before those of the object. */
    Obj *chain[64];
    int depth = 0;
    for (Obj *c = o; c && depth < 64; c = c->proto)
        chain[depth++] = c;
    for (int level = depth - 1; level >= 0; level--) {
        Obj *cur = chain[level];
        if (cur->cls == C_HOST) {
            int rc = host_enum_keys(e, cur, result);
            if (rc)
                return rc;
            continue;
        }
        if (cur->cls == C_ARRAY) {
            for (ULONG i = 0; i < cur->u.arr.len && i < cur->u.arr.cap; i++)
                if (cur->u.arr.items[i].t != T_HOLE) {
                    Str *k = index_to_str(e, i);
                    if (!obj_find_own(seen, k)) {
                        obj_add_prop(e, seen, k, UNDEF, 0);
                        array_push(e, result, v_str(k));
                    }
                }
        } else if (cur->cls == C_ARGS) {
            for (int i = 0; i < cur->u.scope.argc; i++)
                if (cur->u.scope.args[i].t != T_HOLE) {
                    Str *k = index_to_str(e, i);
                    if (!obj_find_own(seen, k)) {
                        obj_add_prop(e, seen, k, UNDEF, 0);
                        array_push(e, result, v_str(k));
                    }
                }
        }
        for (ULONG i = 0; i < cur->pcount; i++) {
            Prop *p = &cur->props[i];
            if (!p->key)
                continue;
            if (obj_find_own(seen, p->key))
                continue;
            if ((p->flags & PF_DONTENUM) && !include_dontenum)
                continue;
            obj_add_prop(e, seen, p->key, UNDEF, 0);
            array_push(e, result, v_str(p->key));
        }
    }
    return RC_OK;
}

/* ------------------------------------------------------------------ functions */

Obj *func_new_native(Engine *e, const WCHAR *name, NativeFn fn, int argc, int magic)
{
    Obj *f = obj_new(e, C_FUNCTION, e->FunctionProto);
    f->u.fn.native = fn;
    f->u.fn.magic = magic;
    obj_define(e, f, str_intern_ascii(e, "length"), v_num(argc), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    if (name && *name)
        f->u.fn.home = nullptr;
    return f;
}

Obj *obj_native(Engine *e, const char *name, NativeFn fn, int argc, int magic)
{
    WCHAR w[64];
    int n = 0;
    while (name[n] && n < 63) {
        w[n] = (BYTE)name[n];
        n++;
    }
    w[n] = 0;
    Obj *f = func_new_native(e, w, fn, argc, magic);
    Str *nm = str_intern(e, w, n);
    f->u.fn.node = nullptr;
    /* the name is kept for toString. */
    obj_define(e, f, str_intern_ascii(e, "\1name"), v_str(nm), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    return f;
}

void obj_method(Engine *e, Obj *o, const char *name, NativeFn fn, int argc, int magic)
{
    Obj *f = obj_native(e, name, fn, argc, magic);
    obj_define(e, o, str_intern_ascii(e, name), v_obj(f), PF_DONTENUM);
}

int call_function(Engine *e, Val *func, Val *self, Val *args, int argc, Val *ret)
{
    *ret = UNDEF;
    if (func->t != T_OBJ)
        return throw_error(e, ERR_TYPE, 5002, nullptr);
    Obj *f = func->u.o;
    if (f->cls == C_FUNCTION) {
        if (f->u.fn.node)
            return call_node_function(e, f, self, args, argc, ret, FALSE);
        if (!f->u.fn.native)
            return RC_OK;
        e->Magic = f->u.fn.magic;
        Val none = UNDEF;
        return f->u.fn.native(e, self ? self : &none, args, argc, ret, FALSE);
    }
    if (f->cls == C_HOST)
        return host_call(e, f, self, args, argc, ret, FALSE);
    return throw_error(e, ERR_TYPE, 5002, nullptr);
}

int construct_function(Engine *e, Val *func, Val *args, int argc, Val *ret)
{
    *ret = UNDEF;
    if (func->t != T_OBJ)
        return throw_error(e, ERR_TYPE, 445, nullptr);
    Obj *f = func->u.o;
    if (f->cls == C_HOST)
        return host_call(e, f, nullptr, args, argc, ret, TRUE);
    if (f->cls != C_FUNCTION)
        return throw_error(e, ERR_TYPE, 445, nullptr);
    if (!f->u.fn.node) {
        Val self = UNDEF;
        if (!f->u.fn.native)
            return throw_error(e, ERR_TYPE, 445, nullptr);
        e->Magic = f->u.fn.magic;
        return f->u.fn.native(e, &self, args, argc, ret, TRUE);
    }
    Val protov;
    int rc = obj_get(e, f, str_intern_ascii(e, "prototype"), &protov);
    if (rc)
        return rc;
    Obj *o = obj_new(e, C_OBJECT, protov.t == T_OBJ ? protov.u.o : e->ObjectProto);
    Val self = v_obj(o), r;
    rc = call_node_function(e, f, &self, args, argc, &r, TRUE);
    if (rc)
        return rc;
    *ret = r.t == T_OBJ ? r : self;
    return RC_OK;
}

BOOL obj_instanceof(Engine *e, Obj *o, Obj *ctor, BOOL *result)
{
    Val protov;
    if (obj_get(e, ctor, str_intern_ascii(e, "prototype"), &protov) || protov.t != T_OBJ)
        return FALSE;
    *result = FALSE;
    for (Obj *p = o->proto; p; p = p->proto)
        if (p == protov.u.o) {
            *result = TRUE;
            break;
        }
    return TRUE;
}

const WCHAR *class_name(Obj *o)
{
    switch (o->cls) {
    case C_FUNCTION:
        return L"Function";
    case C_ARRAY:
        return L"Array";
    case C_STRING:
        return L"String";
    case C_NUMBER:
        return L"Number";
    case C_BOOLEAN:
        return L"Boolean";
    case C_DATE:
        return L"Date";
    case C_REGEXP:
        return L"RegExp";
    case C_ERROR:
        return L"Error";
    case C_MATH:
        return L"Math";
    default:
        return L"Object";
    }
}

static int default_value_ex(Engine *e, Obj *o, int hint, Val *out, BOOL *none)
{
    if (none)
        *none = FALSE;
    if (o->cls == C_HOST) {
        int rc = host_default_value(e, o, hint, out);
        if (rc != 2)
            return rc;
    }
    if (hint == 0)
        hint = o->cls == C_DATE ? 2 : 1;
    static const char *const names[2][2] = {{"valueOf", "toString"}, {"toString", "valueOf"}};
    for (int i = 0; i < 2; i++) {
        Val f;
        int rc = obj_get(e, o, str_intern_ascii(e, names[hint == 2][i]), &f);
        if (rc)
            return rc;
        if (is_callable(&f)) {
            Val self = v_obj(o), r;
            rc = call_function(e, &f, &self, nullptr, 0, &r);
            if (rc)
                return rc;
            if (r.t != T_OBJ) {
                *out = r;
                return RC_OK;
            }
        }
    }
    /* a sum with such an object has no value, any other use of it is an error. */
    if (none) {
        *none = TRUE;
        *out = UNDEF;
        return RC_OK;
    }
    return throw_error(e, ERR_TYPE, 438, nullptr);
}

int obj_default_value(Engine *e, Obj *o, int hint, Val *out)
{
    return default_value_ex(e, o, hint, out, nullptr);
}

int to_primitive_soft(Engine *e, Val *v, int hint, Val *out, BOOL *none)
{
    *none = FALSE;
    if (v->t != T_OBJ) {
        *out = *v;
        return RC_OK;
    }
    return default_value_ex(e, v->u.o, hint, out, none);
}

/* ------------------------------------------------------------------ errors */

/* the kind of error a number stands for, which is also the name of the object a script catches. */
int error_kind_of(int code)
{
    static const short type_errors[] = {5, 13, 91, 424, 438, 445, 449, 450, 451, 458, 5001, 5002, 5004, 5005, 5006, 5007, 5009,
                                        5010, 5012, 5013, 5014, 5015, 5016, 5023, 5028, 5031, 5034, 5036};
    code &= 0xFFFF;
    for (UINT i = 0; i < sizeof(type_errors) / sizeof(type_errors[0]); i++)
        if (type_errors[i] == code)
            return ERR_TYPE;
    if (code == 6 || code == 9 || code == 5026 || code == 5027 || code == 5029 || code == 5030)
        return ERR_RANGE;
    if (code == 501 || code == 5003 || code == 5008)
        return ERR_REFERENCE;
    if (code >= 1001 && code <= 1036)
        return ERR_SYNTAX;
    if (code >= 5017 && code <= 5021)
        return ERR_REGEXP;
    if (code == 5024 || code == 5025)
        return ERR_URI;
    if (code == 5032 || code == 5033)
        return ERR_EVAL;
    return ERR_ERROR;
}

BOOL error_known(int code)
{
    for (UINT i = 0; i < sizeof(g_errors) / sizeof(g_errors[0]); i++)
        if (g_errors[i].Code == code)
            return TRUE;
    return FALSE;
}

Obj *make_error_text(Engine *e, HRESULT number, const WCHAR *message, SIZE_T n, BOOL with_number)
{
    Obj *err = obj_new(e, C_ERROR, e->ErrorProtos[error_kind_of((int)(number & 0xFFFF))]);
    Str *m = str_new(e, message, n);
    obj_define_z(e, err, "message", v_str(m), 0);
    if (with_number)
        obj_define_z(e, err, "number", v_num((LONG)number), 0);
    obj_define_z(e, err, "description", v_str(m), 0);
    return err;
}

Obj *make_error(Engine *e, int kind, int code, const WCHAR *detail)
{
    PCWSTR text = error_text(code);
    WCHAR message[512];
    SIZE_T n = 0;
    for (PCWSTR c = text; *c && n < 480; c++) {
        if (*c == L'|' && detail) {
            for (PCWSTR d = detail; *d && n < 500; d++)
                message[n++] = *d;
        } else
            message[n++] = *c;
    }
    message[n] = 0;
    int base = code >= 15000 ? code - 10000 : code;
    return make_error_text(e, (HRESULT)(0x800A0000u | (ULONG)base), message, n, TRUE);
}

int throw_value(Engine *e, Val v)
{
    e->Exception = v;
    e->ErrLine = e->CurStmt ? e->CurStmt->line : 0;
    e->ErrCol = e->CurStmt ? e->CurStmt->col : 0;
    e->ErrFromHost = FALSE;
    return RC_THROW;
}

int throw_error(Engine *e, int kind, int code, const WCHAR *detail)
{
    /* the numbers with a detail are 15000 plus the number of the plain message. */
    Obj *err = make_error(e, kind, detail ? code + 10000 : code, detail);
    if (detail && code < 15000)
        ;
    return throw_value(e, v_obj(err));
}

int throw_host_error(Engine *e, HRESULT hr, const WCHAR *source, const WCHAR *description)
{
    Obj *err = make_error_text(e, hr, description ? description : L"", description ? wcslen(description) : 0, TRUE);
    int rc = throw_value(e, v_obj(err));
    e->ErrFromHost = TRUE;
    e->ErrScode = hr;
    SysFreeString(e->ErrSource);
    SysFreeString(e->ErrDescription);
    e->ErrSource = source ? SysAllocString(source) : nullptr;
    e->ErrDescription = description ? SysAllocString(description) : nullptr;
    return rc;
}

/* a number of the language, with the text of its table or the one for the numbers it does not know. */
int throw_number(Engine *e, HRESULT hr)
{
    int code = (int)(hr & 0xFFFF);
    PCWSTR text = error_known(code) ? error_text(code) : L"Unknown runtime error";
    Obj *err = make_error_text(e, hr, text, wcslen(text), TRUE);
    return throw_value(e, v_obj(err));
}
