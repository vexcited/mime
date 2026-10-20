/*
 * the wire forms of strings, variants and safe arrays. An interface of automation passes them to a proxy and a stub as
 * user marshaled types: a pair of routines to size and to write one, one to read it back, one to free what reading made.
 * The same forms, with another alignment and 8 byte values where a pointer stood, are used by the second transfer syntax.
 *
 * Sizing and writing are one piece of code that is run twice, so that the two cannot disagree: a cursor either counts or
 * writes. Reading checks every step against the end of the message.
 */
#include "oleaut32_private.h"
#include <mime/win32/rpc.h>
#include <mime/win32/rpcndr.h>

#define USER_MARKER 0x72657355 /* "User" */
#define RPC_BAD_STUB_DATA 1783
#define RPC_NULL_REF 1780
#define RPC_WRONG_STUB 1733
#define ARITH_OVERFLOW ((RPC_STATUS)0x80010010)
#define SF_HAVEIID 0x800D
#define SF_RECORD 36

#define FADF_OWNERSHIP (FADF_AUTO | FADF_STATIC | FADF_EMBEDDED)

static void raise(LONG status)
{
    RpcRaiseException((RPC_STATUS)status);
}

static ULONG add_size(ULONG a, ULONG b)
{
    ULONG sum = a + b;
    if (sum < a)
        raise(ARITH_OVERFLOW);
    return sum;
}

/* ------------------------------------------------------------------ the cursor that counts or writes */

typedef struct {
    /* null while counting, else the start of the buffer. */
    BYTE *Base;
    ULONG Off;
    ULONG *Flags;
    BOOL W64;
} WCUR;

static ULONG pad_of(const WCUR *o, ULONG mask)
{
    ULONG_PTR where = o->Base ? (ULONG_PTR)(o->Base + o->Off) : (ULONG_PTR)o->Off;
    return (ULONG)((0 - where) & mask);
}

static void out_align(WCUR *o, ULONG mask)
{
    ULONG gap = pad_of(o, mask);
    if (o->Base)
        memset(o->Base + o->Off, 0, gap);
    o->Off = add_size(o->Off, gap);
}

/* the place of the next n bytes, or null when counting. */
static BYTE *out_take(WCUR *o, ULONG n)
{
    BYTE *p = o->Base ? o->Base + o->Off : nullptr;
    o->Off = add_size(o->Off, n);
    return p;
}

static void put16(BYTE *p, USHORT v)
{
    if (p)
        memcpy(p, &v, 2);
}

static void put32(BYTE *p, ULONG v)
{
    if (p)
        memcpy(p, &v, 4);
}

static void put64(BYTE *p, ULONGLONG v)
{
    if (p)
        memcpy(p, &v, 8);
}

/* what stands for a pointer on the wire: its value, or marker when it is there and 0 when it is not. */
static void out_referent(WCUR *o, const void *pointer)
{
    if (o->W64) {
        out_align(o, 7);
        put64(out_take(o, 8), (ULONGLONG)(ULONG_PTR)pointer);
    } else {
        out_align(o, 3);
        put32(out_take(o, 4), pointer ? ((ULONG)(ULONG_PTR)pointer | 1) : 0);
    }
}

static void out_marker(WCUR *o)
{
    if (o->W64) {
        out_align(o, 7);
        put64(out_take(o, 8), USER_MARKER);
    } else {
        out_align(o, 3);
        put32(out_take(o, 4), USER_MARKER);
    }
}

/* ------------------------------------------------------------------ the cursor that reads */

typedef struct {
    const BYTE *Cur;
    const BYTE *Start;
    const BYTE *End;
    ULONG *Flags;
    BOOL W64;
} RD;

static void rd_open(RD *r, ULONG *flags, const BYTE *position, BOOL w64)
{
    NDR_USER_MARSHAL_INFO info;
    memset(&info, 0, sizeof(info));
    RPC_STATUS status = NdrGetUserMarshalInfo(flags, 1, &info);
    if (status)
        raise(status);
    r->Cur = position;
    r->Flags = flags;
    r->W64 = w64;
    r->Start = info.Level1.Buffer;
    r->End = info.Level1.BufferSize ? (const BYTE *)info.Level1.Buffer + info.Level1.BufferSize : nullptr;
    if (r->End && (position < r->Start || position > r->End))
        raise(1784);
}

static SIZE_T rd_left(const RD *r)
{
    if (!r->End)
        return (SIZE_T)0x7FFFFFFF;
    return r->Cur <= r->End ? (SIZE_T)(r->End - r->Cur) : 0;
}

static void rd_align(RD *r, ULONG mask)
{
    r->Cur = (const BYTE *)(((ULONG_PTR)r->Cur + mask) & ~(ULONG_PTR)mask);
    if (r->End && r->Cur > r->End)
        raise(RPC_BAD_STUB_DATA);
}

static const BYTE *rd_take(RD *r, SIZE_T n)
{
    if (rd_left(r) < n)
        raise(RPC_BAD_STUB_DATA);
    const BYTE *p = r->Cur;
    r->Cur += n;
    return p;
}

static ULONG rd32(RD *r)
{
    ULONG v;
    memcpy(&v, rd_take(r, 4), 4);
    return v;
}

static USHORT rd16(RD *r)
{
    USHORT v;
    memcpy(&v, rd_take(r, 2), 2);
    return v;
}

/* the value that stands for a pointer: whether it is there. */
static BOOL rd_referent(RD *r)
{
    if (r->W64) {
        rd_align(r, 7);
        ULONGLONG v;
        memcpy(&v, rd_take(r, 8), 8);
        return v != 0;
    }
    rd_align(r, 3);
    return rd32(r) != 0;
}

static void rd_marker(RD *r)
{
    if (r->W64) {
        rd_align(r, 7);
        rd_take(r, 8);
    } else {
        rd_align(r, 3);
        rd_take(r, 4);
    }
}

/* ------------------------------------------------------------------ strings */

static void bstr_emit(WCUR *o, BSTR *pb)
{
    BSTR s = *pb;
    ULONG bytes = s ? SysStringByteLen(s) : 0;
    ULONG len = add_size(bytes, 1) >> 1;
    out_align(o, o->W64 ? 7 : 3);
    BYTE *p = out_take(o, o->W64 ? 16 : 12);
    if (p) {
        if (o->W64) {
            put64(p, len);
            put32(p + 8, s ? bytes : 0xFFFFFFFF);
            put32(p + 12, len);
        } else {
            put32(p, len);
            put32(p + 4, s ? bytes : 0xFFFFFFFF);
            put32(p + 8, len);
        }
    }
    if (len > 0x7FFFFFFF)
        raise(ARITH_OVERFLOW);
    BYTE *data = out_take(o, len * 2);
    if (data && s)
        memcpy(data, s, len * 2);
}

static void bstr_read(RD *r, BSTR *pb)
{
    rd_align(r, r->W64 ? 7 : 3);
    ULONG conformance = 0, flags_word, len;
    if (r->W64) {
        const BYTE *p = rd_take(r, 16);
        memcpy(&flags_word, p + 8, 4);
        memcpy(&len, p + 12, 4);
    } else {
        const BYTE *p = rd_take(r, 12);
        memcpy(&conformance, p, 4);
        memcpy(&flags_word, p + 4, 4);
        memcpy(&len, p + 8, 4);
        if (conformance != len)
            raise(RPC_BAD_STUB_DATA);
    }
    if (len > 0x7FFFFFFF)
        raise(ARITH_OVERFLOW);
    if (flags_word == 0xFFFFFFFF) {
        /* a null string; the count that follows its marker is of nothing. */
        rd_take(r, (SIZE_T)len * 2);
        SysFreeString(*pb);
        *pb = nullptr;
        return;
    }
    if (flags_word != len * 2 && flags_word + 1 != len * 2)
        raise(RPC_BAD_STUB_DATA);
    const BYTE *data = rd_take(r, (SIZE_T)len * 2);
    BSTR fresh = SysAllocStringByteLen((LPCSTR)data, flags_word);
    if (!fresh)
        raise(E_OUTOFMEMORY);
    SysFreeString(*pb);
    *pb = fresh;
}

static ULONG user_size_via(ULONG *flags, ULONG start, BOOL w64, void (*emit)(WCUR *, void *), void *value)
{
    WCUR o = {nullptr, start, flags, w64};
    emit(&o, value);
    return o.Off;
}

static BYTE *user_marshal_via(ULONG *flags, BYTE *buffer, BOOL w64, void (*emit)(WCUR *, void *), void *value)
{
    WCUR o = {buffer, 0, flags, w64};
    emit(&o, value);
    return buffer + o.Off;
}

static void bstr_emit_thunk(WCUR *o, void *value)
{
    bstr_emit(o, value);
}

WINOLEAUTAPI ULONG WINAPI BSTR_UserSize(ULONG *flags, ULONG start, BSTR *value)
{
    return value ? user_size_via(flags, start, FALSE, bstr_emit_thunk, value) : start;
}

WINOLEAUTAPI BYTE *WINAPI BSTR_UserMarshal(ULONG *flags, BYTE *buffer, BSTR *value)
{
    return value ? user_marshal_via(flags, buffer, FALSE, bstr_emit_thunk, value) : buffer;
}

WINOLEAUTAPI BYTE *WINAPI BSTR_UserUnmarshal(ULONG *flags, BYTE *buffer, BSTR *value)
{
    RD r;
    rd_open(&r, flags, buffer, FALSE);
    bstr_read(&r, value);
    return (BYTE *)r.Cur;
}

WINOLEAUTAPI void WINAPI BSTR_UserFree(ULONG *flags, BSTR *value)
{
    if (value && *value) {
        SysFreeString(*value);
        *value = nullptr;
    }
}

WINOLEAUTAPI ULONG WINAPI BSTR_UserSize64(ULONG *flags, ULONG start, BSTR *value)
{
    return value ? user_size_via(flags, start, TRUE, bstr_emit_thunk, value) : start;
}

WINOLEAUTAPI BYTE *WINAPI BSTR_UserMarshal64(ULONG *flags, BYTE *buffer, BSTR *value)
{
    return value ? user_marshal_via(flags, buffer, TRUE, bstr_emit_thunk, value) : buffer;
}

WINOLEAUTAPI BYTE *WINAPI BSTR_UserUnmarshal64(ULONG *flags, BYTE *buffer, BSTR *value)
{
    RD r;
    rd_open(&r, flags, buffer, TRUE);
    bstr_read(&r, value);
    return (BYTE *)r.Cur;
}

WINOLEAUTAPI void WINAPI BSTR_UserFree64(ULONG *flags, BSTR *value)
{
    BSTR_UserFree(flags, value);
}

/* ------------------------------------------------------------------ interfaces */

static void iface_emit(WCUR *o, IUnknown *punk, REFIID iid)
{
    ULONG context = *o->Flags & 0xFF;
    if (!o->Base) {
        o->Off = o->W64 ? WdtpInterfacePointer_UserSize64(o->Flags, context, o->Off, punk, iid)
                        : WdtpInterfacePointer_UserSize(o->Flags, context, o->Off, punk, iid);
    } else {
        BYTE *end = o->W64 ? WdtpInterfacePointer_UserMarshal64(o->Flags, context, o->Base + o->Off, punk, iid)
                           : WdtpInterfacePointer_UserMarshal(o->Flags, context, o->Base + o->Off, punk, iid);
        o->Off = (ULONG)(end - o->Base);
    }
}

static void iface_read(RD *r, IUnknown **pp, REFIID iid)
{
    BYTE *end = r->W64 ? WdtpInterfacePointer_UserUnmarshal64(r->Flags, (BYTE *)r->Cur, pp, iid)
                       : WdtpInterfacePointer_UserUnmarshal(r->Flags, (BYTE *)r->Cur, pp, iid);
    r->Cur = end;
}

/* the kind an array has on the wire, taken from its descriptor: records first, then the size of an element. */
static ULONG discr_of(const SAFEARRAY *sa)
{
    if (sa->fFeatures & FADF_RECORD)
        return SF_RECORD;
    /* an element the size of a pointer is told from a plain integer by what the array holds. */
    if (sa->cbElements == sizeof(void *)) {
        switch (sa->fFeatures & (FADF_BSTR | FADF_UNKNOWN | FADF_DISPATCH | FADF_HAVEIID | FADF_VARIANT)) {
        case FADF_BSTR:
            return VT_BSTR;
        case FADF_UNKNOWN:
            return VT_UNKNOWN;
        case FADF_DISPATCH:
            return VT_DISPATCH;
        case FADF_UNKNOWN | FADF_HAVEIID:
        case FADF_DISPATCH | FADF_HAVEIID:
            return SF_HAVEIID;
        default:
            break;
        }
    }
    switch (sa->cbElements) {
    case 1:
        return VT_I1;
    case 2:
        return VT_I2;
    case 4:
        return VT_I4;
    case 8:
        return VT_I8;
    default:
        break;
    }
    if (sa->cbElements == sizeof(VARIANT) && (sa->fFeatures & 0xF00) == FADF_VARIANT)
        return VT_VARIANT;
    raise(E_INVALIDARG);
    return 0;
}

/* the elements of the array, as many as its bounds make; an overflow means nothing was allocated that way. */
static ULONG count_of(const SAFEARRAY *sa)
{
    ULONGLONG count = 1;
    for (USHORT i = 0; i < sa->cDims; i++) {
        count *= sa->rgsabound[i].cElements;
        if (count > 0x7FFFFFFF)
            raise(ARITH_OVERFLOW);
    }
    return (ULONG)count;
}

static void variant_emit(WCUR *o, const VARIANT *v);
static void variant_read(RD *r, VARIANT *v);
static void safearray_read(RD *r, SAFEARRAY **pp, const GUID *iid_hint);

static void safearray_emit_body(WCUR *o, SAFEARRAY *sa, const GUID *iid_hint, ULONG lock_count)
{
    BOOL w64 = o->W64;
    ULONG discr = discr_of(sa);
    ULONG count = count_of(sa);
    ULONG elements = discr == SF_RECORD ? 1 : count;
    USHORT features = sa->fFeatures;
    ULONG locks = (USHORT)lock_count;
    if (features & FADF_HAVEVARTYPE)
        locks |= (ULONG)(USHORT)((ULONG *)sa)[-1] << 16;
    ULONG cb = sa->cbElements;
    if (discr == VT_BSTR || discr == VT_DISPATCH || discr == VT_UNKNOWN || discr == SF_HAVEIID)
        cb = 4;
    else if (discr == VT_VARIANT)
        cb = 16;

    if (w64) {
        BYTE *p;
        out_align(o, 7);
        put64(out_take(o, 8), sa->cDims);
        p = out_take(o, 12);
        put16(p, sa->cDims);
        put16(p ? p + 2 : nullptr, features);
        put32(p ? p + 4 : nullptr, cb);
        put32(p ? p + 8 : nullptr, locks);
        out_align(o, 7);
        put32(out_take(o, 4), discr);
        out_align(o, 7);
        put64(out_take(o, 8), elements);
        put64(out_take(o, 8), (ULONGLONG)(ULONG_PTR)sa->pvData);
    } else {
        BYTE *p = out_take(o, 28);
        put32(p, sa->cDims);
        put16(p ? p + 4 : nullptr, sa->cDims);
        put16(p ? p + 6 : nullptr, features);
        put32(p ? p + 8 : nullptr, cb);
        put32(p ? p + 12 : nullptr, locks);
        put32(p ? p + 16 : nullptr, discr);
        put32(p ? p + 20 : nullptr, elements);
        put32(p ? p + 24 : nullptr, (ULONG)(ULONG_PTR)sa->pvData);
    }
    GUID iid = {0};
    if (discr == SF_HAVEIID) {
        if (FAILED(SafeArrayGetIID(sa, &iid)))
            raise(E_INVALIDARG);
        BYTE *g = out_take(o, 16);
        if (g)
            memcpy(g, &iid, 16);
        iid_hint = &iid;
        discr = VT_UNKNOWN;
    }
    /* the bounds in the order of the dimensions of the array, the first dimension first. */
    for (int d = sa->cDims - 1; d >= 0; d--) {
        BYTE *b = out_take(o, 8);
        put32(b, sa->rgsabound[d].cElements);
        put32(b ? b + 4 : nullptr, (ULONG)sa->rgsabound[d].lLbound);
    }
    if (w64)
        put64(out_take(o, 8), elements);
    else
        put32(out_take(o, 4), elements);

    BYTE *data = sa->pvData;
    if (discr == VT_I1 || discr == VT_I2 || discr == VT_I4 || discr == VT_I8) {
        if (discr == VT_I8)
            out_align(o, 7);
        ULONGLONG bytes = (ULONGLONG)sa->cbElements * count;
        if (bytes > 0xFFFFFFFF)
            raise(ARITH_OVERFLOW);
        BYTE *dst = out_take(o, (ULONG)bytes);
        if (dst && bytes)
            memcpy(dst, data, (SIZE_T)bytes);
        return;
    }
    if (discr == SF_RECORD)
        raise(E_NOTIMPL);
    /* the other kinds: a pointer for each element, then what the elements that are there hold. */
    for (ULONG i = 0; i < count; i++) {
        if (discr == VT_VARIANT)
            out_marker(o);
        else
            out_referent(o, ((void **)data)[i]);
    }
    for (ULONG i = 0; i < count; i++) {
        switch (discr) {
        case VT_BSTR:
            if (((BSTR *)data)[i])
                bstr_emit(o, &((BSTR *)data)[i]);
            break;
        case VT_VARIANT:
            variant_emit(o, &((VARIANT *)data)[i]);
            break;
        default: {
            IUnknown *punk = ((IUnknown **)data)[i];
            if (punk)
                iface_emit(o, punk, iid_hint ? iid_hint : discr == VT_DISPATCH ? &IID_IDispatch : &IID_IUnknown);
            break;
        }
        }
    }
}

static void safearray_write(WCUR *o, SAFEARRAY **pp, const GUID *iid_hint)
{
    SAFEARRAY *sa = *pp;
    BOOL w64 = o->W64;
    out_align(o, w64 ? 7 : 3);
    if (w64)
        put64(out_take(o, 8), sa != nullptr);
    else
        put32(out_take(o, 4), sa != nullptr);
    if (!sa)
        return;
    /* the array is held while it is read, and the locks it is sent with are those before that. */
    ULONG locks = sa->cLocks;
    HRESULT hr = SafeArrayLock(sa);
    if (FAILED(hr))
        raise(E_UNEXPECTED);
    safearray_emit_body(o, sa, iid_hint, locks);
    SafeArrayUnlock(sa);
}

/* ------------------------------------------------------------------ the variant */

static void variant_emit(WCUR *o, const VARIANT *v)
{
    BOOL w64 = o->W64;
    out_align(o, 7);
    ULONG start = o->Off;
    BYTE *h = out_take(o, 20);
    VARTYPE vt = v->vt;
    VARTYPE sw = (vt & VT_ARRAY) ? (vt & (VT_ARRAY | VT_BYREF)) : vt;
    if (h) {
        put32(h + 4, 0);
        put16(h + 8, vt);
        put16(h + 10, v->wReserved1);
        put16(h + 12, v->wReserved2);
        put16(h + 14, v->wReserved3);
        put32(h + 16, sw);
    }
    if (w64)
        out_align(o, 7);
    BOOL byref = (sw & VT_BYREF) != 0;
    VARTYPE base = sw & ~VT_BYREF;
    void *data;
    BYTE *hint = nullptr;
    ULONG native_hint = w64 ? 8 : 4;
    if (byref) {
        data = v->byref;
        if (!data)
            raise(RPC_NULL_REF);
        hint = out_take(o, w64 ? 8 : 4);
    } else {
        if (sw == VT_VARIANT)
            raise(E_INVALIDARG);
        data = base == VT_DECIMAL ? (void *)v : (void *)&v->llVal;
    }
    ULONG hint_value = native_hint;
    switch (base) {
    case VT_EMPTY:
    case VT_NULL:
        if (byref)
            raise(DISP_E_BADVARTYPE);
        break;
    case VT_I2:
    case VT_BOOL:
    case VT_UI2:
        put16(out_take(o, 2), *(USHORT *)data);
        break;
    case VT_I4:
    case VT_R4:
    case VT_ERROR:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
        put32(out_take(o, 4), *(ULONG *)data);
        break;
    case VT_INT_PTR:
    case VT_UINT_PTR:
        put32(out_take(o, 4), (ULONG)*(ULONG_PTR *)data);
        break;
    case VT_R8:
    case VT_CY:
    case VT_DATE:
        hint_value = 8;
        out_align(o, 7);
        put64(out_take(o, 8), *(ULONGLONG *)data);
        break;
    case VT_I8:
    case VT_UI8:
        out_align(o, 7);
        put64(out_take(o, 8), *(ULONGLONG *)data);
        break;
    case VT_DECIMAL: {
        hint_value = 16;
        out_align(o, 7);
        BYTE *p = out_take(o, 16);
        if (p)
            memcpy(p, data, 16);
        break;
    }
    case VT_I1:
    case VT_UI1: {
        BYTE *p = out_take(o, 1);
        if (p)
            *p = *(BYTE *)data;
        break;
    }
    case VT_BSTR:
        out_referent(o, *(BSTR *)data);
        if (*(BSTR *)data)
            bstr_emit(o, (BSTR *)data);
        break;
    case VT_DISPATCH:
    case VT_UNKNOWN:
        out_referent(o, *(void **)data);
        if (*(IUnknown **)data)
            iface_emit(o, *(IUnknown **)data, base == VT_UNKNOWN ? &IID_IUnknown : &IID_IDispatch);
        break;
    case VT_VARIANT:
        if (!byref)
            raise(E_INVALIDARG);
        hint_value = sizeof(VARIANT) > 16 ? 24 : 16;
        out_marker(o);
        variant_emit(o, data);
        break;
    case VT_ARRAY:
        out_referent(o, *(SAFEARRAY **)data);
        if (*(SAFEARRAY **)data)
            safearray_write(o, (SAFEARRAY **)data, nullptr);
        break;
    case VT_RECORD:
        raise(E_NOTIMPL);
        break;
    default:
        raise(DISP_E_BADVARTYPE);
    }
    if (hint) {
        if (w64)
            put64(hint, hint_value);
        else
            put32(hint, hint_value);
    }
    put32(h, (o->Off - start + 7) >> 3);
}

static void variant_read(RD *r, VARIANT *v)
{
    BOOL w64 = r->W64;
    rd_align(r, 7);
    const BYTE *h = rd_take(r, 20);
    VARTYPE vt;
    memcpy(&vt, h + 8, 2);
    BOOL kept = (v->vt & VT_BYREF) != 0;
    if (kept) {
        /* an [in, out] variant that already refers to something is filled in place. */
        if (v->vt != vt)
            raise(E_INVALIDARG);
    } else {
        VariantClear(v);
        memset(v, 0, sizeof(*v));
        v->vt = vt;
    }
    memcpy(&v->wReserved1, h + 10, 2);
    memcpy(&v->wReserved2, h + 12, 2);
    memcpy(&v->wReserved3, h + 14, 2);
    VARTYPE sw = (vt & VT_ARRAY) ? (vt & (VT_ARRAY | VT_BYREF)) : vt;
    BOOL byref = (sw & VT_BYREF) != 0;
    VARTYPE base = sw & ~VT_BYREF;
    if (w64)
        rd_align(r, 7);
    void *target;
    if (!byref) {
        if (sw == VT_VARIANT)
            raise(RPC_BAD_STUB_DATA);
        target = base == VT_DECIMAL ? (void *)v : (void *)&v->llVal;
        if (base != VT_DECIMAL)
            v->llVal = 0;
    } else if (kept) {
        target = v->byref;
        if (!target)
            raise(RPC_NULL_REF);
        rd_take(r, w64 ? 8 : 4);
    } else {
        ULONGLONG hint = w64 ? 0 : 0;
        if (w64) {
            memcpy(&hint, rd_take(r, 8), 8);
        } else {
            ULONG h32 = rd32(r);
            hint = h32;
        }
        SIZE_T size;
        switch (base) {
        case VT_I1:
        case VT_UI1: size = 1; break;
        case VT_I2:
        case VT_UI2:
        case VT_BOOL: size = 2; break;
        case VT_I4:
        case VT_R4:
        case VT_ERROR:
        case VT_UI4:
        case VT_INT:
        case VT_UINT: size = 4; break;
        case VT_R8:
        case VT_CY:
        case VT_DATE:
        case VT_I8:
        case VT_UI8: size = 8; break;
        case VT_INT_PTR:
        case VT_UINT_PTR:
        case VT_BSTR:
        case VT_DISPATCH:
        case VT_UNKNOWN:
        case VT_ARRAY: size = sizeof(void *); break;
        case VT_DECIMAL: size = 16; break;
        case VT_VARIANT: size = sizeof(VARIANT); break;
        default:
            if (!hint)
                raise(RPC_BAD_STUB_DATA);
            raise(DISP_E_BADVARTYPE);
            size = 0;
            break;
        }
        target = AutAllocZero(size);
        if (!target)
            raise(E_OUTOFMEMORY);
        v->byref = target;
    }
    switch (base) {
    case VT_EMPTY:
    case VT_NULL:
        if (byref)
            raise(DISP_E_BADVARTYPE);
        break;
    case VT_I2:
    case VT_BOOL:
    case VT_UI2: {
        USHORT x = rd16(r);
        memcpy(target, &x, 2);
        break;
    }
    case VT_I4:
    case VT_R4:
    case VT_ERROR:
    case VT_UI4:
    case VT_INT:
    case VT_UINT: {
        ULONG x = rd32(r);
        memcpy(target, &x, 4);
        break;
    }
    case VT_INT_PTR:
    case VT_UINT_PTR: {
        LONG x = (LONG)rd32(r);
        *(LONG_PTR *)target = x;
        break;
    }
    case VT_R8:
    case VT_CY:
    case VT_DATE:
    case VT_I8:
    case VT_UI8:
        rd_align(r, 7);
        memcpy(target, rd_take(r, 8), 8);
        break;
    case VT_DECIMAL:
        rd_align(r, 7);
        memcpy(target, rd_take(r, 16), 16);
        if (!byref)
            v->vt = VT_DECIMAL;
        break;
    case VT_I1:
    case VT_UI1:
        memcpy(target, rd_take(r, 1), 1);
        break;
    case VT_BSTR:
        if (rd_referent(r)) {
            bstr_read(r, target);
        } else {
            SysFreeString(*(BSTR *)target);
            *(BSTR *)target = nullptr;
        }
        break;
    case VT_DISPATCH:
    case VT_UNKNOWN:
        if (rd_referent(r)) {
            iface_read(r, target, base == VT_UNKNOWN ? &IID_IUnknown : &IID_IDispatch);
        } else {
            AutRelease(*(void **)target);
            *(void **)target = nullptr;
        }
        break;
    case VT_VARIANT:
        rd_marker(r);
        variant_read(r, target);
        break;
    case VT_ARRAY:
        if (rd_referent(r)) {
            safearray_read(r, target, nullptr);
        } else {
            SafeArrayDestroy(*(SAFEARRAY **)target);
            *(SAFEARRAY **)target = nullptr;
        }
        break;
    case VT_RECORD:
        raise(E_NOTIMPL);
        break;
    default:
        raise(DISP_E_BADVARTYPE);
    }
}

static void safearray_read(RD *r, SAFEARRAY **pp, const GUID *iid_hint)
{
    BOOL w64 = r->W64;
    rd_align(r, w64 ? 7 : 3);
    BOOL there;
    if (w64) {
        ULONGLONG x;
        memcpy(&x, rd_take(r, 8), 8);
        there = x != 0;
    } else {
        there = rd32(r) != 0;
    }
    if (!there) {
        SafeArrayDestroy(*pp);
        *pp = nullptr;
        return;
    }
    USHORT dims, features;
    ULONG cb, locks, discr, count;
    if (w64) {
        rd_take(r, 8);
        const BYTE *p = rd_take(r, 12);
        memcpy(&dims, p, 2);
        memcpy(&features, p + 2, 2);
        memcpy(&cb, p + 4, 4);
        memcpy(&locks, p + 8, 4);
        rd_align(r, 7);
        discr = rd32(r);
        rd_align(r, 7);
        const BYTE *q = rd_take(r, 16);
        ULONGLONG wide;
        memcpy(&wide, q, 8);
        if (wide > 0x7FFFFFFF)
            raise(ARITH_OVERFLOW);
        count = (ULONG)wide;
    } else {
        const BYTE *p = rd_take(r, 28);
        ULONG conformance;
        memcpy(&conformance, p, 4);
        memcpy(&dims, p + 4, 2);
        memcpy(&features, p + 6, 2);
        memcpy(&cb, p + 8, 4);
        memcpy(&locks, p + 12, 4);
        memcpy(&discr, p + 16, 4);
        memcpy(&count, p + 20, 4);
        if (conformance != dims)
            raise(RPC_BAD_STUB_DATA);
    }
    if (!dims || count > 0x7FFFFFFF)
        raise(RPC_BAD_STUB_DATA);
    GUID iid = {0};
    if (discr == SF_HAVEIID) {
        memcpy(&iid, rd_take(r, 16), 16);
        discr = VT_UNKNOWN;
        iid_hint = &iid;
    }
    SAFEARRAYBOUND stack_bounds[8], *bounds = stack_bounds;
    if (dims > 8) {
        bounds = AutAlloc((SIZE_T)dims * sizeof(*bounds));
        if (!bounds)
            raise(E_OUTOFMEMORY);
    }
    ULONGLONG product = 1;
    for (USHORT i = 0; i < dims; i++) {
        const BYTE *b = rd_take(r, 8);
        memcpy(&bounds[i].cElements, b, 4);
        memcpy(&bounds[i].lLbound, b + 4, 4);
        product *= bounds[i].cElements;
        if (product > 0x7FFFFFFF) {
            if (bounds != stack_bounds)
                AutFree(bounds);
            raise(ARITH_OVERFLOW);
        }
    }
    if (w64)
        rd_take(r, 8);
    else
        rd_take(r, 4);
    ULONG native;
    ULONG wire_cb;
    switch (discr) {
    case VT_I1: wire_cb = native = 1; break;
    case VT_I2: wire_cb = native = 2; break;
    case VT_I4: wire_cb = native = 4; break;
    case VT_I8: wire_cb = native = 8; break;
    case VT_BSTR:
    case VT_UNKNOWN:
    case VT_DISPATCH: wire_cb = 4; native = sizeof(void *); break;
    case VT_VARIANT: wire_cb = 16; native = sizeof(VARIANT); break;
    default:
        if (bounds != stack_bounds)
            AutFree(bounds);
        raise(discr == SF_RECORD ? E_NOTIMPL : RPC_BAD_STUB_DATA);
        return;
    }
    if (cb != wire_cb || product != count) {
        if (bounds != stack_bounds)
            AutFree(bounds);
        raise(RPC_BAD_STUB_DATA);
    }
    SAFEARRAY *sa = *pp;
    if (sa) {
        BOOL same = sa->cDims == dims && sa->cbElements == native;
        for (USHORT i = 0; same && i < dims; i++)
            same = sa->rgsabound[dims - 1 - i].cElements == bounds[i].cElements &&
                   sa->rgsabound[dims - 1 - i].lLbound == bounds[i].lLbound;
        if (!same) {
            if (sa->fFeatures & (FADF_OWNERSHIP | FADF_FIXEDSIZE)) {
                if (bounds != stack_bounds)
                    AutFree(bounds);
                raise(DISP_E_BADCALLEE);
            }
            SafeArrayDestroy(sa);
            sa = nullptr;
        }
    }
    if (!sa) {
        sa = SafeArrayCreate((VARTYPE)discr, dims, bounds);
        if (!sa) {
            if (bounds != stack_bounds)
                AutFree(bounds);
            raise(E_OUTOFMEMORY);
        }
        /* the features of the sender, except those about who owns the memory. */
        sa->fFeatures |= features & ~(FADF_OWNERSHIP | 0x2000 | 0x1000);
        if ((sa->fFeatures & (FADF_HAVEIID | FADF_HAVEVARTYPE)) == (FADF_HAVEIID | FADF_HAVEVARTYPE))
            sa->fFeatures &= ~FADF_HAVEVARTYPE;
        if ((sa->fFeatures & FADF_HAVEVARTYPE) && (locks >> 16))
            ((ULONG *)sa)[-1] = locks >> 16;
        if ((sa->fFeatures & FADF_HAVEIID) && iid_hint)
            SafeArraySetIID(sa, iid_hint);
    }
    if (bounds != stack_bounds)
        AutFree(bounds);
    SafeArrayLock(sa);
    BYTE *data = sa->pvData;
    if (discr == VT_I1 || discr == VT_I2 || discr == VT_I4 || discr == VT_I8) {
        if (discr == VT_I8)
            rd_align(r, 7);
        ULONGLONG bytes = (ULONGLONG)count * native;
        if (bytes > 0xFFFFFFFF || rd_left(r) < bytes) {
            SafeArrayUnlock(sa);
            if (!*pp)
                SafeArrayDestroy(sa);
            raise(RPC_BAD_STUB_DATA);
        }
        if (bytes)
            memcpy(data, rd_take(r, (SIZE_T)bytes), (SIZE_T)bytes);
    } else {
        SIZE_T refs_bytes = (SIZE_T)count * (w64 ? 8 : 4);
        if (rd_left(r) < refs_bytes) {
            SafeArrayUnlock(sa);
            if (!*pp)
                SafeArrayDestroy(sa);
            raise(RPC_BAD_STUB_DATA);
        }
        const BYTE *refs = rd_take(r, refs_bytes);
        for (ULONG i = 0; i < count; i++) {
            BOOL present;
            if (w64) {
                ULONGLONG x;
                memcpy(&x, refs + (SIZE_T)i * 8, 8);
                present = x != 0;
            } else {
                ULONG x;
                memcpy(&x, refs + (SIZE_T)i * 4, 4);
                present = x != 0;
            }
            switch (discr) {
            case VT_BSTR:
                if (present) {
                    bstr_read(r, &((BSTR *)data)[i]);
                } else {
                    SysFreeString(((BSTR *)data)[i]);
                    ((BSTR *)data)[i] = nullptr;
                }
                break;
            case VT_VARIANT:
                variant_read(r, &((VARIANT *)data)[i]);
                break;
            default:
                if (present) {
                    iface_read(r, &((IUnknown **)data)[i],
                               iid_hint ? iid_hint : discr == VT_DISPATCH ? &IID_IDispatch : &IID_IUnknown);
                } else {
                    AutRelease(((IUnknown **)data)[i]);
                    ((IUnknown **)data)[i] = nullptr;
                }
                break;
            }
        }
    }
    SafeArrayUnlock(sa);
    *pp = sa;
}

static void safearray_free(SAFEARRAY **pp)
{
    SAFEARRAY *sa = *pp;
    if (!sa)
        return;
    if (sa->cLocks)
        raise(DISP_E_ARRAYISLOCKED);
    SafeArrayDestroy(sa);
    *pp = nullptr;
}

/* the contents of a variant that reading made, and the memory it allocated for what the variant refers to. */
static void value_free(VARTYPE base, void *data)
{
    switch (base) {
    case VT_BSTR:
        SysFreeString(*(BSTR *)data);
        *(BSTR *)data = nullptr;
        break;
    case VT_UNKNOWN:
    case VT_DISPATCH:
        AutRelease(*(void **)data);
        *(void **)data = nullptr;
        break;
    case VT_ARRAY:
        safearray_free(data);
        break;
    default:
        break;
    }
}

static void variant_free(VARIANT *v)
{
    if (!v || v->vt == VT_EMPTY)
        return;
    VARTYPE vt = v->vt;
    VARTYPE sw = (vt & VT_ARRAY) ? (vt & (VT_ARRAY | VT_BYREF)) : vt;
    if (sw & VT_BYREF) {
        void *p = v->byref;
        if (!p)
            raise(RPC_NULL_REF);
        VARTYPE base = sw & ~VT_BYREF;
        if (base == VT_VARIANT)
            variant_free(p);
        else
            value_free(base, p);
        AutFree(p);
    } else if (sw != VT_VARIANT) {
        value_free(sw, sw == VT_DECIMAL ? (void *)v : (void *)&v->llVal);
    }
    memset(v, 0, sizeof(*v));
}

/* ------------------------------------------------------------------ the routines a stub names */

static void variant_emit_thunk(WCUR *o, void *value)
{
    variant_emit(o, value);
}

typedef struct {
    SAFEARRAY **Array;
    const GUID *Iid;
} ARRAY_ARG;

static void safearray_emit_thunk(WCUR *o, void *value)
{
    ARRAY_ARG *a = value;
    safearray_write(o, a->Array, a->Iid);
}

static ULONG array_size(ULONG *flags, ULONG start, LPSAFEARRAY *value, const GUID *iid, BOOL w64)
{
    if (!value)
        return start;
    ARRAY_ARG a = {value, iid};
    return user_size_via(flags, start, w64, safearray_emit_thunk, &a);
}

static BYTE *array_marshal(ULONG *flags, BYTE *buffer, LPSAFEARRAY *value, const GUID *iid, BOOL w64)
{
    if (!value)
        return buffer;
    ARRAY_ARG a = {value, iid};
    return user_marshal_via(flags, buffer, w64, safearray_emit_thunk, &a);
}

static BYTE *array_unmarshal(ULONG *flags, BYTE *buffer, LPSAFEARRAY *value, const GUID *iid, BOOL w64)
{
    RD r;
    rd_open(&r, flags, buffer, w64);
    safearray_read(&r, value, iid);
    return (BYTE *)r.Cur;
}

WINOLEAUTAPI ULONG WINAPI LPSAFEARRAY_UserSize(ULONG *flags, ULONG start, LPSAFEARRAY *value)
{
    return array_size(flags, start, value, nullptr, FALSE);
}

WINOLEAUTAPI BYTE *WINAPI LPSAFEARRAY_UserMarshal(ULONG *flags, BYTE *buffer, LPSAFEARRAY *value)
{
    return array_marshal(flags, buffer, value, nullptr, FALSE);
}

WINOLEAUTAPI BYTE *WINAPI LPSAFEARRAY_UserUnmarshal(ULONG *flags, BYTE *buffer, LPSAFEARRAY *value)
{
    return array_unmarshal(flags, buffer, value, nullptr, FALSE);
}

WINOLEAUTAPI void WINAPI LPSAFEARRAY_UserFree(ULONG *flags, LPSAFEARRAY *value)
{
    if (value)
        safearray_free(value);
}

WINOLEAUTAPI ULONG WINAPI LPSAFEARRAY_UserSize64(ULONG *flags, ULONG start, LPSAFEARRAY *value)
{
    return array_size(flags, start, value, nullptr, TRUE);
}

WINOLEAUTAPI BYTE *WINAPI LPSAFEARRAY_UserMarshal64(ULONG *flags, BYTE *buffer, LPSAFEARRAY *value)
{
    return array_marshal(flags, buffer, value, nullptr, TRUE);
}

WINOLEAUTAPI BYTE *WINAPI LPSAFEARRAY_UserUnmarshal64(ULONG *flags, BYTE *buffer, LPSAFEARRAY *value)
{
    return array_unmarshal(flags, buffer, value, nullptr, TRUE);
}

WINOLEAUTAPI void WINAPI LPSAFEARRAY_UserFree64(ULONG *flags, LPSAFEARRAY *value)
{
    LPSAFEARRAY_UserFree(flags, value);
}

WINOLEAUTAPI ULONG WINAPI LPSAFEARRAY_Size(ULONG *flags, ULONG start, LPSAFEARRAY *value, const GUID *iid)
{
    return array_size(flags, start, value, iid, FALSE);
}

WINOLEAUTAPI BYTE *WINAPI LPSAFEARRAY_Marshal(ULONG *flags, BYTE *buffer, LPSAFEARRAY *value, const GUID *iid)
{
    return array_marshal(flags, buffer, value, iid, FALSE);
}

WINOLEAUTAPI BYTE *WINAPI LPSAFEARRAY_Unmarshal(ULONG *flags, BYTE *buffer, LPSAFEARRAY *value, const GUID *iid)
{
    return array_unmarshal(flags, buffer, value, iid, FALSE);
}

WINOLEAUTAPI ULONG WINAPI VARIANT_UserSize(ULONG *flags, ULONG start, VARIANT *value)
{
    return value ? user_size_via(flags, start, FALSE, variant_emit_thunk, value) : start;
}

WINOLEAUTAPI BYTE *WINAPI VARIANT_UserMarshal(ULONG *flags, BYTE *buffer, VARIANT *value)
{
    return value ? user_marshal_via(flags, buffer, FALSE, variant_emit_thunk, value) : buffer;
}

WINOLEAUTAPI BYTE *WINAPI VARIANT_UserUnmarshal(ULONG *flags, BYTE *buffer, VARIANT *value)
{
    if (!value)
        return buffer;
    RD r;
    rd_open(&r, flags, buffer, FALSE);
    variant_read(&r, value);
    return (BYTE *)r.Cur;
}

WINOLEAUTAPI void WINAPI VARIANT_UserFree(ULONG *flags, VARIANT *value)
{
    variant_free(value);
}

WINOLEAUTAPI ULONG WINAPI VARIANT_UserSize64(ULONG *flags, ULONG start, VARIANT *value)
{
    return value ? user_size_via(flags, start, TRUE, variant_emit_thunk, value) : start;
}

WINOLEAUTAPI BYTE *WINAPI VARIANT_UserMarshal64(ULONG *flags, BYTE *buffer, VARIANT *value)
{
    return value ? user_marshal_via(flags, buffer, TRUE, variant_emit_thunk, value) : buffer;
}

WINOLEAUTAPI BYTE *WINAPI VARIANT_UserUnmarshal64(ULONG *flags, BYTE *buffer, VARIANT *value)
{
    if (!value)
        return buffer;
    RD r;
    rd_open(&r, flags, buffer, TRUE);
    variant_read(&r, value);
    return (BYTE *)r.Cur;
}

WINOLEAUTAPI void WINAPI VARIANT_UserFree64(ULONG *flags, VARIANT *value)
{
    variant_free(value);
}
