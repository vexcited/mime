/*
 * the global object and its Object, Function, Boolean, Number, Math and Error parts.
 */
#include "js.h"

#define ARG(i) ((i) < argc ? args[i] : UNDEF)

static Str *S(Engine *e, const char *z)
{
    return str_intern_ascii(e, z);
}

static void def(Engine *e, Obj *o, const char *name, Val v)
{
    obj_define_z(e, o, name, v, PF_DONTENUM);
}

static void def_const(Engine *e, Obj *o, const char *name, Val v)
{
    obj_define_z(e, o, name, v, PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
}

/* the number of a value that must be one. */
static int arg_number(Engine *e, Val *args, int argc, int i, double *out)
{
    Val v = ARG(i);
    return to_number(e, &v, out);
}

/* ------------------------------------------------------------------ Object */

static int object_ctor(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Val v = ARG(0);
    if (v.t == T_UNDEF || v.t == T_NULL) {
        *ret = v_obj(obj_new(e, C_OBJECT, e->ObjectProto));
        return RC_OK;
    }
    Obj *o;
    int rc = to_object(e, &v, &o);
    if (rc)
        return rc;
    *ret = v_obj(o);
    return RC_OK;
}

static int object_to_string(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    WCHAR buf[64];
    const WCHAR *cls = L"Object";
    if (self->t == T_OBJ)
        cls = class_name(self->u.o);
    else if (self->t == T_STR)
        cls = L"String";
    else if (self->t == T_NUM)
        cls = L"Number";
    else if (self->t == T_BOOL)
        cls = L"Boolean";
    _snwprintf(buf, 64, L"[object %s]", cls);
    *ret = v_str(str_from_z(e, buf));
    return RC_OK;
}

static int object_to_locale_string(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    Val f;
    if ((rc = obj_get(e, o, S(e, "toString"), &f)))
        return rc;
    return call_function(e, &f, self, nullptr, 0, ret);
}

static int object_value_of(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    *ret = *self;
    return RC_OK;
}

static int object_has_own(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    Str *k;
    Val a = ARG(0);
    if ((rc = to_string(e, &a, &k)))
        return rc;
    *ret = v_bool(obj_has_own(e, o, k));
    return RC_OK;
}

static int object_is_prototype_of(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Val a = ARG(0);
    *ret = v_bool(FALSE);
    if (a.t != T_OBJ || self->t != T_OBJ)
        return RC_OK;
    for (Obj *p = a.u.o->proto; p; p = p->proto)
        if (p == self->u.o) {
            *ret = v_bool(TRUE);
            break;
        }
    return RC_OK;
}

static int object_prop_is_enum(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = to_object(e, self, &o);
    if (rc)
        return rc;
    Str *k;
    Val a = ARG(0);
    if ((rc = to_string(e, &a, &k)))
        return rc;
    Prop *p = obj_find_own(o, k);
    *ret = v_bool(p ? !(p->flags & PF_DONTENUM) : obj_has_own(e, o, k));
    return RC_OK;
}

void init_object(Engine *e)
{
    Obj *proto = e->ObjectProto;
    Obj *ctor = obj_native(e, "Object", object_ctor, 1, 0);
    def_const(e, ctor, "prototype", v_obj(proto));
    def(e, proto, "constructor", v_obj(ctor));
    obj_method(e, proto, "toString", object_to_string, 0, 0);
    obj_method(e, proto, "toLocaleString", object_to_locale_string, 0, 0);
    obj_method(e, proto, "valueOf", object_value_of, 0, 0);
    obj_method(e, proto, "hasOwnProperty", object_has_own, 1, 0);
    obj_method(e, proto, "isPrototypeOf", object_is_prototype_of, 1, 0);
    obj_method(e, proto, "propertyIsEnumerable", object_prop_is_enum, 1, 0);
    def(e, e->Global, "Object", v_obj(ctor));
}

/* ------------------------------------------------------------------ Function */

static Str *function_source(Engine *e, Obj *f)
{
    if (f->u.fn.node) {
        FuncNode *fn = f->u.fn.node;
        Unit *u = fn->unit;
        return str_new(e, u->text + fn->n.start, fn->n.end - fn->n.start);
    }
    Prop *p = obj_find_own(f, S(e, "\1name"));
    WCHAR buf[200];
    _snwprintf(buf, 200, L"\nfunction %s() {\n    [native code]\n}\n", p && p->v.t == T_STR ? p->v.u.s->s : L"");
    return str_from_z(e, buf);
}

static int function_to_string(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    if (self->t != T_OBJ || self->u.o->cls != C_FUNCTION)
        return throw_error(e, ERR_TYPE, 5002, nullptr);
    *ret = v_str(function_source(e, self->u.o));
    return RC_OK;
}

static int function_call(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    if (!is_callable(self))
        return throw_error(e, ERR_TYPE, 5002, nullptr);
    Val t = ARG(0);
    return call_function(e, self, &t, argc > 1 ? args + 1 : nullptr, argc > 1 ? argc - 1 : 0, ret);
}

static int function_apply(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    if (!is_callable(self))
        return throw_error(e, ERR_TYPE, 5002, nullptr);
    Val t = ARG(0), a = ARG(1);
    if (a.t == T_UNDEF || a.t == T_NULL)
        return call_function(e, self, &t, nullptr, 0, ret);
    if (a.t != T_OBJ || (a.u.o->cls != C_ARRAY && a.u.o->cls != C_ARGS))
        return throw_error(e, ERR_TYPE, 5028, nullptr);
    Val lv;
    int rc = obj_get(e, a.u.o, S(e, "length"), &lv);
    if (rc)
        return rc;
    ULONG n;
    if ((rc = to_uint32(e, &lv, &n)))
        return rc;
    Val *av = gc_alloc(e, (n ? n : 1) * sizeof(Val), GC_VALS);
    for (ULONG i = 0; i < n; i++) {
        if ((rc = obj_get_index(e, a.u.o, i, &av[i])))
            return rc;
    }
    return call_function(e, self, &t, av, (int)n, ret);
}

static int function_ctor(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    SIZE_T cap = 256;
    Str **parts = gc_alloc(e, (argc ? argc : 1) * sizeof(Str *), GC_ITEMS);
    for (int i = 0; i < argc; i++) {
        int rc = to_string(e, &args[i], &parts[i]);
        if (rc)
            return rc;
        cap += parts[i]->len + 2;
    }
    WCHAR *text = js_alloc(cap * sizeof(WCHAR));
    const WCHAR *pre = L"(function anonymous(";
    wcscpy(text, pre);
    SIZE_T n = wcslen(pre);
    for (int i = 0; i < argc - 1; i++) {
        if (i) {
            text[n++] = ',';
            text[n++] = ' ';
        }
        memcpy(text + n, parts[i]->s, parts[i]->len * sizeof(WCHAR));
        n += parts[i]->len;
    }
    wcscpy(text + n, L") {\n");
    n += 4;
    if (argc) {
        memcpy(text + n, parts[argc - 1]->s, parts[argc - 1]->len * sizeof(WCHAR));
        n += parts[argc - 1]->len;
    }
    wcscpy(text + n, L"\n})");
    n += 3;
    Str *code = str_new(e, text, n);
    js_free(text);
    Val r;
    int rc = eval_code(e, code, e->Global, nullptr, &r);
    if (rc)
        return rc;
    /* the source of the function is the text without the parentheses. */
    if (r.t == T_OBJ && r.u.o->cls == C_FUNCTION && r.u.o->u.fn.node) {
        FuncNode *f = r.u.o->u.fn.node;
        f->n.start = 1;
        f->n.end = (ULONG)(code->len - 1);
    }
    *ret = r;
    return RC_OK;
}

void init_function(Engine *e)
{
    Obj *proto = e->FunctionProto;
    Obj *ctor = obj_native(e, "Function", function_ctor, 1, 0);
    def_const(e, ctor, "prototype", v_obj(proto));
    def(e, proto, "constructor", v_obj(ctor));
    obj_method(e, proto, "toString", function_to_string, 0, 0);
    obj_method(e, proto, "call", function_call, 1, 0);
    obj_method(e, proto, "apply", function_apply, 2, 0);
    def(e, e->Global, "Function", v_obj(ctor));
}

/* ------------------------------------------------------------------ Error */

static const char *const g_error_names[ERR_COUNT] = {"Error",       "EvalError", "RangeError", "ReferenceError",
                                                      "SyntaxError", "TypeError", "URIError",   "RegExpError"};

static int error_ctor(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    int kind = e->Magic;
    Obj *err = obj_new(e, C_ERROR, e->ErrorProtos[kind]);
    Str *msg = str_empty(e);
    int rc;
    Val num = UNDEF;
    if (argc >= 2) {
        double d;
        if ((rc = to_number(e, &args[0], &d)))
            return rc;
        num = v_dbl(d);
        if ((rc = to_string(e, &args[1], &msg)))
            return rc;
    } else if (argc == 1) {
        if (args[0].t == T_NUM) {
            num = args[0];
        } else if ((rc = to_string(e, &args[0], &msg)))
            return rc;
    }
    if (kind == ERR_ERROR)
        obj_define_z(e, err, "description", v_str(msg), 0);
    obj_define_z(e, err, "message", v_str(msg), 0);
    if (num.t == T_NUM)
        obj_define_z(e, err, "number", num, 0);
    *ret = v_obj(err);
    return RC_OK;
}

void init_error(Engine *e)
{
    for (int k = 0; k < ERR_COUNT; k++) {
        Obj *proto = obj_new(e, C_OBJECT, k == ERR_ERROR ? e->ObjectProto : e->ErrorProtos[ERR_ERROR]);
        e->ErrorProtos[k] = proto;
        obj_define_z(e, proto, "name", v_str(S(e, g_error_names[k])), 0);
        obj_define_z(e, proto, "message", v_str(str_empty(e)), PF_DONTENUM);
        if (k == ERR_REGEXP)
            continue;
        Obj *ctor = obj_native(e, g_error_names[k], error_ctor, 1, k);
        e->ErrorCtors[k] = ctor;
        def_const(e, ctor, "prototype", v_obj(proto));
        def(e, proto, "constructor", v_obj(ctor));
        def(e, e->Global, g_error_names[k], v_obj(ctor));
    }
    e->ErrorProto = e->ErrorProtos[ERR_ERROR];
}

/* ------------------------------------------------------------------ Boolean */

static int boolean_ctor(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Val a = ARG(0);
    Val b = v_bool(to_boolean_val(&a));
    if (!construct) {
        *ret = b;
        return RC_OK;
    }
    Obj *o = obj_new(e, C_BOOLEAN, e->BooleanProto);
    o->u.prim.prim = b;
    *ret = v_obj(o);
    return RC_OK;
}

static int boolean_this(Engine *e, Val *self, Val *out)
{
    if (self->t == T_BOOL) {
        *out = *self;
        return RC_OK;
    }
    if (self->t == T_OBJ && self->u.o->cls == C_BOOLEAN) {
        *out = self->u.o->u.prim.prim;
        return RC_OK;
    }
    return throw_error(e, ERR_TYPE, 5010, nullptr);
}

static int boolean_to_string(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Val b;
    int rc = boolean_this(e, self, &b);
    if (rc)
        return rc;
    *ret = v_str(S(e, b.u.b ? "true" : "false"));
    return RC_OK;
}

static int boolean_value_of(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    return boolean_this(e, self, ret);
}

/* ------------------------------------------------------------------ Number */

static int number_ctor(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    double d = 0;
    BOOL raw = TRUE;
    if (argc) {
        int rc = to_number(e, &args[0], &d);
        if (rc)
            return rc;
        raw = args[0].t != T_NUM;
    }
    Val nv = raw ? v_dbl(d) : args[0];
    if (!construct) {
        *ret = nv;
        return RC_OK;
    }
    Obj *o = obj_new(e, C_NUMBER, e->NumberProto);
    o->u.prim.prim = nv;
    *ret = v_obj(o);
    return RC_OK;
}

static int number_this(Engine *e, Val *self, double *out)
{
    if (self->t == T_NUM) {
        *out = self->u.n;
        return RC_OK;
    }
    if (self->t == T_OBJ && self->u.o->cls == C_NUMBER) {
        *out = self->u.o->u.prim.prim.u.n;
        return RC_OK;
    }
    return throw_error(e, ERR_TYPE, 5001, nullptr);
}

static int number_to_string(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    double d;
    int rc = number_this(e, self, &d);
    if (rc)
        return rc;
    double radix = 10;
    if (argc && args[0].t != T_UNDEF) {
        if ((rc = to_number(e, &args[0], &radix)))
            return rc;
        radix = to_integer_d(radix);
        if (radix < 2 || radix > 36)
            return throw_error(e, ERR_RANGE, 5, nullptr);
    }
    *ret = v_str(radix == 10 ? num_to_str(e, d) : num_to_radix(e, d, (int)radix));
    return RC_OK;
}

static int number_value_of(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    double d;
    int rc = number_this(e, self, &d);
    if (rc)
        return rc;
    *ret = self->t == T_OBJ ? self->u.o->u.prim.prim : *self;
    (void)d;
    return RC_OK;
}

/* the formats of a number work on the shortest digits that read back as it, rounded half up, then padded with zeros. */
typedef struct Digits {
    char d[48];
    int n, E;
} Digits;

static void digits_of_number(double x, Digits *g)
{
    if (x == 0) {
        g->d[0] = '0';
        g->d[1] = 0;
        g->n = 1;
        g->E = 0;
        return;
    }
    g->E = shortest_digits(x, g->d);
    g->n = (int)strlen(g->d);
}

/* keeps the first keep digits, a carry out of the first one makes a one and moves the exponent. */
static void round_digits(Digits *g, int keep)
{
    if (keep >= g->n)
        return;
    BOOL up = g->d[keep] >= '5';
    g->n = keep;
    g->d[keep] = 0;
    if (!up)
        return;
    int i = keep - 1;
    while (i >= 0) {
        if (g->d[i] == '9')
            g->d[i--] = '0';
        else {
            g->d[i]++;
            return;
        }
    }
    memmove(g->d + 1, g->d, g->n + 1);
    g->d[0] = '1';
    g->n++;
    g->E++;
}

static char digit_at(const Digits *g, int i)
{
    return i >= 0 && i < g->n ? g->d[i] : '0';
}

static int number_to_fixed(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    double x;
    int rc = number_this(e, self, &x);
    if (rc)
        return rc;
    double f = 0;
    Val a = ARG(0);
    if ((rc = to_number(e, &a, &f)))
        return rc;
    f = to_integer_d(f);
    if (f < 0 || f > 20)
        return throw_error(e, ERR_RANGE, 5026, nullptr);
    if (is_nan(x) || is_inf(x)) {
        *ret = v_str(num_to_str(e, x));
        return RC_OK;
    }
    int fd = (int)f;
    BOOL neg = x < 0;
    if (neg)
        x = -x;
    Digits g;
    digits_of_number(x, &g);
    if (x != 0) {
        int keep = g.E + 1 + fd;
        if (keep < 0) {
            g.d[0] = '0';
            g.n = 1;
            g.E = -1 - fd;
            x = 0;
        } else if (keep == 0) {
            /* the rounding looks one digit too far here, only a run of nines carries. */
            if (g.d[0] == '9' && g.n > 1 && g.d[1] >= '5') {
                g.d[0] = '1';
                g.d[1] = 0;
                g.n = 1;
                g.E++;
            } else {
                g.d[0] = '0';
                g.n = 1;
                g.E = -1 - fd;
                x = 0;
            }
        } else
            round_digits(&g, keep);
    }
    char out[128];
    int o = 0;
    if (neg)
        out[o++] = '-';
    int intdigits = x == 0 ? 0 : g.E + 1;
    if (intdigits <= 0)
        out[o++] = '0';
    else
        for (int i = 0; i < intdigits; i++)
            out[o++] = x == 0 ? '0' : digit_at(&g, i);
    if (fd) {
        out[o++] = '.';
        for (int i = 0; i < fd; i++) {
            int idx = intdigits + i;
            out[o++] = x == 0 ? '0' : (idx >= 0 ? digit_at(&g, idx - (intdigits < 0 ? 0 : 0)) : '0');
        }
    }
    out[o] = 0;
    *ret = v_str(str_from_ascii(e, out));
    return RC_OK;
}

static int number_to_exp_prec(Engine *e, Val *self, Val *args, int argc, Val *ret, int mode)
{
    double x;
    int rc = number_this(e, self, &x);
    if (rc)
        return rc;
    Val a = ARG(0);
    double f = 0;
    BOOL have = a.t != T_UNDEF;
    if (have) {
        if ((rc = to_number(e, &a, &f)))
            return rc;
        f = to_integer_d(f);
    }
    if (is_nan(x) || is_inf(x)) {
        *ret = v_str(num_to_str(e, x));
        return RC_OK;
    }
    if (mode == 0) {
        if (have && (f < 0 || f > 20))
            return throw_error(e, ERR_RANGE, 5026, nullptr);
    } else {
        if (!have) {
            *ret = v_str(num_to_str(e, x));
            return RC_OK;
        }
        if (f < 1 || f > 21)
            return throw_error(e, ERR_RANGE, 5027, nullptr);
    }
    BOOL neg = x < 0;
    if (neg)
        x = -x;
    Digits g;
    digits_of_number(x, &g);
    int p = !have ? g.n : mode == 0 ? (int)f + 1 : (int)f;
    if (x != 0)
        round_digits(&g, p);
    char out[128];
    int o = 0;
    if (neg)
        out[o++] = '-';
    int E = g.E;
    BOOL expform = mode == 0 || E < -6 || E >= p;
    if (expform) {
        out[o++] = digit_at(&g, 0);
        if (p > 1) {
            out[o++] = '.';
            for (int i = 1; i < p; i++)
                out[o++] = digit_at(&g, i);
        }
        out[o++] = 'e';
        out[o++] = E < 0 ? '-' : '+';
        int ae = E < 0 ? -E : E;
        char t[8];
        int tn = 0;
        do {
            t[tn++] = (char)('0' + ae % 10);
            ae /= 10;
        } while (ae);
        while (tn)
            out[o++] = t[--tn];
    } else if (E >= 0) {
        for (int i = 0; i <= E; i++)
            out[o++] = digit_at(&g, i);
        if (p > E + 1) {
            out[o++] = '.';
            for (int i = E + 1; i < p; i++)
                out[o++] = digit_at(&g, i);
        }
    } else {
        out[o++] = '0';
        out[o++] = '.';
        for (int i = -1; i > E; i--)
            out[o++] = '0';
        for (int i = 0; i < p; i++)
            out[o++] = digit_at(&g, i);
    }
    out[o] = 0;
    *ret = v_str(str_from_ascii(e, out));
    return RC_OK;
}

static int number_to_exponential(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    return number_to_exp_prec(e, self, args, argc, ret, 0);
}

static int number_to_precision(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    return number_to_exp_prec(e, self, args, argc, ret, 1);
}

static int number_to_locale(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    double d;
    int rc = number_this(e, self, &d);
    if (rc)
        return rc;
    if (is_nan(d) || is_inf(d)) {
        *ret = v_str(num_to_str(e, d));
        return RC_OK;
    }
    Str *plain = num_to_str(e, d);
    WCHAR text[400];
    SIZE_T n = plain->len < 399 ? plain->len : 399;
    for (SIZE_T i = 0; i < n; i++)
        text[i] = plain->s[i];
    text[n] = 0;
    if (wcschr(text, 'e')) {
        *ret = v_str(plain);
        return RC_OK;
    }
    WCHAR out[500];
    int len = GetNumberFormatW(LOCALE_USER_DEFAULT, 0, text, nullptr, out, 500);
    if (!len)
        *ret = v_str(plain);
    else
        *ret = v_str(str_from_z(e, out));
    return RC_OK;
}

void init_number(Engine *e)
{
    Obj *proto = obj_new(e, C_NUMBER, e->ObjectProto);
    proto->u.prim.prim = v_num(0);
    e->NumberProto = proto;
    Obj *ctor = obj_native(e, "Number", number_ctor, 1, 0);
    def_const(e, ctor, "prototype", v_obj(proto));
    def(e, proto, "constructor", v_obj(ctor));
    obj_method(e, proto, "toString", number_to_string, 1, 0);
    obj_method(e, proto, "toLocaleString", number_to_locale, 0, 0);
    obj_method(e, proto, "valueOf", number_value_of, 0, 0);
    obj_method(e, proto, "toFixed", number_to_fixed, 1, 0);
    obj_method(e, proto, "toExponential", number_to_exponential, 1, 0);
    obj_method(e, proto, "toPrecision", number_to_precision, 1, 0);
    def_const(e, ctor, "MAX_VALUE", v_num(1.7976931348623157e308));
    def_const(e, ctor, "MIN_VALUE", v_num(5e-324));
    def_const(e, ctor, "NaN", v_num(js_nan));
    def_const(e, ctor, "NEGATIVE_INFINITY", v_num(-__builtin_inf()));
    def_const(e, ctor, "POSITIVE_INFINITY", v_num(__builtin_inf()));
    def(e, e->Global, "Number", v_obj(ctor));

    Obj *bproto = obj_new(e, C_BOOLEAN, e->ObjectProto);
    bproto->u.prim.prim = v_bool(FALSE);
    e->BooleanProto = bproto;
    Obj *bctor = obj_native(e, "Boolean", boolean_ctor, 1, 0);
    def_const(e, bctor, "prototype", v_obj(bproto));
    def(e, bproto, "constructor", v_obj(bctor));
    obj_method(e, bproto, "toString", boolean_to_string, 0, 0);
    obj_method(e, bproto, "valueOf", boolean_value_of, 0, 0);
    def(e, e->Global, "Boolean", v_obj(bctor));
}

/* ------------------------------------------------------------------ Math */

static int math_fn(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    double x = js_nan, y = js_nan;
    int rc;
    int m = e->Magic;
    if ((rc = arg_number(e, args, argc, 0, &x)))
        return rc;
    double r;
    switch (m) {
    case 0: r = fabs(x); break;
    case 1: r = (x > 1 || x < -1) ? js_nan : acos(x); break;
    case 2: r = (x > 1 || x < -1) ? js_nan : asin(x); break;
    case 3: r = atan(x); break;
    case 4:
        if ((rc = arg_number(e, args, argc, 1, &y)))
            return rc;
        r = atan2(x, y);
        break;
    case 5: r = ceil(x); break;
    case 6: r = is_inf(x) ? js_nan : cos(x); break;
    case 7: r = exp(x); break;
    case 8: r = floor(x); break;
    case 9: r = x < 0 ? js_nan : log(x); break;
    case 10:
        if ((rc = arg_number(e, args, argc, 1, &y)))
            return rc;
        if (is_nan(y))
            r = js_nan;
        else if (y == 0)
            r = 1;
        else if ((x == 1 || x == -1) && is_inf(y))
            r = js_nan;
        else
            r = pow(x, y);
        break;
    case 11: {
        if (is_nan(x))
            r = x;
        else if (is_inf(x) || x == 0 || (x == floor(x) && fabs(x) >= 4503599627370496.0))
            r = x;
        else {
            r = floor(x + 0.5);
            if (r == 0 && x < 0)
                r = -0.0;
        }
        break;
    }
    case 12: r = is_inf(x) ? js_nan : sin(x); break;
    case 13: r = x < 0 ? js_nan : sqrt(x); break;
    case 14: r = is_inf(x) ? js_nan : tan(x); break;
    default: r = js_nan;
    }
    *ret = v_num(r);
    return RC_OK;
}

static int math_minmax(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    BOOL is_max = e->Magic == 1;
    double r = is_max ? -__builtin_inf() : __builtin_inf();
    BOOL nan = FALSE;
    for (int i = 0; i < argc; i++) {
        double d;
        int rc = to_number(e, &args[i], &d);
        if (rc)
            return rc;
        if (is_nan(d))
            nan = TRUE;
        else if (is_max ? (d > r || (d == 0 && r == 0 && !__builtin_signbit(d))) : (d < r || (d == 0 && r == 0 && __builtin_signbit(d))))
            r = d;
    }
    *ret = v_num(nan ? js_nan : r);
    return RC_OK;
}

static int math_random(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    if (!e->RndInit) {
        e->RndSeed = (double)(GetTickCount() ^ (ULONG)(SIZE_T)e);
        e->RndInit = TRUE;
    }
    ULONGLONG s = (ULONGLONG)e->RndSeed;
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    e->RndSeed = (double)(s & 0xFFFFFFFFFFFFull);
    *ret = v_num((double)((s >> 11) & ((1ull << 53) - 1)) / 9007199254740992.0);
    return RC_OK;
}

void init_math(Engine *e)
{
    Obj *m = obj_new(e, C_MATH, e->ObjectProto);
    static const char *const names[] = {"abs", "acos", "asin", "atan", "atan2", "ceil", "cos", "exp", "floor", "log", "pow",
                                        "round", "sin", "sqrt", "tan"};
    static const int args_n[] = {1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1};
    for (int i = 0; i < 15; i++)
        obj_method(e, m, names[i], math_fn, args_n[i], i);
    obj_method(e, m, "max", math_minmax, 2, 1);
    obj_method(e, m, "min", math_minmax, 2, 0);
    obj_method(e, m, "random", math_random, 0, 0);
    def_const(e, m, "E", v_num(2.718281828459045));
    def_const(e, m, "LN10", v_num(2.302585092994046));
    def_const(e, m, "LN2", v_num(0.6931471805599453));
    def_const(e, m, "LOG2E", v_num(1.4426950408889634));
    def_const(e, m, "LOG10E", v_num(0.4342944819032518));
    def_const(e, m, "PI", v_num(3.141592653589793));
    def_const(e, m, "SQRT1_2", v_num(0.7071067811865476));
    def_const(e, m, "SQRT2", v_num(1.4142135623730951));
    def(e, e->Global, "Math", v_obj(m));
}

/* ------------------------------------------------------------------ the global functions */

static int global_eval(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    if (argc < 1 || args[0].t != T_STR) {
        *ret = argc ? args[0] : UNDEF;
        return RC_OK;
    }
    return eval_code(e, args[0].u.s, e->Global, nullptr, ret);
}

static int global_parse_int(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    Val a = ARG(0), rv = ARG(1);
    int rc = to_string(e, &a, &s);
    if (rc)
        return rc;
    LONG radix;
    if ((rc = to_int32(e, &rv, &radix)))
        return rc;
    const WCHAR *p = s->s, *end = s->s + s->len;
    while (p < end && is_js_space(*p))
        p++;
    BOOL neg = FALSE;
    if (p < end && (*p == '+' || *p == '-')) {
        neg = *p == '-';
        p++;
    }
    BOOL strip = TRUE;
    if (radix != 0) {
        if (radix < 2 || radix > 36) {
            *ret = v_num(js_nan);
            return RC_OK;
        }
        if (radix != 16)
            strip = FALSE;
    } else
        radix = 10;
    if (strip && p + 1 < end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        radix = 16;
    } else if (rv.t == T_UNDEF || (rv.t == T_NUM && rv.u.n == 0) || radix == 10) {
        if (rv.t == T_UNDEF || (rv.t == T_NUM && rv.u.n == 0))
            if (p < end && p[0] == '0' && p + 1 < end)
                radix = 8;
    }
    double v = 0;
    int digits = 0;
    char dec[400];
    for (; p < end; p++) {
        int d;
        WCHAR c = *p;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'z')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z')
            d = c - 'A' + 10;
        else
            break;
        if (d >= radix)
            break;
        v = v * radix + d;
        digits++;
        if (radix == 10 && digits < 400)
            dec[digits - 1] = (char)c;
    }
    if (radix == 10 && digits >= 16 && digits < 400) {
        dec[digits] = 0;
        v = strtod(dec, nullptr);
    }
    if (!digits) {
        *ret = v_num(js_pnan);
        return RC_OK;
    }
    *ret = v_num(neg ? -v : v);
    return RC_OK;
}

static int global_parse_float(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    Val a = ARG(0);
    int rc = to_string(e, &a, &s);
    if (rc)
        return rc;
    const WCHAR *p = s->s, *end = s->s + s->len;
    while (p < end && is_js_space(*p))
        p++;
    const WCHAR *st = p;
    if (p < end && (*p == '+' || *p == '-'))
        p++;
    if (end - p >= 8 && !wcsncmp(p, L"Infinity", 8)) {
        *ret = v_num(*st == '-' ? -__builtin_inf() : __builtin_inf());
        return RC_OK;
    }
    const WCHAR *ds = p;
    int digits = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        p++;
        digits++;
    }
    if (p < end && *p == '.') {
        p++;
        while (p < end && *p >= '0' && *p <= '9') {
            p++;
            digits++;
        }
    }
    if (!digits) {
        *ret = v_num(js_pnan);
        return RC_OK;
    }
    (void)ds;
    if (p < end && (*p == 'e' || *p == 'E')) {
        const WCHAR *q = p + 1;
        if (q < end && (*q == '+' || *q == '-'))
            q++;
        if (q < end && *q >= '0' && *q <= '9') {
            while (q < end && *q >= '0' && *q <= '9')
                q++;
            p = q;
        }
    }
    char buf[400];
    int n = 0;
    for (const WCHAR *c = st; c < p && n < 399; c++)
        buf[n++] = (char)*c;
    buf[n] = 0;
    *ret = v_num(strtod(buf, nullptr));
    return RC_OK;
}

static int global_is_nan(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    double d;
    int rc = arg_number(e, args, argc, 0, &d);
    if (rc)
        return rc;
    *ret = v_bool(is_nan(d));
    return RC_OK;
}

static int global_is_finite(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    double d;
    int rc = arg_number(e, args, argc, 0, &d);
    if (rc)
        return rc;
    *ret = v_bool(!is_nan(d) && !is_inf(d));
    return RC_OK;
}

static const char *const hexd = "0123456789ABCDEF";

static BOOL in_set(WCHAR c, const char *set)
{
    for (; *set; set++)
        if ((WCHAR)*set == c)
            return TRUE;
    return FALSE;
}

static int global_escape(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    Val a = ARG(0);
    int rc = to_string(e, &a, &s);
    if (rc)
        return rc;
    WCHAR *out = js_alloc((s->len * 6 + 1) * sizeof(WCHAR));
    SIZE_T n = 0;
    for (ULONG i = 0; i < s->len; i++) {
        WCHAR c = s->s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || in_set(c, "@*_+-./"))
            out[n++] = c;
        else if (c < 256) {
            out[n++] = '%';
            out[n++] = hexd[c >> 4];
            out[n++] = hexd[c & 15];
        } else {
            out[n++] = '%';
            out[n++] = 'u';
            out[n++] = hexd[c >> 12];
            out[n++] = hexd[(c >> 8) & 15];
            out[n++] = hexd[(c >> 4) & 15];
            out[n++] = hexd[c & 15];
        }
    }
    *ret = v_str(str_new(e, out, n));
    js_free(out);
    return RC_OK;
}

static int hexv(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int global_unescape(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    Val a = ARG(0);
    int rc = to_string(e, &a, &s);
    if (rc)
        return rc;
    WCHAR *out = js_alloc((s->len + 1) * sizeof(WCHAR));
    SIZE_T n = 0;
    for (ULONG i = 0; i < s->len; i++) {
        WCHAR c = s->s[i];
        if (c == '%') {
            if (i + 5 < s->len + 0 && s->s[i + 1] == 'u' && hexv(s->s[i + 2]) >= 0 && hexv(s->s[i + 3]) >= 0 &&
                hexv(s->s[i + 4]) >= 0 && hexv(s->s[i + 5]) >= 0) {
                c = (WCHAR)((hexv(s->s[i + 2]) << 12) | (hexv(s->s[i + 3]) << 8) | (hexv(s->s[i + 4]) << 4) | hexv(s->s[i + 5]));
                i += 5;
            } else if (i + 2 < s->len && hexv(s->s[i + 1]) >= 0 && hexv(s->s[i + 2]) >= 0) {
                c = (WCHAR)(hexv(s->s[i + 1]) * 16 + hexv(s->s[i + 2]));
                i += 2;
            }
        }
        out[n++] = c;
    }
    *ret = v_str(str_new(e, out, n));
    js_free(out);
    return RC_OK;
}

static int uri_encode(Engine *e, Val *args, int argc, Val *ret, const char *unescaped)
{
    Str *s;
    Val a = ARG(0);
    int rc = to_string(e, &a, &s);
    if (rc)
        return rc;
    WCHAR *out = js_alloc((s->len * 9 + 1) * sizeof(WCHAR));
    SIZE_T n = 0;
    for (ULONG i = 0; i < s->len; i++) {
        WCHAR c = s->s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || in_set(c, unescaped)) {
            out[n++] = c;
            continue;
        }
        ULONG cp = c;
        if (c >= 0xDC00 && c <= 0xDFFF) {
            js_free(out);
            return throw_error(e, ERR_URI, 5024, nullptr);
        }
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 >= s->len || s->s[i + 1] < 0xDC00 || s->s[i + 1] > 0xDFFF) {
                js_free(out);
                return throw_error(e, ERR_URI, 5024, nullptr);
            }
            cp = 0x10000 + (((ULONG)c - 0xD800) << 10) + (s->s[i + 1] - 0xDC00);
            i++;
        }
        BYTE bytes[4];
        int nb;
        if (cp < 0x80) { bytes[0] = (BYTE)cp; nb = 1; }
        else if (cp < 0x800) { bytes[0] = 0xC0 | (cp >> 6); bytes[1] = 0x80 | (cp & 63); nb = 2; }
        else if (cp < 0x10000) { bytes[0] = 0xE0 | (cp >> 12); bytes[1] = 0x80 | ((cp >> 6) & 63); bytes[2] = 0x80 | (cp & 63); nb = 3; }
        else { bytes[0] = 0xF0 | (cp >> 18); bytes[1] = 0x80 | ((cp >> 12) & 63); bytes[2] = 0x80 | ((cp >> 6) & 63); bytes[3] = 0x80 | (cp & 63); nb = 4; }
        for (int k = 0; k < nb; k++) {
            out[n++] = '%';
            out[n++] = hexd[bytes[k] >> 4];
            out[n++] = hexd[bytes[k] & 15];
        }
    }
    *ret = v_str(str_new(e, out, n));
    js_free(out);
    return RC_OK;
}

static int uri_decode(Engine *e, Val *args, int argc, Val *ret, const char *reserved)
{
    Str *s;
    Val a = ARG(0);
    int rc = to_string(e, &a, &s);
    if (rc)
        return rc;
    WCHAR *out = js_alloc((s->len + 1) * sizeof(WCHAR));
    SIZE_T n = 0;
    for (ULONG i = 0; i < s->len; i++) {
        WCHAR c = s->s[i];
        if (c != '%') {
            out[n++] = c;
            continue;
        }
        ULONG start = i;
        if (i + 2 >= s->len || hexv(s->s[i + 1]) < 0 || hexv(s->s[i + 2]) < 0)
            goto bad;
        BYTE b = (BYTE)(hexv(s->s[i + 1]) * 16 + hexv(s->s[i + 2]));
        i += 2;
        if (b < 0x80) {
            if (in_set(b, reserved)) {
                for (ULONG k = start; k <= i; k++)
                    out[n++] = s->s[k];
            } else
                out[n++] = b;
            continue;
        }
        int nb;
        ULONG cp;
        if ((b & 0xE0) == 0xC0) { nb = 1; cp = b & 0x1F; }
        else if ((b & 0xF0) == 0xE0) { nb = 2; cp = b & 0x0F; }
        else if ((b & 0xF8) == 0xF0) { nb = 3; cp = b & 0x07; }
        else
            goto bad;
        if (i + 3 * nb >= s->len + 0 && i + 3 * nb > s->len - 1 + 0)
            goto bad;
        for (int k = 0; k < nb; k++) {
            if (i + 3 >= s->len + 1 || s->s[i + 1] != '%' || hexv(s->s[i + 2]) < 0 || hexv(s->s[i + 3]) < 0)
                goto bad;
            BYTE cb = (BYTE)(hexv(s->s[i + 2]) * 16 + hexv(s->s[i + 3]));
            if ((cb & 0xC0) != 0x80)
                goto bad;
            cp = (cp << 6) | (cb & 0x3F);
            i += 3;
        }
        if ((nb == 1 && cp < 0x80) || (nb == 2 && cp < 0x800) || (nb == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            goto bad;
        if (cp < 0x10000)
            out[n++] = (WCHAR)cp;
        else {
            cp -= 0x10000;
            out[n++] = (WCHAR)(0xD800 + (cp >> 10));
            out[n++] = (WCHAR)(0xDC00 + (cp & 0x3FF));
        }
    }
    *ret = v_str(str_new(e, out, n));
    js_free(out);
    return RC_OK;
bad:
    js_free(out);
    return throw_error(e, ERR_URI, 5025, nullptr);
}

static int global_uri(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    switch (e->Magic) {
    case 0: return uri_encode(e, args, argc, ret, ";/?:@&=+$,#-_.!~*'()");
    case 1: return uri_encode(e, args, argc, ret, "-_.!~*'()");
    case 2: return uri_decode(e, args, argc, ret, ";/?:@&=+$,#");
    default: return uri_decode(e, args, argc, ret, "");
    }
}

static int global_script_engine(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    switch (e->Magic) {
    case 0: *ret = v_str(S(e, "JScript")); break;
    case 1: *ret = v_num(10); break;
    case 2: *ret = v_num(8); break;
    default: *ret = v_num(16384); break;
    }
    return RC_OK;
}

static int global_collect(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    gc_collect(e);
    *ret = UNDEF;
    return RC_OK;
}

static int debug_write(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    for (int i = 0; i < argc; i++) {
        Str *s;
        if (to_string(e, &args[i], &s))
            return RC_THROW;
        OutputDebugStringW(s->s);
    }
    if (e->Magic == 1)
        OutputDebugStringW(L"\n");
    *ret = UNDEF;
    return RC_OK;
}

void init_globals(Engine *e)
{
    Obj *dbg = obj_new(e, C_DEBUG, e->ObjectProto);
    obj_method(e, dbg, "write", debug_write, 0, 0);
    obj_method(e, dbg, "writeln", debug_write, 0, 1);
    obj_define_z(e, e->Global, "Debug", v_obj(dbg), PF_DONTENUM);
    Obj *g = e->Global;
    def_const(e, g, "NaN", v_num(js_pnan));
    def_const(e, g, "Infinity", v_num(__builtin_inf()));
    def_const(e, g, "undefined", UNDEF);
    Obj *ev = obj_native(e, "eval", global_eval, 1, -77);
    obj_define_z(e, g, "eval", v_obj(ev), PF_DONTENUM);
    obj_method(e, g, "parseInt", global_parse_int, 2, 0);
    obj_method(e, g, "parseFloat", global_parse_float, 1, 0);
    obj_method(e, g, "isNaN", global_is_nan, 1, 0);
    obj_method(e, g, "isFinite", global_is_finite, 1, 0);
    obj_method(e, g, "escape", global_escape, 1, 0);
    obj_method(e, g, "unescape", global_unescape, 1, 0);
    obj_method(e, g, "encodeURI", global_uri, 1, 0);
    obj_method(e, g, "encodeURIComponent", global_uri, 1, 1);
    obj_method(e, g, "decodeURI", global_uri, 1, 2);
    obj_method(e, g, "decodeURIComponent", global_uri, 1, 3);
    obj_method(e, g, "ScriptEngine", global_script_engine, 0, 0);
    obj_method(e, g, "ScriptEngineMajorVersion", global_script_engine, 0, 1);
    obj_method(e, g, "ScriptEngineMinorVersion", global_script_engine, 0, 2);
    obj_method(e, g, "ScriptEngineBuildVersion", global_script_engine, 0, 3);
    obj_method(e, g, "CollectGarbage", global_collect, 0, 0);
}
