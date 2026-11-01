/*
 * regular expressions: a backtracking matcher for the syntax of ECMAScript 3, and the RegExp object with the string
 * functions that use it.
 */
#include "js.h"

#include "regexp_core.inc"

/* ------------------------------------------------------------------ a growing buffer of text */

typedef struct Buf {
    WCHAR *s;
    SIZE_T len, cap;
} Buf;

static void buf_add(Buf *b, const WCHAR *t, SIZE_T n)
{
    if (b->len + n + 1 > b->cap) {
        SIZE_T cap = b->cap ? b->cap * 2 : 128;
        while (cap < b->len + n + 1)
            cap *= 2;
        b->s = js_realloc(b->s, cap * sizeof(WCHAR));
        b->cap = cap;
    }
    memcpy(b->s + b->len, t, n * sizeof(WCHAR));
    b->len += n;
}

static Str *buf_str(Engine *e, Buf *b)
{
    Str *s = str_new(e, b->s ? b->s : L"", b->len);
    js_free(b->s);
    b->s = nullptr;
    return s;
}

/* ------------------------------------------------------------------ the object */

static Str *S(Engine *e, const char *z)
{
    return str_intern_ascii(e, z);
}

Obj *make_regexp(Engine *e, Str *source, const WCHAR *flags, SIZE_T flag_len, BOOL *bad)
{
    BOOL g = FALSE, i = FALSE, m = FALSE;
    *bad = FALSE;
    for (SIZE_T k = 0; k < flag_len; k++) {
        WCHAR c = flags[k];
        BOOL *f = c == 'g' ? &g : c == 'i' ? &i : c == 'm' ? &m : nullptr;
        if (!f || *f) {
            *bad = TRUE;
            throw_error(e, ERR_REGEXP, 5017, nullptr);
            return nullptr;
        }
        *f = TRUE;
    }
    int error = 0;
    WCHAR *z = js_alloc((source->len + 1) * sizeof(WCHAR));
    memcpy(z, source->s, source->len * sizeof(WCHAR));
    Regex *re = regex_compile(z, i, m, &error);
    js_free(z);
    if (!re) {
        *bad = TRUE;
        throw_error(e, ERR_REGEXP, error ? error : 5017, nullptr);
        return nullptr;
    }
    Obj *o = obj_new(e, C_REGEXP, e->RegExpProto);
    o->u.re.re = re;
    o->u.re.source = source;
    o->u.re.flags = (g ? 1 : 0) | (i ? 2 : 0) | (m ? 4 : 0);
    obj_define(e, o, S(e, "source"), v_str(source), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    obj_define(e, o, S(e, "global"), v_bool(g), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    obj_define(e, o, S(e, "ignoreCase"), v_bool(i), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    obj_define(e, o, S(e, "multiline"), v_bool(m), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    obj_define(e, o, S(e, "lastIndex"), v_num(0), PF_DONTENUM | PF_DONTDELETE);
    return o;
}

static void remember(Engine *e, Str *input, LONG *caps, UINT groups)
{
    e->LastInput = input;
    e->LastMatchCount = groups + 1 > 10 ? 10 : groups + 1;
    for (UINT k = 0; k < 20 && k < (groups + 1) * 2; k++)
        e->LastCaptures[k] = (ULONG)caps[k];
    e->LastCapturesValid = TRUE;
}

BOOL regexp_static_get(Engine *e, Str *key, Val *out)
{
    if (!e->LastInput) {
        if (key->len == 5 && !wcscmp(key->s, L"index")) {
            *out = v_num(-1);
            return TRUE;
        }
        return FALSE;
    }
    Str *in = e->LastInput;
    const WCHAR *k = key->s;
    ULONG s0 = e->LastCaptures[0], e0 = e->LastCaptures[1];
    if (key->len == 2 && k[0] == '$' && k[1] >= '1' && k[1] <= '9') {
        UINT g = k[1] - '0';
        if (g < e->LastMatchCount && (LONG)e->LastCaptures[g * 2] >= 0)
            *out = v_str(str_new(e, in->s + e->LastCaptures[g * 2], e->LastCaptures[g * 2 + 1] - e->LastCaptures[g * 2]));
        else
            *out = v_str(str_empty(e));
        return TRUE;
    }
    if ((key->len == 9 && !wcscmp(k, L"lastMatch")) || (key->len == 2 && !wcscmp(k, L"$&"))) {
        *out = v_str(str_new(e, in->s + s0, e0 - s0));
        return TRUE;
    }
    if ((key->len == 11 && !wcscmp(k, L"leftContext")) || (key->len == 2 && !wcscmp(k, L"$`"))) {
        *out = v_str(str_new(e, in->s, s0));
        return TRUE;
    }
    if ((key->len == 12 && !wcscmp(k, L"rightContext")) || (key->len == 2 && !wcscmp(k, L"$'"))) {
        *out = v_str(str_new(e, in->s + e0, in->len - e0));
        return TRUE;
    }
    if ((key->len == 9 && !wcscmp(k, L"lastParen")) || (key->len == 2 && !wcscmp(k, L"$+"))) {
        if (e->LastMatchCount > 1) {
            UINT g = e->LastMatchCount - 1;
            if ((LONG)e->LastCaptures[g * 2] >= 0) {
                *out = v_str(str_new(e, in->s + e->LastCaptures[g * 2], e->LastCaptures[g * 2 + 1] - e->LastCaptures[g * 2]));
                return TRUE;
            }
        }
        *out = v_str(str_empty(e));
        return TRUE;
    }
    if (key->len == 5 && !wcscmp(k, L"index")) {
        *out = v_num(s0);
        return TRUE;
    }
    if (key->len == 9 && !wcscmp(k, L"lastIndex")) {
        *out = v_num(e0);
        return TRUE;
    }
    if ((key->len == 5 && !wcscmp(k, L"input")) || (key->len == 2 && !wcscmp(k, L"$_"))) {
        *out = v_str(in);
        return TRUE;
    }
    return FALSE;
}

BOOL regexp_static_put(Engine *e, Str *key, Val *v)
{
    if ((key->len == 5 && !wcscmp(key->s, L"input")) || (key->len == 2 && !wcscmp(key->s, L"$_"))) {
        Str *s;
        if (to_string(e, v, &s))
            return TRUE;
        e->LastInput = s;
        return TRUE;
    }
    return FALSE;
}

static int get_last_index(Engine *e, Obj *re, double *out)
{
    Val v;
    int rc = obj_get(e, re, S(e, "lastIndex"), &v);
    if (rc)
        return rc;
    double d;
    rc = to_number(e, &v, &d);
    if (rc)
        return rc;
    *out = to_integer_d(d);
    return RC_OK;
}

int regexp_exec(Engine *e, Obj *re, Str *input, Val *out, BOOL test_only, BOOL *matched)
{
    Regex *rx = re->u.re.re;
    BOOL global = (re->u.re.flags & 1) != 0;
    double li = 0;
    int rc;
    if (global && (rc = get_last_index(e, re, &li)))
        return rc;
    if (!global)
        li = 0;
    *matched = FALSE;
    if (li < 0 || li > input->len) {
        Val z = v_num(0);
        obj_put(e, re, S(e, "lastIndex"), &z);
        *out = v_null();
        return RC_OK;
    }
    LONG *caps = js_alloc((rx->Groups + 1) * 2 * sizeof(LONG));
    BOOL ok = regex_exec(rx, input->s, input->len, (LONG)li, caps);
    if (!ok) {
        js_free(caps);
        if (global) {
            Val z = v_num(0);
            obj_put(e, re, S(e, "lastIndex"), &z);
        }
        *out = v_null();
        return RC_OK;
    }
    *matched = TRUE;
    remember(e, input, caps, rx->Groups);
    if (global) {
        Val z = v_num(caps[1]);
        obj_put(e, re, S(e, "lastIndex"), &z);
    }
    if (test_only) {
        js_free(caps);
        *out = v_bool(TRUE);
        return RC_OK;
    }
    Obj *a = array_new(e, 0);
    *out = v_obj(a);
    for (UINT g = 0; g <= rx->Groups; g++) {
        if (caps[g * 2] >= 0)
            array_push(e, a, v_str(str_new(e, input->s + caps[g * 2], caps[g * 2 + 1] - caps[g * 2])));
        else
            array_push(e, a, UNDEF);
    }
    obj_define(e, a, S(e, "index"), v_num(caps[0]), 0);
    obj_define(e, a, S(e, "input"), v_str(input), 0);
    obj_define(e, a, S(e, "lastIndex"), v_num(caps[1]), 0);
    js_free(caps);
    return RC_OK;
}

/* ------------------------------------------------------------------ RegExp.prototype and the constructor */

static int re_check(Engine *e, Val *self, Obj **re)
{
    if (self->t != T_OBJ || self->u.o->cls != C_REGEXP)
        return throw_error(e, ERR_TYPE, 5016, nullptr);
    *re = self->u.o;
    return RC_OK;
}

static int re_ctor(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Val pv = argc > 0 ? args[0] : UNDEF, fv = argc > 1 ? args[1] : UNDEF;
    Str *src;
    const WCHAR *flags = L"";
    SIZE_T flen = 0;
    Str *fstr = nullptr;
    int rc;
    if (pv.t == T_OBJ && pv.u.o->cls == C_REGEXP) {
        if (fv.t != T_UNDEF)
            return throw_error(e, ERR_TYPE, 5016, nullptr);
        if (!construct) {
            *ret = pv;
            return RC_OK;
        }
        src = pv.u.o->u.re.source;
        WCHAR f[4];
        int n = 0;
        if (pv.u.o->u.re.flags & 1) f[n++] = 'g';
        if (pv.u.o->u.re.flags & 2) f[n++] = 'i';
        if (pv.u.o->u.re.flags & 4) f[n++] = 'm';
        BOOL bad;
        Obj *o = make_regexp(e, src, f, n, &bad);
        if (!o)
            return RC_THROW;
        *ret = v_obj(o);
        return RC_OK;
    }
    if (pv.t == T_UNDEF)
        src = str_empty(e);
    else if ((rc = to_string(e, &pv, &src)))
        return rc;
    if (fv.t != T_UNDEF) {
        if ((rc = to_string(e, &fv, &fstr)))
            return rc;
        flags = fstr->s;
        flen = fstr->len;
    }
    BOOL bad;
    Obj *o = make_regexp(e, src, flags, flen, &bad);
    if (!o)
        return RC_THROW;
    *ret = v_obj(o);
    return RC_OK;
}

static int re_exec(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *re;
    int rc = re_check(e, self, &re);
    if (rc)
        return rc;
    Str *s;
    if (argc < 1) {
        s = e->LastInput ? e->LastInput : str_intern_ascii(e, "undefined");
    } else if ((rc = to_string(e, &args[0], &s)))
        return rc;
    BOOL m;
    return regexp_exec(e, re, s, ret, e->Magic == 1, &m);
}

static int re_test(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *re;
    int rc = re_check(e, self, &re);
    if (rc)
        return rc;
    Str *s;
    if (argc < 1)
        s = e->LastInput ? e->LastInput : str_intern_ascii(e, "undefined");
    else if ((rc = to_string(e, &args[0], &s)))
        return rc;
    BOOL m = FALSE;
    Val r;
    rc = regexp_exec(e, re, s, &r, TRUE, &m);
    if (rc)
        return rc;
    *ret = v_bool(m);
    return RC_OK;
}

static int re_to_string(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *re;
    int rc = re_check(e, self, &re);
    if (rc)
        return rc;
    Buf b = {};
    buf_add(&b, L"/", 1);
    buf_add(&b, re->u.re.source->s, re->u.re.source->len);
    buf_add(&b, L"/", 1);
    if (re->u.re.flags & 1) buf_add(&b, L"g", 1);
    if (re->u.re.flags & 2) buf_add(&b, L"i", 1);
    if (re->u.re.flags & 4) buf_add(&b, L"m", 1);
    *ret = v_str(buf_str(e, &b));
    return RC_OK;
}

static int re_compile(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *re;
    int rc = re_check(e, self, &re);
    if (rc)
        return rc;
    Val r;
    if ((rc = re_ctor(e, self, args, argc, &r, TRUE)))
        return rc;
    Obj *n = r.u.o;
    if (re->u.re.re)
        regexp_free(re->u.re.re);
    re->u.re = n->u.re;
    n->u.re.re = nullptr;
    obj_define(e, re, S(e, "source"), v_str(re->u.re.source), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    obj_define(e, re, S(e, "global"), v_bool(re->u.re.flags & 1), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    obj_define(e, re, S(e, "ignoreCase"), v_bool((re->u.re.flags & 2) != 0), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    obj_define(e, re, S(e, "multiline"), v_bool((re->u.re.flags & 4) != 0), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    Val z = v_num(0);
    obj_put(e, re, S(e, "lastIndex"), &z);
    *ret = self ? *self : UNDEF;
    return RC_OK;
}

void init_regexp(Engine *e)
{
    Obj *proto = obj_new(e, C_OBJECT, e->ObjectProto);
    e->RegExpProto = proto;
    Obj *ctor = obj_native(e, "RegExp", re_ctor, 2, 0);
    e->RegExpCtor = ctor;
    obj_define_z(e, ctor, "prototype", v_obj(proto), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    obj_define_z(e, proto, "constructor", v_obj(ctor), PF_DONTENUM);
    obj_method(e, proto, "exec", re_exec, 1, 0);
    obj_method(e, proto, "toString", re_to_string, 0, 0);
    obj_method(e, proto, "compile", re_compile, 2, 0);
    obj_define_z(e, e->Global, "RegExp", v_obj(ctor), PF_DONTENUM);
    /* test answers a boolean: the magic of 1 is exec's test only mode, so give it its own function. */
    obj_method(e, proto, "test", re_test, 1, 0);
}

/* ------------------------------------------------------------------ string functions */

/* expands $-patterns of a replacement text. */
static void expand(Buf *b, Str *rep, Str *input, LONG *caps, UINT groups)
{
    for (ULONG i = 0; i < rep->len; i++) {
        WCHAR c = rep->s[i];
        if (c != '$' || i + 1 >= rep->len) {
            buf_add(b, &c, 1);
            continue;
        }
        WCHAR d = rep->s[i + 1];
        if (d == '$') {
            buf_add(b, L"$", 1);
            i++;
        } else if (d == '&') {
            buf_add(b, input->s + caps[0], caps[1] - caps[0]);
            i++;
        } else if (d == '`') {
            buf_add(b, input->s, caps[0]);
            i++;
        } else if (d == '\'') {
            buf_add(b, input->s + caps[1], input->len - caps[1]);
            i++;
        } else if (d >= '0' && d <= '9') {
            UINT g = d - '0';
            ULONG used = 1;
            if (i + 2 < rep->len && rep->s[i + 2] >= '0' && rep->s[i + 2] <= '9') {
                UINT g2 = g * 10 + (rep->s[i + 2] - '0');
                if (g2 >= 1 && g2 <= groups) {
                    g = g2;
                    used = 2;
                }
            }
            if (g >= 1 && g <= groups) {
                if (caps[g * 2] >= 0)
                    buf_add(b, input->s + caps[g * 2], caps[g * 2 + 1] - caps[g * 2]);
                i += used;
            } else
                buf_add(b, &c, 1);
        } else
            buf_add(b, &c, 1);
    }
}

int string_replace_regexp(Engine *e, Str *s, Obj *re, Val *repl, Val *ret)
{
    Regex *rx = re->u.re.re;
    BOOL global = (re->u.re.flags & 1) != 0;
    Str *rep = nullptr;
    int rc;
    BOOL fn = is_callable(repl);
    if (!fn && (rc = to_string(e, repl, &rep)))
        return rc;
    Buf b = {};
    LONG pos = 0, last = 0;
    LONG *caps = js_alloc((rx->Groups + 1) * 2 * sizeof(LONG));
    if (global) {
        Val z = v_num(0);
        obj_put(e, re, S(e, "lastIndex"), &z);
    }
    for (;;) {
        if (pos > (LONG)s->len)
            break;
        if (!regex_exec(rx, s->s, s->len, pos, caps))
            break;
        remember(e, s, caps, rx->Groups);
        buf_add(&b, s->s + last, caps[0] - last);
        if (fn) {
            Val argv[40];
            int n = 0;
            UINT groups = rx->Groups > 30 ? 30 : rx->Groups;
            for (UINT g = 0; g <= groups; g++)
                argv[n++] = caps[g * 2] >= 0 ? v_str(str_new(e, s->s + caps[g * 2], caps[g * 2 + 1] - caps[g * 2])) : v_str(str_empty(e));
            argv[n++] = v_num(caps[0]);
            argv[n++] = v_str(s);
            Val self = UNDEF, r;
            rc = call_function(e, repl, &self, argv, n, &r);
            if (rc) {
                js_free(caps);
                js_free(b.s);
                return rc;
            }
            Str *rs;
            if ((rc = to_string(e, &r, &rs))) {
                js_free(caps);
                js_free(b.s);
                return rc;
            }
            buf_add(&b, rs->s, rs->len);
        } else
            expand(&b, rep, s, caps, rx->Groups);
        last = caps[1];
        pos = caps[1] == caps[0] ? caps[1] + 1 : caps[1];
        if (caps[1] == caps[0] && caps[0] < (LONG)s->len && global) {
            /* an empty match copies the character it stands before. */
        }
        if (!global)
            break;
    }
    js_free(caps);
    if (last < (LONG)s->len)
        buf_add(&b, s->s + last, s->len - last);
    *ret = v_str(buf_str(e, &b));
    return RC_OK;
}

int string_match_regexp(Engine *e, Str *s, Obj *re, Val *ret)
{
    if (!(re->u.re.flags & 1)) {
        BOOL m;
        return regexp_exec(e, re, s, ret, FALSE, &m);
    }
    Regex *rx = re->u.re.re;
    Obj *a = array_new(e, 0);
    LONG *caps = js_alloc((rx->Groups + 1) * 2 * sizeof(LONG));
    LONG pos = 0;
    Val z = v_num(0);
    obj_put(e, re, S(e, "lastIndex"), &z);
    while (pos <= (LONG)s->len && regex_exec(rx, s->s, s->len, pos, caps)) {
        remember(e, s, caps, rx->Groups);
        array_push(e, a, v_str(str_new(e, s->s + caps[0], caps[1] - caps[0])));
        pos = caps[1] == caps[0] ? caps[1] + 1 : caps[1];
    }
    js_free(caps);
    if (a->u.arr.len == 0)
        *ret = v_null();
    else
        *ret = v_obj(a);
    return RC_OK;
}

int string_search_regexp(Engine *e, Str *s, Obj *re, Val *ret)
{
    Regex *rx = re->u.re.re;
    LONG *caps = js_alloc((rx->Groups + 1) * 2 * sizeof(LONG));
    BOOL ok = regex_exec(rx, s->s, s->len, 0, caps);
    if (ok)
        remember(e, s, caps, rx->Groups);
    *ret = v_num(ok ? caps[0] : -1);
    js_free(caps);
    return RC_OK;
}

int string_split_regexp(Engine *e, Str *s, Obj *re, ULONG limit, Val *ret)
{
    Regex *rx = re->u.re.re;
    Obj *a = array_new(e, 0);
    *ret = v_obj(a);
    if (limit == 0)
        return RC_OK;
    LONG *caps = js_alloc((rx->Groups + 1) * 2 * sizeof(LONG));
    if (s->len == 0) {
        if (!regex_exec(rx, s->s, 0, 0, caps) || caps[1] != 0)
            array_push(e, a, v_str(s));
        js_free(caps);
        return RC_OK;
    }
    LONG p = 0, q = 0;
    while (q < (LONG)s->len) {
        /* the match must start exactly at q for the split position to count. */
        LONG *c2 = caps;
        BOOL found = FALSE;
        LONG from = q;
        while (from < (LONG)s->len && regex_exec(rx, s->s, s->len, from, c2)) {
            if (c2[1] == c2[0] && c2[0] >= (LONG)s->len)
                break;
            if (c2[1] == p) {
                from = c2[0] + 1;
                if (from <= (LONG)s->len && c2[0] == c2[1]) {
                    /* an empty match at the end of the last piece is skipped. */
                    continue;
                }
                continue;
            }
            found = TRUE;
            break;
        }
        if (!found)
            break;
        LONG ms = c2[0], me = c2[1];
        array_push(e, a, v_str(str_new(e, s->s + p, ms - p)));
        if (a->u.arr.len >= limit) {
            js_free(caps);
            return RC_OK;
        }
        p = me;
        q = me > ms ? me : ms + 1;
        if (me == ms)
            q = ms + 1;
    }
    array_push(e, a, v_str(str_new(e, s->s + p, s->len - p)));
    js_free(caps);
    return RC_OK;
}

int string_replace_string(Engine *e, Str *s, Str *t, Val *repl, Val *ret)
{
    LONG pos = -1;
    for (ULONG i = 0; i + t->len <= s->len; i++)
        if (!memcmp(s->s + i, t->s, t->len * sizeof(WCHAR))) {
            pos = (LONG)i;
            break;
        }
    if (pos < 0) {
        *ret = v_str(s);
        return RC_OK;
    }
    Buf b = {};
    buf_add(&b, s->s, pos);
    int rc;
    if (is_callable(repl)) {
        Val argv[3] = {v_str(t), v_num(pos), v_str(s)}, self = UNDEF, r;
        if ((rc = call_function(e, repl, &self, argv, 3, &r))) {
            js_free(b.s);
            return rc;
        }
        Str *rs;
        if ((rc = to_string(e, &r, &rs))) {
            js_free(b.s);
            return rc;
        }
        buf_add(&b, rs->s, rs->len);
    } else {
        Str *rep;
        if ((rc = to_string(e, repl, &rep))) {
            js_free(b.s);
            return rc;
        }
        buf_add(&b, rep->s, rep->len);
    }
    buf_add(&b, s->s + pos + t->len, s->len - pos - t->len);
    *ret = v_str(buf_str(e, &b));
    return RC_OK;
}
