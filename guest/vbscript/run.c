/*
 * the interpreter: names, expressions and statements, procedure calls, and the errors a script raises.
 */
#include "vbs.h"

/* ------------------------------------------------------------------ errors */

static const struct {
    LONG Number;
    PCWSTR Text;
} g_messages[] = {
    {5, L"Invalid procedure call or argument"}, {6, L"Overflow"}, {7, L"Out of memory"}, {9, L"Subscript out of range"},
    {10, L"This array is fixed or temporarily locked"}, {11, L"Division by zero"}, {13, L"Type mismatch"},
    {14, L"Out of string space"}, {17, L"Can't perform requested operation"}, {28, L"Out of stack space"},
    {35, L"Sub or Function not defined"}, {48, L"Error in loading DLL"}, {51, L"Internal error"},
    {52, L"Bad file name or number"}, {53, L"File not found"}, {54, L"Bad file mode"}, {55, L"File already open"},
    {57, L"Device I/O error"}, {58, L"File already exists"}, {61, L"Disk full"}, {62, L"Input past end of file"},
    {67, L"Too many files"}, {68, L"Device unavailable"}, {70, L"Permission denied"}, {71, L"Disk not ready"},
    {74, L"Can't rename with different drive"}, {75, L"Path/File access error"}, {76, L"Path not found"},
    {91, L"Object variable not set"}, {92, L"For loop not initialized"}, {94, L"Invalid use of Null"},
    {322, L"Can't create necessary temporary file"}, {424, L"Object required"},
    {429, L"ActiveX component can't create object"}, {430, L"Class doesn't support Automation"},
    {432, L"File name or class name not found during Automation operation"},
    {438, L"Object doesn't support this property or method"}, {440, L"Automation error"},
    {445, L"Object doesn't support this action"}, {446, L"Object doesn't support named arguments"},
    {447, L"Object doesn't support current locale setting"}, {448, L"Named argument not found"},
    {449, L"Argument not optional"}, {450, L"Wrong number of arguments or invalid property assignment"},
    {451, L"Object not a collection"}, {453, L"Specified DLL function not found"}, {455, L"Code resource lock error"},
    {457, L"This key is already associated with an element of this collection"},
    {458, L"Variable uses an Automation type not supported in VBScript"},
    {462, L"The remote server machine does not exist or is unavailable"}, {481, L"Invalid picture"},
    {500, L"Variable is undefined"}, {501, L"Illegal assignment"}, {502, L"Object not safe for scripting"},
    {503, L"Object not safe for initializing"}, {504, L"Object not safe for creating"},
    {505, L"Invalid or unqualified reference"}, {506, L"Class not defined"}, {507, L"An exception occurred"},
    {1001, L"Out of memory"}, {1002, L"Syntax error"}, {1003, L"Expected ':'"}, {1005, L"Expected '('"},
    {1006, L"Expected ')'"}, {1007, L"Expected ']'"}, {1010, L"Expected identifier"}, {1011, L"Expected '='"},
    {1012, L"Expected 'If'"}, {1013, L"Expected 'To'"}, {1014, L"Expected 'End'"}, {1015, L"Expected 'Function'"},
    {1016, L"Expected 'Sub'"}, {1017, L"Expected 'Then'"}, {1018, L"Expected 'Wend'"}, {1019, L"Expected 'Loop'"},
    {1020, L"Expected 'Next'"}, {1021, L"Expected 'Case'"}, {1022, L"Expected 'Select'"},
    {1023, L"Expected expression"}, {1024, L"Expected statement"}, {1025, L"Expected end of statement"},
    {1026, L"Expected integer constant"}, {1027, L"Expected 'While' or 'Until'"},
    {1028, L"Expected 'While', 'Until' or end of statement"}, {1029, L"Expected 'With'"},
    {1030, L"Identifier too long"}, {1031, L"Invalid number"}, {1032, L"Invalid character"},
    {1033, L"Unterminated string constant"}, {1034, L"Unterminated comment"}, {1037, L"Invalid use of 'Me' keyword"},
    {1038, L"'loop' without 'do'"}, {1039, L"Invalid 'exit' statement"}, {1040, L"Invalid 'for' loop control variable"},
    {1041, L"Name redefined"}, {1042, L"Must be first statement on the line"},
    {1043, L"Cannot assign to non-ByVal argument"}, {1044, L"Cannot use parentheses when calling a Sub"},
    {1045, L"Expected literal constant"}, {1046, L"Expected 'In'"}, {1047, L"Expected 'Class'"},
    {1048, L"Must be defined inside a Class"}, {1049, L"Expected Let or Set or Get in property declaration"},
    {1050, L"Expected 'Property'"}, {1051, L"Number of arguments must be consistent across properties specification"},
    {1052, L"Cannot have multiple default property/method in a Class"},
    {1053, L"Class initialize or terminate do not have arguments"},
    {1054, L"Property set or let must have at least one argument"}, {1055, L"Unexpected 'Next'"},
    {1056, L"'Default' can be specified only on 'Property' or 'Function' or 'Sub'"},
    {1057, L"'Default' specification must also specify 'Public'"},
    {1058, L"'Default' specification can only be on Property Get"}, {4098, L"Unknown runtime error"},
    {5016, L"Regular Expression object expected"}, {5017, L"Syntax error in regular expression"},
    {5018, L"Unexpected quantifier"}, {5019, L"Expected ']' in regular expression"},
    {5020, L"Expected ')' in regular expression"}, {5021, L"Invalid range in character set"},
};

PCWSTR error_text(LONG number)
{
    for (UINT i = 0; i < sizeof(g_messages) / sizeof(g_messages[0]); i++)
        if (g_messages[i].Number == number)
            return g_messages[i].Text;
    return nullptr;
}

HRESULT error_hresult(LONG number)
{
    if (number > 0 && number <= 0xFFFF)
        return (HRESULT)(VBS_ERR_BASE | (ULONG)number);
    return (HRESULT)number;
}

LONG error_number_of_hresult(HRESULT hr)
{
    if (((ULONG)hr >> 16) == 0x800A)
        return (LONG)((ULONG)hr & 0xFFFF);
    switch ((ULONG)hr) {
    case 0x8002000A: return 6;
    case 0x80020005: return 13;
    case 0x80020003: return 438;
    case 0x80020006: return 438;
    case 0x8002000E: return 450;
    case 0x8002000F: return 449;
    case 0x8002000B: return 9;
    case 0x80020004: return 448;
    case 0x80020007: return 446;
    case 0x80020008: return 458;
    case 0x8002000D: return 10;
    case 0x8002000C: return 447;
    case 0x8007000E: return 7;
    case 0x80040154: return 429;
    }
    return (LONG)hr;
}

void err_clear(Engine *e)
{
    e->Err.Number = 0;
    SysFreeString(e->Err.Description);
    SysFreeString(e->Err.Source);
    SysFreeString(e->Err.HelpFile);
    e->Err.Description = e->Err.Source = e->Err.HelpFile = nullptr;
    e->Err.HelpContext = 0;
}

void err_set(Engine *e, LONG number, PCWSTR source, PCWSTR description)
{
    err_clear(e);
    e->Err.Number = number;
    e->Err.Source = SysAllocString(source ? source : L"");
    e->Err.Description = SysAllocString(description ? description : L"");
}

void fault_clear(Engine *e)
{
    SysFreeString(e->Fault.Source);
    SysFreeString(e->Fault.Description);
    vbs_free(e->Fault.Text);
    memset(&e->Fault, 0, sizeof(e->Fault));
}

static void fault_set(Engine *e, HRESULT scode, PCWSTR source, PCWSTR description, ULONG line, ULONG col)
{
    fault_clear(e);
    e->Fault.Set = TRUE;
    e->Fault.Scode = scode;
    e->Fault.Source = source ? SysAllocString(source) : nullptr;
    e->Fault.Description = description ? SysAllocString(description) : nullptr;
    e->Fault.Line = line;
    e->Fault.Col = col;
}

static const WCHAR g_runtime_source[] = L"Microsoft VBScript runtime error";
static const WCHAR g_compile_source[] = L"Microsoft VBScript compilation error";

/* a detail that is shown though it is empty. */
const WCHAR g_detail_empty[] = L"";

int raise_error(Frame *f, LONG number, PCWSTR detail)
{
    Engine *e = f->E;
    PCWSTR text = error_text(number);
    if (!text)
        text = error_text(4098);
    err_set(e, number, g_runtime_source, text);
    WCHAR full[400];
    if (detail && (*detail || detail == g_detail_empty))
        _snwprintf(full, 400, L"%s: '%s'", text, detail);
    else
        _snwprintf(full, 400, L"%s", text);
    fault_set(e, error_hresult(number), g_runtime_source, full, f->Line, f->Col);
    e->Fault.Cookie = f->Unit ? f->Unit->Cookie : 0;
    return R_ERR;
}

/* an error that came back from an object: its exception information, or only the hresult. */
int raise_hresult(Frame *f, HRESULT hr, PCWSTR source, PCWSTR description)
{
    Engine *e = f->E;
    LONG number = error_number_of_hresult(hr);
    PCWSTR table = error_text(number);
    BOOL mapped = number != (LONG)hr || ((ULONG)hr >> 16) == 0x800A;
    if (!description && mapped && table)
        description = table;
    if (!source && mapped)
        source = g_runtime_source;
    err_set(e, number, source ? source : L"", description ? description : L"");
    HRESULT scode = mapped && number > 0 && number <= 0xFFFF ? error_hresult(number) : hr;
    fault_set(e, scode, source, description, f->Line, f->Col);
    e->Fault.Cookie = f->Unit ? f->Unit->Cookie : 0;
    e->Fault.Host = TRUE;
    return R_ERR;
}

int raise_compile(Engine *e, LONG number, ULONG line, ULONG col, PCWSTR text, DWORD_PTR cookie)
{
    PCWSTR msg = error_text(number);
    err_set(e, number, g_compile_source, msg);
    fault_set(e, error_hresult(number), g_compile_source, msg, line, col);
    e->Fault.Compile = TRUE;
    e->Fault.Text = text ? vbs_wcsdup(text) : nullptr;
    e->Fault.Cookie = cookie;
    return R_ERR;
}

void describe_value(const VARIANT *v, WCHAR out[80])
{
    while ((v->vt & VT_BYREF) && (v->vt & VT_TYPEMASK) == VT_VARIANT && v->pvarVal)
        v = v->pvarVal;
    switch (v->vt) {
    case VT_EMPTY:
        wcscpy(out, L"[undefined]");
        return;
    case VT_BSTR:
        _snwprintf(out, 80, L"[string: \"%.20s\"]", v->bstrVal ? v->bstrVal : L"");
        return;
    case VT_DISPATCH:
    case VT_UNKNOWN:
        wcscpy(out, L"[object]");
        return;
    }
    VARIANT s;
    VariantInit(&s);
    if (v->vt & VT_ARRAY) {
        wcscpy(out, L"[object]");
        return;
    }
    if (FAILED(VariantChangeTypeEx(&s, (VARIANT *)v, 0x400, 2, VT_BSTR))) {
        out[0] = 0;
        return;
    }
    if (v->vt == VT_I2 || v->vt == VT_I4 || v->vt == VT_R4 || v->vt == VT_R8 || v->vt == VT_UI1)
        _snwprintf(out, 80, L"[number: %.20s]", s.bstrVal);
    else if (v->vt == VT_CY)
        _snwprintf(out, 80, L"[currency: %.20s]", s.bstrVal);
    else
        _snwprintf(out, 80, L"%.20s", s.bstrVal);
    VariantClear(&s);
}

/* the name an operand goes by in an error message. */
static void describe_expr(Expr *x, const VARIANT *v, WCHAR out[80])
{
    if (x && x->Kind == X_NAME && !x->Parens) {
        _snwprintf(out, 80, L"%.60s", x->u.Name.Name);
        return;
    }
    if (x && x->Kind == X_MEMBER) {
        if (x->u.Member.Object && x->u.Member.Object->Kind == X_NAME)
            _snwprintf(out, 80, L"%.30s.%.30s", x->u.Member.Object->u.Name.Name, x->u.Member.Name);
        else
            _snwprintf(out, 80, L"%.60s", x->u.Member.Name);
        return;
    }
    describe_value(v, out);
}

/* ------------------------------------------------------------------ symbols */

Sym *sym_find(Engine *e, PCWSTR name, ULONG hash)
{
    for (Sym *s = e->Buckets[hash & 127]; s; s = s->Next)
        if (s->Hash == hash && name_equal(s->Name, name))
            return s;
    return nullptr;
}

Sym *sym_add(Engine *e, PCWSTR name, int kind)
{
    ULONG hash = name_hash(name);
    Sym *s = vbs_alloc(sizeof(Sym));
    if (!s)
        return nullptr;
    s->Name = vbs_wcsdup(name);
    s->Hash = hash;
    s->Kind = (BYTE)kind;
    s->Next = e->Buckets[hash & 127];
    e->Buckets[hash & 127] = s;
    e->Generation++;
    return s;
}

Member *member_find(Class *cls, PCWSTR name, ULONG hash)
{
    for (Member *m = cls->Members; m; m = m->Next)
        if (m->Hash == hash && name_equal(m->Name, name))
            return m;
    return nullptr;
}

/* ------------------------------------------------------------------ names */

enum { TG_NONE, TG_CELL, TG_PROC, TG_MEMBER, TG_BUILTIN, TG_ITEM, TG_CLASS, TG_CONSTANT };

typedef struct Target {
    int Kind;
    VARIANT *Cell;
    BOOL ReadOnly;
    Proc *P;
    Instance *Me;
    Member *Mem;
    const BuiltinDef *Builtin;
    VARIANT Item;
    PCWSTR ItemName;
    VARIANT Constant;
    Sym *Symbol;
} Target;

static VARIANT *cell_target(VARIANT *cell)
{
    while ((cell->vt & VT_BYREF) && (cell->vt & VT_TYPEMASK) == VT_VARIANT && cell->pvarVal)
        cell = cell->pvarVal;
    return cell;
}

static int item_object(Frame *f, Sym *s, VARIANT *out)
{
    Engine *e = f->E;
    if (s->Value.vt != VT_DISPATCH || !s->Value.pdispVal) {
        IUnknown *unk = nullptr;
        if (!e->Site)
            return R_ERR;
        HRESULT hr = e->Site->lpVtbl->GetItemInfo(e->Site, s->Name, SCRIPTINFO_IUNKNOWN, &unk, nullptr);
        if (FAILED(hr) || !unk)
            return R_ERR;
        IDispatch *d = nullptr;
        unk->lpVtbl->QueryInterface(unk, &IID_IDispatch, (void **)&d);
        unk->lpVtbl->Release(unk);
        if (!d)
            return R_ERR;
        VariantClear(&s->Value);
        s->Value.vt = VT_DISPATCH;
        s->Value.pdispVal = d;
    }
    VariantCopy(out, &s->Value);
    return R_OK;
}

static BOOL item_has_member(Frame *f, Sym *item, PCWSTR name)
{
    VARIANT obj;
    VariantInit(&obj);
    if (item_object(f, item, &obj) != R_OK)
        return FALSE;
    DISPID id;
    LPOLESTR names[1] = {(LPOLESTR)name};
    HRESULT hr = obj.pdispVal->lpVtbl->GetIDsOfNames(obj.pdispVal, &IID_NULL, names, 1, f->E->Lcid, &id);
    VariantClear(&obj);
    return SUCCEEDED(hr);
}

static void resolve_late(Frame *f, Expr *e, Target *t, BOOL assign)
{
    Engine *eng = f->E;
    PCWSTR name = e->u.Name.Name;
    ULONG hash = e->u.Name.Hash;
    memset(t, 0, sizeof(*t));
    for (DynName *d = f->Dyn; d; d = d->Next)
        if (d->Hash == hash && name_equal(d->Name, name)) {
            t->Kind = TG_CELL;
            t->Cell = &d->Value;
            return;
        }
    if (f->P) {
        for (UINT i = 0; i < f->P->LocalCount && i < f->LocalCount; i++)
            if (f->P->LocalHashes[i] == hash && name_equal(f->P->Locals[i], name)) {
                t->Kind = TG_CELL;
                t->Cell = &f->Locals[i];
                return;
            }
    }
    if (f->Me) {
        Member *m = member_find(f->Me->Cls, name, hash);
        if (m) {
            if (m->Kind == M_FIELD) {
                t->Kind = TG_CELL;
                t->Cell = &f->Me->Fields[m->Slot];
            } else {
                t->Kind = TG_MEMBER;
                t->Mem = m;
                t->Me = f->Me;
            }
            return;
        }
    }
    Sym *s = sym_find(eng, name, hash);
    if (s) {
        e->Bound = B_GLOBAL;
        e->u.Name.Symbol = s;
        t->Symbol = s;
        switch (s->Kind) {
        case SYM_VAR:
        case SYM_CONST:
            t->Kind = TG_CELL;
            t->Cell = &s->Value;
            t->ReadOnly = s->Kind == SYM_CONST;
            break;
        case SYM_PROC:
            t->Kind = TG_PROC;
            t->P = s->Procedure;
            break;
        case SYM_CLASS:
            t->Kind = TG_CLASS;
            break;
        case SYM_ITEM:
            t->Kind = TG_ITEM;
            VariantInit(&t->Item);
            if (item_object(f, s, &t->Item) != R_OK)
                t->Kind = TG_NONE;
            t->ItemName = nullptr;
            break;
        }
        return;
    }
    /* members of the items that are global. */
    for (Sym *item = nullptr, **b = eng->Buckets; b < eng->Buckets + 128; b++)
        for (item = *b; item; item = item->Next)
            if (item->Kind == SYM_ITEM && (item->ItemFlags & SCRIPTITEM_GLOBALMEMBERS) && item_has_member(f, item, name)) {
                t->Kind = TG_ITEM;
                VariantInit(&t->Item);
                item_object(f, item, &t->Item);
                t->ItemName = name;
                return;
            }
    if (!assign) {
        const BuiltinDef *b2 = builtin_find(name, hash);
        if (b2) {
            t->Kind = TG_BUILTIN;
            t->Builtin = b2;
            return;
        }
        VARIANT c;
        VariantInit(&c);
        if (constant_find(name, &c)) {
            t->Kind = TG_CONSTANT;
            t->Constant = c;
            return;
        }
        if (name_equal(name, L"Err")) {
            t->Kind = TG_CONSTANT;
            t->Constant.vt = VT_DISPATCH;
            t->Constant.pdispVal = eng->ErrObject;
            eng->ErrObject->lpVtbl->AddRef(eng->ErrObject);
            return;
        }
    }
    t->Kind = TG_NONE;
}

static void resolve_name(Frame *f, Expr *e, Target *t, BOOL assign)
{
    switch (e->Bound) {
    case B_LOCAL:
    case B_RESULT:
        memset(t, 0, sizeof(*t));
        t->Kind = TG_CELL;
        t->Cell = &f->Locals[e->u.Name.Slot];
        return;
    case B_GLOBAL: {
        Sym *s = e->u.Name.Symbol;
        memset(t, 0, sizeof(*t));
        t->Symbol = s;
        if (s->Kind == SYM_VAR || s->Kind == SYM_CONST) {
            t->Kind = TG_CELL;
            t->Cell = &s->Value;
            t->ReadOnly = s->Kind == SYM_CONST;
            return;
        }
        break;
    }
    case B_FIELD:
        memset(t, 0, sizeof(*t));
        t->Kind = TG_CELL;
        t->Cell = &f->Me->Fields[e->u.Name.Slot];
        return;
    default:
        break;
    }
    resolve_late(f, e, t, assign);
}

static void target_release(Target *t)
{
    if (t->Kind == TG_ITEM)
        VariantClear(&t->Item);
    if (t->Kind == TG_CONSTANT)
        VariantClear(&t->Constant);
}

/* ------------------------------------------------------------------ values */

static int stack_check(Frame *f)
{
    PTEB teb = NtCurrentTeb();
    BYTE marker;
    if ((BYTE *)&marker < (BYTE *)teb->DeallocationStack + 0x14000)
        return raise_error(f, E_VBS_STACK, nullptr);
    return R_OK;
}

extern const WCHAR g_detail_empty[];

static int obj_value(Frame *f, VARIANT *v, Expr *src)
{
    /* a value read from an object: its default property. */
    for (int depth = 0; depth < 8; depth++) {
        IDispatch *d = nullptr;
        if (v->vt == VT_DISPATCH)
            d = v->pdispVal;
        else if (v->vt == VT_UNKNOWN && v->punkVal) {
            v->punkVal->lpVtbl->QueryInterface(v->punkVal, &IID_IDispatch, (void **)&d);
            if (!d)
                return raise_error(f, E_VBS_NO_MEMBER, nullptr);
            VariantClear(v);
            v->vt = VT_DISPATCH;
            v->pdispVal = d;
        } else
            return R_OK;
        if (!d)
            return raise_error(f, E_VBS_NOT_SET, nullptr);
        VARIANT r;
        VariantInit(&r);
        int rc = obj_default(f, v, DISPATCH_METHOD | DISPATCH_PROPERTYGET, nullptr, 0, &r);
        if (rc != R_OK) {
            if (src && f->E->Fault.Set && !f->E->Fault.Host && f->E->Err.Number == E_VBS_NO_MEMBER) {
                WCHAR name[80];
                describe_expr(src, v, name);
                raise_error(f, E_VBS_NO_MEMBER, name);
            }
            return rc;
        }
        VariantClear(v);
        *v = r;
    }
    return R_OK;
}

int value_of(Frame *f, VARIANT *v)
{
    return obj_value(f, v, nullptr);
}

static int value_of_expr(Frame *f, VARIANT *v, Expr *src)
{
    return obj_value(f, v, src);
}

/* the value of an expression: an object gives its default property. */
static int rvalue(Frame *f, Expr *x, VARIANT *out)
{
    int rc = eval(f, x, out);
    if (rc != R_OK)
        return rc;
    if (out->vt == VT_DISPATCH || out->vt == VT_UNKNOWN) {
        rc = obj_value(f, out, x);
        if (rc != R_OK)
            VariantClear(out);
    }
    return rc;
}

int to_bool(Frame *f, VARIANT *v, BOOL *out)
{
    VARIANT b;
    VariantInit(&b);
    if (v->vt == VT_BOOL) {
        *out = v->boolVal != 0;
        return R_OK;
    }
    if (v->vt == VT_NULL) {
        *out = FALSE;
        return R_OK;
    }
    if (v->vt == VT_EMPTY) {
        *out = FALSE;
        return R_OK;
    }
    HRESULT hr = VariantChangeTypeEx(&b, v, f->E->Lcid, 0, VT_BOOL);
    if (FAILED(hr)) {
        WCHAR d[80];
        describe_value(v, d);
        return raise_error(f, hr == DISP_E_OVERFLOW ? E_VBS_OVERFLOW : E_VBS_TYPE_MISMATCH, d);
    }
    *out = b.boolVal != 0;
    return R_OK;
}

int to_long(Frame *f, VARIANT *v, LONG *out, PCWSTR detail)
{
    VARIANT b;
    VariantInit(&b);
    if (v->vt == VT_I4) {
        *out = v->lVal;
        return R_OK;
    }
    if (v->vt == VT_NULL)
        return raise_error(f, detail ? E_VBS_TYPE_MISMATCH : E_VBS_NULL, detail ? detail : L"Null");
    HRESULT hr = VariantChangeTypeEx(&b, v, f->E->Lcid, 0, VT_I4);
    if (FAILED(hr))
        return raise_error(f, hr == DISP_E_OVERFLOW ? E_VBS_OVERFLOW : E_VBS_TYPE_MISMATCH, detail);
    *out = b.lVal;
    return R_OK;
}

/* ------------------------------------------------------------------ arrays */

#define FADF_DYNAMIC_MARK 0x2000

static SAFEARRAY *array_of(VARIANT *v)
{
    if (!(v->vt & VT_ARRAY))
        return nullptr;
    if (v->vt & VT_BYREF)
        return v->pparray ? *v->pparray : nullptr;
    return v->parray;
}

static int array_indices(Frame *f, SAFEARRAY *sa, VARIANT *argv, UINT argc, LONG *indices)
{
    if (argc != sa->cDims)
        return raise_error(f, E_VBS_SUBSCRIPT, nullptr);
    for (UINT i = 0; i < argc; i++) {
        VARIANT v;
        var_deref(&v, &argv[i]);
        if (v.vt == VT_DISPATCH || v.vt == VT_UNKNOWN)
            value_of(f, &v);
        LONG idx;
        int rc = to_long(f, &v, &idx, L"Null");
        VariantClear(&v);
        if (rc != R_OK) {
            if (f->E->Err.Number == E_VBS_NULL)
                raise_error(f, E_VBS_TYPE_MISMATCH, L"Null");
            return rc;
        }
        indices[i] = idx;
    }
    for (UINT i = 0; i < argc; i++) {
        LONG lo, hi;
        SafeArrayGetLBound(sa, i + 1, &lo);
        SafeArrayGetUBound(sa, i + 1, &hi);
        if (indices[i] < lo || indices[i] > hi) {
            WCHAR d[40];
            _snwprintf(d, 40, L"[number: %d]", (int)indices[i]);
            return raise_error(f, E_VBS_SUBSCRIPT, d);
        }
    }
    return R_OK;
}

static int array_element_get(Frame *f, VARIANT *arr, VARIANT *argv, UINT argc, VARIANT *out)
{
    SAFEARRAY *sa = array_of(arr);
    LONG indices[8];
    if (!sa || sa->cDims > 8)
        return raise_error(f, E_VBS_SUBSCRIPT, nullptr);
    int rc = array_indices(f, sa, argv, argc, indices);
    if (rc != R_OK)
        return rc;
    VARTYPE vt = (arr->vt & VT_TYPEMASK);
    if (vt == VT_VARIANT) {
        VARIANT *p;
        SafeArrayPtrOfIndex(sa, indices, (void **)&p);
        VariantCopyInd(out, p);
        return R_OK;
    }
    VARTYPE ev = 0;
    SafeArrayGetVartype(sa, &ev);
    VariantInit(out);
    if (ev == VT_BSTR || ev == VT_DISPATCH || ev == VT_UNKNOWN || ev == VT_VARIANT) {
        void *p;
        SafeArrayPtrOfIndex(sa, indices, &p);
        out->vt = ev;
        if (ev == VT_BSTR)
            out->bstrVal = SysAllocStringLen(*(BSTR *)p, SysStringLen(*(BSTR *)p));
        else {
            out->punkVal = *(IUnknown **)p;
            if (out->punkVal)
                out->punkVal->lpVtbl->AddRef(out->punkVal);
        }
        return R_OK;
    }
    out->vt = ev;
    SafeArrayGetElement(sa, indices, &out->lVal);
    return R_OK;
}

static int array_element_ref(Frame *f, VARIANT *arr, VARIANT *argv, UINT argc, VARIANT **cell)
{
    SAFEARRAY *sa = array_of(arr);
    LONG indices[8];
    if (!sa || sa->cDims > 8 || (arr->vt & VT_TYPEMASK) != VT_VARIANT)
        return raise_error(f, E_VBS_SUBSCRIPT, nullptr);
    int rc = array_indices(f, sa, argv, argc, indices);
    if (rc != R_OK)
        return rc;
    SafeArrayPtrOfIndex(sa, indices, (void **)cell);
    return R_OK;
}

/* a new array of variants, fixed when declared with bounds. */
static SAFEARRAY *array_new(UINT dims, LONG *upper, BOOL fixed)
{
    SAFEARRAYBOUND bounds[8];
    for (UINT i = 0; i < dims; i++) {
        bounds[i].lLbound = 0;
        bounds[i].cElements = upper[i] + 1;
    }
    SAFEARRAY *sa = SafeArrayCreate(VT_VARIANT, dims, bounds);
    if (sa && fixed)
        sa->fFeatures |= FADF_DYNAMIC_MARK;
    return sa;
}

/* ------------------------------------------------------------------ calls */

typedef struct ArgList {
    VARIANT Local[8];
    VARIANT *V;
    UINT Count;
    BOOL Heap;
    BOOL *Owned;
    BOOL OwnedLocal[8];
} ArgList;

static void arglist_free(ArgList *a)
{
    for (UINT i = 0; i < a->Count; i++)
        if (a->Owned[i])
            VariantClear(&a->V[i]);
    if (a->Heap) {
        vbs_free(a->V);
        vbs_free(a->Owned);
    }
}

static BOOL ref_able(Expr *x)
{
    if (!x || x->Parens == 1)
        return FALSE;
    return x->Kind == X_NAME || x->Kind == X_CALL || (x->Kind == X_MEMBER && x->u.Member.Object && 0);
}

static int lvalue_cell(Frame *f, Expr *x, VARIANT **cell);

/* evaluates the arguments of a call. a plain variable goes by reference when the callee takes references. */
static int arglist_eval(Frame *f, Args *args, Proc *callee, BOOL refs, ArgList *a)
{
    memset(a, 0, sizeof(*a));
    a->Count = args ? args->Count : 0;
    a->V = a->Local;
    a->Owned = a->OwnedLocal;
    if (a->Count > 8) {
        a->V = vbs_alloc(a->Count * sizeof(VARIANT));
        a->Owned = vbs_alloc(a->Count * sizeof(BOOL));
        a->Heap = TRUE;
    }
    for (UINT i = 0; i < a->Count; i++)
        VariantInit(&a->V[i]);
    for (UINT i = 0; i < a->Count; i++) {
        Expr *x = args->Items[i];
        if (!x) {
            a->V[i].vt = VT_ERROR;
            a->V[i].scode = DISP_E_PARAMNOTFOUND;
            continue;
        }
        BOOL want_ref = refs && ref_able(x) && (!callee || (i < callee->ParamCount && !callee->Params[i].ByVal));
        if (want_ref) {
            VARIANT *cell = nullptr;
            int rc = lvalue_cell(f, x, &cell);
            if (rc == R_OK && cell) {
                VARIANT *t = cell_target(cell);
                if (t->vt == VT_NULL || t->vt >= 0) {
                    a->V[i].vt = VT_VARIANT | VT_BYREF;
                    a->V[i].pvarVal = t;
                    continue;
                }
            } else if (rc != R_OK) {
                a->Count = i;
                arglist_free(a);
                a->Count = 0;
                return rc;
            }
        }
        int rc = eval(f, x, &a->V[i]);
        if (rc != R_OK) {
            a->Count = i;
            arglist_free(a);
            a->Count = 0;
            return rc;
        }
        a->Owned[i] = TRUE;
    }
    for (UINT i = 0; i < a->Count; i++)
        if (!a->Owned[i] && !(a->V[i].vt & VT_BYREF))
            a->Owned[i] = TRUE;
    return R_OK;
}

/* calls a procedure of the script. */
int call_proc(Engine *e, Proc *proc, Instance *me, VARIANT *args, UINT count, VARIANT *result, Frame *caller)
{
    Frame f = {};
    f.E = e;
    f.P = proc;
    f.Me = me;
    f.Caller = caller;
    f.Unit = proc->Unit;
    f.Line = proc->Line;
    f.Col = proc->Col;
    if (count != proc->ParamCount) {
        Frame c = caller ? *caller : f;
        return raise_error(&c, E_VBS_WRONG_ARGS, proc->Name);
    }
    if (stack_check(caller ? caller : &f) != R_OK)
        return R_ERR;
    UINT n = proc->LocalCount;
    VARIANT local[24];
    VARIANT *locals = n <= 24 ? local : vbs_alloc(n * sizeof(VARIANT));
    for (UINT i = 0; i < n; i++)
        VariantInit(&locals[i]);
    f.Locals = locals;
    f.LocalCount = n;
    for (UINT i = 0; i < count; i++) {
        VARIANT *a = &args[i];
        if (!proc->Params[i].ByVal && (a->vt == (VT_VARIANT | VT_BYREF)) && a->pvarVal) {
            locals[i] = *a;
        } else {
            var_deref(&locals[i], a);
            if (locals[i].vt == VT_DISPATCH || locals[i].vt == VT_UNKNOWN) {
                /* an object stays an object when passed. */
            }
        }
    }
    e->Depth++;
    int rc = run_statements(&f, proc->Body);
    e->Depth--;
    if (rc >= R_EXIT_FOR)
        rc = R_OK;
    if (rc == R_OK && result) {
        if (proc->ResultSlot >= 0) {
            VARIANT *rs = cell_target(&locals[proc->ResultSlot]);
            VariantCopy(result, rs);
        } else
            VariantInit(result);
    }
    for (UINT i = 0; i < n; i++) {
        if (!(locals[i].vt == (VT_VARIANT | VT_BYREF)))
            VariantClear(&locals[i]);
    }
    for (DynName *d = f.Dyn; d;) {
        DynName *next = d->Next;
        VariantClear(&d->Value);
        vbs_free(d->Name);
        vbs_free(d);
        d = next;
    }
    while (f.With) {
        WithLink *w = f.With;
        f.With = w->Prev;
        VariantClear(&w->Object);
        vbs_free(w);
    }
    if (locals != local)
        vbs_free(locals);
    return rc;
}

/* ------------------------------------------------------------------ lvalues */

static int field_cell(Frame *f, Instance *inst, PCWSTR name, VARIANT **cell)
{
    Member *m = member_find(inst->Cls, name, name_hash(name));
    if (m && m->Kind == M_FIELD && (m->Public & 1)) {
        *cell = &inst->Fields[m->Slot];
        return R_OK;
    }
    *cell = nullptr;
    return R_OK;
}

static int lvalue_cell(Frame *f, Expr *x, VARIANT **cell)
{
    *cell = nullptr;
    if (x->Kind == X_NAME) {
        Target t;
        resolve_name(f, x, &t, TRUE);
        if (t.Kind == TG_CELL && !t.ReadOnly) {
            *cell = cell_target(t.Cell);
            if (t.Cell->vt == (VT_VARIANT | VT_BYREF))
                *cell = t.Cell->pvarVal;
            else
                *cell = t.Cell;
        }
        target_release(&t);
        return R_OK;
    }
    if (x->Kind == X_CALL && x->u.Call.Target->Kind == X_NAME) {
        Target t;
        resolve_name(f, x->u.Call.Target, &t, TRUE);
        if (t.Kind == TG_CELL) {
            VARIANT *arr = cell_target(t.Cell);
            if (arr->vt & VT_ARRAY) {
                ArgList a;
                int rc = arglist_eval(f, &x->u.Call.Arguments, nullptr, FALSE, &a);
                if (rc != R_OK)
                    return rc;
                rc = array_element_ref(f, arr, a.V, a.Count, cell);
                arglist_free(&a);
                return rc;
            }
        }
        target_release(&t);
        return R_OK;
    }
    if (x->Kind == X_MEMBER && x->u.Member.Object && !x->u.Member.HasArguments) {
        VARIANT obj;
        VariantInit(&obj);
        if (x->u.Member.Object->Kind == X_ME && f->Me)
            return field_cell(f, f->Me, x->u.Member.Name, cell);
        (void)obj;
    }
    return R_OK;
}

/* ------------------------------------------------------------------ expressions */

static int eval_binary(Frame *f, Expr *x, VARIANT *out)
{
    VARIANT l, r;
    VariantInit(&l);
    VariantInit(&r);
    int rc = eval(f, x->u.Operands.Left, &l);
    if (rc != R_OK)
        return rc;
    if (x->Op == OP_IS) {
        rc = eval(f, x->u.Operands.Right, &r);
        if (rc != R_OK) {
            VariantClear(&l);
            return rc;
        }
        BOOL same;
        IUnknown *a = nullptr, *b = nullptr;
        int ok = 1;
        VARIANT *pl = cell_target(&l), *pr = cell_target(&r);
        if ((pl->vt != VT_DISPATCH && pl->vt != VT_UNKNOWN) || (pr->vt != VT_DISPATCH && pr->vt != VT_UNKNOWN))
            ok = 0;
        if (!ok) {
            VariantClear(&l);
            VariantClear(&r);
            return raise_error(f, E_VBS_OBJECT_REQUIRED, nullptr);
        }
        if (pl->punkVal)
            pl->punkVal->lpVtbl->QueryInterface(pl->punkVal, &IID_IUnknown, (void **)&a);
        if (pr->punkVal)
            pr->punkVal->lpVtbl->QueryInterface(pr->punkVal, &IID_IUnknown, (void **)&b);
        same = a == b;
        if (a)
            a->lpVtbl->Release(a);
        if (b)
            b->lpVtbl->Release(b);
        VariantClear(&l);
        VariantClear(&r);
        out->vt = VT_BOOL;
        out->boolVal = same ? VARIANT_TRUE : VARIANT_FALSE;
        return R_OK;
    }
    rc = value_of_expr(f, &l, x->u.Operands.Left);
    if (rc != R_OK) {
        VariantClear(&l);
        return rc;
    }
    rc = rvalue(f, x->u.Operands.Right, &r);
    if (rc != R_OK) {
        VariantClear(&l);
        return rc;
    }
    VARIANT *pl = cell_target(&l), *pr = cell_target(&r);
    HRESULT hr;
    VARIANT res;
    VariantInit(&res);
    if (x->Op >= OP_EQ && x->Op <= OP_GE) {
        int cmp;
        hr = var_compare(pl, pr, f->E->Lcid, &cmp);
        if (SUCCEEDED(hr)) {
            if (cmp == 2) {
                out->vt = VT_NULL;
            } else {
                BOOL v;
                switch (x->Op) {
                case OP_EQ: v = cmp == 0; break;
                case OP_NE: v = cmp != 0; break;
                case OP_LT: v = cmp < 0; break;
                case OP_GT: v = cmp > 0; break;
                case OP_LE: v = cmp <= 0; break;
                default: v = cmp >= 0; break;
                }
                out->vt = VT_BOOL;
                out->boolVal = v ? VARIANT_TRUE : VARIANT_FALSE;
            }
            VariantClear(&l);
            VariantClear(&r);
            return R_OK;
        }
    } else {
        hr = var_binary(x->Op, pl, pr, &res, f->E->Lcid);
        if (SUCCEEDED(hr)) {
            *out = res;
            VariantClear(&l);
            VariantClear(&r);
            return R_OK;
        }
    }
    WCHAR d[80];
    d[0] = 0;
    LONG number = error_number_of_hresult(hr);
    if (hr == DISP_E_TYPEMISMATCH) {
        number = E_VBS_TYPE_MISMATCH;
        BOOL left_bad = pl->vt == VT_BSTR || pl->vt == VT_DISPATCH || (pl->vt & VT_ARRAY);
        BOOL right_bad = pr->vt == VT_BSTR || pr->vt == VT_DISPATCH || (pr->vt & VT_ARRAY);
        if (left_bad && pl->vt == VT_BSTR && right_bad && pr->vt == VT_BSTR)
            left_bad = TRUE;
        if (pl->vt == VT_BSTR && pr->vt != VT_BSTR) {
            VARIANT t;
            VariantInit(&t);
            if (FAILED(VariantChangeTypeEx(&t, pl, f->E->Lcid, 0, VT_R8)))
                describe_expr(x->u.Operands.Left, pl, d);
            else
                describe_expr(x->u.Operands.Right, pr, d);
        } else if (pr->vt == VT_BSTR && pl->vt != VT_BSTR) {
            VARIANT t;
            VariantInit(&t);
            if (FAILED(VariantChangeTypeEx(&t, pr, f->E->Lcid, 0, VT_R8)))
                describe_expr(x->u.Operands.Right, pr, d);
            else
                describe_expr(x->u.Operands.Left, pl, d);
        } else if (left_bad)
            describe_expr(x->u.Operands.Left, pl, d);
        else
            describe_expr(x->u.Operands.Right, pr, d);
    } else if (hr == DISP_E_OVERFLOW) {
        number = E_VBS_OVERFLOW;
        if (x->Op == OP_AND || x->Op == OP_OR || x->Op == OP_XOR || x->Op == OP_EQV || x->Op == OP_IMP) {
            VARIANT t;
            VariantInit(&t);
            if (FAILED(VariantChangeTypeEx(&t, pl, f->E->Lcid, 0, VT_I4)))
                describe_expr(x->u.Operands.Left, pl, d);
            else
                describe_expr(x->u.Operands.Right, pr, d);
        }
    } else if (hr == DISP_E_DIVBYZERO)
        number = E_VBS_DIV_ZERO;
    VariantClear(&l);
    VariantClear(&r);
    return raise_error(f, number, d);
}

static int eval_unary(Frame *f, Expr *x, VARIANT *out)
{
    VARIANT v;
    VariantInit(&v);
    int rc = rvalue(f, x->u.Operand, &v);
    if (rc != R_OK)
        return rc;
    VARIANT *p = cell_target(&v);
    HRESULT hr = var_unary(x->Op, p, out, f->E->Lcid);
    if (FAILED(hr)) {
        WCHAR d[80];
        d[0] = 0;
        LONG number = hr == DISP_E_OVERFLOW ? E_VBS_OVERFLOW : hr == DISP_E_TYPEMISMATCH ? E_VBS_TYPE_MISMATCH : error_number_of_hresult(hr);
        describe_expr(x->u.Operand, p, d);
        VariantClear(&v);
        return raise_error(f, number, d);
    }
    VariantClear(&v);
    return R_OK;
}

/* the object a member is looked up on. */
static int eval_object(Frame *f, Expr *x, VARIANT *obj)
{
    if (!x->u.Member.Object) {
        if (!f->With)
            return raise_error(f, E_VBS_UNQUALIFIED, nullptr);
        VariantInit(obj);
        VariantCopy(obj, &f->With->Object);
        return R_OK;
    }
    int rc = eval(f, x->u.Member.Object, obj);
    if (rc != R_OK)
        return rc;
    if (obj->vt == (VT_VARIANT | VT_BYREF)) {
        VARIANT t;
        var_deref(&t, obj);
        VariantClear(obj);
        *obj = t;
    }
    if (obj->vt == VT_DISPATCH || obj->vt == VT_UNKNOWN)
        return R_OK;
    /* a plain value stands for its default property when that is an object. */
    VariantClear(obj);
    return raise_error(f, E_VBS_OBJECT_REQUIRED, nullptr);
}

static void member_detail(Expr *x, WCHAR out[80])
{
    if (x->u.Member.Object && x->u.Member.Object->Kind == X_NAME)
        _snwprintf(out, 80, L"%.30s.%.30s", x->u.Member.Object->u.Name.Name, x->u.Member.Name);
    else if (x->u.Member.Object && x->u.Member.Object->Kind == X_MEMBER)
        _snwprintf(out, 80, L"%.60s", x->u.Member.Name);
    else
        _snwprintf(out, 80, L"%.60s", x->u.Member.Name);
}

static int eval_member(Frame *f, Expr *x, VARIANT *out, BOOL statement)
{
    VARIANT obj;
    VariantInit(&obj);
    int rc = eval_object(f, x, &obj);
    if (rc != R_OK) {
        if (f->E->Err.Number == E_VBS_OBJECT_REQUIRED && x->u.Member.Object) {
            if (x->u.Member.Object->Kind == X_NAME)
                raise_error(f, E_VBS_OBJECT_REQUIRED, g_detail_empty);
        }
        return rc;
    }
    ArgList a;
    rc = arglist_eval(f, x->u.Member.HasArguments ? &x->u.Member.Arguments : nullptr, nullptr, TRUE, &a);
    if (rc != R_OK) {
        VariantClear(&obj);
        return rc;
    }
    WORD flags = DISPATCH_METHOD;
    if (!statement && !(x->u.Member.HasArguments && a.Count == 0))
        flags |= DISPATCH_PROPERTYGET;
    WCHAR detail[80];
    member_detail(x, detail);
    VARIANT result;
    VariantInit(&result);
    rc = obj_get(f, &obj, x->u.Member.Name, nullptr, a.V, a.Count, flags, &result, nullptr);
    if (rc != R_OK && f->E->Err.Number == E_VBS_NO_MEMBER && !f->E->Fault.Host)
        raise_error(f, E_VBS_NO_MEMBER, detail);
    arglist_free(&a);
    VariantClear(&obj);
    if (rc != R_OK)
        return rc;
    *out = result;
    return R_OK;
}

static int eval_new(Frame *f, Expr *x, VARIANT *out)
{
    Engine *e = f->E;
    Sym *s = sym_find(e, x->u.New.Name, name_hash(x->u.New.Name));
    if (s && s->Kind == SYM_CLASS) {
        Instance *inst = instance_create(e, s->Cls, f);
        if (!inst)
            return R_ERR;
        out->vt = VT_DISPATCH;
        out->pdispVal = &inst->Dispatch;
        return R_OK;
    }
    if (name_equal(x->u.New.Name, L"RegExp")) {
        out->vt = VT_DISPATCH;
        out->pdispVal = regexp_create(e);
        return R_OK;
    }
    return raise_error(f, E_VBS_CLASS_UNDEFINED, x->u.New.Name);
}

/* calls what a name stands for with arguments. */
static int eval_name_call(Frame *f, Expr *name_expr, Args *args, BOOL statement, BOOL has_parens, VARIANT *out, Expr *whole)
{
    Target t;
    resolve_name(f, name_expr, &t, FALSE);
    int rc = R_OK;
    ArgList a;
    PCWSTR name = name_expr->u.Name.Name;
    BOOL has_args = args && args->Count;
    switch (t.Kind) {
    case TG_CELL: {
        VARIANT *cell = cell_target(t.Cell);
        if (!has_args && !(args && has_parens)) {
            VariantInit(out);
            VariantCopy(out, cell);
            if (out->vt & VT_ARRAY) {
                /* an array stays an array. */
            }
            break;
        }
        rc = arglist_eval(f, args, nullptr, TRUE, &a);
        if (rc != R_OK)
            break;
        if (cell->vt & VT_ARRAY) {
            rc = array_element_get(f, cell, a.V, a.Count, out);
        } else if (cell->vt == VT_DISPATCH || cell->vt == VT_UNKNOWN) {
            VARIANT obj;
            VariantInit(&obj);
            VariantCopy(&obj, cell);
            rc = obj_default(f, &obj, DISPATCH_METHOD | DISPATCH_PROPERTYGET, a.V, a.Count, out);
            VariantClear(&obj);
        } else {
            rc = raise_error(f, E_VBS_TYPE_MISMATCH, nullptr);
        }
        arglist_free(&a);
        break;
    }
    case TG_PROC: {
        rc = arglist_eval(f, args, t.P, TRUE, &a);
        if (rc != R_OK)
            break;
        if (t.P->Kind == PK_SUB && !statement) {
            arglist_free(&a);
            rc = raise_error(f, E_VBS_TYPE_MISMATCH, name);
            break;
        }
        VARIANT result;
        VariantInit(&result);
        rc = call_proc(f->E, t.P, nullptr, a.V, a.Count, &result, f);
        arglist_free(&a);
        if (rc == R_OK)
            *out = result;
        break;
    }
    case TG_MEMBER: {
        Member *m = t.Mem;
        Proc *p = m->Kind == M_METHOD ? m->Method : m->Get;
        if (!p) {
            rc = raise_error(f, E_VBS_NO_MEMBER, name);
            break;
        }
        rc = arglist_eval(f, args, p, TRUE, &a);
        if (rc != R_OK)
            break;
        if (p->Kind == PK_SUB && !statement) {
            arglist_free(&a);
            rc = raise_error(f, E_VBS_TYPE_MISMATCH, name);
            break;
        }
        VARIANT result;
        VariantInit(&result);
        rc = call_proc(f->E, p, t.Me, a.V, a.Count, &result, f);
        arglist_free(&a);
        if (rc == R_OK)
            *out = result;
        break;
    }
    case TG_BUILTIN: {
        rc = arglist_eval(f, args, nullptr, FALSE, &a);
        if (rc != R_OK)
            break;
        for (UINT i = 0; i < a.Count && !t.Builtin->Raw; i++) {
            if (a.V[i].vt == VT_ERROR && a.V[i].scode == DISP_E_PARAMNOTFOUND)
                continue;
            VARIANT *p = &a.V[i];
            if (p->vt == VT_DISPATCH || p->vt == VT_UNKNOWN) {
                rc = obj_value(f, p, nullptr);
                if (rc != R_OK)
                    break;
            }
        }
        if (rc == R_OK) {
            if (a.Count < t.Builtin->Min || a.Count > t.Builtin->Max)
                rc = raise_error(f, E_VBS_WRONG_ARGS, name);
            else {
                VARIANT result;
                VariantInit(&result);
                rc = t.Builtin->Function(f, a.V, a.Count, &result);
                if (rc == R_OK)
                    *out = result;
                else
                    VariantClear(&result);
            }
        }
        arglist_free(&a);
        break;
    }
    case TG_ITEM: {
        rc = arglist_eval(f, args, nullptr, TRUE, &a);
        if (rc != R_OK)
            break;
        if (t.ItemName) {
            WORD flags = statement ? DISPATCH_METHOD : (DISPATCH_METHOD | DISPATCH_PROPERTYGET);
            rc = obj_get(f, &t.Item, t.ItemName, nullptr, a.V, a.Count, flags, out, nullptr);
        } else if (a.Count) {
            rc = obj_default(f, &t.Item, DISPATCH_METHOD | DISPATCH_PROPERTYGET, a.V, a.Count, out);
        } else {
            VariantCopy(out, &t.Item);
        }
        arglist_free(&a);
        break;
    }
    case TG_CONSTANT:
        VariantCopy(out, &t.Constant);
        break;
    case TG_CLASS:
        rc = raise_error(f, E_VBS_TYPE_MISMATCH, name);
        break;
    default:
        if (f->E->Explicit || (f->Unit && f->Unit->Explicit)) {
            rc = raise_error(f, E_VBS_UNDEFINED, name);
        } else if (has_args) {
            rc = raise_error(f, E_VBS_TYPE_MISMATCH, name);
        } else if (statement && whole && whole->Kind == X_CALL && has_parens) {
            rc = raise_error(f, E_VBS_TYPE_MISMATCH, name);
        } else {
            VariantInit(out);
        }
        break;
    }
    target_release(&t);
    return rc;
}

static int eval_call(Frame *f, Expr *x, VARIANT *out, BOOL statement)
{
    Expr *target = x->u.Call.Target;
    if (target->Kind == X_NAME)
        return eval_name_call(f, target, &x->u.Call.Arguments, statement, x->Parens == 2, out, x);
    /* a value called with arguments: an array, or an object with a default property. */
    VARIANT base;
    VariantInit(&base);
    int rc = eval(f, target, &base);
    if (rc != R_OK)
        return rc;
    VARIANT *cell = cell_target(&base);
    ArgList a;
    rc = arglist_eval(f, &x->u.Call.Arguments, nullptr, TRUE, &a);
    if (rc != R_OK) {
        VariantClear(&base);
        return rc;
    }
    if (cell->vt & VT_ARRAY)
        rc = array_element_get(f, cell, a.V, a.Count, out);
    else if (cell->vt == VT_DISPATCH || cell->vt == VT_UNKNOWN)
        rc = obj_default(f, cell, DISPATCH_METHOD | DISPATCH_PROPERTYGET, a.V, a.Count, out);
    else
        rc = raise_error(f, E_VBS_TYPE_MISMATCH, nullptr);
    arglist_free(&a);
    VariantClear(&base);
    return rc;
}

static int eval_stmt_expr(Frame *f, Expr *x, VARIANT *out, BOOL statement)
{
    switch (x->Kind) {
    case X_NAME: return eval_name_call(f, x, nullptr, statement, FALSE, out, x);
    case X_CALL: return eval_call(f, x, out, statement);
    case X_MEMBER: return eval_member(f, x, out, statement);
    }
    return eval(f, x, out);
}

int eval(Frame *f, Expr *x, VARIANT *out)
{
    VariantInit(out);
    switch (x->Kind) {
    case X_CONST:
        if (x->u.Value.vt == VT_BSTR) {
            out->vt = VT_BSTR;
            out->bstrVal = SysAllocStringLen(x->u.Value.bstrVal, SysStringLen(x->u.Value.bstrVal));
            return R_OK;
        }
        *out = x->u.Value;
        return R_OK;
    case X_NAME: return eval_name_call(f, x, nullptr, FALSE, FALSE, out, x);
    case X_MEMBER: return eval_member(f, x, out, FALSE);
    case X_CALL: return eval_call(f, x, out, FALSE);
    case X_UNARY: return eval_unary(f, x, out);
    case X_BINARY: return eval_binary(f, x, out);
    case X_NEW: return eval_new(f, x, out);
    case X_ME:
        if (!f->Me)
            return raise_error(f, E_VBS_UNQUALIFIED, nullptr);
        out->vt = VT_DISPATCH;
        out->pdispVal = &f->Me->Dispatch;
        out->pdispVal->lpVtbl->AddRef(out->pdispVal);
        return R_OK;
    case X_EVAL: return rvalue(f, x->u.Operand, out);
    }
    return R_ERR;
}

/* ------------------------------------------------------------------ assignment */

static int assign_cell(Frame *f, VARIANT *cell, VARIANT *value)
{
    VARIANT *t = cell_target(cell);
    if (cell->vt == (VT_VARIANT | VT_BYREF))
        t = cell->pvarVal;
    else
        t = cell;
    VariantClear(t);
    if (value->vt == (VT_VARIANT | VT_BYREF)) {
        var_deref(t, value);
        return R_OK;
    }
    *t = *value;
    VariantInit(value);
    return R_OK;
}

static int assign_array_element(Frame *f, VARIANT *arr, VARIANT *argv, UINT argc, VARIANT *value)
{
    VARIANT *cell;
    int rc = array_element_ref(f, arr, argv, argc, &cell);
    if (rc != R_OK)
        return rc;
    VariantClear(cell);
    *cell = *value;
    VariantInit(value);
    return R_OK;
}

static int property_let(Frame *f, Instance *inst, Member *m, VARIANT *argv, UINT argc, VARIANT *value, BOOL set)
{
    Proc *p = set ? m->Set : m->Let;
    if (!p && !set && m->Set && (value->vt == VT_DISPATCH))
        p = m->Set;
    if (!p)
        return raise_error(f, E_VBS_ILLEGAL_ASSIGN, m->Name);
    VARIANT args[9];
    UINT n = argc + 1;
    if (n > 9)
        return raise_error(f, E_VBS_WRONG_ARGS, m->Name);
    for (UINT i = 0; i < argc; i++)
        args[i] = argv[i];
    args[argc] = *value;
    return call_proc(f->E, p, inst, args, n, nullptr, f);
}

/* assigns a value: a let when set is false, which stores the default property of an object. */
static int assign_to(Frame *f, Expr *target, VARIANT *value, BOOL set)
{
    Engine *e = f->E;
    if (target->Kind == X_NAME) {
        Target t;
        resolve_name(f, target, &t, TRUE);
        int rc = R_OK;
        switch (t.Kind) {
        case TG_CELL:
            if (t.ReadOnly)
                rc = raise_error(f, E_VBS_ILLEGAL_ASSIGN, target->u.Name.Name);
            else
                rc = assign_cell(f, t.Cell, value);
            break;
        case TG_MEMBER: {
            Member *m = t.Mem;
            if (m->Kind == M_PROPERTY)
                rc = property_let(f, t.Me, m, nullptr, 0, value, set);
            else
                rc = raise_error(f, E_VBS_ILLEGAL_ASSIGN, target->u.Name.Name);
            break;
        }
        case TG_ITEM:
            if (t.ItemName) {
                VARIANT args[1] = {*value};
                rc = obj_put(f, &t.Item, t.ItemName, nullptr, 0, args, set);
            } else
                rc = raise_error(f, E_VBS_ILLEGAL_ASSIGN, target->u.Name.Name);
            break;
        case TG_PROC:
        case TG_CLASS:
        case TG_BUILTIN:
        case TG_CONSTANT:
            rc = raise_error(f, E_VBS_ILLEGAL_ASSIGN, target->u.Name.Name);
            break;
        default:
            if (e->Explicit || (f->Unit && f->Unit->Explicit))
                rc = raise_error(f, E_VBS_UNDEFINED, target->u.Name.Name);
            else {
                /* an undeclared name assigned at run time becomes a variable of the script. */
                Sym *s = sym_add(e, target->u.Name.Name, SYM_VAR);
                s->Public = 1;
                target->Bound = B_GLOBAL;
                target->u.Name.Symbol = s;
                rc = assign_cell(f, &s->Value, value);
            }
            break;
        }
        target_release(&t);
        return rc;
    }
    if (target->Kind == X_MEMBER) {
        VARIANT obj;
        VariantInit(&obj);
        int rc = eval_object(f, target, &obj);
        if (rc != R_OK)
            return rc;
        ArgList a;
        rc = arglist_eval(f, target->u.Member.HasArguments ? &target->u.Member.Arguments : nullptr, nullptr, FALSE, &a);
        if (rc != R_OK) {
            VariantClear(&obj);
            return rc;
        }
        rc = obj_put(f, &obj, target->u.Member.Name, a.V, a.Count, value, set);
        if (rc != R_OK && f->E->Err.Number == E_VBS_NO_MEMBER && !f->E->Fault.Host) {
            WCHAR d[80];
            member_detail(target, d);
            raise_error(f, E_VBS_NO_MEMBER, d);
        }
        arglist_free(&a);
        VariantClear(&obj);
        return rc;
    }
    if (target->Kind == X_CALL) {
        Expr *base = target->u.Call.Target;
        VARIANT bv;
        VariantInit(&bv);
        Target t;
        memset(&t, 0, sizeof(t));
        VARIANT *cell = nullptr;
        if (base->Kind == X_NAME) {
            resolve_name(f, base, &t, TRUE);
            if (t.Kind == TG_CELL)
                cell = cell_target(t.Cell);
            else if (t.Kind == TG_MEMBER && t.Mem->Kind == M_PROPERTY) {
                ArgList a;
                int rc = arglist_eval(f, &target->u.Call.Arguments, nullptr, FALSE, &a);
                if (rc == R_OK) {
                    rc = property_let(f, t.Me, t.Mem, a.V, a.Count, value, set);
                    arglist_free(&a);
                }
                return rc;
            } else if (t.Kind == TG_NONE) {
                target_release(&t);
                return raise_error(f, E_VBS_TYPE_MISMATCH, base->u.Name.Name);
            }
        } else {
            int rc = eval(f, base, &bv);
            if (rc != R_OK)
                return rc;
            cell = cell_target(&bv);
        }
        int rc;
        ArgList a;
        rc = arglist_eval(f, &target->u.Call.Arguments, nullptr, FALSE, &a);
        if (rc == R_OK) {
            if (cell && (cell->vt & VT_ARRAY))
                rc = assign_array_element(f, cell, a.V, a.Count, value);
            else if (cell && (cell->vt == VT_DISPATCH || cell->vt == VT_UNKNOWN)) {
                VARIANT args[9];
                for (UINT i = 0; i < a.Count && i < 8; i++)
                    args[i] = a.V[i];
                args[a.Count] = *value;
                rc = obj_default(f, cell, set ? DISPATCH_PROPERTYPUTREF : DISPATCH_PROPERTYPUT, args, a.Count + 1, nullptr);
            } else
                rc = raise_error(f, E_VBS_TYPE_MISMATCH, base->Kind == X_NAME ? base->u.Name.Name : nullptr);
            arglist_free(&a);
        }
        target_release(&t);
        VariantClear(&bv);
        return rc;
    }
    return raise_error(f, E_VBS_ILLEGAL_ASSIGN, nullptr);
}

/* ------------------------------------------------------------------ statements */

static int eval_condition(Frame *f, Expr *x, BOOL *result)
{
    VARIANT v;
    int rc = rvalue(f, x, &v);
    if (rc != R_OK) {
        if (f->ResumeNext) {
            *result = TRUE;
            return R_OK;
        }
        return rc;
    }
    VARIANT *p = cell_target(&v);
    if (p->vt == VT_DISPATCH || p->vt == VT_UNKNOWN) {
        VariantClear(&v);
        return raise_error(f, E_VBS_NOT_SET, nullptr);
    }
    rc = to_bool(f, p, result);
    VariantClear(&v);
    if (rc != R_OK) {
        WCHAR d[80];
        describe_value(p, d);
        if (f->ResumeNext) {
            *result = TRUE;
            return R_OK;
        }
    }
    return rc;
}

static int exec_dim(Frame *f, Stmt *s)
{
    for (DimItem *d = s->u.Dim; d; d = d->Next) {
        if (!d->IsArray)
            continue;
        Expr nameexpr = {};
        nameexpr.Kind = X_NAME;
        nameexpr.u.Name.Name = d->Name;
        nameexpr.u.Name.Hash = name_hash(d->Name);
        Target t;
        resolve_name(f, &nameexpr, &t, TRUE);
        if (t.Kind != TG_CELL) {
            target_release(&t);
            continue;
        }
        VARIANT *cell = cell_target(t.Cell);
        if (d->Bounds.Count) {
            LONG upper[8];
            UINT n = d->Bounds.Count;
            if (n > 8)
                return raise_error(f, E_VBS_SUBSCRIPT, nullptr);
            for (UINT i = 0; i < n; i++) {
                VARIANT v;
                int rc = rvalue(f, d->Bounds.Items[i], &v);
                if (rc != R_OK)
                    return rc;
                rc = to_long(f, &v, &upper[i], L"Null");
                VariantClear(&v);
                if (rc != R_OK)
                    return rc;
                if (upper[i] < 0)
                    return raise_error(f, E_VBS_SUBSCRIPT, nullptr);
            }
            VariantClear(cell);
            cell->parray = array_new(n, upper, TRUE);
            cell->vt = VT_ARRAY | VT_VARIANT;
        } else {
            LONG upper[1] = {-1};
            VariantClear(cell);
            SAFEARRAYBOUND b = {0, 0};
            cell->parray = SafeArrayCreate(VT_VARIANT, 1, &b);
            cell->vt = VT_ARRAY | VT_VARIANT;
            (void)upper;
        }
        target_release(&t);
    }
    return R_OK;
}

static int exec_redim(Frame *f, Stmt *s)
{
    for (DimItem *d = s->u.Dim; d; d = d->Next) {
        Target t;
        resolve_name(f, d->Target, &t, TRUE);
        VARIANT *cell = nullptr;
        if (t.Kind == TG_CELL)
            cell = cell_target(t.Cell);
        else if (t.Kind == TG_NONE && !f->E->Explicit && !(f->Unit && f->Unit->Explicit)) {
            Sym *sym = sym_add(f->E, d->Name, SYM_VAR);
            sym->Public = 1;
            d->Target->Bound = B_GLOBAL;
            d->Target->u.Name.Symbol = sym;
            cell = &sym->Value;
        } else {
            target_release(&t);
            return raise_error(f, t.Kind == TG_NONE ? E_VBS_UNDEFINED : E_VBS_ILLEGAL_ASSIGN, d->Name);
        }
        LONG upper[8];
        UINT n = d->Bounds.Count;
        if (n > 8) {
            target_release(&t);
            return raise_error(f, E_VBS_SUBSCRIPT, nullptr);
        }
        for (UINT i = 0; i < n; i++) {
            VARIANT v;
            int rc = rvalue(f, d->Bounds.Items[i], &v);
            if (rc == R_OK)
                rc = to_long(f, &v, &upper[i], L"Null");
            VariantClear(&v);
            if (rc != R_OK) {
                target_release(&t);
                return rc;
            }
            if (upper[i] < 0) {
                target_release(&t);
                return raise_error(f, E_VBS_SUBSCRIPT, nullptr);
            }
        }
        SAFEARRAY *old = (cell->vt & VT_ARRAY) ? array_of(cell) : nullptr;
        if (old && (old->fFeatures & FADF_DYNAMIC_MARK)) {
            target_release(&t);
            return raise_error(f, E_VBS_LOCKED, nullptr);
        }
        SAFEARRAY *sa = array_new(n, upper, FALSE);
        if (!sa) {
            target_release(&t);
            return raise_error(f, E_VBS_OUT_OF_MEMORY, nullptr);
        }
        if (s->Flag && old) {
            if (old->cDims != n) {
                SafeArrayDestroy(sa);
                target_release(&t);
                return raise_error(f, E_VBS_SUBSCRIPT, nullptr);
            }
            for (UINT i = 0; i + 1 < n; i++) {
                LONG hi;
                SafeArrayGetUBound(old, i + 1, &hi);
                if (hi != upper[i]) {
                    SafeArrayDestroy(sa);
                    target_release(&t);
                    return raise_error(f, E_VBS_SUBSCRIPT, nullptr);
                }
            }
            /* copy what fits, element by element. */
            LONG idx[8];
            LONG total = 1;
            for (UINT i = 0; i < n; i++) {
                LONG hi;
                SafeArrayGetUBound(old, i + 1, &hi);
                total *= hi + 1;
            }
            for (LONG k = 0; k < total; k++) {
                LONG rem = k;
                BOOL fits = TRUE;
                for (UINT i = 0; i < n; i++) {
                    LONG hi;
                    SafeArrayGetUBound(old, i + 1, &hi);
                    idx[i] = rem % (hi + 1);
                    rem /= (hi + 1);
                    if (idx[i] > upper[i])
                        fits = FALSE;
                }
                if (!fits)
                    continue;
                VARIANT *from, *to;
                SafeArrayPtrOfIndex(old, idx, (void **)&from);
                SafeArrayPtrOfIndex(sa, idx, (void **)&to);
                *to = *from;
                VariantInit(from);
            }
        }
        VariantClear(cell);
        cell->vt = VT_ARRAY | VT_VARIANT;
        cell->parray = sa;
        target_release(&t);
    }
    return R_OK;
}

static int exec_erase(Frame *f, Stmt *s)
{
    Expr *x = s->u.Randomize;
    Target t;
    if (x->Kind != X_NAME)
        return raise_error(f, E_VBS_TYPE_MISMATCH, L"Erase");
    resolve_name(f, x, &t, FALSE);
    if (t.Kind != TG_CELL) {
        target_release(&t);
        return raise_error(f, E_VBS_TYPE_MISMATCH, L"Erase");
    }
    VARIANT *cell = cell_target(t.Cell);
    if (!(cell->vt & VT_ARRAY)) {
        target_release(&t);
        return raise_error(f, E_VBS_TYPE_MISMATCH, L"Erase");
    }
    SAFEARRAY *sa = array_of(cell);
    if (sa && (sa->fFeatures & FADF_DYNAMIC_MARK)) {
        /* a fixed array keeps its bounds and loses its values. */
        LONG total = 1;
        for (UINT i = 0; i < sa->cDims; i++)
            total *= (LONG)sa->rgsabound[i].cElements;
        VARIANT *p = sa->pvData;
        for (LONG i = 0; i < total; i++) {
            VariantClear(&p[i]);
        }
    } else {
        VariantClear(cell);
        SAFEARRAYBOUND b = {0, 0};
        cell->vt = VT_ARRAY | VT_VARIANT;
        cell->parray = SafeArrayCreate(VT_VARIANT, 1, &b);
    }
    target_release(&t);
    return R_OK;
}

static int exec_for(Frame *f, Stmt *s)
{
    VARIANT from, to, step;
    int rc = rvalue(f, s->u.For.From, &from);
    if (rc != R_OK)
        return rc;
    rc = rvalue(f, s->u.For.To, &to);
    if (rc != R_OK) {
        VariantClear(&from);
        return rc;
    }
    VariantInit(&step);
    if (s->u.For.Step) {
        rc = rvalue(f, s->u.For.Step, &step);
        if (rc != R_OK) {
            VariantClear(&from);
            VariantClear(&to);
            return rc;
        }
    } else {
        step.vt = VT_I2;
        step.iVal = 1;
    }
    VARIANT *vs[3] = {&from, &to, &step};
    for (int i = 0; i < 3; i++) {
        VARIANT *v = cell_target(vs[i]);
        if (v->vt == VT_BSTR) {
            VARIANT d;
            VariantInit(&d);
            HRESULT hr = VariantChangeTypeEx(&d, v, f->E->Lcid, 0, VT_R8);
            if (FAILED(hr)) {
                WCHAR dd[80];
                describe_value(v, dd);
                VariantClear(&from);
                VariantClear(&to);
                VariantClear(&step);
                return raise_error(f, E_VBS_TYPE_MISMATCH, dd);
            }
            VariantClear(v);
            *v = d;
        } else if (v->vt == VT_NULL) {
            VariantClear(&from);
            VariantClear(&to);
            VariantClear(&step);
            return raise_error(f, E_VBS_TYPE_MISMATCH, L"Null");
        }
    }
    VARIANT zero;
    VariantInit(&zero);
    zero.vt = VT_I2;
    int cmp = 0;
    HRESULT hr = var_compare(&step, &zero, f->E->Lcid, &cmp);
    BOOL down = SUCCEEDED(hr) && cmp < 0;
    VARIANT counter;
    VariantInit(&counter);
    VariantCopy(&counter, &from);
    rc = R_OK;
    for (;;) {
        hr = var_compare(&counter, &to, f->E->Lcid, &cmp);
        if (FAILED(hr)) {
            rc = raise_error(f, E_VBS_TYPE_MISMATCH, nullptr);
            break;
        }
        if (down ? cmp < 0 : cmp > 0)
            break;
        VARIANT copy;
        VariantInit(&copy);
        VariantCopy(&copy, &counter);
        rc = assign_to(f, s->u.For.Var, &copy, FALSE);
        VariantClear(&copy);
        if (rc != R_OK)
            break;
        rc = run_statements(f, s->u.For.Body);
        if (rc == R_EXIT_FOR) {
            rc = R_OK;
            break;
        }
        if (rc != R_OK)
            break;
        if (f->E->Interrupted) {
            rc = raise_error(f, 4096, nullptr);
            break;
        }
        VARIANT cur_var;
        VariantInit(&cur_var);
        /* the counter is the variable itself, so a change by the body counts. */
        Target t;
        resolve_name(f, s->u.For.Var, &t, TRUE);
        if (t.Kind == TG_CELL)
            var_deref(&cur_var, t.Cell);
        target_release(&t);
        VARIANT next;
        VariantInit(&next);
        hr = var_binary(OP_ADD, cell_target(&cur_var), &step, &next, f->E->Lcid);
        VariantClear(&cur_var);
        if (FAILED(hr)) {
            rc = raise_error(f, hr == DISP_E_OVERFLOW ? E_VBS_OVERFLOW : E_VBS_TYPE_MISMATCH, nullptr);
            break;
        }
        VariantClear(&counter);
        counter = next;
    }
    VariantClear(&from);
    VariantClear(&to);
    VariantClear(&step);
    VariantClear(&counter);
    return rc;
}

static int exec_foreach(Frame *f, Stmt *s)
{
    VARIANT group;
    int rc = eval(f, s->u.Each.Group, &group);
    if (rc != R_OK)
        return rc;
    VARIANT *g = cell_target(&group);
    BOOL exited = FALSE;
    if (g->vt & VT_ARRAY) {
        SAFEARRAY *sa = array_of(g);
        VARIANT copy;
        VariantInit(&copy);
        VariantCopy(&copy, g);
        sa = array_of(&copy);
        LONG total = 1;
        for (UINT i = 0; sa && i < sa->cDims; i++)
            total *= (LONG)sa->rgsabound[i].cElements;
        VARTYPE ev = 0;
        if (sa)
            SafeArrayGetVartype(sa, &ev);
        for (LONG k = 0; sa && k < total; k++) {
            VARIANT elem;
            VariantInit(&elem);
            if (ev == VT_VARIANT)
                VariantCopyInd(&elem, (VARIANT *)sa->pvData + k);
            else {
                LONG idx[8];
                LONG rem = k;
                for (UINT i = 0; i < sa->cDims && i < 8; i++) {
                    idx[i] = sa->rgsabound[i].lLbound + rem % (LONG)sa->rgsabound[i].cElements;
                    rem /= (LONG)sa->rgsabound[i].cElements;
                }
                VARIANT tmp;
                VariantInit(&tmp);
                if (ev == VT_BSTR) {
                    BSTR b = nullptr;
                    SafeArrayGetElement(sa, idx, &b);
                    tmp.vt = VT_BSTR;
                    tmp.bstrVal = b;
                } else if (ev == VT_DISPATCH || ev == VT_UNKNOWN) {
                    IUnknown *u = nullptr;
                    SafeArrayGetElement(sa, idx, &u);
                    tmp.vt = ev;
                    tmp.punkVal = u;
                } else {
                    tmp.vt = ev;
                    SafeArrayGetElement(sa, idx, &tmp.lVal);
                }
                elem = tmp;
            }
            rc = assign_to(f, s->u.Each.Var, &elem, TRUE);
            VariantClear(&elem);
            if (rc != R_OK)
                break;
            rc = run_statements(f, s->u.Each.Body);
            if (rc == R_EXIT_FOR) {
                rc = R_OK;
                exited = TRUE;
                break;
            }
            if (rc != R_OK)
                break;
        }
        if (rc == R_OK && !exited) {
            VARIANT none;
            VariantInit(&none);
            rc = assign_to(f, s->u.Each.Var, &none, FALSE);
        }
        VariantClear(&copy);
        VariantClear(&group);
        return rc;
    }
    if (g->vt != VT_DISPATCH && g->vt != VT_UNKNOWN) {
        VariantClear(&group);
        return raise_error(f, E_VBS_NOT_COLLECTION, nullptr);
    }
    IEnumVARIANT *en = nullptr;
    rc = obj_enum(f, g, &en);
    if (rc != R_OK) {
        VariantClear(&group);
        return rc;
    }
    for (;;) {
        VARIANT elem;
        ULONG got = 0;
        VariantInit(&elem);
        HRESULT hr = en->lpVtbl->Next(en, 1, &elem, &got);
        if (FAILED(hr) || got == 0)
            break;
        rc = assign_to(f, s->u.Each.Var, &elem, TRUE);
        VariantClear(&elem);
        if (rc != R_OK)
            break;
        rc = run_statements(f, s->u.Each.Body);
        if (rc == R_EXIT_FOR) {
            rc = R_OK;
            exited = TRUE;
            break;
        }
        if (rc != R_OK)
            break;
    }
    if (rc == R_OK && !exited) {
        VARIANT none;
        VariantInit(&none);
        rc = assign_to(f, s->u.Each.Var, &none, FALSE);
    }
    en->lpVtbl->Release(en);
    VariantClear(&group);
    return rc;
}

static int exec_select(Frame *f, Stmt *s)
{
    VARIANT subject;
    int rc = rvalue(f, s->u.Select.Subject, &subject);
    if (rc != R_OK)
        return rc;
    for (CaseClause *c = s->u.Select.Cases; c; c = c->Next) {
        BOOL match = c->IsElse;
        for (UINT i = 0; !match && i < c->Count; i++) {
            VARIANT v;
            rc = rvalue(f, c->Values[i], &v);
            if (rc != R_OK) {
                VariantClear(&subject);
                return rc;
            }
            int cmp;
            HRESULT hr = var_compare(cell_target(&subject), cell_target(&v), f->E->Lcid, &cmp);
            VariantClear(&v);
            if (FAILED(hr)) {
                VariantClear(&subject);
                return raise_error(f, E_VBS_TYPE_MISMATCH, nullptr);
            }
            match = cmp == 0;
        }
        if (match) {
            VariantClear(&subject);
            return run_statements(f, c->Body);
        }
    }
    VariantClear(&subject);
    return R_OK;
}

static int exec_with(Frame *f, Stmt *s)
{
    VARIANT obj;
    int rc = eval(f, s->u.With.Object, &obj);
    if (rc != R_OK)
        return rc;
    if (obj.vt == (VT_VARIANT | VT_BYREF)) {
        VARIANT t;
        var_deref(&t, &obj);
        VariantClear(&obj);
        obj = t;
    }
    if (obj.vt != VT_DISPATCH && obj.vt != VT_UNKNOWN) {
        VariantClear(&obj);
        return raise_error(f, E_VBS_OBJECT_REQUIRED, nullptr);
    }
    WithLink *w = vbs_alloc(sizeof(WithLink));
    w->Object = obj;
    w->Prev = f->With;
    f->With = w;
    rc = run_statements(f, s->u.With.Body);
    f->With = w->Prev;
    VariantClear(&w->Object);
    vbs_free(w);
    return rc;
}

static int exec_set(Frame *f, Stmt *s)
{
    VARIANT v;
    int rc = eval(f, s->u.Assign.Value, &v);
    if (rc != R_OK)
        return rc;
    if (v.vt == (VT_VARIANT | VT_BYREF)) {
        VARIANT t;
        var_deref(&t, &v);
        VariantClear(&v);
        v = t;
    }
    if (v.vt == VT_UNKNOWN && v.punkVal) {
        IDispatch *d = nullptr;
        v.punkVal->lpVtbl->QueryInterface(v.punkVal, &IID_IDispatch, (void **)&d);
        if (d) {
            VariantClear(&v);
            v.vt = VT_DISPATCH;
            v.pdispVal = d;
        }
    }
    if (v.vt != VT_DISPATCH && v.vt != VT_UNKNOWN) {
        WCHAR d[80];
        describe_expr(s->u.Assign.Value, &v, d);
        BOOL array = (v.vt & VT_ARRAY) != 0;
        VariantClear(&v);
        return raise_error(f, E_VBS_OBJECT_REQUIRED, array ? nullptr : d);
    }
    rc = assign_to(f, s->u.Assign.Target, &v, TRUE);
    VariantClear(&v);
    return rc;
}

static int exec_let(Frame *f, Stmt *s)
{
    VARIANT v;
    int rc = eval(f, s->u.Assign.Value, &v);
    if (rc != R_OK)
        return rc;
    if (v.vt == (VT_VARIANT | VT_BYREF)) {
        VARIANT t;
        var_deref(&t, &v);
        VariantClear(&v);
        v = t;
    }
    Expr *tg = s->u.Assign.Target;
    BOOL object_ok = FALSE;
    /* an object assigned to a member of an object is passed on as it is. */
    if (tg->Kind == X_MEMBER || (tg->Kind == X_CALL))
        object_ok = TRUE;
    if (!object_ok && (v.vt == VT_DISPATCH || v.vt == VT_UNKNOWN)) {
        rc = value_of_expr(f, &v, s->u.Assign.Value);
        if (rc != R_OK) {
            VariantClear(&v);
            return rc;
        }
    } else if (object_ok && (v.vt == VT_DISPATCH || v.vt == VT_UNKNOWN) && tg->Kind == X_CALL) {
        rc = value_of_expr(f, &v, s->u.Assign.Value);
        if (rc != R_OK) {
            VariantClear(&v);
            return rc;
        }
    }
    if ((v.vt & VT_ARRAY)) {
        VARIANT c;
        VariantInit(&c);
        VariantCopy(&c, &v);
        VariantClear(&v);
        v = c;
    }
    rc = assign_to(f, tg, &v, FALSE);
    VariantClear(&v);
    return rc;
}

static int exec_one(Frame *f, Stmt *s)
{
    Engine *e = f->E;
    switch (s->Kind) {
    case S_ASSIGN: return exec_let(f, s);
    case S_SETASSIGN: return exec_set(f, s);
    case S_CALL: {
        VARIANT r;
        int rc = eval_stmt_expr(f, s->u.Call, &r, TRUE);
        if (rc == R_OK)
            VariantClear(&r);
        return rc;
    }
    case S_IF: {
        BOOL c;
        int rc = eval_condition(f, s->u.If.Condition, &c);
        if (rc != R_OK)
            return rc;
        if (c)
            return run_statements(f, s->u.If.Then);
        if (s->u.If.Else)
            return run_statements(f, s->u.If.Else);
        return R_OK;
    }
    case S_SELECT: return exec_select(f, s);
    case S_FOR: return exec_for(f, s);
    case S_FOREACH: return exec_foreach(f, s);
    case S_DO:
    case S_WHILE: {
        for (;;) {
            BOOL c = TRUE;
            if (s->Kind == S_WHILE || (!s->Flag2 && s->u.Loop.Condition)) {
                int rc = eval_condition(f, s->u.Loop.Condition, &c);
                if (rc != R_OK)
                    return rc;
                if (s->Kind == S_DO && s->Flag)
                    c = !c;
                if (!c)
                    return R_OK;
            }
            int rc = run_statements(f, s->u.Loop.Body);
            if (rc == R_EXIT_DO)
                return R_OK;
            if (rc != R_OK)
                return rc;
            if (e->Interrupted)
                return raise_error(f, 4096, nullptr);
            if (s->Kind == S_DO && s->Flag2) {
                rc = eval_condition(f, s->u.Loop.Condition, &c);
                if (rc != R_OK)
                    return rc;
                if (s->Flag)
                    c = !c;
                if (!c)
                    return R_OK;
            }
        }
    }
    case S_WITH: return exec_with(f, s);
    case S_EXIT:
        switch (s->Flag) {
        case EXIT_FOR: return R_EXIT_FOR;
        case EXIT_DO: return R_EXIT_DO;
        case EXIT_SUB: return R_EXIT_SUB;
        case EXIT_FUNCTION: return R_EXIT_FUNCTION;
        default: return R_EXIT_PROPERTY;
        }
    case S_ONERROR:
        f->ResumeNext = s->Flag == 1;
        err_clear(e);
        return R_OK;
    case S_DIM: return exec_dim(f, s);
    case S_REDIM: return exec_redim(f, s);
    case S_ERASE: return exec_erase(f, s);
    case S_CONST:
    case S_NOP:
    case S_PROCDECL:
    case S_CLASSDECL:
    case S_OPTION:
        return R_OK;
    case S_RANDOMIZE: {
        VARIANT v;
        VariantInit(&v);
        if (s->u.Randomize) {
            int rc = rvalue(f, s->u.Randomize, &v);
            if (rc != R_OK)
                return rc;
        }
        VARIANT args[1] = {v};
        extern int vbs_randomize(Frame *, VARIANT *, UINT);
        int rc = vbs_randomize(f, args, s->u.Randomize ? 1 : 0);
        VariantClear(&v);
        return rc;
    }
    case S_EXECUTE: {
        if (!s->u.Execute.Source)
            return raise_error(f, E_VBS_WRONG_ARGS, nullptr);
        VARIANT v;
        int rc = rvalue(f, s->u.Execute.Source, &v);
        if (rc != R_OK)
            return rc;
        VARIANT r;
        VariantInit(&r);
        rc = builtin_eval(f, &v, &r, TRUE, s->u.Execute.Global);
        VariantClear(&v);
        VariantClear(&r);
        return rc;
    }
    }
    return R_OK;
}

int run_statements(Frame *f, Stmt *s)
{
    for (; s; s = s->Next) {
        f->Line = s->Line;
        f->Col = s->Col;
        int rc = exec_one(f, s);
        if (rc == R_ERR) {
            if (f->ResumeNext && !f->E->Interrupted)
                continue;
            return R_ERR;
        }
        if (rc != R_OK)
            return rc;
    }
    return R_OK;
}

int run_unit(Engine *e, Unit *unit, Frame *frame)
{
    frame->Unit = unit;
    return run_statements(frame, unit->Statements);
}

int eval_unit_expression(Engine *e, Expr *expr, Frame *frame, VARIANT *out)
{
    return rvalue(frame, expr, out);
}

/* ------------------------------------------------------------------ classes */

Instance *instance_create(Engine *e, Class *cls, Frame *frame)
{
    Instance *inst = vbs_alloc(sizeof(Instance));
    extern const IDispatchVtbl g_instance_vtbl;
    inst->Dispatch.lpVtbl = &g_instance_vtbl;
    inst->Refs = 1;
    inst->E = e;
    inst->Cls = cls;
    inst->Fields = vbs_alloc((cls->FieldCount + 1) * sizeof(VARIANT));
    for (UINT i = 0; i < cls->FieldCount; i++)
        VariantInit(&inst->Fields[i]);
    InterlockedIncrement(&e->Refs);
    InterlockedIncrement(&g_vbs_objects);
    /* a field declared as an array holds one. */
    if (cls->Initialize) {
        int rc = call_proc(e, cls->Initialize, inst, nullptr, 0, nullptr, frame);
        if (rc != R_OK) {
            inst->Terminated = TRUE;
            inst->Dispatch.lpVtbl->Release(&inst->Dispatch);
            return nullptr;
        }
    }
    return inst;
}

void instance_destroy(Instance *inst)
{
    Engine *e = inst->E;
    if (!inst->Terminated && inst->Cls->Terminate && !e->Closing) {
        inst->Terminated = TRUE;
        inst->Refs = 1;
        Frame g = {};
        g.E = e;
        g.Unit = inst->Cls->Unit;
        ErrState saved = e->Err;
        memset(&e->Err, 0, sizeof(e->Err));
        Fault sf = e->Fault;
        memset(&e->Fault, 0, sizeof(e->Fault));
        call_proc(e, inst->Cls->Terminate, inst, nullptr, 0, nullptr, &g);
        err_clear(e);
        e->Err = saved;
        fault_clear(e);
        e->Fault = sf;
        if (--inst->Refs > 0)
            return;
    }
    for (UINT i = 0; i < inst->Cls->FieldCount; i++)
        VariantClear(&inst->Fields[i]);
    vbs_free(inst->Fields);
    InterlockedDecrement(&g_vbs_objects);
    extern void engine_release(Engine *e);
    engine_release(e);
    vbs_free(inst);
}
