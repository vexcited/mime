/*
 * the evaluator: expressions and statements are run by walking the tree over a chain of scope objects.
 */
#include "js.h"

enum { PARSE_PROGRAM, PARSE_EVAL, PARSE_FUNCTION };

typedef struct Env {
    Engine *e;
    Obj *scope;
    Val self;
    Obj *func;
    Obj *var_scope;
    Str *label;
    Str *my_labels[8];
    int my_count;
    BOOL want_value;
} Env;

/* true when the stack of the thread is nearly used up. */
static BOOL stack_low(void)
{
    SIZE_T sp, limit;
    char marker;
    sp = (SIZE_T)&marker;
#ifdef _WIN64
    __asm__ volatile("movq %%gs:0x1478, %0" : "=r"(limit));
#else
    __asm__ volatile("movl %%fs:0xE0C, %0" : "=r"(limit));
#endif
    return sp < limit + 160 * 1024;
}

/* the site is asked now and then whether the script may go on. */
static BOOL poll_site(Engine *e)
{
    if (!e->Site)
        return FALSE;
    if (!e->PollSite) {
        IActiveScriptSiteInterruptPoll *poll = nullptr;
        if (SUCCEEDED(e->Site->lpVtbl->QueryInterface(e->Site, &IID_IActiveScriptSiteInterruptPoll, (void **)&poll)))
            e->PollSite = poll;
        else
            e->PollSite = (IActiveScriptSiteInterruptPoll *)(SIZE_T)1;
    }
    if (e->PollSite == (IActiveScriptSiteInterruptPoll *)(SIZE_T)1)
        return FALSE;
    return e->PollSite->lpVtbl->QueryContinue(e->PollSite) == S_FALSE;
}

static int eval_ctx(Env *env, Node *n, Val *out, int ctx);
static int exec(Env *env, Node *n, Val *completion);

#define EVAL(env, n, out) eval_ctx((env), (n), (out), 0)

static Str *S(Engine *e, const char *z)
{
    return str_intern_ascii(e, z);
}

/* ------------------------------------------------------------------ scopes */

static Obj *scope_new(Engine *e, int cls, Obj *outer)
{
    Obj *o = obj_new(e, cls, nullptr);
    o->u.scope.outer = outer;
    return o;
}

static int lookup_item(Engine *e, Str *name, Val *out, BOOL *found)
{
    *found = FALSE;
    for (NamedItem *it = e->Items; it; it = it->next) {
        if ((it->flags & SCRIPTITEM_ISVISIBLE) && (it->name == name || (it->name->len == name->len && !_wcsicmp(it->name->s, name->s)))) {
            if (it->value.t == T_UNDEF && !(it->flags & 0x80000000)) {
                int rc = item_load(e, it);
                if (rc)
                    return rc;
            }
            if (it->value.t == T_OBJ) {
                *out = it->value;
                *found = TRUE;
                return RC_OK;
            }
        }
    }
    for (NamedItem *it = e->Items; it; it = it->next) {
        if (it->flags & SCRIPTITEM_GLOBALMEMBERS) {
            if (it->value.t == T_UNDEF)
                item_load(e, it);
            if (it->value.t != T_OBJ)
                continue;
            BOOL f2 = FALSE;
            int rc = host_get(e, it->value.u.o, name, out, &f2);
            if (rc)
                return rc;
            if (f2) {
                *found = TRUE;
                return RC_OK;
            }
        }
    }
    return RC_OK;
}

/* finds a name along the scope chain: *base is the object that holds it when it is a with object. */
static int ident_get_scope(Env *env, Str *name, Val *out, Obj **with_base, BOOL *found)
{
    Engine *e = env->e;
    *found = FALSE;
    if (with_base)
        *with_base = nullptr;
    for (Obj *s = env->scope; s; s = s->u.scope.outer) {
        if (s->cls == C_WITH) {
            Obj *t = s->u.scope.target;
            if (obj_has(e, t, name)) {
                if (with_base)
                    *with_base = t;
                *found = TRUE;
                return obj_get(e, t, name, out);
            }
        } else {
            Prop *p = obj_find_own(s, name);
            if (p) {
                *out = p->v;
                *found = TRUE;
                return RC_OK;
            }
            if (s->cls == C_GLOBAL)
                break;
        }
        if (s->cls == C_GLOBAL)
            break;
    }
    return RC_OK;
}

static int ident_get(Env *env, Str *name, Val *out, Obj **with_base, BOOL *found)
{
    int rc = ident_get_scope(env, name, out, with_base, found);
    if (rc || *found)
        return rc;
    return lookup_item(env->e, name, out, found);
}

static int ident_put(Env *env, Str *name, Val *v)
{
    Engine *e = env->e;
    for (Obj *s = env->scope; s; s = s->u.scope.outer) {
        if (s->cls == C_WITH) {
            Obj *t = s->u.scope.target;
            if (obj_has(e, t, name))
                return obj_put(e, t, name, v);
        } else {
            Prop *p = obj_find_own(s, name);
            if (p) {
                if (!(p->flags & PF_READONLY))
                    p->v = *v;
                return RC_OK;
            }
        }
        if (s->cls == C_GLOBAL)
            break;
    }
    /* a global item that is writable, such as a member of the host, takes the value. */
    for (NamedItem *it = e->Items; it; it = it->next)
        if ((it->flags & SCRIPTITEM_GLOBALMEMBERS) && it->value.t == T_OBJ) {
            BOOL f2 = FALSE;
            Val tmp;
            if (host_get_quiet(e, it->value.u.o, name, &tmp, &f2) == RC_OK && f2)
                return host_put(e, it->value.u.o, name, v);
        }
    obj_add_prop(e, e->Global, name, *v, 0);
    return RC_OK;
}

static int undefined_ident(Env *env, Str *name)
{
    return throw_error(env->e, ERR_TYPE, 5009, name->s);
}

/* ------------------------------------------------------------------ source text for messages */

/* the name of what a message talks about, as the text of the tree. */
typedef struct TextBuf {
    WCHAR s[300];
    ULONG n;
} TextBuf;

static void tb_add(TextBuf *t, const WCHAR *s, ULONG len)
{
    for (ULONG i = 0; i < len && t->n < 290; i++)
        t->s[t->n++] = s[i];
    t->s[t->n] = 0;
}

static void tb_add_z(TextBuf *t, const char *z)
{
    for (; *z && t->n < 290; z++)
        t->s[t->n++] = (BYTE)*z;
    t->s[t->n] = 0;
}

static void node_text(Engine *e, Node *n, TextBuf *t)
{
    switch (n->kind) {
    case N_IDENT:
        tb_add(t, n->u.str->s, n->u.str->len);
        break;
    case N_THIS:
        tb_add_z(t, "this");
        break;
    case N_NULL:
        tb_add_z(t, "null");
        break;
    case N_TRUE:
        tb_add_z(t, "true");
        break;
    case N_FALSE:
        tb_add_z(t, "false");
        break;
    case N_NUM:
        tb_add_z(t, "[number]");
        break;
    case N_STR:
        tb_add_z(t, "[string]");
        break;
    case N_MEMBER:
    case N_INDEX: {
        node_text(e, n->a, t);
        if (n->kind == N_MEMBER) {
            if (t->n)
                tb_add_z(t, ".");
            tb_add(t, n->u.str->s, n->u.str->len);
        } else if (n->b->kind == N_NUM || n->b->kind == N_STR) {
            if (t->n)
                tb_add_z(t, ".");
            Str *k = n->b->kind == N_NUM ? num_to_str(e, n->b->u.num) : n->b->u.str;
            tb_add(t, k->s, k->len);
        } else
            tb_add_z(t, "[...]");
        break;
    }
    case N_CALL:
        if (n->a->kind == N_IDENT)
            break;
        node_text(e, n->a, t);
        tb_add_z(t, n->u.count ? "(...)" : "()");
        break;
    case N_NEW:
        tb_add_z(t, "new ");
        node_text(e, n->a, t);
        tb_add_z(t, n->u.count ? "(...)" : "()");
        break;
    case N_LOGICAL:
    case N_COMMA:
        node_text(e, n->b, t);
        break;
    case N_UNARY:
        if (n->op == OP_VOID)
            tb_add_z(t, "undefined");
        break;
    default:
        break;
    }
}

/* a member of null or undefined: a read names the whole member, the other uses name the object. */
static int throw_null_base(Env *env, Node *n, Val *base, BOOL read)
{
    TextBuf t = {};
    node_text(env->e, read ? n : n->a, &t);
    if (!t.n)
        tb_add_z(&t, base->t == T_NULL ? "null" : "undefined");
    return throw_error(env->e, ERR_TYPE, 5007, t.s);
}

/* what cannot be assigned to. */
static int assign_error(Env *env, Node *t, BOOL compound)
{
    Engine *e = env->e;
    TextBuf b = {};
    (void)compound;
    switch (t->kind) {
    case N_CALL:
        return throw_error(e, ERR_REFERENCE, 5003, nullptr);
    case N_NUM: tb_add_z(&b, "[number]"); break;
    case N_STR: tb_add_z(&b, "[string]"); break;
    case N_THIS: tb_add_z(&b, "this"); break;
    case N_NULL: tb_add_z(&b, "null"); break;
    case N_TRUE: tb_add_z(&b, "true"); break;
    case N_FALSE: tb_add_z(&b, "false"); break;
    default: {
        Val v = UNDEF;
        if (!(t->flag & 4) || EVAL(env, t, &v)) {
            tb_add_z(&b, "[object]");
            break;
        }
        if (v.t == T_NUM) {
            Str *ns = num_to_str(e, v.u.n);
            tb_add_z(&b, "[number: ");
            tb_add(&b, ns->s, ns->len);
            tb_add_z(&b, "]");
        } else
            tb_add_z(&b, "[object]");
        break;
    }
    }
    return throw_error(e, ERR_REFERENCE, 5008, b.s);
}

/* ------------------------------------------------------------------ operators */

static int op_add(Engine *e, Val *a, Val *b, Val *out)
{
    Val pa, pb;
    BOOL none_a, none_b;
    int rc = to_primitive_soft(e, a, 0, &pa, &none_a);
    if (rc)
        return rc;
    rc = to_primitive_soft(e, b, 0, &pb, &none_b);
    if (rc)
        return rc;
    if (none_a || none_b) {
        *out = UNDEF;
        return RC_OK;
    }
    if (pa.t == T_STR || pb.t == T_STR) {
        Str *sa, *sb;
        if ((rc = to_string(e, &pa, &sa)) || (rc = to_string(e, &pb, &sb)))
            return rc;
        *out = v_str(str_cat(e, sa, sb));
        return RC_OK;
    }
    double x, y;
    if ((rc = to_number(e, &pa, &x)) || (rc = to_number(e, &pb, &y)))
        return rc;
    *out = v_num(x + y);
    return RC_OK;
}

static double js_mod(double x, double y)
{
    if (is_nan(x) || is_nan(y) || is_inf(x) || y == 0)
        return js_nan;
    if (is_inf(y))
        return x;
    if (x == 0)
        return x;
    return fmod(x, y);
}

static int op_arith(Engine *e, int op, Val *a, Val *b, Val *out)
{
    double x, y;
    int rc;
    if ((rc = to_number(e, a, &x)) || (rc = to_number(e, b, &y)))
        return rc;
    switch (op) {
    case '-': *out = v_num(x - y); break;
    case '*': *out = v_num(x * y); break;
    case '/': *out = v_num(x / y); break;
    case '%': *out = v_num(js_mod(x, y)); break;
    case '&': *out = v_num(d_to_int32(x) & d_to_int32(y)); break;
    case '|': *out = v_num(d_to_int32(x) | d_to_int32(y)); break;
    case '^': *out = v_num(d_to_int32(x) ^ d_to_int32(y)); break;
    case OP_SHL: *out = v_num((LONG)((ULONG)d_to_int32(x) << (d_to_uint32(y) & 31))); break;
    case OP_SHR: *out = v_num(d_to_int32(x) >> (d_to_uint32(y) & 31)); break;
    case OP_USHR: *out = v_num((double)(d_to_uint32(x) >> (d_to_uint32(y) & 31))); break;
    }
    return RC_OK;
}

static int binary_op(Env *env, int op, Val *a, Val *b, Val *out)
{
    Engine *e = env->e;
    int rc, r;
    BOOL eq;
    switch (op) {
    case '+':
        return op_add(e, a, b, out);
    case '-': case '*': case '/': case '%': case '&': case '|': case '^': case OP_SHL: case OP_SHR: case OP_USHR:
        return op_arith(e, op, a, b, out);
    case OP_EQ:
        if ((rc = loose_equals(e, a, b, &eq)))
            return rc;
        *out = v_bool(eq);
        return RC_OK;
    case OP_NE:
        if ((rc = loose_equals(e, a, b, &eq)))
            return rc;
        *out = v_bool(!eq);
        return RC_OK;
    case OP_SEQ:
        *out = v_bool(strict_equals(a, b) || (a->t == T_OBJ && b->t == T_OBJ && a->u.o->cls == C_HOST && b->u.o->cls == C_HOST && host_same_object(a->u.o, b->u.o)));
        return RC_OK;
    case OP_SNE:
        *out = v_bool(!(strict_equals(a, b) || (a->t == T_OBJ && b->t == T_OBJ && a->u.o->cls == C_HOST && b->u.o->cls == C_HOST && host_same_object(a->u.o, b->u.o))));
        return RC_OK;
    case '<':
        if ((rc = compare_values(e, a, b, TRUE, &r)))
            return rc;
        *out = v_bool(r == 1);
        return RC_OK;
    case '>':
        if ((rc = compare_values(e, b, a, FALSE, &r)))
            return rc;
        *out = v_bool(r == 1);
        return RC_OK;
    case OP_LE:
        if ((rc = compare_values(e, b, a, FALSE, &r)))
            return rc;
        *out = v_bool(r == 0);
        return RC_OK;
    case OP_GE:
        if ((rc = compare_values(e, a, b, TRUE, &r)))
            return rc;
        *out = v_bool(r == 0);
        return RC_OK;
    case OP_IN: {
        if (b->t != T_OBJ)
            return throw_error(e, ERR_TYPE, 5007, nullptr);
        Str *k;
        if ((rc = to_string(e, a, &k)))
            return rc;
        *out = v_bool(obj_has(e, b->u.o, k));
        return RC_OK;
    }
    case OP_INSTANCEOF: {
        if (b->t != T_OBJ || (b->u.o->cls != C_FUNCTION && b->u.o->cls != C_HOST))
            return throw_error(e, ERR_TYPE, 5002, nullptr);
        if (a->t != T_OBJ) {
            *out = v_bool(FALSE);
            return RC_OK;
        }
        BOOL res = FALSE;
        if (b->u.o->cls == C_FUNCTION)
            obj_instanceof(e, a->u.o, b->u.o, &res);
        *out = v_bool(res);
        return RC_OK;
    }
    }
    return RC_OK;
}

/* ------------------------------------------------------------------ calls */

static Str *member_key(Env *env, Node *n, int *rc)
{
    *rc = RC_OK;
    if (n->kind == N_MEMBER)
        return n->u.str;
    Val k;
    *rc = EVAL(env, n->b, &k);
    if (*rc)
        return nullptr;
    Str *s;
    if (k.t == T_NUM) {
        double d = k.u.n;
        if (d >= 0 && d < 4294967295.0 && d == (double)(ULONG)d)
            return index_to_str(env->e, (ULONG)d);
    }
    *rc = to_string(env->e, &k, &s);
    if (*rc)
        return nullptr;
    return s;
}

/* the value of a property of any value. */
static int get_property(Env *env, Val *base, Str *key, Val *out)
{
    Engine *e = env->e;
    if (base->t == T_OBJ)
        return obj_get(e, base->u.o, key, out);
    if (base->t == T_STR) {
        Str *s = base->u.s;
        if (key->len == 6 && !wcscmp(key->s, L"length")) {
            *out = v_num(s->len);
            return RC_OK;
        }
        return obj_get(e, e->StringProto, key, out);
    }
    if (base->t == T_NUM)
        return obj_get(e, e->NumberProto, key, out);
    if (base->t == T_BOOL)
        return obj_get(e, e->BooleanProto, key, out);
    return RC_THROW;
}

static int eval_args(Env *env, Node *first, Val *args, int argc)
{
    int i = 0;
    for (Node *a = first; a; a = a->next, i++) {
        int rc = EVAL(env, a, &args[i]);
        if (rc)
            return rc;
    }
    return RC_OK;
}

static int eval_call(Env *env, Node *n, Val *out, int ctx, BOOL is_new)
{
    Engine *e = env->e;
    int argc = (int)n->u.count;
    Val stack_args[8];
    Val *args = stack_args;
    if (argc > 8)
        args = gc_alloc(e, argc * sizeof(Val), GC_VALS);
    int rc;
    Node *callee = n->a;
    Val self = UNDEF, fv;
    Obj *callobj = nullptr;
    Val base;
    base = UNDEF;
    if (is_new) {
        rc = EVAL(env, callee, &fv);
        if (rc)
            return rc;
        if ((rc = eval_args(env, n->b, args, argc)))
            return rc;
        if (fv.t == T_UNDEF || fv.t == T_NULL)
            return throw_error(e, ERR_TYPE, 5007, nullptr);
        if (fv.t != T_OBJ || (fv.u.o->cls != C_FUNCTION && fv.u.o->cls != C_HOST))
            return throw_error(e, ERR_TYPE, 445, nullptr);
        return construct_function(e, &fv, args, argc, out);
    }
    if (callee->kind == N_MEMBER || callee->kind == N_INDEX) {
        rc = eval_ctx(env, callee->a, &base, 0);
        if (rc)
            return rc;
        Str *key = member_key(env, callee, &rc);
        if (rc)
            return rc;
        if (base.t == T_UNDEF || base.t == T_NULL)
            return throw_null_base(env, callee, &base, FALSE);
        if ((rc = eval_args(env, n->b, args, argc)))
            return rc;
        if (base.t == T_OBJ && base.u.o->cls == C_HOST) {
            BOOL found = FALSE;
            rc = host_call_member(e, base.u.o, key, args, argc, out, &found, ctx);
            if (rc || found)
                return rc;
            /* a name the host does not know may be an expando of the engine's wrapper. */
            Val f;
            rc = obj_get(e, base.u.o, key, &f);
            if (rc)
                return rc;
            if (!is_callable(&f))
                return throw_error(e, ERR_TYPE, 438, nullptr);
            return call_function(e, &f, &base, args, argc, out);
        }
        rc = get_property(env, &base, key, &fv);
        if (rc)
            return rc;
        self = base;
    } else if (callee->kind == N_IDENT) {
        Obj *wb = nullptr;
        BOOL found;
        rc = ident_get_scope(env, callee->u.str, &fv, &wb, &found);
        if (rc)
            return rc;
        if (!found) {
            /* the items of the host by name, then the members of those that give theirs to the script. */
            for (NamedItem *it = e->Items; it && !found; it = it->next)
                if ((it->flags & SCRIPTITEM_ISVISIBLE) && it->name->len == callee->u.str->len &&
                    !_wcsicmp(it->name->s, callee->u.str->s)) {
                    if (it->value.t == T_UNDEF && !(it->flags & 0x80000000))
                        item_load(e, it);
                    if (it->value.t == T_OBJ) {
                        fv = it->value;
                        found = TRUE;
                    }
                }
            if (!found) {
                if ((rc = eval_args(env, n->b, args, argc)))
                    return rc;
                for (NamedItem *it = e->Items; it && !found; it = it->next)
                    if (it->flags & SCRIPTITEM_GLOBALMEMBERS) {
                        if (it->value.t == T_UNDEF && !(it->flags & 0x80000000))
                            item_load(e, it);
                        if (it->value.t != T_OBJ)
                            continue;
                        rc = host_call_member(e, it->value.u.o, callee->u.str, args, argc, out, &found, ctx);
                        if (rc || found)
                            return rc;
                    }
                /* an unknown function is "Object expected". */
                return throw_error(e, ERR_TYPE, 5007, nullptr);
            }
        }
        if (wb)
            self = v_obj(wb);
        /* a member of the host called by its bare name. */
        if (fv.t != T_OBJ || (fv.u.o->cls != C_FUNCTION && fv.u.o->cls != C_HOST)) {
            BOOL isitem = FALSE;
            (void)isitem;
        }
        if ((rc = eval_args(env, n->b, args, argc)))
            return rc;
        goto do_call;
    } else {
        rc = eval_ctx(env, callee, &fv, 1);
        if (rc)
            return rc;
    }
    if ((rc = eval_args(env, n->b, args, argc)))
        return rc;
do_call:
    if (fv.t != T_OBJ || (fv.u.o->cls != C_FUNCTION && fv.u.o->cls != C_HOST)) {
        if (fv.t == T_UNDEF && (callee->kind == N_MEMBER || callee->kind == N_INDEX))
            return throw_error(e, ERR_TYPE, 438, nullptr);
        if (fv.t == T_UNDEF || fv.t == T_NULL)
            return throw_error(e, ERR_TYPE, 5007, nullptr);
        return throw_error(e, ERR_TYPE, 5002, nullptr);
    }
    (void)callobj;
    /* eval runs in the scope of the caller, whatever it was called through. */
    if (fv.u.o->cls == C_FUNCTION && fv.u.o->u.fn.native && fv.u.o->u.fn.magic == -77) {
        if (argc < 1 || args[0].t != T_STR) {
            *out = argc ? args[0] : UNDEF;
            return RC_OK;
        }
        return eval_code(e, args[0].u.s, env->scope, &env->self, out);
    }
    if (fv.u.o->cls == C_HOST) {
        e->CallCtx = ctx;
        return host_call(e, fv.u.o, &self, args, argc, out, FALSE);
    }
    return call_function(e, &fv, &self, args, argc, out);
}

/* ------------------------------------------------------------------ references */

static int do_assign_op(Env *env, int op, Val *old, Val *rhs, Val *out)
{
    int bop;
    switch (op) {
    case OP_ADDA: bop = '+'; break;
    case OP_SUBA: bop = '-'; break;
    case OP_MULA: bop = '*'; break;
    case OP_DIVA: bop = '/'; break;
    case OP_MODA: bop = '%'; break;
    case OP_SHLA: bop = OP_SHL; break;
    case OP_SHRA: bop = OP_SHR; break;
    case OP_USHRA: bop = OP_USHR; break;
    case OP_ANDA: bop = '&'; break;
    case OP_ORA: bop = '|'; break;
    default: bop = '^'; break;
    }
    return binary_op(env, bop, old, rhs, out);
}

static int put_property(Env *env, Val *base, Str *key, Val *v, Node *where)
{
    Engine *e = env->e;
    if (base->t == T_OBJ)
        return obj_put(e, base->u.o, key, v);
    if (base->t == T_UNDEF || base->t == T_NULL)
        return throw_null_base(env, where, base, FALSE);
    return RC_OK;
}

/* update: += style assignments and increments read then write the same reference. */
static int eval_update(Env *env, Node *target, Val *newv, int kind, int op, Node *rhs_node, Val *result, BOOL post)
{
    Engine *e = env->e;
    int rc;
    Val old, rhs;
    if (target->kind == N_IDENT) {
        Str *name = target->u.str;
        BOOL found;
        rc = ident_get(env, name, &old, nullptr, &found);
        if (rc)
            return rc;
        if (!found)
            return undefined_ident(env, name);
        if (kind == 0) {
            if ((rc = EVAL(env, rhs_node, &rhs)))
                return rc;
            if ((rc = do_assign_op(env, op, &old, &rhs, newv)))
                return rc;
        } else {
            double d;
            if ((rc = to_number(e, &old, &d)))
                return rc;
            old = v_num(d);
            *newv = v_num(op == OP_INC ? d + 1 : d - 1);
        }
        if ((rc = ident_put(env, name, newv)))
            return rc;
        *result = post ? old : *newv;
        return RC_OK;
    }
    if (target->kind == N_MEMBER || target->kind == N_INDEX) {
        Val base;
        if ((rc = EVAL(env, target->a, &base)))
            return rc;
        Str *key = member_key(env, target, &rc);
        if (rc)
            return rc;
        if (base.t == T_UNDEF || base.t == T_NULL)
            return throw_null_base(env, target, &base, FALSE);
        if ((rc = get_property(env, &base, key, &old)))
            return rc;
        if (kind == 0) {
            if ((rc = EVAL(env, rhs_node, &rhs)))
                return rc;
            if ((rc = do_assign_op(env, op, &old, &rhs, newv)))
                return rc;
        } else {
            double d;
            if ((rc = to_number(e, &old, &d)))
                return rc;
            old = v_num(d);
            *newv = v_num(op == OP_INC ? d + 1 : d - 1);
        }
        if ((rc = put_property(env, &base, key, newv, target)))
            return rc;
        *result = post ? old : *newv;
        return RC_OK;
    }
    if (target->kind == N_CALL)
        return kind == 1 ? throw_error(e, ERR_TYPE, 5007, nullptr) : assign_error(env, target, TRUE);
    return assign_error(env, target, kind == 0);
}

/* ------------------------------------------------------------------ expressions */

static int eval_ctx(Env *env, Node *n, Val *out, int ctx)
{
    Engine *e = env->e;
    int rc;
    switch (n->kind) {
    case N_NUM:
        *out = v_num(n->u.num);
        return RC_OK;
    case N_STR:
        *out = v_str(n->u.str);
        return RC_OK;
    case N_TRUE:
        *out = v_bool(TRUE);
        return RC_OK;
    case N_FALSE:
        *out = v_bool(FALSE);
        return RC_OK;
    case N_NULL:
        *out = v_null();
        return RC_OK;
    case N_THIS:
        *out = env->self;
        return RC_OK;
    case N_IDENT: {
        BOOL found;
        rc = ident_get(env, n->u.str, out, nullptr, &found);
        if (rc)
            return rc;
        if (!found)
            return undefined_ident(env, n->u.str);
        return RC_OK;
    }
    case N_REGEX: {
        Str *src = n->u.str;
        SIZE_T slash = src->len;
        while (slash > 1 && src->s[slash - 1] != '/')
            slash--;
        Str *pattern = str_new(e, src->s + 1, slash - 2);
        BOOL bad = FALSE;
        Obj *re = make_regexp(e, pattern, src->s + slash, src->len - slash, &bad);
        if (!re)
            return RC_THROW;
        *out = v_obj(re);
        return RC_OK;
    }
    case N_ARRAY: {
        Obj *a = array_new(e, 0);
        *out = v_obj(a);
        ULONG i = 0;
        for (Node *el = n->a; el; el = el->next, i++) {
            if (el->kind == N_ELISION) {
                a->u.arr.len = i + 1;
                if (a->u.arr.cap < i + 1) {
                    Val hole = {T_HOLE};
                    obj_put_index(e, a, i, &hole);
                    if (a->u.arr.cap > i)
                        a->u.arr.items[i].t = T_HOLE;
                }
                continue;
            }
            Val v;
            if ((rc = EVAL(env, el, &v)))
                return rc;
            if ((rc = obj_put_index(e, a, i, &v)))
                return rc;
        }
        if (a->u.arr.len < n->u.count)
            a->u.arr.len = n->u.count;
        return RC_OK;
    }
    case N_OBJECT: {
        Obj *o = obj_new(e, C_OBJECT, e->ObjectProto);
        *out = v_obj(o);
        for (Node *p = n->a; p; p = p->next) {
            Val v;
            if ((rc = EVAL(env, p->a, &v)))
                return rc;
            if ((rc = obj_put(e, o, p->u.str, &v)))
                return rc;
        }
        return RC_OK;
    }
    case N_FUNC: {
        FuncNode *f = n->u.fn;
        Obj *scope = env->scope;
        /* JScript makes the name of a function expression a variable of the scope it is written in. */
        Obj *fo = func_new_node(e, f, scope);
        if (f->name) {
            Val fv = v_obj(fo);
            Obj *vs = env->var_scope ? env->var_scope : e->Global;
            Prop *p = obj_find_own(vs, f->name);
            if (p)
                p->v = fv;
            else
                obj_add_prop(e, vs, f->name, fv, 0);
        }
        *out = v_obj(fo);
        return RC_OK;
    }
    case N_MEMBER:
    case N_INDEX: {
        Val base;
        if ((rc = eval_ctx(env, n->a, &base, 0)))
            return rc;
        Str *key = member_key(env, n, &rc);
        if (rc)
            return rc;
        if (base.t == T_UNDEF || base.t == T_NULL || base.t == T_VAR)
            return throw_null_base(env, n, &base, TRUE);
        rc = get_property(env, &base, key, out);
        return rc;
    }
    case N_CALL:
        return eval_call(env, n, out, ctx, FALSE);
    case N_NEW:
        return eval_call(env, n, out, ctx, TRUE);
    case N_UNARY: {
        int op = n->op;
        if (op == OP_TYPEOF) {
            Val v;
            if (n->a->kind == N_IDENT) {
                BOOL found;
                rc = ident_get(env, n->a->u.str, &v, nullptr, &found);
                if (rc)
                    return rc;
                if (!found) {
                    *out = v_str(S(e, "undefined"));
                    return RC_OK;
                }
            } else if (n->a->kind == N_MEMBER || n->a->kind == N_INDEX) {
                Val base;
                if ((rc = EVAL(env, n->a->a, &base)))
                    return rc;
                Str *key = member_key(env, n->a, &rc);
                if (rc)
                    return rc;
                if (base.t == T_UNDEF || base.t == T_NULL)
                    return throw_null_base(env, n->a, &base, FALSE);
                if ((rc = get_property(env, &base, key, &v))) {
                    /* a member of a host that cannot be read by its name alone is of a type that cannot be said. */
                    if (rc == RC_THROW && base.t == T_OBJ && base.u.o->cls == C_HOST) {
                        *out = v_str(S(e, "unknown"));
                        return RC_OK;
                    }
                    return rc;
                }
            } else if ((rc = EVAL(env, n->a, &v)))
                return rc;
            *out = v_str(str_intern_z(e, typeof_name(&v)));
            return RC_OK;
        }
        if (op == OP_DELETE) {
            Node *t = n->a;
            BOOL res = TRUE;
            if (t->kind == N_MEMBER || t->kind == N_INDEX) {
                Val base;
                if ((rc = EVAL(env, t->a, &base)))
                    return rc;
                Str *key = member_key(env, t, &rc);
                if (rc)
                    return rc;
                if (base.t == T_UNDEF || base.t == T_NULL)
                    return throw_null_base(env, t, &base, FALSE);
                if (base.t == T_OBJ && (rc = obj_delete(e, base.u.o, key, &res)))
                    return rc;
            } else if (t->kind == N_IDENT) {
                for (Obj *s = env->scope; s; s = s->u.scope.outer) {
                    if (s->cls == C_WITH) {
                        if (obj_has(e, s->u.scope.target, t->u.str)) {
                            obj_delete(e, s->u.scope.target, t->u.str, &res);
                            break;
                        }
                    } else if (obj_find_own(s, t->u.str)) {
                        obj_delete(e, s, t->u.str, &res);
                        break;
                    }
                    if (s->cls == C_GLOBAL)
                        break;
                }
            } else {
                Val tmp;
                if ((rc = EVAL(env, t, &tmp)))
                    return rc;
            }
            *out = v_bool(res);
            return RC_OK;
        }
        Val v;
        if ((rc = EVAL(env, n->a, &v)))
            return rc;
        if (op == OP_VOID) {
            *out = UNDEF;
            return RC_OK;
        }
        if (op == '!') {
            *out = v_bool(!to_boolean_val(&v));
            return RC_OK;
        }
        double d;
        if ((rc = to_number(e, &v, &d)))
            return rc;
        if (op == '-')
            *out = v_num(-d);
        else if (op == '+')
            *out = v_num(d);
        else
            *out = v_num(~d_to_int32(d));
        return RC_OK;
    }
    case N_PREFIX:
    case N_POSTFIX: {
        Val nv;
        return eval_update(env, n->a, &nv, 1, n->op, nullptr, out, n->kind == N_POSTFIX);
    }
    case N_BINARY: {
        Val a, b;
        if ((rc = EVAL(env, n->a, &a)) || (rc = EVAL(env, n->b, &b)))
            return rc;
        return binary_op(env, n->op, &a, &b, out);
    }
    case N_LOGICAL: {
        Val a;
        if ((rc = EVAL(env, n->a, &a)))
            return rc;
        BOOL t = to_boolean_val(&a);
        if (n->op == OP_AND ? !t : t) {
            *out = a;
            return RC_OK;
        }
        return EVAL(env, n->b, out);
    }
    case N_COND: {
        Val c;
        if ((rc = EVAL(env, n->a, &c)))
            return rc;
        return eval_ctx(env, to_boolean_val(&c) ? n->b : n->c, out, ctx);
    }
    case N_COMMA: {
        Val a;
        if ((rc = eval_ctx(env, n->a, &a, 1)))
            return rc;
        return eval_ctx(env, n->b, out, ctx);
    }
    case N_ASSIGN: {
        Node *t = n->a;
        if (n->op != '=') {
            Val nv;
            return eval_update(env, t, &nv, 0, n->op, n->b, out, FALSE);
        }
        Val v;
        if (t->kind == N_IDENT) {
            if ((rc = EVAL(env, n->b, &v)))
                return rc;
            if ((rc = ident_put(env, t->u.str, &v)))
                return rc;
            *out = v;
            return RC_OK;
        }
        if (t->kind == N_MEMBER || t->kind == N_INDEX) {
            Val base;
            if ((rc = EVAL(env, t->a, &base)))
                return rc;
            Str *key = member_key(env, t, &rc);
            if (rc)
                return rc;
            if ((rc = EVAL(env, n->b, &v)))
                return rc;
            if ((rc = put_property(env, &base, key, &v, t)))
                return rc;
            *out = v;
            return RC_OK;
        }
        if (t->kind == N_CALL && (t->a->kind == N_MEMBER || t->a->kind == N_INDEX)) {
            /* a property of a host with arguments takes the value as its last one. */
            Val base;
            if ((rc = EVAL(env, t->a->a, &base)))
                return rc;
            Str *key = member_key(env, t->a, &rc);
            if (rc)
                return rc;
            int pc = (int)t->u.count;
            Val *pargs = gc_alloc(e, (pc + 1) * sizeof(Val), GC_VALS);
            if ((rc = eval_args(env, t->b, pargs, pc)))
                return rc;
            if ((rc = EVAL(env, n->b, &v)))
                return rc;
            if (base.t == T_OBJ && base.u.o->cls == C_HOST) {
                pargs[pc] = v;
                if ((rc = host_put_args(e, base.u.o, key, pargs, pc + 1)))
                    return rc;
                *out = v;
                return RC_OK;
            }
            return assign_error(env, t, FALSE);
        }
        if ((rc = EVAL(env, n->b, &v)))
            return rc;
        return assign_error(env, t, FALSE);
    }
    }
    return RC_OK;
}

/* ------------------------------------------------------------------ statements */

static BOOL is_loop(Node *n)
{
    return n->kind == N_FOR || n->kind == N_FORIN || n->kind == N_WHILE || n->kind == N_DO;
}

/* the result of the body of a loop: whether the loop goes on, stops or leaves. */
static int loop_signal(Env *env, int rc, Str **my, int mycount, BOOL *stop)
{
    *stop = FALSE;
    if (rc == RC_OK)
        return RC_OK;
    if (rc == RC_BREAK) {
        if (!env->label) {
            *stop = TRUE;
            return RC_OK;
        }
        return RC_BREAK;
    }
    if (rc == RC_CONTINUE) {
        if (!env->label)
            return RC_OK;
        for (int i = 0; i < mycount; i++)
            if (my[i] == env->label) {
                env->label = nullptr;
                return RC_OK;
            }
        return RC_CONTINUE;
    }
    return rc;
}

static int exec_list(Env *env, Node *n, Val *completion)
{
    for (; n; n = n->next) {
        int rc = exec(env, n, completion);
        if (rc)
            return rc;
    }
    return RC_OK;
}

static int exec(Env *env, Node *n, Val *completion)
{
    Engine *e = env->e;
    int rc;
    Str *my[8];
    int mycount = 0;
    if (is_loop(n)) {
        mycount = env->my_count;
        memcpy(my, env->my_labels, mycount * sizeof(Str *));
        env->my_count = 0;
    } else if (n->kind != N_LABEL)
        env->my_count = 0;
    e->CurStmt = n;
    if (*(volatile BOOL *)&e->Interrupted || ((++e->Polls & 0xFFF) == 0 && poll_site(e))) {
        e->Interrupted = FALSE;
        e->Abort = TRUE;
        e->Exception = UNDEF;
        return RC_THROW;
    }
    switch (n->kind) {
    case N_EMPTY:
        return RC_OK;
    case N_EXPR: {
        Val v;
        rc = eval_ctx(env, n->a, &v, 1);
        if (rc)
            return rc;
        if (completion && env->want_value)
            *completion = v;
        return RC_OK;
    }
    case N_VAR:
        for (Node *d = n->a; d; d = d->next) {
            if (!d->a)
                continue;
            Val v;
            e->CurStmt = n;
            if ((rc = EVAL(env, d->a, &v)))
                return rc;
            if ((rc = ident_put(env, d->u.str, &v)))
                return rc;
        }
        return RC_OK;
    case N_BLOCK:
        return exec_list(env, n->a, completion);
    case N_IF: {
        Val c;
        if ((rc = EVAL(env, n->a, &c)))
            return rc;
        if (to_boolean_val(&c))
            return exec(env, n->b, completion);
        if (n->c)
            return exec(env, n->c, completion);
        return RC_OK;
    }
    case N_WHILE: {
        for (;;) {
            Val c;
            e->CurStmt = n;
            if ((rc = EVAL(env, n->a, &c)))
                return rc;
            if (!to_boolean_val(&c))
                return RC_OK;
            rc = exec(env, n->d, completion);
            BOOL stop;
            rc = loop_signal(env, rc, my, mycount, &stop);
            if (rc)
                return rc;
            if (stop)
                return RC_OK;
        }
    }
    case N_DO: {
        for (;;) {
            rc = exec(env, n->d, completion);
            BOOL stop;
            rc = loop_signal(env, rc, my, mycount, &stop);
            if (rc)
                return rc;
            if (stop)
                return RC_OK;
            Val c;
            e->CurStmt = n;
            if ((rc = EVAL(env, n->a, &c)))
                return rc;
            if (!to_boolean_val(&c))
                return RC_OK;
        }
    }
    case N_FOR: {
        if (n->a) {
            if (n->a->kind == N_VAR)
                rc = exec(env, n->a, completion);
            else {
                Val v;
                rc = eval_ctx(env, n->a, &v, 1);
            }
            if (rc)
                return rc;
        }
        for (;;) {
            if (n->b) {
                Val c;
                e->CurStmt = n;
                if ((rc = EVAL(env, n->b, &c)))
                    return rc;
                if (!to_boolean_val(&c))
                    return RC_OK;
            }
            rc = exec(env, n->d, completion);
            BOOL stop;
            rc = loop_signal(env, rc, my, mycount, &stop);
            if (rc)
                return rc;
            if (stop)
                return RC_OK;
            if (n->c) {
                Val v;
                e->CurStmt = n;
                if ((rc = eval_ctx(env, n->c, &v, 1)))
                    return rc;
            }
        }
    }
    case N_FORIN: {
        Val ov;
        if ((rc = EVAL(env, n->b, &ov)))
            return rc;
        if (ov.t == T_UNDEF || ov.t == T_NULL)
            return RC_OK;
        Obj *o;
        if ((rc = to_object(e, &ov, &o)))
            return rc;
        Obj *keys;
        if ((rc = obj_enum_keys(e, o, &keys, FALSE)))
            return rc;
        Node *target = n->a;
        Str *varname = nullptr;
        if (target->kind == N_VAR)
            varname = target->a->u.str;
        for (ULONG i = 0; i < keys->u.arr.len; i++) {
            Val kv = keys->u.arr.items[i];
            if (kv.t != T_STR || !obj_has(e, o, kv.u.s))
                continue;
            if (varname)
                rc = ident_put(env, varname, &kv);
            else if (target->kind == N_IDENT)
                rc = ident_put(env, target->u.str, &kv);
            else if (target->kind == N_MEMBER || target->kind == N_INDEX) {
                Val base;
                if ((rc = EVAL(env, target->a, &base)))
                    return rc;
                Str *key = member_key(env, target, &rc);
                if (rc)
                    return rc;
                rc = put_property(env, &base, key, &kv, target);
            } else
                rc = assign_error(env, target, FALSE);
            if (rc)
                return rc;
            rc = exec(env, n->d, completion);
            BOOL stop;
            rc = loop_signal(env, rc, my, mycount, &stop);
            if (rc)
                return rc;
            if (stop)
                return RC_OK;
        }
        return RC_OK;
    }
    case N_CONTINUE:
        env->label = n->u.str;
        return RC_CONTINUE;
    case N_BREAK:
        env->label = n->u.str;
        return RC_BREAK;
    case N_RETURN:
        if (n->a) {
            if ((rc = EVAL(env, n->a, &e->RetVal)))
                return rc;
        } else
            e->RetVal = UNDEF;
        return RC_RETURN;
    case N_WITH: {
        Val ov;
        if ((rc = EVAL(env, n->a, &ov)))
            return rc;
        Obj *o;
        if (ov.t == T_UNDEF || ov.t == T_NULL)
            return throw_error(e, ERR_TYPE, 424, nullptr);
        if ((rc = to_object(e, &ov, &o)))
            return rc;
        Obj *saved = env->scope;
        Obj *w = scope_new(e, C_WITH, saved);
        w->u.scope.target = o;
        env->scope = w;
        rc = exec(env, n->d, completion);
        env->scope = saved;
        return rc;
    }
    case N_SWITCH: {
        Val d;
        if ((rc = EVAL(env, n->a, &d)))
            return rc;
        Node *match = nullptr;
        for (Node *c = n->b; c; c = c->next) {
            if (c->flag)
                continue;
            Val cv;
            if ((rc = EVAL(env, c->a, &cv)))
                return rc;
            if (strict_equals(&d, &cv)) {
                match = c;
                break;
            }
        }
        if (!match)
            for (Node *c = n->b; c; c = c->next)
                if (c->flag)
                    match = c;
        for (Node *c = match; c; c = c->next) {
            rc = exec_list(env, c->b, completion);
            if (rc == RC_BREAK && !env->label)
                return RC_OK;
            if (rc)
                return rc;
        }
        return RC_OK;
    }
    case N_LABEL: {
        if (env->my_count < 8)
            env->my_labels[env->my_count++] = n->u.str;
        rc = exec(env, n->a, completion);
        env->my_count = 0;
        if (rc == RC_BREAK && env->label == n->u.str) {
            env->label = nullptr;
            return RC_OK;
        }
        return rc;
    }
    case N_THROW: {
        Val v;
        if ((rc = EVAL(env, n->a, &v)))
            return rc;
        e->CurStmt = n;
        return throw_value(e, v);
    }
    case N_TRY: {
        rc = exec(env, n->a, completion);
        if (rc == RC_THROW && e->Abort)
            return rc;
        if (rc == RC_THROW && n->b) {
            /* the variable of the catch is a variable of the function, as JScript has it. */
            Val exc = e->Exception;
            BOOL found = FALSE;
            Val tmp;
            Obj *wb;
            ident_get(env, n->u.str, &tmp, &wb, &found);
            if (found)
                ident_put(env, n->u.str, &exc);
            else
                obj_add_prop(e, env->var_scope ? env->var_scope : e->Global, n->u.str, exc, 0);
            rc = exec(env, n->b, completion);
        }
        if (n->d) {
            Val saved_exc = e->Exception, saved_ret = e->RetVal;
            Str *saved_label = env->label;
            ULONG sl = e->ErrLine, sc = e->ErrCol;
            BOOL sh = e->ErrFromHost;
            Obj *saved2 = env->scope;
            int rc2 = exec(env, n->d, completion);
            env->scope = saved2;
            if (rc2)
                return rc2;
            e->Exception = saved_exc;
            e->RetVal = saved_ret;
            env->label = saved_label;
            e->ErrLine = sl;
            e->ErrCol = sc;
            e->ErrFromHost = sh;
        }
        return rc;
    }
    }
    return RC_OK;
}

/* ------------------------------------------------------------------ functions and programs */

Obj *func_new_node(Engine *e, FuncNode *f, Obj *scope)
{
    Obj *fo = obj_new(e, C_FUNCTION, e->FunctionProto);
    fo->u.fn.node = f;
    fo->u.fn.scope = scope;
    obj_define(e, fo, S(e, "length"), v_num(f->nparams), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    return fo;
}

static void hoist(Env *env, FuncNode *f, Obj *vars)
{
    Engine *e = env->e;
    for (ULONG i = 0; i < f->nfuncs; i++) {
        FuncNode *fn = f->funcs[i]->u.fn;
        Obj *fo = func_new_node(e, fn, env->scope);
        Prop *p = obj_find_own(vars, fn->name);
        if (p)
            p->v = v_obj(fo);
        else
            obj_add_prop(e, vars, fn->name, v_obj(fo), vars->cls == C_GLOBAL ? PF_DONTDELETE : 0);
    }
    for (ULONG i = 0; i < f->nvars; i++)
        if (!obj_find_own(vars, f->vars[i]))
            obj_add_prop(e, vars, f->vars[i], UNDEF, vars->cls == C_GLOBAL ? PF_DONTDELETE : 0);
}

static Obj *make_arguments(Env *env, Obj *func, Obj *act, Val *args, int argc)
{
    Engine *e = env->e;
    Obj *a = obj_new(e, C_ARGS, e->ObjectProto);
    a->u.scope.outer = act;
    a->u.scope.func = func;
    a->u.scope.argc = argc;
    a->u.scope.args = gc_alloc(e, (argc ? argc : 1) * sizeof(Val), GC_VALS);
    for (int i = 0; i < argc; i++)
        a->u.scope.args[i] = args[i];
    FuncNode *f = func->u.fn.node;
    a->u.scope.mapped = argc < (int)f->nparams ? argc : (int)f->nparams;
    obj_define(e, a, S(e, "length"), v_num(argc), PF_DONTENUM);
    obj_define(e, a, S(e, "callee"), v_obj(func), PF_DONTENUM);
    return a;
}

int call_node_function(Engine *e, Obj *func, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    FuncNode *f = func->u.fn.node;
    engine_note_code(e);
    if (e->Depth > 3000 || stack_low())
        return throw_error(e, ERR_RANGE, 28, nullptr);
    Env env = {};
    env.e = e;
    env.func = func;
    Obj *act = scope_new(e, C_ACTIVATION, func->u.fn.scope);
    env.scope = act;
    env.var_scope = act;
    if (!self || self->t == T_UNDEF || self->t == T_NULL)
        env.self = e->GlobalThis.t == T_OBJ ? e->GlobalThis : v_obj(e->Global);
    else if (self->t != T_OBJ) {
        Obj *o;
        to_object(e, self, &o);
        env.self = v_obj(o);
    } else
        env.self = *self;
    for (ULONG i = 0; i < f->nparams; i++) {
        Val v = (int)i < argc ? args[i] : UNDEF;
        Prop *p = obj_find_own(act, f->params[i]);
        if (p)
            p->v = v;
        else
            obj_add_prop(e, act, f->params[i], v, 0);
    }
    if (f->uses_args || f->uses_eval) {
        Obj *ao = make_arguments(&env, func, act, args, argc);
        act->u.scope.arguments = ao;
        if (!obj_find_own(act, S(e, "arguments")))
            obj_add_prop(e, act, S(e, "arguments"), v_obj(ao), PF_DONTENUM);
    }
    hoist(&env, f, act);
    CallFrame frame = {e->Frames, func, nullptr};
    e->Frames = &frame;
    e->Depth++;
    Node *saved_stmt = e->CurStmt;
    e->RetVal = UNDEF;
    int rc = exec_list(&env, f->body, nullptr);
    e->CurStmt = saved_stmt;
    e->Depth--;
    e->Frames = frame.prev;
    if (rc == RC_THROW)
        return rc;
    *ret = rc == RC_RETURN ? e->RetVal : UNDEF;
    e->RetVal = UNDEF;
    return RC_OK;
}

int run_program(Engine *e, Unit *u, Obj *scope, Val *result, BOOL want_value)
{
    engine_note_code(e);
    Env env = {};
    env.e = e;
    env.scope = scope ? scope : e->Global;
    env.var_scope = e->Global;
    env.self = e->GlobalThis.t == T_OBJ ? e->GlobalThis : v_obj(e->Global);
    env.want_value = want_value;
    FuncNode *f = u->program;
    hoist(&env, f, e->Global);
    Val completion = UNDEF;
    int rc = exec_list(&env, f->body, &completion);
    if (result)
        *result = completion;
    if (rc == RC_RETURN)
        rc = RC_OK;
    return rc;
}

int eval_code(Engine *e, Str *code, Obj *scope, Val *self, Val *out)
{
    ParseError err = {};
    Unit *u = parse_unit(e, code->s, code->len, 0, 0, PARSE_EVAL, &err);
    if (!u) {
        Obj *eo = make_error(e, ERR_SYNTAX, err.Code, nullptr);
        eo->flags |= OF_COMPILE;
        js_free(err.LineText);
        return throw_value(e, v_obj(eo));
    }
    u->next = e->Units;
    e->Units = u;
    Env env = {};
    env.e = e;
    env.scope = scope;
    env.self = self ? *self : (e->GlobalThis.t == T_OBJ ? e->GlobalThis : v_obj(e->Global));
    env.want_value = TRUE;
    Obj *vs = scope;
    while (vs && (vs->cls == C_WITH || (vs->cls == C_ACTIVATION && (vs->flags & 2))))
        vs = vs->u.scope.outer;
    env.var_scope = vs ? vs : e->Global;
    hoist(&env, u->program, env.var_scope);
    Val completion = UNDEF;
    int rc = exec_list(&env, u->program->body, &completion);
    *out = completion;
    return rc == RC_RETURN ? RC_OK : rc;
}
