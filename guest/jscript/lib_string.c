/*
 * String and its functions.
 */
#include "js.h"

#define ARG(i) ((i) < argc ? args[i] : UNDEF)

static int this_string(Engine *e, Val *self, Str **out)
{
    if (self->t == T_STR) {
        *out = self->u.s;
        return RC_OK;
    }
    return to_string(e, self, out);
}

static int string_ctor(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s = str_empty(e);
    if (argc) {
        int rc = to_string(e, &args[0], &s);
        if (rc)
            return rc;
    }
    if (!construct) {
        *ret = v_str(s);
        return RC_OK;
    }
    Obj *o = obj_new(e, C_STRING, e->StringProto);
    o->u.prim.prim = v_str(s);
    *ret = v_obj(o);
    return RC_OK;
}

static int string_to_string(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    if (self->t == T_STR) {
        *ret = *self;
        return RC_OK;
    }
    if (self->t == T_OBJ && self->u.o->cls == C_STRING) {
        *ret = self->u.o->u.prim.prim;
        return RC_OK;
    }
    return throw_error(e, ERR_TYPE, 5005, nullptr);
}

static int string_from_char_code(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s = str_alloc(e, argc);
    for (int i = 0; i < argc; i++) {
        ULONG v;
        int rc = to_uint32(e, &args[i], &v);
        if (rc)
            return rc;
        s->s[i] = (WCHAR)v;
    }
    *ret = v_str(s);
    return RC_OK;
}

static int string_char_at(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    int rc = this_string(e, self, &s);
    if (rc)
        return rc;
    double d = 0;
    Val a = ARG(0);
    if ((rc = to_number(e, &a, &d)))
        return rc;
    d = to_integer_d(d);
    if (e->Magic == 0) {
        *ret = d >= 0 && d < s->len ? v_str(str_new(e, &s->s[(ULONG)d], 1)) : v_str(str_empty(e));
    } else
        *ret = v_num(d >= 0 && d < s->len ? s->s[(ULONG)d] : js_nan);
    return RC_OK;
}

static int string_concat(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    int rc = this_string(e, self, &s);
    if (rc)
        return rc;
    for (int i = 0; i < argc; i++) {
        Str *t;
        if ((rc = to_string(e, &args[i], &t)))
            return rc;
        s = str_cat(e, s, t);
    }
    *ret = v_str(s);
    return RC_OK;
}

static int string_index_of(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s, *t;
    int rc = this_string(e, self, &s);
    if (rc)
        return rc;
    Val a = ARG(0), b = ARG(1);
    if ((rc = to_string(e, &a, &t)))
        return rc;
    double pos;
    if ((rc = to_number(e, &b, &pos)))
        return rc;
    BOOL last = e->Magic == 1;
    if (!last) {
        pos = to_integer_d(pos);
        if (pos < 0)
            pos = 0;
        if (pos > s->len)
            pos = s->len;
        for (ULONG i = (ULONG)pos; i + t->len <= s->len; i++)
            if (!memcmp(s->s + i, t->s, t->len * sizeof(WCHAR))) {
                *ret = v_num(i);
                return RC_OK;
            }
    } else {
        double start = is_nan(pos) ? __builtin_inf() : to_integer_d(pos);
        if (start < 0)
            start = 0;
        if (s->len < t->len) {
            *ret = v_num(-1);
            return RC_OK;
        }
        double maxs = s->len - t->len;
        if (start > maxs)
            start = maxs;
        for (long i = (long)start; i >= 0; i--)
            if (!memcmp(s->s + i, t->s, t->len * sizeof(WCHAR))) {
                *ret = v_num(i);
                return RC_OK;
            }
    }
    *ret = v_num(-1);
    return RC_OK;
}

static int string_locale_compare(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s, *t;
    int rc = this_string(e, self, &s);
    if (rc)
        return rc;
    Val a = ARG(0);
    if ((rc = to_string(e, &a, &t)))
        return rc;
    int r = CompareStringW(LOCALE_USER_DEFAULT, 0, s->s, s->len, t->s, t->len);
    *ret = v_num(r == CSTR_LESS_THAN ? -1 : r == CSTR_GREATER_THAN ? 1 : 0);
    return RC_OK;
}

static Obj *regexp_arg(Engine *e, Val *v, int *rc)
{
    *rc = RC_OK;
    if (v->t == T_OBJ && v->u.o->cls == C_REGEXP)
        return v->u.o;
    Val args[1] = {*v}, r, self = UNDEF;
    if (v->t == T_UNDEF)
        args[0] = v_str(str_empty(e));
    Val ctor = v_obj(e->RegExpCtor);
    *rc = construct_function(e, &ctor, args, 1, &r);
    (void)self;
    return *rc ? nullptr : r.u.o;
}

static int string_match(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    int rc = this_string(e, self, &s);
    if (rc)
        return rc;
    Val a = ARG(0);
    Obj *re = regexp_arg(e, &a, &rc);
    if (rc)
        return rc;
    return string_match_regexp(e, s, re, ret);
}

static int string_search(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    int rc = this_string(e, self, &s);
    if (rc)
        return rc;
    Val a = ARG(0);
    Obj *re = regexp_arg(e, &a, &rc);
    if (rc)
        return rc;
    return string_search_regexp(e, s, re, ret);
}

static int string_replace(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    int rc = this_string(e, self, &s);
    if (rc)
        return rc;
    Val a = ARG(0), b = ARG(1);
    if (a.t == T_OBJ && a.u.o->cls == C_REGEXP)
        return string_replace_regexp(e, s, a.u.o, &b, ret);
    Str *t;
    if ((rc = to_string(e, &a, &t)))
        return rc;
    return string_replace_string(e, s, t, &b, ret);
}

static int string_slice(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    int rc = this_string(e, self, &s);
    if (rc)
        return rc;
    double len = s->len, from = 0, to = len;
    Val a = ARG(0), b = ARG(1);
    if ((rc = to_number(e, &a, &from)))
        return rc;
    from = to_integer_d(from);
    if (from < 0) {
        from += len;
        if (from < 0)
            from = 0;
    } else if (from > len)
        from = len;
    if (b.t != T_UNDEF) {
        if ((rc = to_number(e, &b, &to)))
            return rc;
        to = to_integer_d(to);
        if (to < 0) {
            to += len;
            if (to < 0)
                to = 0;
        } else if (to > len)
            to = len;
    }
    *ret = to > from ? v_str(str_new(e, s->s + (ULONG)from, (SIZE_T)(to - from))) : v_str(str_empty(e));
    return RC_OK;
}

static int string_substring(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    int rc = this_string(e, self, &s);
    if (rc)
        return rc;
    double len = s->len, from = 0, to = len;
    Val a = ARG(0), b = ARG(1);
    if ((rc = to_number(e, &a, &from)))
        return rc;
    from = to_integer_d(from);
    from = from < 0 ? 0 : from > len ? len : from;
    if (b.t != T_UNDEF) {
        if ((rc = to_number(e, &b, &to)))
            return rc;
        to = to_integer_d(to);
        to = to < 0 ? 0 : to > len ? len : to;
    }
    if (from > to) {
        double t = from;
        from = to;
        to = t;
    }
    *ret = v_str(str_new(e, s->s + (ULONG)from, (SIZE_T)(to - from)));
    return RC_OK;
}

static int string_substr(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    int rc = this_string(e, self, &s);
    if (rc)
        return rc;
    double len = s->len, from = 0, n = len;
    Val a = ARG(0), b = ARG(1);
    if ((rc = to_number(e, &a, &from)))
        return rc;
    from = to_integer_d(from);
    if (from < 0)
        from = 0;
    if (from > len)
        from = len;
    if (b.t != T_UNDEF) {
        if ((rc = to_number(e, &b, &n)))
            return rc;
        n = to_integer_d(n);
    } else
        n = len;
    if (n < 0)
        n = 0;
    if (n > len - from)
        n = len - from;
    *ret = v_str(str_new(e, s->s + (ULONG)from, (SIZE_T)n));
    return RC_OK;
}

static int string_case(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    int rc = this_string(e, self, &s);
    if (rc)
        return rc;
    BOOL upper = e->Magic == 1;
    Str *r = str_alloc(e, s->len);
    for (ULONG i = 0; i < s->len; i++)
        r->s[i] = upper ? (WCHAR)towupper(s->s[i]) : (WCHAR)towlower(s->s[i]);
    *ret = v_str(r);
    return RC_OK;
}

static int string_split(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    int rc = this_string(e, self, &s);
    if (rc)
        return rc;
    Val sep = ARG(0), lim = ARG(1);
    ULONG limit = 0xFFFFFFFF;
    if (lim.t != T_UNDEF && (rc = to_uint32(e, &lim, &limit)))
        return rc;
    if (sep.t == T_OBJ && sep.u.o->cls == C_REGEXP)
        return string_split_regexp(e, s, sep.u.o, limit, ret);
    Obj *a = array_new(e, 0);
    *ret = v_obj(a);
    if (sep.t == T_UNDEF) {
        if (limit)
            array_push(e, a, v_str(s));
        return RC_OK;
    }
    Str *t;
    if ((rc = to_string(e, &sep, &t)))
        return rc;
    if (limit == 0)
        return RC_OK;
    if (t->len == 0) {
        for (ULONG i = 0; i < s->len && a->u.arr.len < limit; i++)
            array_push(e, a, v_str(str_new(e, &s->s[i], 1)));
        return RC_OK;
    }
    ULONG p = 0;
    for (ULONG i = 0; i + t->len <= s->len;) {
        if (!memcmp(s->s + i, t->s, t->len * sizeof(WCHAR))) {
            array_push(e, a, v_str(str_new(e, s->s + p, i - p)));
            if (a->u.arr.len >= limit)
                return RC_OK;
            i += t->len;
            p = i;
        } else
            i++;
    }
    array_push(e, a, v_str(str_new(e, s->s + p, s->len - p)));
    return RC_OK;
}

static int string_html(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    static const struct {
        const char *Tag, *Attr;
    } table[] = {
        {"A", "NAME"}, {"BIG", nullptr}, {"BLINK", nullptr}, {"B", nullptr}, {"TT", nullptr}, {"FONT", "COLOR"},
        {"FONT", "SIZE"}, {"I", nullptr}, {"A", "HREF"}, {"SMALL", nullptr}, {"STRIKE", nullptr}, {"SUB", nullptr},
        {"SUP", nullptr},
    };
    int m = e->Magic;
    Str *s;
    int rc = this_string(e, self, &s);
    if (rc)
        return rc;
    WCHAR head[300];
    SIZE_T n = 0;
    head[n++] = '<';
    for (const char *c = table[m].Tag; *c; c++)
        head[n++] = (BYTE)*c;
    if (table[m].Attr) {
        Str *v;
        Val a = ARG(0);
        if ((rc = to_string(e, &a, &v)))
            return rc;
        head[n++] = ' ';
        for (const char *c = table[m].Attr; *c; c++)
            head[n++] = (BYTE)*c;
        head[n++] = '=';
        head[n++] = '"';
        for (ULONG i = 0; i < v->len && n < 280; i++)
            head[n++] = v->s[i];
        head[n++] = '"';
    }
    head[n++] = '>';
    WCHAR tail[20];
    SIZE_T t = 0;
    tail[t++] = '<';
    tail[t++] = '/';
    for (const char *c = table[m].Tag; *c; c++)
        tail[t++] = (BYTE)*c;
    tail[t++] = '>';
    Str *r = str_alloc(e, n + s->len + t);
    memcpy(r->s, head, n * sizeof(WCHAR));
    memcpy(r->s + n, s->s, s->len * sizeof(WCHAR));
    memcpy(r->s + n + s->len, tail, t * sizeof(WCHAR));
    *ret = v_str(r);
    return RC_OK;
}

void init_string(Engine *e)
{
    Obj *proto = obj_new(e, C_STRING, e->ObjectProto);
    proto->u.prim.prim = v_str(str_empty(e));
    e->StringProto = proto;
    Obj *ctor = obj_native(e, "String", string_ctor, 1, 0);
    obj_define_z(e, ctor, "prototype", v_obj(proto), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    obj_define_z(e, proto, "constructor", v_obj(ctor), PF_DONTENUM);
    obj_method(e, ctor, "fromCharCode", string_from_char_code, 1, 0);
    obj_method(e, proto, "toString", string_to_string, 0, 0);
    obj_method(e, proto, "valueOf", string_to_string, 0, 0);
    obj_method(e, proto, "charAt", string_char_at, 1, 0);
    obj_method(e, proto, "charCodeAt", string_char_at, 1, 1);
    obj_method(e, proto, "concat", string_concat, 1, 0);
    obj_method(e, proto, "indexOf", string_index_of, 1, 0);
    obj_method(e, proto, "lastIndexOf", string_index_of, 1, 1);
    obj_method(e, proto, "localeCompare", string_locale_compare, 1, 0);
    obj_method(e, proto, "match", string_match, 1, 0);
    obj_method(e, proto, "replace", string_replace, 2, 0);
    obj_method(e, proto, "search", string_search, 1, 0);
    obj_method(e, proto, "slice", string_slice, 2, 0);
    obj_method(e, proto, "split", string_split, 2, 0);
    obj_method(e, proto, "substring", string_substring, 2, 0);
    obj_method(e, proto, "substr", string_substr, 2, 0);
    obj_method(e, proto, "toLowerCase", string_case, 0, 0);
    obj_method(e, proto, "toLocaleLowerCase", string_case, 0, 0);
    obj_method(e, proto, "toUpperCase", string_case, 0, 1);
    obj_method(e, proto, "toLocaleUpperCase", string_case, 0, 1);
    static const char *const html[] = {"anchor", "big", "blink", "bold", "fixed", "fontcolor", "fontsize", "italics",
                                       "link", "small", "strike", "sub", "sup"};
    for (int i = 0; i < 13; i++)
        obj_method(e, proto, html[i], string_html, i == 0 || i == 5 || i == 6 || i == 8 ? 1 : 0, i);
    obj_define_z(e, e->Global, "String", v_obj(ctor), PF_DONTENUM);
}
