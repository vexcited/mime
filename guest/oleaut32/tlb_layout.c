/*
 * the sizes and offsets of types for a processor: where the members of a structure are, how big a structure or a union
 * is, and where the functions of an interface are in its table of virtual functions. A library written for another word
 * size than this processor's gets its structures laid out again; a library being created gets everything laid out
 * before it is written.
 */
#include "typelib.h"

#define MAX_DEPTH 16

static ULONG ptr_size(SYSKIND kind)
{
    return kind == SYS_WIN64 ? 8 : 4;
}

static ULONG align_up(ULONG value, ULONG align)
{
    return align ? (value + align - 1) / align * align : value;
}

static HRESULT size_align(TL_TYPE *scope, const TYPEDESC *td, SYSKIND kind, int depth, ULONG *size, ULONG *align);

/* the size and alignment of a structure, a union, or what an alias or an enumeration stands for. */
static HRESULT type_size_align(TL_TYPE *t, SYSKIND kind, int depth, ULONG *size, ULONG *align)
{
    if (depth > MAX_DEPTH)
        return TYPE_E_CIRCULARTYPE;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    switch (t->Kind) {
    case TKIND_ENUM:
        *size = *align = 4;
        return S_OK;
    case TKIND_ALIAS:
        return size_align(t, &t->Alias, kind, depth + 1, size, align);
    case TKIND_RECORD:
    case TKIND_UNION: {
        ULONG pack = t->PackSet ? t->Pack : 8;
        if (!t->PackSet && t->Lib->Image && t->Alignment && t->Alignment < 8)
            pack = t->Alignment;
        ULONG offset = 0, biggest = 1, largest_size = 0;
        for (UINT i = 0; i < t->VarCount; i++) {
            ULONG s, a;
            hr = size_align(t, &t->Vars[i].Type, kind, depth + 1, &s, &a);
            if (FAILED(hr))
                return hr;
            ULONG member_align = a < pack ? a : pack;
            if (member_align > biggest)
                biggest = member_align;
            if (t->Kind == TKIND_UNION) {
                if (s > largest_size)
                    largest_size = s;
            } else {
                offset = align_up(offset, member_align);
                offset += s;
            }
        }
        ULONG total = t->Kind == TKIND_UNION ? largest_size : offset;
        *size = align_up(total, biggest);
        *align = biggest;
        return S_OK;
    }
    default:
        *size = ptr_size(kind);
        *align = ptr_size(kind);
        return S_OK;
    }
}

static HRESULT size_align(TL_TYPE *scope, const TYPEDESC *td, SYSKIND kind, int depth, ULONG *size, ULONG *align)
{
    if (depth > MAX_DEPTH)
        return TYPE_E_CIRCULARTYPE;
    ULONG p = ptr_size(kind);
    switch (td->vt) {
    case VT_I1:
    case VT_UI1: *size = *align = 1; return S_OK;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL: *size = *align = 2; return S_OK;
    case VT_I4:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
    case VT_R4:
    case VT_ERROR:
    case VT_HRESULT: *size = *align = 4; return S_OK;
    case VT_I8:
    case VT_UI8:
    case VT_R8:
    case VT_CY:
    case VT_DATE: *size = *align = 8; return S_OK;
    case VT_VARIANT: *size = 8 + 2 * p; *align = 8; return S_OK;
    case VT_DECIMAL: *size = 16; *align = 8; return S_OK;
    case VT_CARRAY: {
        ULONG s, a, count = 1;
        HRESULT hr = size_align(scope, &td->lpadesc->tdescElem, kind, depth + 1, &s, &a);
        if (FAILED(hr))
            return hr;
        for (USHORT i = 0; i < td->lpadesc->cDims; i++)
            count *= td->lpadesc->rgbounds[i].cElements;
        *size = s * count;
        *align = a;
        return S_OK;
    }
    case VT_USERDEFINED: {
        ITypeInfo *info;
        HRESULT hr = TlResolveHref(scope, td->hreftype & 0xFFFFFF, &info);
        if (FAILED(hr))
            return hr;
        ITypeLib *lib;
        UINT index;
        hr = info->lpVtbl->GetContainingTypeLib(info, &lib, &index);
        if (SUCCEEDED(hr)) {
            TL_TYPE *t = ((TL_LIB *)lib)->Types[index];
            hr = type_size_align(t, kind, depth + 1, size, align);
            lib->lpVtbl->Release(lib);
        }
        info->lpVtbl->Release(info);
        return hr;
    }
    default:
        /* pointers, strings, interfaces and arrays of the system. */
        *size = *align = p;
        return S_OK;
    }
}

/* what a type derived from this one inherits: the size of the table of virtual functions, the functions, the bases. */
static void chain_of(ITypeInfo *info, ULONG *vsize, ULONG *funcs, ULONG *depth)
{
    *vsize = *funcs = *depth = 0;
    ITypeInfo *cur = info;
    cur->lpVtbl->AddRef(cur);
    BOOL first = TRUE;
    for (int guard = 0; cur && guard < 32; guard++) {
        ITypeInfo *half = TlInfoVtableHalf(cur);
        ITypeInfo *use = half ? half : cur;
        TYPEATTR *a;
        if (FAILED(use->lpVtbl->GetTypeAttr(use, &a))) {
            AutRelease(half);
            break;
        }
        if (first) {
            *vsize = a->cbSizeVft;
            first = FALSE;
        }
        *funcs += a->cFuncs;
        BOOL has_base = a->cImplTypes && (a->typekind == TKIND_INTERFACE || a->typekind == TKIND_DISPATCH);
        use->lpVtbl->ReleaseTypeAttr(use, a);
        ITypeInfo *next = nullptr;
        HREFTYPE ref;
        if (has_base && SUCCEEDED(use->lpVtbl->GetRefTypeOfImplType(use, 0, &ref)))
            use->lpVtbl->GetRefTypeInfo(use, ref, &next);
        AutRelease(half);
        cur->lpVtbl->Release(cur);
        cur = next;
        if (next)
            (*depth)++;
    }
    AutRelease(cur);
}

/* the vtable position and the size of the table of an interface type, in the pointer size of the library. */
static HRESULT layout_interface(TL_TYPE *t)
{
    TL_LIB *lib = t->Lib;
    ULONG slot = ptr_size(lib->Syskind);
    ULONG start = 0;
    t->InheritedFuncs = 0;
    t->InheritedImpls = 0;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    if (t->ImplCount && (t->Kind == TKIND_INTERFACE || t->Kind == TKIND_DISPATCH)) {
        ITypeInfo *base;
        hr = TlResolveHref(t, t->Impls[0].Ref | (t->Kind == TKIND_INTERFACE ? 2 : 0), &base);
        if (SUCCEEDED(hr)) {
            ULONG vsize, funcs, depth;
            chain_of(base, &vsize, &funcs, &depth);
            base->lpVtbl->Release(base);
            start = vsize * slot / sizeof(void *);
            t->InheritedFuncs = (USHORT)funcs;
            t->InheritedImpls = (USHORT)(1 + depth);
        }
    }
    ULONG offset = start;
    for (UINT i = 0; i < t->FuncCount; i++) {
        TL_FUNC *f = &t->Funcs[i];
        if (t->Kind == TKIND_DISPATCH && !(t->Flags & TYPEFLAG_FDUAL)) {
            f->VtOffset = 0;
            continue;
        }
        if (f->Kind == FUNC_STATIC || f->Kind == FUNC_NONVIRTUAL || f->Kind == FUNC_DISPATCH)
            continue;
        f->VtOffset = (SHORT)offset;
        f->VtOffsetSet = TRUE;
        offset += slot;
    }
    t->SizeVft = (USHORT)(t->Kind == TKIND_DISPATCH && !(t->Flags & TYPEFLAG_FDUAL) ? 0 : offset);
    t->SizeInstance = slot;
    if (t->Alignment == 0)
        t->Alignment = (USHORT)slot;
    return S_OK;
}

HRESULT TlEnsureLayout(TL_TYPE *t)
{
    TL_LIB *lib = t->Lib;
    if (t->Layouted)
        return S_OK;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    EnterCriticalSection(&lib->Lock);
    if (t->Layouted) {
        LeaveCriticalSection(&lib->Lock);
        return S_OK;
    }
    BOOL native_file = lib->Image && lib->FileSyskind == lib->Syskind;
    hr = S_OK;
    switch (t->Kind) {
    case TKIND_RECORD:
    case TKIND_UNION:
        if (!native_file) {
            ULONG pack = t->PackSet ? t->Pack : 8;
            if (!t->PackSet && lib->Image && t->Alignment && t->Alignment < 8)
                pack = t->Alignment;
            ULONG offset = 0, biggest = 1, largest = 0;
            for (UINT i = 0; i < t->VarCount && SUCCEEDED(hr); i++) {
                ULONG s, a;
                hr = size_align(t, &t->Vars[i].Type, lib->Syskind, 0, &s, &a);
                if (FAILED(hr))
                    break;
                ULONG ma = a < pack ? a : pack;
                if (ma > biggest)
                    biggest = ma;
                if (t->Kind == TKIND_UNION) {
                    t->Vars[i].Offset = 0;
                    if (s > largest)
                        largest = s;
                } else {
                    offset = align_up(offset, ma);
                    t->Vars[i].Offset = offset;
                    offset += s;
                }
            }
            if (SUCCEEDED(hr)) {
                t->SizeInstance = align_up(t->Kind == TKIND_UNION ? largest : offset, biggest);
                t->Alignment = (USHORT)biggest;
            }
        }
        break;
    case TKIND_INTERFACE:
    case TKIND_DISPATCH:
        if (!lib->Image)
            hr = layout_interface(t);
        break;
    case TKIND_ENUM:
        if (!native_file) {
            t->SizeInstance = 4;
            t->Alignment = 4;
        }
        break;
    case TKIND_ALIAS:
        if (!native_file) {
            ULONG s, a;
            if (SUCCEEDED(size_align(t, &t->Alias, lib->Syskind, 0, &s, &a))) {
                t->SizeInstance = s;
                t->Alignment = (USHORT)a;
            }
        }
        break;
    default:
        break;
    }
    if (SUCCEEDED(hr))
        t->Layouted = TRUE;
    LeaveCriticalSection(&lib->Lock);
    return hr;
}
