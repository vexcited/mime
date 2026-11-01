/*
 * declares the names of a unit and binds the names its statements use: locals and script level variables are
 * bound at once, the rest are looked up when they run.
 */
#include "vbs.h"

typedef struct Linker {
    Engine *E;
    Unit *U;
    Proc *P;
    Class *C;
    BOOL Exec;
    BOOL Explicit;
    Frame *Frame;
} Linker;

static int proc_local_find(Proc *p, PCWSTR name, ULONG hash)
{
    for (UINT i = 0; i < p->LocalCount; i++)
        if (p->LocalHashes[i] == hash && name_equal(p->Locals[i], name))
            return (int)i;
    return -1;
}

static int proc_local_add(Unit *u, Proc *p, PCWSTR name)
{
    if (p->LocalCount == p->LocalCapacity) {
        UINT capacity = p->LocalCapacity ? p->LocalCapacity * 2 : 8;
        PWSTR *names = arena_alloc(u->Memory, capacity * sizeof(PWSTR));
        ULONG *hashes = arena_alloc(u->Memory, capacity * sizeof(ULONG));
        if (p->LocalCount) {
            memcpy(names, p->Locals, p->LocalCount * sizeof(PWSTR));
            memcpy(hashes, p->LocalHashes, p->LocalCount * sizeof(ULONG));
        }
        p->Locals = names;
        p->LocalHashes = hashes;
        p->LocalCapacity = capacity;
    }
    p->Locals[p->LocalCount] = (PWSTR)name;
    p->LocalHashes[p->LocalCount] = name_hash(name);
    return (int)p->LocalCount++;
}

static void bind_name(Linker *L, Expr *x, BOOL assign)
{
    PCWSTR name = x->u.Name.Name;
    ULONG hash = x->u.Name.Hash;
    Proc *p = L->P;
    if (L->Exec && L->Frame && L->Frame->P)
        p = L->Frame->P;
    if (p) {
        int slot = proc_local_find(p, name, hash);
        if (slot >= 0 && slot == p->ResultSlot && !assign)
            slot = -1;
        else if (slot >= 0) {
            x->Bound = B_LOCAL;
            x->u.Name.Slot = slot;
            return;
        }
    }
    Class *cls = L->C ? L->C : (L->Frame && L->Frame->Me ? L->Frame->Me->Cls : nullptr);
    if (cls && member_find(cls, name, hash))
        return;
    Sym *s = sym_find(L->E, name, hash);
    if (s && (s->Kind == SYM_VAR || s->Kind == SYM_CONST)) {
        x->Bound = B_GLOBAL;
        x->u.Name.Symbol = s;
        return;
    }
    if (s || !assign || L->Explicit)
        return;
    /* a member of the host's global items takes the name. */
    {
        Frame probe = {};
        probe.E = L->E;
        extern BOOL engine_global_member(Frame * f, PCWSTR name);
        if (engine_global_member(&probe, name))
            return;
    }
    if (L->Exec && L->Frame)
        for (DynName *d = L->Frame->Dyn; d; d = d->Next)
            if (d->Hash == hash && name_equal(d->Name, name))
                return;
    if (L->P && !L->Exec) {
        int slot = proc_local_add(L->U, L->P, name);
        x->Bound = B_LOCAL;
        x->u.Name.Slot = slot;
        return;
    }
    s = sym_add(L->E, name, SYM_VAR);
    if (s) {
        s->Public = 1;
        x->Bound = B_GLOBAL;
        x->u.Name.Symbol = s;
    }
}

static void link_expr(Linker *L, Expr *x, BOOL assign);

static void link_args(Linker *L, Args *a)
{
    for (UINT i = 0; i < a->Count; i++)
        if (a->Items[i])
            link_expr(L, a->Items[i], FALSE);
}

static void link_expr(Linker *L, Expr *x, BOOL assign)
{
    switch (x->Kind) {
    case X_NAME:
        bind_name(L, x, assign);
        break;
    case X_MEMBER:
        if (x->u.Member.Object)
            link_expr(L, x->u.Member.Object, FALSE);
        link_args(L, &x->u.Member.Arguments);
        break;
    case X_CALL:
        link_expr(L, x->u.Call.Target, assign && x->u.Call.Target->Kind == X_NAME ? FALSE : FALSE);
        link_args(L, &x->u.Call.Arguments);
        break;
    case X_UNARY:
    case X_EVAL:
        link_expr(L, x->u.Operand, FALSE);
        break;
    case X_BINARY:
        link_expr(L, x->u.Operands.Left, FALSE);
        link_expr(L, x->u.Operands.Right, FALSE);
        break;
    }
}

/* an assignment target: a bare name is declared when nothing else has it. */
static void link_target(Linker *L, Expr *t)
{
    if (t->Kind == X_NAME)
        bind_name(L, t, TRUE);
    else if (t->Kind == X_CALL && t->u.Call.Target->Kind == X_NAME) {
        bind_name(L, t->u.Call.Target, FALSE);
        link_args(L, &t->u.Call.Arguments);
    } else
        link_expr(L, t, FALSE);
}

static void link_stmts(Linker *L, Stmt *s);

static void link_stmt(Linker *L, Stmt *s)
{
    switch (s->Kind) {
    case S_ASSIGN:
    case S_SETASSIGN:
        link_target(L, s->u.Assign.Target);
        link_expr(L, s->u.Assign.Value, FALSE);
        break;
    case S_CALL:
        link_expr(L, s->u.Call, FALSE);
        break;
    case S_IF:
        link_expr(L, s->u.If.Condition, FALSE);
        link_stmts(L, s->u.If.Then);
        link_stmts(L, s->u.If.Else);
        break;
    case S_SELECT:
        link_expr(L, s->u.Select.Subject, FALSE);
        for (CaseClause *c = s->u.Select.Cases; c; c = c->Next) {
            for (UINT i = 0; i < c->Count; i++)
                link_expr(L, c->Values[i], FALSE);
            link_stmts(L, c->Body);
        }
        break;
    case S_FOR:
        link_target(L, s->u.For.Var);
        link_expr(L, s->u.For.From, FALSE);
        link_expr(L, s->u.For.To, FALSE);
        if (s->u.For.Step)
            link_expr(L, s->u.For.Step, FALSE);
        link_stmts(L, s->u.For.Body);
        break;
    case S_FOREACH:
        link_target(L, s->u.Each.Var);
        link_expr(L, s->u.Each.Group, FALSE);
        link_stmts(L, s->u.Each.Body);
        break;
    case S_DO:
    case S_WHILE:
        if (s->u.Loop.Condition)
            link_expr(L, s->u.Loop.Condition, FALSE);
        link_stmts(L, s->u.Loop.Body);
        break;
    case S_WITH:
        link_expr(L, s->u.With.Object, FALSE);
        link_stmts(L, s->u.With.Body);
        break;
    case S_DIM:
        for (DimItem *d = s->u.Dim; d; d = d->Next)
            link_args(L, &d->Bounds);
        break;
    case S_REDIM:
        for (DimItem *d = s->u.Dim; d; d = d->Next) {
            link_target(L, d->Target);
            link_args(L, &d->Bounds);
        }
        break;
    case S_ERASE:
        link_expr(L, s->u.Randomize, FALSE);
        break;
    case S_RANDOMIZE:
        if (s->u.Randomize)
            link_expr(L, s->u.Randomize, FALSE);
        break;
    case S_EXECUTE:
        if (s->u.Execute.Source)
            link_expr(L, s->u.Execute.Source, FALSE);
        break;
    }
}

static void link_stmts(Linker *L, Stmt *s)
{
    for (; s; s = s->Next)
        link_stmt(L, s);
}

/* ------------------------------------------------------------------ declarations */

static BOOL declare_dims(Engine *e, Unit *u, Stmt *s, Frame *frame, BOOL exec)
{
    for (; s; s = s->Next) {
        switch (s->Kind) {
        case S_DIM:
            for (DimItem *d = s->u.Dim; d; d = d->Next) {
                ULONG hash = name_hash(d->Name);
                Sym *old = sym_find(e, d->Name, hash);
                if (exec && frame && frame->P) {
                    DynName *dn = vbs_alloc(sizeof(DynName));
                    dn->Name = vbs_wcsdup(d->Name);
                    dn->Hash = hash;
                    dn->Next = frame->Dyn;
                    frame->Dyn = dn;
                    continue;
                }
                if (old) {
                    if (old->Kind == SYM_ITEM) {
                        /* a variable of the same name hides the item. */
                        VariantClear(&old->Value);
                        old->Kind = SYM_VAR;
                        old->Declared = 1;
                        continue;
                    }
                    if (exec && old->Kind == SYM_VAR)
                        continue;
                    return FALSE;
                }
                Sym *sym = sym_add(e, d->Name, SYM_VAR);
                sym->Public = d->Public;
                sym->Declared = 1;
            }
            break;
        case S_CONST: {
            ULONG hash = name_hash(s->u.Const.Name);
            Sym *old = sym_find(e, s->u.Const.Name, hash);
            if (exec && frame && frame->P) {
                DynName *dn = vbs_alloc(sizeof(DynName));
                dn->Name = vbs_wcsdup(s->u.Const.Name);
                dn->Hash = hash;
                dn->Next = frame->Dyn;
                if (s->u.Const.Value->u.Value.vt == VT_BSTR) {
                    dn->Value.vt = VT_BSTR;
                    dn->Value.bstrVal = SysAllocString(s->u.Const.Value->u.Value.bstrVal);
                } else
                    dn->Value = s->u.Const.Value->u.Value;
                frame->Dyn = dn;
                continue;
            }
            if (old && !(old->Kind == SYM_ITEM)) {
                if (!exec)
                    return FALSE;
                VariantClear(&old->Value);
            }
            Sym *sym = old ? old : sym_add(e, s->u.Const.Name, SYM_CONST);
            sym->Kind = SYM_CONST;
            sym->Declared = 1;
            VariantClear(&sym->Value);
            if (s->u.Const.Value->u.Value.vt == VT_BSTR) {
                sym->Value.vt = VT_BSTR;
                sym->Value.bstrVal = SysAllocString(s->u.Const.Value->u.Value.bstrVal);
            } else
                sym->Value = s->u.Const.Value->u.Value;
            break;
        }
        case S_IF:
            if (!declare_dims(e, u, s->u.If.Then, frame, exec) || !declare_dims(e, u, s->u.If.Else, frame, exec))
                return FALSE;
            break;
        case S_FOR:
            declare_dims(e, u, s->u.For.Body, frame, exec);
            break;
        case S_FOREACH:
            declare_dims(e, u, s->u.Each.Body, frame, exec);
            break;
        case S_DO:
        case S_WHILE:
            declare_dims(e, u, s->u.Loop.Body, frame, exec);
            break;
        case S_WITH:
            declare_dims(e, u, s->u.With.Body, frame, exec);
            break;
        case S_SELECT:
            for (CaseClause *c = s->u.Select.Cases; c; c = c->Next)
                declare_dims(e, u, c->Body, frame, exec);
            break;
        }
    }
    return TRUE;
}

/* returns FALSE when a name is declared twice, with the error recorded. */
BOOL unit_declare(Engine *e, Unit *unit, Frame *frame)
{
    BOOL exec = !unit->Global;
    Frame g = {};
    g.E = e;
    g.Unit = unit;
    if (!declare_dims(e, unit, unit->Statements, frame, exec)) {
        raise_error(&g, 1041, nullptr);
        return FALSE;
    }
    for (Proc *p = unit->Procedures; p; p = p->Next) {
        Sym *old = sym_find(e, p->Name, name_hash(p->Name));
        if (old && old->Kind != SYM_PROC && !(exec && old->Kind == SYM_VAR) && old->Kind != SYM_ITEM) {
            raise_error(&g, 1041, nullptr);
            return FALSE;
        }
        if (old && old->Kind == SYM_ITEM) {
            raise_error(&g, 1041, nullptr);
            return FALSE;
        }
        Sym *s = old ? old : sym_add(e, p->Name, SYM_PROC);
        s->Kind = SYM_PROC;
        s->Procedure = p;
        s->Public = p->Public;
    }
    for (Class *c = unit->Classes; c; c = c->Next) {
        Sym *old = sym_find(e, c->Name, name_hash(c->Name));
        if (old) {
            raise_error(&g, 1041, nullptr);
            return FALSE;
        }
        Sym *s = sym_add(e, c->Name, SYM_CLASS);
        s->Cls = c;
    }
    return TRUE;
}

void unit_link(Engine *e, Unit *unit, Frame *frame)
{
    Linker L = {};
    L.E = e;
    L.U = unit;
    L.Explicit = e->Explicit || unit->Explicit;
    L.Exec = !unit->Global;
    L.Frame = frame;
    for (Proc *p = unit->Procedures; p; p = p->Next) {
        L.P = p;
        L.C = nullptr;
        link_stmts(&L, p->Body);
    }
    for (Class *c = unit->Classes; c; c = c->Next) {
        for (Member *m = c->Members; m; m = m->Next) {
            Proc *procs[4] = {m->Method, m->Get, m->Let, m->Set};
            for (int i = 0; i < 4; i++)
                if (procs[i]) {
                    L.P = procs[i];
                    L.C = c;
                    link_stmts(&L, procs[i]->Body);
                }
        }
    }
    L.P = nullptr;
    L.C = nullptr;
    link_stmts(&L, unit->Statements);
    unit->Linked = TRUE;
}
