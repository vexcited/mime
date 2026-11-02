/*
 * Array and its functions.
 */
#include "js.h"

#define ARG(i) ((i) < argc ? args[i] : UNDEF)

static Str *S(Engine *e, const char *z)
{
    return str_intern_ascii(e, z);
}

/* ------------------------------------------------------------------ element access over any object */

static int a_len(Engine *e, Obj *o, double *len)
{
    if (o->cls == C_ARRAY) {
        *len = o->u.arr.len;
        return RC_OK;
    }
    Val v;
    int rc = obj_get(e, o, S(e, "length"), &v);
    if (rc)
        return rc;
    ULONG n;
    if ((rc = to_uint32(e, &v, &n)))
        return rc;
    *len = n;
    return RC_OK;
}

static int a_set_len(Engine *e, Obj *o, double len)
{
    Val v = v_num(len);
    if (o->cls == C_ARRAY)
        return array_set_length(e, o, len);
    return obj_put(e, o, S(e, "length"), &v);
}

static BOOL a_has(Engine *e, Obj *o, double i)
{
    if (o->cls == C_ARRAY && i < o->u.arr.cap && i < o->u.arr.len && o->u.arr.items[(ULONG)i].t != T_HOLE)
        return TRUE;
    return obj_has(e, o, index_to_str(e, (ULONG)i));
}

static int a_get(Engine *e, Obj *o, double i, Val *out)
{
    if (o->cls == C_ARRAY && i < o->u.arr.cap && i < o->u.arr.len && o->u.arr.items[(ULONG)i].t != T_HOLE) {
        *out = o->u.arr.items[(ULONG)i];
        return RC_OK;
    }
    return obj_get(e, o, index_to_str(e, (ULONG)i), out);
}

static int a_put(Engine *e, Obj *o, double i, Val *v)
{
    return obj_put_index(e, o, (ULONG)i, v);
}

static int a_del(Engine *e, Obj *o, double i)
{
    BOOL r;
    return obj_delete(e, o, index_to_str(e, (ULONG)i), &r);
}

/* ------------------------------------------------------------------ constructor */

static int array_ctor(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *a;
    if (argc == 1 && args[0].t == T_NUM) {
        double d = args[0].u.n;
        ULONG n = d_to_uint32(d);
        if ((double)n != d)
            return throw_error(e, ERR_RANGE, 5029, nullptr);
        a = array_new(e, n);
    } else {
        a = array_new(e, 0);
        for (int i = 0; i < argc; i++)
            array_push(e, a, args[i]);
    }
    *ret = v_obj(a);
    return RC_OK;
}

/* ------------------------------------------------------------------ join */

typedef struct JoinFrame {
    Obj *o;
    struct JoinFrame *prev;
} JoinFrame;

static JoinFrame *g_join;

static int array_join_impl(Engine *e, Obj *o, Str *sep, BOOL locale, Val *ret)
{
    for (JoinFrame *f = g_join; f; f = f->prev)
        if (f->o == o) {
            *ret = v_str(str_empty(e));
            return RC_OK;
        }
    JoinFrame frame = {o, g_join};
    g_join = &frame;
    double len;
    int rc = a_len(e, o, &len);
    if (rc) {
        g_join = frame.prev;
        return rc;
    }
    Str **parts = gc_alloc(e, (len ? (SIZE_T)len : 1) * sizeof(Str *), GC_ITEMS);
    SIZE_T total = 0;
    for (double i = 0; i < len; i++) {
        Val v;
        if ((rc = a_get(e, o, i, &v)))
            break;
        if (v.t == T_UNDEF || v.t == T_NULL)
            parts[(ULONG)i] = str_empty(e);
        else if (locale) {
            Val f;
            Obj *tmp;
            if ((rc = to_object(e, &v, &tmp)) || (rc = obj_get(e, tmp, S(e, "toLocaleString"), &f)))
                break;
            Val r;
            if ((rc = call_function(e, &f, &v, nullptr, 0, &r)) || (rc = to_string(e, &r, &parts[(ULONG)i])))
                break;
        } else if ((rc = to_string(e, &v, &parts[(ULONG)i])))
            break;
        total += parts[(ULONG)i]->len;
    }
    g_join = frame.prev;
    if (rc)
        return rc;
    total += (SIZE_T)(len > 0 ? len - 1 : 0) * sep->len;
    Str *r = str_alloc(e, total);
    SIZE_T pos = 0;
    for (double i = 0; i < len; i++) {
        if (i && sep->len) {
            memcpy(r->s + pos, sep->s, sep->len * sizeof(WCHAR));
            pos += sep->len;
        }
        Str *p = parts[(ULONG)i];
        memcpy(r->s + pos, p->s, p->len * sizeof(WCHAR));
        pos += p->len;
    }
    *ret = v_str(r);
    return RC_OK;
}

static int array_join(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    Str *sep;
    if (argc < 1 || args[0].t == T_UNDEF)
        sep = S(e, ",");
    else if ((rc = to_string(e, &args[0], &sep)))
        return rc;
    return array_join_impl(e, o, sep, FALSE, ret);
}

static int array_to_string(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    if (o->cls != C_ARRAY)
        return throw_error(e, ERR_TYPE, 5031, nullptr);
    return array_join_impl(e, o, S(e, ","), FALSE, ret);
}

static int array_to_locale_string(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    return array_join_impl(e, o, S(e, ", "), TRUE, ret);
}

/* ------------------------------------------------------------------ mutators */

static int array_push_fn(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    double len;
    if ((rc = a_len(e, o, &len)))
        return rc;
    for (int i = 0; i < argc; i++) {
        if (o->cls == C_ARRAY && len == o->u.arr.len) {
            array_push(e, o, args[i]);
            len++;
        } else if ((rc = a_put(e, o, len++, &args[i])))
            return rc;
    }
    if ((rc = a_set_len(e, o, len)))
        return rc;
    *ret = v_num(len);
    return RC_OK;
}

static int array_pop(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    double len;
    if ((rc = a_len(e, o, &len)))
        return rc;
    if (len == 0) {
        if ((rc = a_set_len(e, o, 0)))
            return rc;
        *ret = UNDEF;
        return RC_OK;
    }
    if ((rc = a_get(e, o, len - 1, ret)))
        return rc;
    a_del(e, o, len - 1);
    return a_set_len(e, o, len - 1);
}

static int array_shift(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    double len;
    if ((rc = a_len(e, o, &len)))
        return rc;
    if (len == 0) {
        *ret = UNDEF;
        return a_set_len(e, o, 0);
    }
    if ((rc = a_get(e, o, 0, ret)))
        return rc;
    if (o->cls == C_ARRAY && len <= o->u.arr.cap && len == o->u.arr.len) {
        memmove(&o->u.arr.items[0], &o->u.arr.items[1], ((ULONG)len - 1) * sizeof(Val));
        o->u.arr.items[(ULONG)len - 1].t = T_HOLE;
        o->u.arr.len = (ULONG)len - 1;
        return RC_OK;
    }
    for (double i = 1; i < len; i++) {
        if (a_has(e, o, i)) {
            Val v;
            if ((rc = a_get(e, o, i, &v)) || (rc = a_put(e, o, i - 1, &v)))
                return rc;
        } else
            a_del(e, o, i - 1);
    }
    a_del(e, o, len - 1);
    return a_set_len(e, o, len - 1);
}

static int array_unshift(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    double len;
    if ((rc = a_len(e, o, &len)))
        return rc;
    for (double i = len; i > 0; i--) {
        if (a_has(e, o, i - 1)) {
            Val v;
            if ((rc = a_get(e, o, i - 1, &v)) || (rc = a_put(e, o, i - 1 + argc, &v)))
                return rc;
        } else
            a_del(e, o, i - 1 + argc);
    }
    for (int i = 0; i < argc; i++)
        if ((rc = a_put(e, o, i, &args[i])))
            return rc;
    if ((rc = a_set_len(e, o, len + argc)))
        return rc;
    *ret = UNDEF;
    return RC_OK;
}

static int array_reverse(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    double len;
    if ((rc = a_len(e, o, &len)))
        return rc;
    double mid = floor(len / 2);
    for (double lo = 0; lo < mid; lo++) {
        double hi = len - lo - 1;
        BOOL hl = a_has(e, o, lo), hh = a_has(e, o, hi);
        Val vl = UNDEF, vh = UNDEF;
        if (hl && (rc = a_get(e, o, lo, &vl)))
            return rc;
        if (hh && (rc = a_get(e, o, hi, &vh)))
            return rc;
        if (hh) {
            if ((rc = a_put(e, o, lo, &vh)))
                return rc;
        } else
            a_del(e, o, lo);
        if (hl) {
            if ((rc = a_put(e, o, hi, &vl)))
                return rc;
        } else
            a_del(e, o, hi);
    }
    *ret = v_obj(o);
    return RC_OK;
}

static double rel_index(double v, double len, double dflt, BOOL have)
{
    if (!have)
        return dflt;
    v = to_integer_d(v);
    if (v < 0) {
        v += len;
        if (v < 0)
            v = 0;
    } else if (v > len)
        v = len;
    return v;
}

static int array_slice(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    double len, s = 0, en;
    if ((rc = a_len(e, o, &len)))
        return rc;
    Val a0 = ARG(0), a1 = ARG(1);
    if ((rc = to_number(e, &a0, &s)))
        return rc;
    s = rel_index(s, len, 0, TRUE);
    en = len;
    if (a1.t != T_UNDEF) {
        if ((rc = to_number(e, &a1, &en)))
            return rc;
        en = rel_index(en, len, len, TRUE);
    }
    Obj *r = array_new(e, 0);
    *ret = v_obj(r);
    double n = 0;
    for (double i = s; i < en; i++, n++) {
        if (a_has(e, o, i)) {
            Val v;
            if ((rc = a_get(e, o, i, &v)) || (rc = a_put(e, r, n, &v)))
                return rc;
        }
    }
    return array_set_length(e, r, en > s ? en - s : 0);
}

static int array_splice(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    double len, s = 0, dc = 0;
    if ((rc = a_len(e, o, &len)))
        return rc;
    Val a0 = ARG(0), a1 = ARG(1);
    if ((rc = to_number(e, &a0, &s)))
        return rc;
    s = rel_index(s, len, 0, TRUE);
    if (argc >= 2) {
        if ((rc = to_number(e, &a1, &dc)))
            return rc;
        dc = to_integer_d(dc);
        if (dc < 0)
            dc = 0;
        if (dc > len - s)
            dc = len - s;
    } else if (argc == 1)
        dc = len - s;
    int ic = argc > 2 ? argc - 2 : 0;
    Obj *removed = array_new(e, 0);
    *ret = v_obj(removed);
    for (double i = 0; i < dc; i++) {
        if (a_has(e, o, s + i)) {
            Val v;
            if ((rc = a_get(e, o, s + i, &v)) || (rc = a_put(e, removed, i, &v)))
                return rc;
        }
    }
    array_set_length(e, removed, dc);
    if (ic < dc) {
        for (double k = s; k < len - dc; k++) {
            if (a_has(e, o, k + dc)) {
                Val v;
                if ((rc = a_get(e, o, k + dc, &v)) || (rc = a_put(e, o, k + ic, &v)))
                    return rc;
            } else
                a_del(e, o, k + ic);
        }
        for (double k = len; k > len - dc + ic; k--)
            a_del(e, o, k - 1);
    } else if (ic > dc) {
        for (double k = len - dc; k > s; k--) {
            if (a_has(e, o, k + dc - 1)) {
                Val v;
                if ((rc = a_get(e, o, k + dc - 1, &v)) || (rc = a_put(e, o, k + ic - 1, &v)))
                    return rc;
            } else
                a_del(e, o, k + ic - 1);
        }
    }
    for (int i = 0; i < ic; i++)
        if ((rc = a_put(e, o, s + i, &args[i + 2])))
            return rc;
    return a_set_len(e, o, len - dc + ic);
}

static int array_concat(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    Obj *r = array_new(e, 0);
    *ret = v_obj(r);
    double n = 0;
    for (int k = -1; k < argc; k++) {
        Val item = k < 0 ? (self->t == T_OBJ ? v_obj(o) : *self) : args[k];
        if (item.t == T_OBJ && item.u.o->cls == C_ARRAY) {
            Obj *src = item.u.o;
            double len = src->u.arr.len;
            for (double i = 0; i < len; i++) {
                if (a_has(e, src, i)) {
                    Val v;
                    if ((rc = a_get(e, src, i, &v)) || (rc = a_put(e, r, n + i, &v)))
                        return rc;
                }
            }
            n += len;
        } else {
            if ((rc = a_put(e, r, n, &item)))
                return rc;
            n++;
        }
    }
    return array_set_length(e, r, n);
}

/* ------------------------------------------------------------------ sort */

typedef struct SortCtx {
    Engine *e;
    Val *cmp;
    int rc;
} SortCtx;

static int sort_compare(SortCtx *c, Val *a, Val *b, int *res)
{
    Engine *e = c->e;
    int rc;
    if (c->cmp->t != T_UNDEF) {
        Val argv[2] = {*a, *b}, self = UNDEF, r;
        if ((rc = call_function(e, c->cmp, &self, argv, 2, &r)))
            return rc;
        double d;
        if ((rc = to_number(e, &r, &d)))
            return rc;
        *res = d < 0 ? -1 : d > 0 ? 1 : 0;
        return RC_OK;
    }
    Str *x, *y;
    if ((rc = to_string(e, a, &x)) || (rc = to_string(e, b, &y)))
        return rc;
    *res = str_compare(x, y);
    return RC_OK;
}

static int merge_sort(SortCtx *c, Val *v, Val *tmp, ULONG n)
{
    if (n < 2)
        return RC_OK;
    ULONG mid = n / 2;
    int rc;
    if ((rc = merge_sort(c, v, tmp, mid)) || (rc = merge_sort(c, v + mid, tmp, n - mid)))
        return rc;
    ULONG i = 0, j = mid, k = 0;
    while (i < mid && j < n) {
        int r;
        if ((rc = sort_compare(c, &v[j], &v[i], &r)))
            return rc;
        if (r < 0)
            tmp[k++] = v[j++];
        else
            tmp[k++] = v[i++];
    }
    while (i < mid)
        tmp[k++] = v[i++];
    while (j < n)
        tmp[k++] = v[j++];
    memcpy(v, tmp, n * sizeof(Val));
    return RC_OK;
}

static int array_sort(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    Val cmp = ARG(0);
    if (argc > 1 || (argc == 1 && cmp.t != T_NULL && !is_callable(&cmp)))
        return throw_error(e, ERR_TYPE, 5014, nullptr);
    if (cmp.t == T_NULL)
        cmp = UNDEF;
    double len;
    if ((rc = a_len(e, o, &len)))
        return rc;
    Val *vals = gc_alloc(e, (len ? (SIZE_T)len : 1) * sizeof(Val), GC_VALS);
    Val *tmp = gc_alloc(e, (len ? (SIZE_T)len : 1) * sizeof(Val), GC_VALS);
    ULONG n = 0, total = (ULONG)len;
    for (double i = 0; i < len; i++) {
        if (!a_has(e, o, i))
            continue;
        Val v;
        if ((rc = a_get(e, o, i, &v)))
            return rc;
        if (v.t != T_UNDEF)
            vals[n++] = v;
    }
    SortCtx c = {e, &cmp, 0};
    if ((rc = merge_sort(&c, vals, tmp, n)))
        return rc;
    ULONG k = 0;
    for (; k < n; k++)
        if ((rc = a_put(e, o, k, &vals[k])))
            return rc;
    for (; k < total; k++)
        a_del(e, o, k);
    *ret = v_obj(o);
    return RC_OK;
}

void init_array(Engine *e)
{
    Obj *proto = obj_new(e, C_ARRAY, e->ObjectProto);
    e->ArrayProto = proto;
    Obj *ctor = obj_native(e, "Array", array_ctor, 1, 0);
    e->ArrayCtor = ctor;
    obj_define_z(e, ctor, "prototype", v_obj(proto), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    obj_define_z(e, proto, "constructor", v_obj(ctor), PF_DONTENUM);
    obj_method(e, proto, "toString", array_to_string, 0, 0);
    obj_method(e, proto, "toLocaleString", array_to_locale_string, 0, 0);
    obj_method(e, proto, "concat", array_concat, 1, 0);
    obj_method(e, proto, "join", array_join, 1, 0);
    obj_method(e, proto, "pop", array_pop, 0, 0);
    obj_method(e, proto, "push", array_push_fn, 1, 0);
    obj_method(e, proto, "reverse", array_reverse, 0, 0);
    obj_method(e, proto, "shift", array_shift, 0, 0);
    obj_method(e, proto, "slice", array_slice, 2, 0);
    obj_method(e, proto, "sort", array_sort, 1, 0);
    obj_method(e, proto, "splice", array_splice, 2, 0);
    obj_method(e, proto, "unshift", array_unshift, 1, 0);
    obj_define_z(e, e->Global, "Array", v_obj(ctor), PF_DONTENUM);
}
