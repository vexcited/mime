/*
 * proxies and stubs built at run time from a type library. CreateProxyFromTypeInfo and CreateStubFromTypeInfo write
 * the interpreter format strings midl would write for the interface, and the standard factory builds on them. this is
 * what the oleautomation proxy class does for every interface of a type library.
 *
 * supported: numbers, enums, strings, BSTR, VARIANT, SAFEARRAY, interface pointers and records of these, by value or
 * by pointer. the oleaut32 wire routines for BSTR, VARIANT and SAFEARRAY are looked up on first use.
 */
#include "rpcrt4_private.h"
#include <mime/win32/oleauto.h>
#include <mime/win32/rpcproxy.h>
#include <mime/win32/rpcndr.h>

#define AutAlloc(size) HeapAlloc(GetProcessHeap(), 0, (size))
#define AutAllocZero(size) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size))
#define AutFree(p) HeapFree(GetProcessHeap(), 0, (p))

#ifdef _WIN64
#define NATIVE_X64 1
#else
#define NATIVE_X64 0
#endif

#define PSZ ((ULONG)sizeof(void *))

enum {
    FCx_BYTE = 0x01, FCx_SMALL = 0x03, FCx_USMALL = 0x04, FCx_SHORT = 0x06, FCx_USHORT = 0x07, FCx_LONG = 0x08,
    FCx_ULONG = 0x09, FCx_FLOAT = 0x0A, FCx_HYPER = 0x0B, FCx_DOUBLE = 0x0C, FCx_ENUM32 = 0x0E, FCx_RP = 0x11,
    FCx_UP = 0x12, FCx_STRUCT = 0x15, FCx_BOGUS_STRUCT = 0x1A, FCx_C_CSTRING = 0x22, FCx_C_WSTRING = 0x25,
    FCx_IP = 0x2F, FCx_AUTO_HANDLE = 0x33, FCx_STRUCTPAD1 = 0x3D, FCx_EMBEDDED_COMPLEX = 0x4C, FCx_CONSTANT_IID = 0x5A,
    FCx_END = 0x5B, FCx_PAD = 0x5C, FCx_USER_MARSHAL = 0xB4
};

#define PTRx_SIMPLE 0x08
#define PTRx_DEREF 0x10
#define PTRx_ON_STACK 0x04

#define PAx_MUST_SIZE 0x1
#define PAx_MUST_FREE 0x2
#define PAx_IS_IN 0x8
#define PAx_IS_OUT 0x10
#define PAx_IS_RETURN 0x20
#define PAx_IS_BASETYPE 0x40
#define PAx_IS_BY_VALUE 0x80
#define PAx_IS_SIMPLE_REF 0x100
#define PAx_ALLOC_SHIFT 13

#define MAX_METHODS 1024

/* indices into the stub descriptor's user marshal table. */
enum { USER_BSTR, USER_VARIANT, USER_SAFEARRAY, USER_COUNT };

/* ------------------------------------------------------------------ buffers and arena */

typedef struct {
    BYTE *Data;
    ULONG Size;
    ULONG Cap;
    BOOL Failed;
} BUF;

static void buf_put(BUF *b, unsigned v)
{
    if (b->Size == b->Cap) {
        ULONG cap = b->Cap ? b->Cap * 2 : 256;
        BYTE *data = b->Data ? HeapReAlloc(GetProcessHeap(), 0, b->Data, cap) : AutAlloc(cap);
        if (!data) {
            b->Failed = TRUE;
            return;
        }
        b->Data = data;
        b->Cap = cap;
    }
    b->Data[b->Size++] = (BYTE)v;
}

static void buf_put16(BUF *b, unsigned v)
{
    buf_put(b, v & 0xFF);
    buf_put(b, (v >> 8) & 0xFF);
}

static void buf_put32(BUF *b, ULONG v)
{
    buf_put16(b, v & 0xFFFF);
    buf_put16(b, v >> 16);
}

/* format strings reference items by a signed 16-bit offset from where the offset is written. */
static void buf_ref(BUF *b, ULONG target)
{
    LONG rel = (LONG)target - (LONG)b->Size;
    if (rel < -32768 || rel > 32767)
        b->Failed = TRUE;
    buf_put16(b, (unsigned)rel & 0xFFFF);
}

static ULONG align_up(ULONG v, ULONG a)
{
    return (v + a - 1) & ~(a - 1);
}

/* everything allocated while reading an interface, freed together when done. */
typedef struct BLOCK {
    struct BLOCK *Next;
} BLOCK;

typedef enum {
    T_BASE,
    T_ENUM,
    T_USER,
    T_IFACE,
    T_STRUCT,
    T_STRING,
    T_PTR,
    T_IFBODY
} KIND;

typedef struct TY TY;

typedef struct {
    TY *Type;
    ULONG Offset;
} FIELD;

struct TY {
    KIND Kind;
    BYTE Fc;
    ULONG Size;
    ULONG Align;
    /* wire size and alignment, when the size is fixed. */
    ULONG WSize;
    ULONG WAlign;
    int User;
    IID Iid;
    TY *Target;
    FIELD *Fields;
    ULONG FieldCount;
    BOOL Simple;
    ULONG Off;
    ULONG PtrOff[2];
};

typedef struct {
    IID Iid;
    ULONG Off;
} IP_OFF;

typedef struct {
    BLOCK *Blocks;
    BUF Types;
    BUF Procs;
    ULONG BaseOff[256];
    IP_OFF Ips[64];
    ULONG IpCount;
    ULONG UserBase;
    BOOL Failed;
} CTX;

static void *ctx_alloc(CTX *c, SIZE_T size)
{
    BLOCK *b = AutAllocZero(sizeof(BLOCK) + size);
    if (!b) {
        c->Failed = TRUE;
        return nullptr;
    }
    b->Next = c->Blocks;
    c->Blocks = b;
    return b + 1;
}

static void ctx_free(CTX *c)
{
    while (c->Blocks) {
        BLOCK *b = c->Blocks;
        c->Blocks = b->Next;
        AutFree(b);
    }
    AutFree(c->Types.Data);
    AutFree(c->Procs.Data);
}

static TY *ty_new(CTX *c, KIND kind)
{
    TY *t = ctx_alloc(c, sizeof(TY));
    if (t)
        t->Kind = kind;
    return t;
}

/* ------------------------------------------------------------------ types */

static TY *ty_base(CTX *c, BYTE fc, ULONG size, ULONG align)
{
    TY *t = ty_new(c, T_BASE);
    if (!t)
        return nullptr;
    t->Fc = fc;
    t->Size = t->WSize = size;
    t->Align = t->WAlign = align;
    return t;
}

static TY *ty_user(CTX *c, int user, ULONG size, ULONG align, ULONG walign)
{
    TY *t = ty_new(c, T_USER);
    if (!t)
        return nullptr;
    t->User = user;
    t->Size = size;
    t->Align = align;
    t->WAlign = walign;
    return t;
}

static TY *ty_pointer(CTX *c, KIND kind, TY *target)
{
    TY *t = ty_new(c, kind);
    if (!t)
        return nullptr;
    t->Target = target;
    t->Size = t->Align = PSZ;
    t->WAlign = 4;
    return t;
}

static TY *ty_iface(CTX *c, REFIID iid)
{
    TY *t = ty_pointer(c, T_IFACE, nullptr);
    if (t)
        t->Iid = *iid;
    return t;
}

/* a record of plain numbers, used for CY and DECIMAL. */
static TY *ty_record(CTX *c, ULONG size, ULONG align, ULONG fields, const BYTE *fcs, const ULONG *offsets, const ULONG *sizes)
{
    TY *t = ty_new(c, T_STRUCT);
    if (!t)
        return nullptr;
    t->Fields = ctx_alloc(c, sizeof(FIELD) * fields);
    if (!t->Fields)
        return nullptr;
    t->FieldCount = fields;
    t->Size = size;
    t->Align = align;
    t->WAlign = align;
    t->Simple = TRUE;
    for (ULONG i = 0; i < fields; i++) {
        t->Fields[i].Type = ty_base(c, fcs[i], sizes[i], sizes[i]);
        t->Fields[i].Offset = offsets[i];
    }
    return t;
}

static TY *ty_cy(CTX *c)
{
    static const BYTE fcs[2] = {FCx_ULONG, FCx_LONG};
    static const ULONG offsets[2] = {0, 4};
    static const ULONG sizes[2] = {4, 4};
    TY *t = ty_record(c, 8, 4, 2, fcs, offsets, sizes);
    if (t)
        t->WSize = 8;
    return t;
}

static TY *ty_decimal(CTX *c)
{
    static const BYTE fcs[5] = {FCx_USHORT, FCx_BYTE, FCx_BYTE, FCx_ULONG, FCx_HYPER};
    static const ULONG offsets[5] = {0, 2, 3, 4, 8};
    static const ULONG sizes[5] = {2, 1, 1, 4, 8};
    TY *t = ty_record(c, 16, 8, 5, fcs, offsets, sizes);
    if (t)
        t->WSize = 16;
    return t;
}

static HRESULT classify(CTX *c, ITypeInfo *ti, const TYPEDESC *td, int depth, TY **out);

static HRESULT classify_record(CTX *c, ITypeInfo *ti, const TYPEATTR *attr, TY **out)
{
    TY *t = ty_new(c, T_STRUCT);
    if (!t)
        return E_OUTOFMEMORY;
    t->Size = attr->cbSizeInstance;
    t->Align = attr->cbAlignment ? attr->cbAlignment : 1;
    t->Simple = TRUE;
    t->WAlign = 1;
    t->Fields = ctx_alloc(c, sizeof(FIELD) * (attr->cVars ? attr->cVars : 1));
    if (!t->Fields)
        return E_OUTOFMEMORY;
    t->FieldCount = attr->cVars;
    for (UINT i = 0; i < attr->cVars; i++) {
        VARDESC *vd;
        HRESULT hr = ti->lpVtbl->GetVarDesc(ti, i, &vd);
        if (FAILED(hr))
            return hr;
        TY *m = nullptr;
        if (vd->varkind == VAR_PERINSTANCE)
            hr = classify(c, ti, &vd->elemdescVar.tdesc, 0, &m);
        else
            hr = TYPE_E_UNSUPFORMAT;
        if (SUCCEEDED(hr) && m->Kind != T_BASE && m->Kind != T_ENUM && m->Kind != T_STRUCT && m->Kind != T_USER &&
            m->Kind != T_IFACE)
            hr = TYPE_E_UNSUPFORMAT;
        if (SUCCEEDED(hr)) {
            t->Fields[i].Type = m;
            t->Fields[i].Offset = vd->oInst;
            if (m->Kind != T_BASE && !(m->Kind == T_STRUCT && m->Simple))
                t->Simple = FALSE;
            if (m->WAlign > t->WAlign)
                t->WAlign = m->WAlign;
        }
        ti->lpVtbl->ReleaseVarDesc(ti, vd);
        if (FAILED(hr))
            return hr;
    }
    t->WSize = t->Simple ? t->Size : 0;
    *out = t;
    return S_OK;
}

static HRESULT classify_user(CTX *c, ITypeInfo *ti, HREFTYPE ref, int depth, TY **out)
{
    ITypeInfo *rti;
    TYPEATTR *attr;
    HRESULT hr = ti->lpVtbl->GetRefTypeInfo(ti, ref, &rti);
    if (FAILED(hr))
        return hr;
    hr = rti->lpVtbl->GetTypeAttr(rti, &attr);
    if (FAILED(hr)) {
        rti->lpVtbl->Release(rti);
        return hr;
    }
    switch (attr->typekind) {
    case TKIND_ALIAS:
        hr = classify(c, rti, &attr->tdescAlias, depth + 1, out);
        break;
    case TKIND_ENUM: {
        TY *t = ty_new(c, T_ENUM);
        if (!t) {
            hr = E_OUTOFMEMORY;
            break;
        }
        t->Fc = FCx_ENUM32;
        t->Size = t->WSize = 4;
        t->Align = t->WAlign = 4;
        *out = t;
        break;
    }
    case TKIND_RECORD:
        hr = classify_record(c, rti, attr, out);
        break;
    case TKIND_INTERFACE:
    case TKIND_DISPATCH: {
        TY *t = ty_new(c, T_IFBODY);
        if (!t) {
            hr = E_OUTOFMEMORY;
            break;
        }
        t->Iid = attr->guid;
        /* a dispinterface that is not dual is reached as IDispatch. */
        if (attr->typekind == TKIND_DISPATCH && !(attr->wTypeFlags & TYPEFLAG_FDUAL))
            t->Iid = IID_IDispatch;
        *out = t;
        break;
    }
    default:
        hr = TYPE_E_UNSUPFORMAT;
        break;
    }
    rti->lpVtbl->ReleaseTypeAttr(rti, attr);
    rti->lpVtbl->Release(rti);
    return hr;
}

static HRESULT classify(CTX *c, ITypeInfo *ti, const TYPEDESC *td, int depth, TY **out)
{
    TY *t = nullptr;
    *out = nullptr;
    if (depth > 16)
        return TYPE_E_UNSUPFORMAT;
    switch (td->vt) {
    case VT_I1: t = ty_base(c, FCx_SMALL, 1, 1); break;
    case VT_UI1: t = ty_base(c, FCx_USMALL, 1, 1); break;
    case VT_I2:
    case VT_BOOL: t = ty_base(c, FCx_SHORT, 2, 2); break;
    case VT_UI2: t = ty_base(c, FCx_USHORT, 2, 2); break;
    case VT_I4:
    case VT_INT:
    case VT_ERROR:
    case VT_HRESULT: t = ty_base(c, FCx_LONG, 4, 4); break;
    case VT_UI4:
    case VT_UINT: t = ty_base(c, FCx_ULONG, 4, 4); break;
    case VT_R4: t = ty_base(c, FCx_FLOAT, 4, 4); break;
    case VT_R8:
    case VT_DATE: t = ty_base(c, FCx_DOUBLE, 8, 8); break;
    case VT_I8:
    case VT_UI8: t = ty_base(c, FCx_HYPER, 8, 8); break;
    case VT_CY: t = ty_cy(c); break;
    case VT_DECIMAL: t = ty_decimal(c); break;
    case VT_BSTR: t = ty_user(c, USER_BSTR, PSZ, PSZ, 4); break;
    case VT_VARIANT: t = ty_user(c, USER_VARIANT, sizeof(VARIANT), 8, 8); break;
    case VT_UNKNOWN: t = ty_iface(c, &IID_IUnknown); break;
    case VT_DISPATCH: t = ty_iface(c, &IID_IDispatch); break;
    /* SAFEARRAY(type) is the pointer to the array, passed by value, and a pointer to that is SAFEARRAY(type)*. */
    case VT_SAFEARRAY: t = ty_user(c, USER_SAFEARRAY, PSZ, PSZ, 4); break;
    case VT_LPSTR:
    case VT_LPWSTR:
        t = ty_new(c, T_STRING);
        if (t) {
            t->Fc = td->vt == VT_LPSTR ? FCx_C_CSTRING : FCx_C_WSTRING;
            t->Size = t->Align = PSZ;
            t->WAlign = 4;
        }
        break;
    case VT_USERDEFINED: return classify_user(c, ti, td->hreftype, depth, out);
    case VT_PTR: {
        TY *target;
        HRESULT hr = classify(c, ti, td->lptdesc, depth + 1, &target);
        if (FAILED(hr))
            return hr;
        if (target->Kind == T_IFBODY) {
            t = ty_iface(c, &target->Iid);
        } else if (target->Kind == T_PTR) {
            return TYPE_E_UNSUPFORMAT;
        } else {
            t = ty_pointer(c, T_PTR, target);
        }
        break;
    }
    default: return TYPE_E_UNSUPFORMAT;
    }
    if (!t)
        return E_OUTOFMEMORY;
    *out = t;
    return S_OK;
}

/* ------------------------------------------------------------------ type format string */

static ULONG off_type(CTX *c, TY *t);

static ULONG off_base(CTX *c, BYTE fc)
{
    if (!c->BaseOff[fc]) {
        c->BaseOff[fc] = c->Types.Size;
        buf_put(&c->Types, fc);
        buf_put(&c->Types, FCx_PAD);
    }
    return c->BaseOff[fc];
}

static ULONG off_ip(CTX *c, REFIID iid)
{
    for (ULONG i = 0; i < c->IpCount; i++)
        if (IsEqualGUID(&c->Ips[i].Iid, iid))
            return c->Ips[i].Off;
    ULONG off = c->Types.Size;
    BUF *b = &c->Types;
    buf_put(b, FCx_IP);
    buf_put(b, FCx_CONSTANT_IID);
    buf_put32(b, iid->Data1);
    buf_put16(b, iid->Data2);
    buf_put16(b, iid->Data3);
    for (int i = 0; i < 8; i++)
        buf_put(b, iid->Data4[i]);
    if (c->IpCount < ARRAYSIZE(c->Ips)) {
        c->Ips[c->IpCount].Iid = *iid;
        c->Ips[c->IpCount].Off = off;
        c->IpCount++;
    }
    return off;
}

static ULONG amask(ULONG align)
{
    if (align < 1)
        align = 1;
    if (align > 8)
        align = 8;
    return align - 1;
}

static ULONG off_user(CTX *c, TY *t)
{
    if (t->Off)
        return t->Off;
    ULONG wire = off_base(c, FCx_ULONG);
    BUF *b = &c->Types;
    t->Off = b->Size;
    buf_put(b, FCx_USER_MARSHAL);
    /* all three wire types are unique pointers: the referent id goes first and the routines align. */
    buf_put(b, amask(4) | 0x80);
    buf_put16(b, (unsigned)t->User);
    buf_put16(b, t->Size);
    buf_put16(b, 0);
    buf_ref(b, wire);
    return t->Off;
}

static void pad_bytes(BUF *b, ULONG gap)
{
    while (gap > 0) {
        ULONG n = gap < 7 ? gap : 7;
        buf_put(b, FCx_STRUCTPAD1 + n - 1);
        gap -= n;
    }
}

/* members of a simple struct, nested structs flattened. */
static ULONG simple_members(BUF *b, const TY *t, ULONG base, ULONG pos)
{
    for (ULONG i = 0; i < t->FieldCount; i++) {
        const TY *m = t->Fields[i].Type;
        ULONG at = base + t->Fields[i].Offset;
        if (at > pos)
            pad_bytes(b, at - pos);
        if (m->Kind == T_BASE) {
            buf_put(b, m->Fc);
            pos = at + m->Size;
        } else {
            pos = simple_members(b, m, at, at);
        }
    }
    return pos;
}

static ULONG off_struct(CTX *c, TY *t)
{
    if (t->Off)
        return t->Off;
    BUF *b = &c->Types;
    if (t->Simple) {
        ULONG off = b->Size;
        buf_put(b, FCx_STRUCT);
        buf_put(b, amask(t->WAlign));
        buf_put16(b, t->Size);
        ULONG pos = simple_members(b, t, 0, 0);
        pad_bytes(b, t->Size - pos);
        buf_put(b, FCx_END);
        t->Off = off;
        return off;
    }
    /* non-numeric members need their own formats, written before the struct. */
    ULONG *refs = ctx_alloc(c, sizeof(ULONG) * (t->FieldCount + 1));
    if (!refs) {
        b->Failed = TRUE;
        return 0;
    }
    for (ULONG i = 0; i < t->FieldCount; i++)
        if (t->Fields[i].Type->Kind != T_BASE)
            refs[i] = off_type(c, t->Fields[i].Type);
    ULONG off = b->Size;
    buf_put(b, FCx_BOGUS_STRUCT);
    buf_put(b, amask(t->WAlign));
    buf_put16(b, t->Size);
    buf_put16(b, 0);
    buf_put16(b, 0);
    ULONG pos = 0;
    for (ULONG i = 0; i < t->FieldCount; i++) {
        TY *m = t->Fields[i].Type;
        ULONG at = t->Fields[i].Offset;
        ULONG gap = at > pos ? at - pos : 0;
        if (m->Kind == T_BASE || m->Kind == T_ENUM) {
            pad_bytes(b, gap);
            buf_put(b, m->Fc);
        } else {
            if (gap > 7)
                b->Failed = TRUE;
            buf_put(b, FCx_EMBEDDED_COMPLEX);
            buf_put(b, gap);
            buf_ref(b, refs[i]);
        }
        pos = at + m->Size;
    }
    pad_bytes(b, t->Size > pos ? t->Size - pos : 0);
    buf_put(b, FCx_END);
    t->Off = off;
    return off;
}

/* four bytes: pointer kind, attributes, then the target. */
static ULONG off_pointer(CTX *c, BYTE kind, TY *target, BOOL on_stack)
{
    BUF *b = &c->Types;
    if (target->Kind == T_BASE || target->Kind == T_ENUM || target->Kind == T_STRING) {
        ULONG off = b->Size;
        buf_put(b, kind);
        buf_put(b, PTRx_SIMPLE);
        buf_put(b, target->Fc);
        buf_put(b, FCx_PAD);
        return off;
    }
    ULONG child = off_type(c, target);
    ULONG off = b->Size;
    buf_put(b, kind);
    buf_put(b, (target->Kind == T_IFACE ? PTRx_DEREF : 0) | (on_stack ? PTRx_ON_STACK : 0));
    buf_ref(b, child);
    return off;
}

static ULONG off_type(CTX *c, TY *t)
{
    switch (t->Kind) {
    case T_BASE:
    case T_ENUM: return off_base(c, t->Fc);
    case T_USER: return off_user(c, t);
    case T_IFACE: return off_ip(c, &t->Iid);
    case T_STRUCT: return off_struct(c, t);
    case T_STRING:
        if (!t->Off)
            t->Off = off_pointer(c, FCx_UP, t, FALSE);
        return t->Off;
    case T_PTR:
        if (!t->PtrOff[0])
            t->PtrOff[0] = off_pointer(c, FCx_RP, t->Target, FALSE);
        return t->PtrOff[0];
    default:
        c->Failed = TRUE;
        return 0;
    }
}

/* ------------------------------------------------------------------ procedure format string */

typedef struct {
    TY *Type;
    BYTE Dirs;
    ULONG Attrs;
    BYTE Base;
    BOOL IsBase;
    ULONG Offset;
    BOOL Fixed;
    ULONG WSize;
    ULONG WAlign;
    ULONG Slot;
    ULONG Frame;
    BOOL Return;
} PARAM;

#define DIR_IN 1
#define DIR_OUT 2

/* the stack slot size of an argument in the callee's frame. */
static ULONG slot_of(const TY *t)
{
    if (NATIVE_X64)
        return 8;
    ULONG size = t->Size < 4 ? 4 : t->Size;
    if (t->Kind == T_USER)
        size = t->User == USER_VARIANT ? (ULONG)sizeof(VARIANT) : PSZ;
    return align_up(size, 4);
}

static HRESULT describe(CTX *c, PARAM *p)
{
    TY *t = p->Type;
    ULONG attrs = p->Return ? (PAx_IS_RETURN | PAx_IS_OUT)
                            : (((p->Dirs & DIR_IN) ? PAx_IS_IN : 0) | ((p->Dirs & DIR_OUT) ? PAx_IS_OUT : 0));
    BOOL out_only = !p->Return && p->Dirs == DIR_OUT;
    if (!p->Return && (p->Dirs & DIR_OUT) && t->Kind != T_PTR)
        return TYPE_E_UNSUPFORMAT;
    switch (t->Kind) {
    case T_BASE:
    case T_ENUM:
        p->Attrs = attrs | PAx_IS_BASETYPE;
        p->Base = t->Fc;
        p->IsBase = TRUE;
        p->Fixed = TRUE;
        p->WSize = t->WSize;
        p->WAlign = t->WAlign;
        return S_OK;
    case T_PTR: {
        TY *target = t->Target;
        if (p->Return)
            return TYPE_E_UNSUPFORMAT;
        if (target->Kind == T_BASE || target->Kind == T_ENUM) {
            p->Attrs = attrs | PAx_IS_BASETYPE | PAx_IS_SIMPLE_REF;
            if (out_only)
                p->Attrs |= 1 << PAx_ALLOC_SHIFT;
            p->Base = target->Fc;
            p->IsBase = TRUE;
            p->Fixed = TRUE;
            p->WSize = target->WSize;
            p->WAlign = target->WAlign;
            return S_OK;
        }
        if (target->Kind == T_STRUCT) {
            p->Attrs = attrs | PAx_IS_SIMPLE_REF | PAx_MUST_FREE;
            p->Offset = off_struct(c, target);
            if (out_only && target->Size <= 56)
                p->Attrs |= ((target->Size + 7) / 8) << PAx_ALLOC_SHIFT;
            p->Fixed = target->Simple;
            if (p->Fixed) {
                p->WSize = target->Size;
                p->WAlign = target->WAlign;
            }
            return S_OK;
        }
        if (target->Kind != T_USER && target->Kind != T_IFACE)
            return TYPE_E_UNSUPFORMAT;
        p->Attrs = attrs | PAx_MUST_FREE;
        if (out_only && target->Kind == T_IFACE)
            p->Attrs |= 1 << PAx_ALLOC_SHIFT;
        if (!t->PtrOff[out_only ? 1 : 0])
            t->PtrOff[out_only ? 1 : 0] = off_pointer(c, FCx_RP, target, out_only);
        p->Offset = t->PtrOff[out_only ? 1 : 0];
        return S_OK;
    }
    case T_IFACE:
        p->Attrs = attrs | PAx_MUST_FREE;
        p->Offset = off_ip(c, &t->Iid);
        return S_OK;
    case T_STRING:
        p->Attrs = attrs | PAx_MUST_FREE;
        p->Offset = off_type(c, t);
        return S_OK;
    case T_STRUCT:
        p->Attrs = attrs | PAx_MUST_FREE;
        p->Offset = off_struct(c, t);
        if (NATIVE_X64 && t->Size != 1 && t->Size != 2 && t->Size != 4 && t->Size != 8)
            p->Attrs |= PAx_IS_SIMPLE_REF;
        else
            p->Attrs |= PAx_IS_BY_VALUE;
        p->Fixed = t->Simple;
        if (p->Fixed) {
            p->WSize = t->Size;
            p->WAlign = t->WAlign;
        }
        return S_OK;
    case T_USER:
        p->Attrs = attrs | PAx_MUST_FREE;
        p->Offset = off_user(c, t);
        /* on x64 a value larger than a slot is passed by address. */
        p->Attrs |= NATIVE_X64 && t->Size > 8 ? PAx_IS_SIMPLE_REF : PAx_IS_BY_VALUE;
        return S_OK;
    default:
        return TYPE_E_UNSUPFORMAT;
    }
}

typedef struct {
    FUNCDESC *Desc;
    ITypeInfo *Owner;
} FUNC;

/* appends the method's procedure and returns its start offset. */
static HRESULT emit_proc(CTX *c, const FUNC *f, ULONG number, ULONG *start)
{
    const FUNCDESC *fd = f->Desc;
    PARAM params[64];
    ULONG count = 0;
    if (fd->cParamsOpt == -1 || fd->cParams > 60)
        return TYPE_E_UNSUPFORMAT;
    if (fd->callconv != CC_STDCALL && fd->callconv != CC_CDECL && fd->callconv != CC_MSCPASCAL && fd->callconv != CC_SYSCALL)
        return TYPE_E_UNSUPFORMAT;
    HRESULT hr;
    ULONG frame = PSZ;
    for (SHORT i = 0; i < fd->cParams; i++) {
        PARAM *p = &params[count];
        memset(p, 0, sizeof(*p));
        const ELEMDESC *e = &fd->lprgelemdescParam[i];
        USHORT flags = e->paramdesc.wParamFlags;
        hr = classify(c, f->Owner, &e->tdesc, 0, &p->Type);
        if (FAILED(hr))
            return hr;
        p->Dirs = (flags & PARAMFLAG_FOUT) ? ((flags & PARAMFLAG_FIN) ? DIR_IN | DIR_OUT : DIR_OUT) : DIR_IN;
        if ((flags & PARAMFLAG_FRETVAL) && !(flags & PARAMFLAG_FOUT))
            return TYPE_E_UNSUPFORMAT;
        p->Slot = slot_of(p->Type);
        p->Frame = frame;
        frame += p->Slot;
        count++;
    }
    BOOL has_return = FALSE;
    PARAM ret;
    memset(&ret, 0, sizeof(ret));
    const TYPEDESC *rt = &fd->elemdescFunc.tdesc;
    if (rt->vt != VT_VOID) {
        hr = classify(c, f->Owner, rt, 0, &ret.Type);
        if (FAILED(hr))
            return hr;
        ret.Return = TRUE;
        ret.Dirs = DIR_OUT;
        ret.Slot = slot_of(ret.Type);
        ret.Frame = frame;
        frame += ret.Slot;
        has_return = TRUE;
    }
    for (ULONG i = 0; i < count; i++) {
        hr = describe(c, &params[i]);
        if (FAILED(hr))
            return hr;
    }
    if (has_return) {
        if (ret.Type->Kind != T_BASE && ret.Type->Kind != T_ENUM)
            return TYPE_E_UNSUPFORMAT;
        hr = describe(c, &ret);
        if (FAILED(hr))
            return hr;
    }
    if (c->Failed || c->Types.Failed)
        return E_OUTOFMEMORY;

    BUF *b = &c->Procs;
    *start = b->Size;
    ULONG client = 0, server = 0;
    BOOL client_must = FALSE, server_must = FALSE;
    BUF body = {0};
    for (ULONG i = 0; i <= count; i++) {
        PARAM *p = i < count ? &params[i] : &ret;
        if (i == count && !has_return)
            break;
        ULONG attrs = p->Attrs;
        if (!p->Fixed && (attrs & (PAx_IS_IN | PAx_IS_OUT)))
            attrs |= PAx_MUST_SIZE;
        buf_put16(&body, attrs);
        buf_put16(&body, p->Frame);
        if (p->IsBase) {
            buf_put(&body, p->Base);
            buf_put(&body, FCx_PAD);
        } else {
            buf_put16(&body, p->Offset);
        }
        if ((attrs & PAx_IS_IN) && !p->Return) {
            if (p->Fixed)
                client = align_up(client, p->WAlign) + p->WSize;
            else
                client_must = TRUE;
        }
        if (attrs & PAx_IS_OUT) {
            if (p->Fixed)
                server = align_up(server, p->WAlign) + p->WSize;
            else
                server_must = TRUE;
        }
    }
    /* object procedure header: rpc flags, method number and frame size. */
    buf_put(b, FCx_AUTO_HANDLE);
    buf_put(b, 0x40 | 0x08 | 0x04);
    buf_put32(b, 0);
    buf_put16(b, number);
    buf_put16(b, frame);
    buf_put16(b, client);
    buf_put16(b, server);
    buf_put(b, 0x40 | (has_return ? 0x04 : 0) | (client_must ? 0x02 : 0) | (server_must ? 0x01 : 0));
    buf_put(b, count + (has_return ? 1 : 0));
    buf_put(b, NATIVE_X64 ? 10 : 8);
    buf_put(b, 0x01);
    buf_put16(b, 0);
    buf_put16(b, 0);
    buf_put16(b, 0);
    if (NATIVE_X64) {
        /* float register mask: slot 0 is this, so only three arguments fit in registers. */
        unsigned mask = 0;
        for (ULONG i = 0; i < count && i < 3; i++) {
            const TY *t = params[i].Type;
            if (t->Kind == T_BASE && t->Fc == FCx_FLOAT)
                mask |= 1u << (2 * (i + 1));
            else if (t->Kind == T_BASE && t->Fc == FCx_DOUBLE)
                mask |= 2u << (2 * (i + 1));
        }
        buf_put16(b, mask);
    }
    for (ULONG i = 0; i < body.Size; i++)
        buf_put(b, body.Data[i]);
    AutFree(body.Data);
    return b->Failed ? E_OUTOFMEMORY : S_OK;
}

/* ------------------------------------------------------------------ reading an interface */

/* methods of the interface and its bases, in vtable order. */
typedef struct {
    FUNC Funcs[MAX_METHODS];
    ULONG Count;
    ULONG First;
    BOOL Present[MAX_METHODS];
} METHODS;

static void methods_release(METHODS *m)
{
    for (ULONG i = 0; i < MAX_METHODS; i++)
        if (m->Present[i]) {
            m->Funcs[i].Owner->lpVtbl->ReleaseFuncDesc(m->Funcs[i].Owner, m->Funcs[i].Desc);
            m->Funcs[i].Owner->lpVtbl->Release(m->Funcs[i].Owner);
        }
}

static HRESULT collect(ITypeInfo *ti, METHODS *m, int depth)
{
    TYPEATTR *attr;
    HRESULT hr = ti->lpVtbl->GetTypeAttr(ti, &attr);
    if (FAILED(hr))
        return hr;
    if (depth > 16) {
        ti->lpVtbl->ReleaseTypeAttr(ti, attr);
        return TYPE_E_UNSUPFORMAT;
    }
    UINT funcs = attr->cFuncs;
    UINT impls = attr->cImplTypes;
    m->First = 3;
    if (IsEqualGUID(&attr->guid, &IID_IUnknown)) {
        funcs = 0;
        impls = 0;
    } else if (IsEqualGUID(&attr->guid, &IID_IDispatch)) {
        funcs = 0;
        impls = 0;
        m->First = 7;
    }
    ti->lpVtbl->ReleaseTypeAttr(ti, attr);
    if (impls) {
        HREFTYPE ref;
        ITypeInfo *base;
        hr = ti->lpVtbl->GetRefTypeOfImplType(ti, 0, &ref);
        if (SUCCEEDED(hr))
            hr = ti->lpVtbl->GetRefTypeInfo(ti, ref, &base);
        if (FAILED(hr))
            return hr;
        hr = collect(base, m, depth + 1);
        base->lpVtbl->Release(base);
        if (FAILED(hr))
            return hr;
    }
    for (UINT i = 0; i < funcs; i++) {
        FUNCDESC *fd;
        hr = ti->lpVtbl->GetFuncDesc(ti, i, &fd);
        if (FAILED(hr))
            return hr;
        ULONG slot = (ULONG)(fd->oVft / (SHORT)PSZ);
        if (fd->funckind != FUNC_PUREVIRTUAL && fd->funckind != FUNC_VIRTUAL) {
            ti->lpVtbl->ReleaseFuncDesc(ti, fd);
            return TYPE_E_UNSUPFORMAT;
        }
        if (slot < m->First || slot >= MAX_METHODS || m->Present[slot]) {
            ti->lpVtbl->ReleaseFuncDesc(ti, fd);
            return TYPE_E_UNSUPFORMAT;
        }
        m->Funcs[slot].Desc = fd;
        m->Funcs[slot].Owner = ti;
        ti->lpVtbl->AddRef(ti);
        m->Present[slot] = TRUE;
        if (slot + 1 > m->Count)
            m->Count = slot + 1;
    }
    if (m->Count < m->First)
        m->Count = m->First;
    return S_OK;
}

/* ------------------------------------------------------------------ one interface */

typedef struct UNIV {
    struct UNIV *Next;
    IID Iid;
    BYTE *Types;
    BYTE *Procs;
    unsigned short *Offsets;
    MIDL_STUB_DESC Desc;
    MIDL_STUBLESS_PROXY_INFO ProxyInfo;
    MIDL_SERVER_INFO ServerInfo;
    PRPC_STUB_FUNCTION *StubTable;
    CInterfaceProxyVtbl *ProxyVtbl;
    CInterfaceStubVtbl StubVtbl;
    PCInterfaceProxyVtblList ProxyList[1];
    PCInterfaceStubVtblList StubList[1];
    PCInterfaceName Names[1];
    const IID *Delegated[1];
    ProxyFileInfo File;
    const ProxyFileInfo *FileList[2];
    CStdPSFactoryBuffer Factory;
    IPSFactoryBuffer *Inner;
} UNIV;

/* ole32's task allocator, or the process heap without ole32. */
static void *(WINAPI *g_co_alloc)(SIZE_T);
static void(WINAPI *g_co_free)(void *);

static void *__RPC_API task_alloc(size_t size)
{
    return g_co_alloc ? g_co_alloc(size) : HeapAlloc(GetProcessHeap(), 0, size);
}

static void __RPC_API task_free(void *p)
{
    if (g_co_free)
        g_co_free(p);
    else
        HeapFree(GetProcessHeap(), 0, p);
}

static USER_MARSHAL_ROUTINE_QUADRUPLE g_quadruples[USER_COUNT];
static LONG g_quadruples_state;

static BOOL load_quadruples(void)
{
    static const char *const names[USER_COUNT][4] = {
        {"BSTR_UserSize", "BSTR_UserMarshal", "BSTR_UserUnmarshal", "BSTR_UserFree"},
        {"VARIANT_UserSize", "VARIANT_UserMarshal", "VARIANT_UserUnmarshal", "VARIANT_UserFree"},
        {"LPSAFEARRAY_UserSize", "LPSAFEARRAY_UserMarshal", "LPSAFEARRAY_UserUnmarshal", "LPSAFEARRAY_UserFree"},
    };
    if (g_quadruples_state != 2) {
        if (InterlockedCompareExchange(&g_quadruples_state, 1, 0) == 0) {
            HMODULE ole = LoadLibraryW(L"ole32.dll");
            if (ole) {
                g_co_alloc = (void *)GetProcAddress(ole, "CoTaskMemAlloc");
                g_co_free = (void *)GetProcAddress(ole, "CoTaskMemFree");
            }
            HMODULE m = LoadLibraryW(L"oleaut32.dll");
            BOOL ok = m != nullptr;
            for (int i = 0; ok && i < USER_COUNT; i++) {
                void *routines[4];
                for (int k = 0; k < 4; k++) {
                    routines[k] = (void *)GetProcAddress(m, names[i][k]);
                    ok = ok && routines[k] != nullptr;
                }
                if (ok) {
                    g_quadruples[i].pfnBufferSize = (USER_MARSHAL_SIZING_ROUTINE)routines[0];
                    g_quadruples[i].pfnMarshall = (USER_MARSHAL_MARSHALLING_ROUTINE)routines[1];
                    g_quadruples[i].pfnUnmarshall = (USER_MARSHAL_UNMARSHALLING_ROUTINE)routines[2];
                    g_quadruples[i].pfnFree = (USER_MARSHAL_FREEING_ROUTINE)routines[3];
                }
            }
            g_quadruples_state = ok ? 2 : 3;
        } else {
            while (g_quadruples_state == 1)
                Sleep(1);
        }
    }
    return g_quadruples_state == 2;
}

/* left empty so the runtime fills in the standard stub methods. */
static const CInterfaceStubVtbl g_stub_template;

/* a version 2 proxy vtable starts with the stubless info and the iid. */
typedef struct {
    const MIDL_STUBLESS_PROXY_INFO *Info;
    const IID *Iid;
    const void *Methods[1];
} PROXY_TABLE;

static HRESULT build(ITypeInfo *ti, REFIID iid, UNIV **out)
{
    if (!load_quadruples())
        return TYPE_E_CANTLOADLIBRARY;
    METHODS *methods = AutAllocZero(sizeof(METHODS));
    CTX c;
    memset(&c, 0, sizeof(c));
    if (!methods)
        return E_OUTOFMEMORY;
    HRESULT hr = collect(ti, methods, 0);
    if (SUCCEEDED(hr) && methods->Count < 4)
        hr = TYPE_E_UNSUPFORMAT;
    for (ULONG i = methods->First; SUCCEEDED(hr) && i < methods->Count; i++)
        if (!methods->Present[i])
            hr = TYPE_E_UNSUPFORMAT;
    UNIV *u = nullptr;
    unsigned short *offsets = nullptr;
    if (SUCCEEDED(hr)) {
        /* like midl, pad the start so no type sits at offset 0. */
        buf_put16(&c.Types, 0);
        buf_put16(&c.Procs, 0);
        offsets = AutAllocZero(sizeof(unsigned short) * methods->Count);
        u = AutAllocZero(sizeof(UNIV));
        if (!offsets || !u)
            hr = E_OUTOFMEMORY;
    }
    for (ULONG i = 0; SUCCEEDED(hr) && i < methods->Count; i++) {
        if (i < methods->First) {
            offsets[i] = 0xFFFF;
            continue;
        }
        ULONG start = 0;
        hr = emit_proc(&c, &methods->Funcs[i], i, &start);
        if (SUCCEEDED(hr) && start > 0xFFFF)
            hr = TYPE_E_UNSUPFORMAT;
        offsets[i] = (unsigned short)start;
    }
    if (SUCCEEDED(hr) && (c.Failed || c.Types.Failed || c.Procs.Failed))
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) {
        ULONG count = methods->Count;
        ULONG first = methods->First;
        u->Iid = *iid;
        u->Types = c.Types.Data;
        u->Procs = c.Procs.Data;
        c.Types.Data = c.Procs.Data = nullptr;
        u->Offsets = offsets;
        u->Desc.pfnAllocate = task_alloc;
        u->Desc.pfnFree = task_free;
        u->Desc.pFormatTypes = u->Types;
        u->Desc.fCheckBounds = 1;
        u->Desc.Version = 0x60001;
        u->Desc.MIDLVersion = 0x8000253;
        u->Desc.aUserMarshalQuadruple = g_quadruples;
        u->Desc.mFlags = 1;
        u->ProxyInfo.pStubDesc = &u->Desc;
        u->ProxyInfo.ProcFormatString = u->Procs;
        u->ProxyInfo.FormatStringOffset = u->Offsets;
        u->ServerInfo.pStubDesc = &u->Desc;
        u->ServerInfo.ProcString = u->Procs;
        u->ServerInfo.FmtStringOffset = u->Offsets;
        PROXY_TABLE *proxy = AutAllocZero(sizeof(PROXY_TABLE) + sizeof(void *) * count);
        u->StubTable = AutAllocZero(sizeof(PRPC_STUB_FUNCTION) * (count - 3));
        if (!proxy || !u->StubTable) {
            AutFree(proxy);
            AutFree(u->StubTable);
            hr = E_OUTOFMEMORY;
        } else {
            proxy->Info = &u->ProxyInfo;
            proxy->Iid = &u->Iid;
            const void **vtbl = proxy->Methods;
            vtbl[0] = (const void *)IUnknown_QueryInterface_Proxy;
            vtbl[1] = (const void *)IUnknown_AddRef_Proxy;
            vtbl[2] = (const void *)IUnknown_Release_Proxy;
            for (ULONG i = 3; i < first; i++)
                u->StubTable[i - 3] = NdrStubForwardingFunction;
            for (ULONG i = first; i < count; i++) {
                vtbl[i] = (const void *)(LONG_PTR)-1;
                u->StubTable[i - 3] = (PRPC_STUB_FUNCTION)(void *)NdrStubCall2;
            }
            u->ProxyVtbl = (CInterfaceProxyVtbl *)proxy;
            u->StubVtbl = g_stub_template;
            u->StubVtbl.header.piid = &u->Iid;
            u->StubVtbl.header.pServerInfo = &u->ServerInfo;
            u->StubVtbl.header.DispatchTableCount = count;
            u->StubVtbl.header.pDispatchTable = u->StubTable - 3;
            u->ProxyList[0] = (PCInterfaceProxyVtblList)u->ProxyVtbl;
            u->StubList[0] = (PCInterfaceStubVtblList)&u->StubVtbl;
            u->Names[0] = "universal";
            u->Delegated[0] = first == 7 ? &IID_IDispatch : nullptr;
            u->File.pProxyVtblList = u->ProxyList;
            u->File.pStubVtblList = u->StubList;
            u->File.pNamesArray = u->Names;
            u->File.pDelegatedIIDs = first == 7 ? u->Delegated : nullptr;
            u->File.TableSize = 1;
            u->File.TableVersion = 2;
            u->FileList[0] = &u->File;
            /* the clsid only matches itself: the file serves this one interface. */
            static const CLSID clsid = {0x00020424, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
            hr = NdrDllGetClassObject(&clsid, &IID_IPSFactoryBuffer, (void **)&u->Inner, u->FileList, &clsid, &u->Factory);
        }
    }
    ctx_free(&c);
    methods_release(methods);
    AutFree(methods);
    if (FAILED(hr)) {
        if (u) {
            AutFree(u->Types);
            AutFree(u->Procs);
            AutFree(u->StubTable);
            AutFree(u->ProxyVtbl);
            AutFree(u);
        }
        AutFree(offsets);
        return hr;
    }
    *out = u;
    return S_OK;
}

/* ------------------------------------------------------------------ cache */

static UNIV *g_univ;
static SRWLOCK g_univ_lock = SRWLOCK_INIT;

/* the vtable half of a dual interface, or the interface itself, with a reference. */
static HRESULT vtable_half(ITypeInfo *info, ITypeInfo **half)
{
    TYPEATTR *attr;
    HRESULT hr = info->lpVtbl->GetTypeAttr(info, &attr);
    if (FAILED(hr))
        return hr;
    if (attr->typekind == TKIND_INTERFACE) {
        info->lpVtbl->ReleaseTypeAttr(info, attr);
        info->lpVtbl->AddRef(info);
        *half = info;
        return S_OK;
    }
    BOOL dual = attr->typekind == TKIND_DISPATCH && (attr->wTypeFlags & TYPEFLAG_FDUAL);
    info->lpVtbl->ReleaseTypeAttr(info, attr);
    if (!dual)
        return E_NOINTERFACE;
    HREFTYPE ref;
    hr = info->lpVtbl->GetRefTypeOfImplType(info, (UINT)-1, &ref);
    if (SUCCEEDED(hr))
        hr = info->lpVtbl->GetRefTypeInfo(info, ref, half);
    return hr;
}

static HRESULT univ_get(ITypeInfo *info, REFIID iid, UNIV **out)
{
    AcquireSRWLockShared(&g_univ_lock);
    for (UNIV *u = g_univ; u; u = u->Next)
        if (IsEqualGUID(&u->Iid, iid)) {
            ReleaseSRWLockShared(&g_univ_lock);
            *out = u;
            return S_OK;
        }
    ReleaseSRWLockShared(&g_univ_lock);
    ITypeInfo *half;
    HRESULT hr = vtable_half(info, &half);
    if (FAILED(hr))
        return hr;
    TYPEATTR *attr;
    hr = half->lpVtbl->GetTypeAttr(half, &attr);
    if (SUCCEEDED(hr)) {
        BOOL same = IsEqualGUID(&attr->guid, iid);
        half->lpVtbl->ReleaseTypeAttr(half, attr);
        if (!same)
            hr = E_NOINTERFACE;
    }
    UNIV *u = nullptr;
    if (SUCCEEDED(hr))
        hr = build(half, iid, &u);
    half->lpVtbl->Release(half);
    if (FAILED(hr))
        return hr;
    AcquireSRWLockExclusive(&g_univ_lock);
    for (UNIV *other = g_univ; other; other = other->Next)
        if (IsEqualGUID(&other->Iid, iid)) {
            /* another thread won the race: ours is dropped, leaking its tables. */
            ReleaseSRWLockExclusive(&g_univ_lock);
            *out = other;
            return S_OK;
        }
    u->Next = g_univ;
    g_univ = u;
    ReleaseSRWLockExclusive(&g_univ_lock);
    *out = u;
    return S_OK;
}

RPCRTAPI HRESULT WINAPI CreateProxyFromTypeInfo(ITypeInfo *info, IUnknown *outer, REFIID riid, IRpcProxyBuffer **proxy,
                                                void **object)
{
    UNIV *u;
    if (!proxy || !object)
        return E_POINTER;
    *proxy = nullptr;
    *object = nullptr;
    if (!info || !riid)
        return E_INVALIDARG;
    HRESULT hr = univ_get(info, riid, &u);
    if (FAILED(hr))
        return hr;
    return u->Inner->lpVtbl->CreateProxy(u->Inner, outer, riid, proxy, object);
}

RPCRTAPI HRESULT WINAPI CreateStubFromTypeInfo(ITypeInfo *info, REFIID riid, IUnknown *server, IRpcStubBuffer **stub)
{
    UNIV *u;
    if (!stub)
        return E_POINTER;
    *stub = nullptr;
    if (!info || !riid)
        return E_INVALIDARG;
    HRESULT hr = univ_get(info, riid, &u);
    if (FAILED(hr))
        return hr;
    return u->Inner->lpVtbl->CreateStub(u->Inner, riid, server, stub);
}
