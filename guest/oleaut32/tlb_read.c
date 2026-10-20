/*
 * type libraries in the msft format: a library read from memory. The file is a header, the offsets of the type
 * records, a directory of segments (the type records, imports, references, guids, names, strings, type descriptions,
 * array descriptions and custom data) and then the members of each type. Everything the file says is checked against
 * its size before it is used; a type's functions and variables are decoded when they are first asked for.
 */
#include "typelib.h"
#include <mime/win32/winnls.h>

#define MSFT_MAGIC1 0x5446534D
#define MSFT_MAGIC2 0x00010002
#define TYPE_RECORD_SIZE 100
#define MAX_DEPTH 32

/* ------------------------------------------------------------------ the heap of a library */

typedef struct TL_CHUNK {
    struct TL_CHUNK *Next;
    SIZE_T Used, Size;
} TL_CHUNK;

#define CHUNK_HEADER ((sizeof(TL_CHUNK) + 15) & ~(SIZE_T)15)

void *TlAlloc(TL_LIB *lib, SIZE_T size)
{
    size = (size + 15) & ~(SIZE_T)15;
    TL_CHUNK *chunk = lib->Chunks;
    if (!chunk || chunk->Size - chunk->Used < size) {
        SIZE_T total = size > 16384 ? size : 16384;
        chunk = AutAllocZero(CHUNK_HEADER + total);
        if (!chunk)
            return nullptr;
        chunk->Size = total;
        chunk->Next = lib->Chunks;
        lib->Chunks = chunk;
        /* a big block does not hide the room that is left in the one before it. */
        if (size > 16384 && chunk->Next) {
            chunk->Used = size;
            lib->Chunks = chunk->Next;
            chunk->Next = lib->Chunks->Next;
            lib->Chunks->Next = chunk;
            return (BYTE *)chunk + CHUNK_HEADER;
        }
    }
    void *p = (BYTE *)chunk + CHUNK_HEADER + chunk->Used;
    chunk->Used += size;
    return p;
}

void TlHeapFree(TL_LIB *lib)
{
    TL_CHUNK *chunk = lib->Chunks;
    lib->Chunks = nullptr;
    while (chunk) {
        TL_CHUNK *next = chunk->Next;
        AutFree(chunk);
        chunk = next;
    }
}

WCHAR *TlStrDupLen(TL_LIB *lib, const WCHAR *text, SIZE_T length)
{
    WCHAR *copy = TlAlloc(lib, (length + 1) * sizeof(WCHAR));
    if (copy) {
        memcpy(copy, text, length * sizeof(WCHAR));
        copy[length] = 0;
    }
    return copy;
}

WCHAR *TlStrDup(TL_LIB *lib, const WCHAR *text)
{
    return text ? TlStrDupLen(lib, text, wcslen(text)) : nullptr;
}

/* a string of the library's heap that can be a bstr's value: the length goes before it. */
static BSTR tl_bstr(TL_LIB *lib, const WCHAR *text, ULONG length)
{
    BYTE *block = TlAlloc(lib, 8 + (SIZE_T)length * sizeof(WCHAR) + 4);
    if (!block)
        return nullptr;
    *(ULONG *)(block + 4) = length * sizeof(WCHAR);
    BSTR b = (BSTR)(block + 8);
    memcpy(b, text, length * sizeof(WCHAR));
    return b;
}

/* ------------------------------------------------------------------ the pointer size of the library and of this machine */

BOOL TlNativeSyskind(SYSKIND kind)
{
    return kind == (sizeof(void *) == 8 ? SYS_WIN64 : SYS_WIN32);
}

/* a position in the vtable is in words of the pointer size of the library's processor. */
SHORT TlNativeVtOffset(const TL_LIB *lib, SHORT offset)
{
    if (lib->FileSyskind == SYS_WIN32 && sizeof(void *) == 8)
        return (SHORT)(offset * 2);
    if (lib->FileSyskind == SYS_WIN64 && sizeof(void *) == 4)
        return (SHORT)(offset / 2);
    return offset;
}

USHORT TlNativeVtSize(const TL_LIB *lib, USHORT size)
{
    return (USHORT)TlNativeVtOffset(lib, (SHORT)size);
}

/* ------------------------------------------------------------------ the image */

static const BYTE *segment_at(const TL_LIB *lib, int seg, ULONG offset, ULONG length)
{
    const TL_SEGMENT *s = &lib->Seg[seg];
    if (offset > s->Length || length > s->Length - offset)
        return nullptr;
    return lib->Image + s->Offset + offset;
}

static ULONG rd32(const BYTE *p)
{
    ULONG v;
    memcpy(&v, p, 4);
    return v;
}

static USHORT rd16(const BYTE *p)
{
    USHORT v;
    memcpy(&v, p, 2);
    return v;
}

static const BYTE *image_at(const TL_LIB *lib, ULONGLONG offset, ULONGLONG length)
{
    if (offset > lib->ImageSize || length > lib->ImageSize - offset)
        return nullptr;
    return lib->Image + offset;
}

/* the text of a record of bytes in the library's code page, on the heap of the library. */
static WCHAR *text_of_bytes(TL_LIB *lib, const char *bytes, ULONG length)
{
    if (!length)
        return TlStrDupLen(lib, L"", 0);
    WCHAR *text = TlAlloc(lib, ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (!text)
        return nullptr;
    int n = MultiByteToWideChar(lib->CodePage, 0, bytes, (int)length, text, (int)length);
    text[n > 0 ? n : 0] = 0;
    return text;
}

WCHAR *TlNameAt(TL_LIB *lib, ULONG offset)
{
    if (offset == 0xFFFFFFFF)
        return nullptr;
    const BYTE *p = segment_at(lib, SEG_NAME, offset, 12);
    if (!p)
        return nullptr;
    ULONG length = rd16(p + 8) & 0x3FF;
    if (!segment_at(lib, SEG_NAME, offset + 12, length))
        return nullptr;
    return text_of_bytes(lib, (const char *)p + 12, length);
}

WCHAR *TlStringAt(TL_LIB *lib, ULONG offset)
{
    if (offset == 0xFFFFFFFF)
        return nullptr;
    const BYTE *p = segment_at(lib, SEG_STRING, offset, 2);
    if (!p)
        return nullptr;
    ULONG length = rd16(p);
    if (!segment_at(lib, SEG_STRING, offset + 2, length))
        return nullptr;
    return text_of_bytes(lib, (const char *)p + 2, length);
}

BOOL TlGuidAt(TL_LIB *lib, ULONG offset, GUID *guid)
{
    if (offset == 0xFFFFFFFF)
        return FALSE;
    const BYTE *p = segment_at(lib, SEG_GUID, offset, 16);
    if (!p)
        return FALSE;
    memcpy(guid, p, 16);
    return TRUE;
}

/* ------------------------------------------------------------------ references to types */

HREFTYPE TlHrefOfIndex(ULONG index)
{
    return index * HREF_LOCAL_STEP;
}

BOOL TlIndexOfHref(HREFTYPE ref, ULONG *index)
{
    ref &= 0xFFFFFF;
    if (ref & 1)
        return FALSE;
    ref &= ~(ULONG)3;
    if (ref % HREF_LOCAL_STEP)
        return FALSE;
    *index = ref / HREF_LOCAL_STEP;
    return TRUE;
}

/* the reference as the file wrote it: where the type's record is, or the position of an import. */
static HREFTYPE href_from_file(TL_LIB *lib, ULONG file_ref)
{
    if (file_ref == 0xFFFFFFFF)
        return file_ref;
    if (file_ref & 1)
        return file_ref & ~(ULONG)2;
    ULONG offset = file_ref & ~(ULONG)3;
    return TlHrefOfIndex(offset / TYPE_RECORD_SIZE);
}

/* ------------------------------------------------------------------ type descriptions */

static HRESULT read_typedesc(TL_LIB *lib, LONG encoded, TYPEDESC *out, int depth)
{
    memset(out, 0, sizeof(*out));
    if (encoded < 0) {
        out->vt = (VARTYPE)encoded;
        return S_OK;
    }
    if (depth > MAX_DEPTH)
        return TYPE_E_INVDATAREAD;
    const BYTE *p = segment_at(lib, SEG_TYPDESC, (ULONG)encoded, 8);
    if (!p)
        return TYPE_E_INVDATAREAD;
    VARTYPE vt = rd16(p);
    ULONG data = rd32(p + 4);
    out->vt = vt;
    switch (vt) {
    case VT_PTR:
    case VT_SAFEARRAY: {
        TYPEDESC *inner = TlAlloc(lib, sizeof(TYPEDESC));
        if (!inner)
            return E_OUTOFMEMORY;
        out->lptdesc = inner;
        return read_typedesc(lib, (LONG)data, inner, depth + 1);
    }
    case VT_CARRAY: {
        const BYTE *a = segment_at(lib, SEG_ARRAYDESC, data, 8);
        if (!a)
            return TYPE_E_INVDATAREAD;
        USHORT dims = rd16(a + 4);
        if (!dims || !segment_at(lib, SEG_ARRAYDESC, data + 8, (ULONG)dims * 8))
            return TYPE_E_INVDATAREAD;
        ARRAYDESC *desc = TlAlloc(lib, sizeof(ARRAYDESC) + (dims - 1) * sizeof(SAFEARRAYBOUND));
        if (!desc)
            return E_OUTOFMEMORY;
        desc->cDims = dims;
        for (USHORT i = 0; i < dims; i++) {
            desc->rgbounds[i].cElements = rd32(a + 8 + i * 8);
            desc->rgbounds[i].lLbound = (LONG)rd32(a + 12 + i * 8);
        }
        out->lpadesc = desc;
        return read_typedesc(lib, (LONG)rd32(a), &desc->tdescElem, depth + 1);
    }
    case VT_USERDEFINED:
        out->hreftype = href_from_file(lib, data);
        return S_OK;
    default:
        return S_OK;
    }
}

void TlCopyTypeDesc(TL_LIB *lib, TYPEDESC *to, const TYPEDESC *from)
{
    *to = *from;
    if (from->vt == VT_PTR || from->vt == VT_SAFEARRAY) {
        to->lptdesc = TlAlloc(lib, sizeof(TYPEDESC));
        if (to->lptdesc)
            TlCopyTypeDesc(lib, to->lptdesc, from->lptdesc);
    } else if (from->vt == VT_CARRAY) {
        SIZE_T size = sizeof(ARRAYDESC) + (from->lpadesc->cDims - 1) * sizeof(SAFEARRAYBOUND);
        to->lpadesc = TlAlloc(lib, size);
        if (to->lpadesc) {
            memcpy(to->lpadesc, from->lpadesc, size);
            TlCopyTypeDesc(lib, &to->lpadesc->tdescElem, &from->lpadesc->tdescElem);
        }
    }
}

/* ------------------------------------------------------------------ constants */

/* a value of the custom data segment, or one that is in the reference itself. */
static HRESULT const_value(TL_LIB *lib, ULONG ref, VARIANT *out)
{
    memset(out, 0, sizeof(*out));
    if (ref & 0x80000000) {
        VARTYPE vt = (VARTYPE)((ref >> 26) & 0x1F);
        out->vt = vt;
        switch (vt) {
        case VT_I2:
        case VT_BOOL:
        case VT_UI2: out->uiVal = (USHORT)ref; return S_OK;
        case VT_I1:
        case VT_UI1: out->bVal = (BYTE)ref; return S_OK;
        case VT_I4:
        case VT_UI4:
        case VT_INT:
        case VT_UINT:
        case VT_ERROR:
        case VT_R4: out->ulVal = ref & 0x3FFFFFF; return S_OK;
        case VT_UNKNOWN:
        case VT_DISPATCH: out->ullVal = 0; out->byref = (PVOID)(ULONG_PTR)(ref & 0x3FFFFFF); return S_OK;
        default: out->vt = VT_EMPTY; return DISP_E_BADVARTYPE;
        }
    }
    const BYTE *p = segment_at(lib, SEG_CUSTDATA, ref, 2);
    if (!p)
        return TYPE_E_INVDATAREAD;
    VARTYPE vt = rd16(p);
    switch (vt) {
    case VT_I4:
    case VT_R4:
    case VT_ERROR:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
        if (!segment_at(lib, SEG_CUSTDATA, ref, 6))
            return TYPE_E_INVDATAREAD;
        out->vt = vt;
        out->ulVal = rd32(p + 2);
        return S_OK;
    case VT_R8:
    case VT_CY:
    case VT_DATE:
    case VT_I8:
    case VT_UI8:
        if (!segment_at(lib, SEG_CUSTDATA, ref, 10))
            return TYPE_E_INVDATAREAD;
        out->vt = vt;
        memcpy(&out->llVal, p + 2, 8);
        return S_OK;
    case VT_DECIMAL: {
        if (!segment_at(lib, SEG_CUSTDATA, ref, 16))
            return TYPE_E_INVDATAREAD;
        DECIMAL d;
        memcpy(&d, p, 16);
        out->decVal = d;
        out->vt = VT_DECIMAL;
        return S_OK;
    }
    case VT_BSTR: {
        if (!segment_at(lib, SEG_CUSTDATA, ref, 6))
            return TYPE_E_INVDATAREAD;
        ULONG length = rd32(p + 2);
        out->vt = VT_BSTR;
        if (length == 0xFFFFFFFF)
            return S_OK;
        if (!segment_at(lib, SEG_CUSTDATA, ref + 6, length))
            return TYPE_E_INVDATAREAD;
        WCHAR *wide = TlAlloc(lib, ((SIZE_T)length + 1) * sizeof(WCHAR));
        if (!wide)
            return E_OUTOFMEMORY;
        int n = length ? MultiByteToWideChar(lib->CodePage, 0, (const char *)p + 6, (int)length, wide, (int)length) : 0;
        out->bstrVal = tl_bstr(lib, wide, n > 0 ? n : 0);
        return out->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    default:
        return DISP_E_BADVARTYPE;
    }
}

HRESULT TlVariantCopy(TL_LIB *lib, VARIANT *to, const VARIANT *from)
{
    *to = *from;
    if (from->vt == VT_BSTR && from->bstrVal) {
        to->bstrVal = tl_bstr(lib, from->bstrVal, SysStringLen(from->bstrVal));
        return to->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    return S_OK;
}

/* a chain of custom data items starting at a position of the custom data guid segment. */
static TL_CUST *read_custom(TL_LIB *lib, ULONG offset)
{
    TL_CUST *head = nullptr, **tail = &head;
    for (int guard = 0; offset != 0xFFFFFFFF && guard < 1024; guard++) {
        const BYTE *p = segment_at(lib, SEG_CDGUID, offset, 12);
        if (!p)
            break;
        TL_CUST *item = TlAlloc(lib, sizeof(*item));
        if (!item)
            break;
        if (TlGuidAt(lib, rd32(p), &item->Guid) && SUCCEEDED(const_value(lib, rd32(p + 4), &item->Value))) {
            *tail = item;
            tail = &item->Next;
        }
        offset = rd32(p + 8);
    }
    return head;
}

/* ------------------------------------------------------------------ the library */

static HRESULT read_imports(TL_LIB *lib)
{
    ULONG count = lib->Seg[SEG_IMPINFO].Length / 12;
    if (!count)
        return S_OK;
    lib->Imports = TlAlloc(lib, count * sizeof(TL_IMPORT));
    if (!lib->Imports)
        return E_OUTOFMEMORY;
    lib->ImportCount = count;
    for (ULONG i = 0; i < count; i++) {
        const BYTE *p = segment_at(lib, SEG_IMPINFO, i * 12, 12);
        TL_IMPORT *imp = &lib->Imports[i];
        BYTE flags = p[2];
        imp->Kind = (TYPEKIND)p[3];
        imp->ByGuid = flags & 1;
        if (imp->ByGuid)
            TlGuidAt(lib, rd32(p + 8), &imp->Type);
        else
            imp->Index = rd32(p + 8);
        const BYTE *f = segment_at(lib, SEG_IMPFILES, rd32(p + 4), 14);
        if (f) {
            TlGuidAt(lib, rd32(f), &imp->Lib);
            imp->Lcid = rd32(f + 4);
            imp->Major = rd16(f + 8);
            imp->Minor = rd16(f + 10);
            ULONG length = rd16(f + 12) >> 2;
            if (segment_at(lib, SEG_IMPFILES, rd32(p + 4) + 14, length))
                imp->File = text_of_bytes(lib, (const char *)f + 14, length);
        }
    }
    return S_OK;
}

HRESULT TlReadImage(const BYTE *image, SIZE_T size, SYSKIND target, void (*release)(void *, const BYTE *),
                    void *context, TL_LIB **result)
{
    *result = nullptr;
    if (size < 0x54 + 15 * 16)
        return TYPE_E_INVDATAREAD;
    if (rd32(image) != MSFT_MAGIC1)
        return TYPE_E_UNSUPFORMAT;
    if (rd32(image + 4) != MSFT_MAGIC2)
        return TYPE_E_UNSUPFORMAT;
    TL_LIB *lib = TlLibCreate();
    if (!lib)
        return E_OUTOFMEMORY;
    lib->Image = image;
    lib->ImageSize = size;
    lib->ReleaseImage = release;
    lib->ImageContext = context;
    ULONG varflags = rd32(image + 0x14);
    lib->FileSyskind = (SYSKIND)(varflags & 3);
    lib->Syskind = lib->FileSyskind;
    if (target == SYS_WIN32 || target == SYS_WIN64)
        lib->Syskind = target;
    lib->Lcid = rd32(image + 0x0C);
    lib->Major = rd16(image + 0x18);
    lib->Minor = rd16(image + 0x1A);
    lib->Flags = (WORD)rd32(image + 0x1C);
    ULONG count = rd32(image + 0x20);
    lib->HelpContext = rd32(image + 0x2C);
    lib->HelpStringContext = rd32(image + 0x28);
    UINT cp = 0;
    if (lib->Lcid && GetLocaleInfoW(lib->Lcid, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, (WCHAR *)&cp,
                                    sizeof(cp) / sizeof(WCHAR)) && cp)
        lib->CodePage = cp;
    else
        lib->CodePage = CP_ACP;

    ULONG pos = 0x54;
    ULONG help_string_dll = 0xFFFFFFFF;
    if (varflags & 0x100) {
        help_string_dll = rd32(image + pos);
        pos += 4;
    }
    if (count > size / 4 || pos + (ULONGLONG)count * 4 + 15 * 16 > size) {
        TlLibDestroy(lib);
        return TYPE_E_INVDATAREAD;
    }
    lib->TypeOffsets = pos;
    pos += count * 4;
    for (int i = 0; i < 15; i++) {
        const BYTE *s = image + pos + i * 16;
        ULONG offset = rd32(s), length = rd32(s + 4);
        if (offset == 0xFFFFFFFF || !length) {
            lib->Seg[i].Offset = 0;
            lib->Seg[i].Length = 0;
            continue;
        }
        if (offset > size || length > size - offset) {
            TlLibDestroy(lib);
            return TYPE_E_INVDATAREAD;
        }
        lib->Seg[i].Offset = offset;
        lib->Seg[i].Length = length;
    }
    GUID guid;
    if (TlGuidAt(lib, rd32(image + 0x08), &guid)) {
        lib->Guid = guid;
        lib->HasGuid = TRUE;
    }
    lib->Name = TlNameAt(lib, rd32(image + 0x38));
    lib->Doc = TlStringAt(lib, rd32(image + 0x24));
    lib->HelpFile = TlStringAt(lib, rd32(image + 0x3C));
    lib->HelpStringDll = TlStringAt(lib, help_string_dll);
    lib->Cust = read_custom(lib, rd32(image + 0x40));
    lib->DispatchRef = href_from_file(lib, rd32(image + 0x4C));
    HRESULT hr = read_imports(lib);
    if (FAILED(hr)) {
        TlLibDestroy(lib);
        return hr;
    }
    lib->Types = TlAlloc(lib, (SIZE_T)(count ? count : 1) * sizeof(TL_TYPE *));
    lib->TypeCapacity = count;
    if (!lib->Types) {
        TlLibDestroy(lib);
        return E_OUTOFMEMORY;
    }
    for (ULONG i = 0; i < count; i++) {
        ULONG offset = rd32(image + lib->TypeOffsets + i * 4);
        const BYTE *b = segment_at(lib, SEG_TYPEINFO, offset, TYPE_RECORD_SIZE);
        if (!b || offset % 4) {
            TlLibDestroy(lib);
            return TYPE_E_INVDATAREAD;
        }
        TL_TYPE *type = TlTypeAdd(lib);
        if (!type) {
            TlLibDestroy(lib);
            return E_OUTOFMEMORY;
        }
        type->FileOffset = offset;
        ULONG kind_word = rd32(b);
        type->Kind = (TYPEKIND)(kind_word & 15);
        type->Alignment = (USHORT)((kind_word >> 11) & 0x1F);
        type->GuidOff = rd32(b + 44);
        type->Flags = (USHORT)rd32(b + 48);
        type->NameOff = rd32(b + 52);
        type->Major = rd16(b + 56);
        type->Minor = rd16(b + 58);
        type->DocOff = rd32(b + 60);
        type->HelpStringContext = rd32(b + 64);
        type->HelpContext = rd32(b + 68);
        type->CustOff = rd32(b + 72);
        type->ImplCount = rd16(b + 76);
        type->SizeVft = rd16(b + 78);
        type->SizeInstance = rd32(b + 80);
        type->FuncCount = rd16(b + 24);
        type->VarCount = rd16(b + 26);
        ULONG d1 = rd32(b + 84), d2 = rd32(b + 88);
        if (type->Kind == TKIND_INTERFACE || type->Kind == TKIND_DISPATCH) {
            type->InheritedImpls = (USHORT)(d2 & 0xFFFF);
            type->InheritedFuncs = (USHORT)(d2 >> 16);
        }
        (void)d1;
        if (TlGuidAt(lib, type->GuidOff, &type->Guid))
            type->HasGuid = TRUE;
    }
    *result = lib;
    return S_OK;
}

/* the members of a type, decoded when they are first needed. */
HRESULT TlLoadMembers(TL_TYPE *type)
{
    TL_LIB *lib = type->Lib;
    if (type->Loaded)
        return S_OK;
    EnterCriticalSection(&lib->Lock);
    HRESULT hr = S_OK;
    if (type->Loaded)
        goto done;
    if (!lib->Image) {
        type->Loaded = TRUE;
        goto done;
    }
    {
        const BYTE *b = segment_at(lib, SEG_TYPEINFO, type->FileOffset, TYPE_RECORD_SIZE);
        ULONG memoff = rd32(b + 4);
        ULONG d1 = rd32(b + 84);
        USHORT cf = type->FuncCount, cv = type->VarCount;
        /* the references the type holds, whatever it is. */
        if (type->ImplCount) {
            type->Impls = TlAlloc(lib, (SIZE_T)type->ImplCount * sizeof(TL_IMPL));
            if (!type->Impls) {
                hr = E_OUTOFMEMORY;
                goto done;
            }
            if (type->Kind == TKIND_COCLASS) {
                ULONG at = d1;
                USHORT n = 0;
                for (int guard = 0; at != 0xFFFFFFFF && n < type->ImplCount && guard < 4096; guard++) {
                    const BYTE *r = segment_at(lib, SEG_REFTAB, at, 16);
                    if (!r)
                        break;
                    type->Impls[n].Ref = href_from_file(lib, rd32(r) & ~(ULONG)2);
                    type->Impls[n].Flags = (INT)rd32(r + 4);
                    type->Impls[n].Cust = read_custom(lib, rd32(r + 8));
                    n++;
                    at = rd32(r + 12);
                }
                type->ImplCount = n;
            } else {
                type->Impls[0].Ref = d1 != 0xFFFFFFFF ? href_from_file(lib, d1) : lib->DispatchRef;
                if (type->Kind == TKIND_DISPATCH && d1 == 0xFFFFFFFF)
                    type->Impls[0].Ref = lib->DispatchRef;
            }
        }
        if (type->Kind == TKIND_MODULE)
            type->DllName = TlStringAt(lib, d1);
        if (type->Kind == TKIND_ALIAS) {
            hr = read_typedesc(lib, (LONG)d1, &type->Alias, 0);
            if (FAILED(hr))
                goto done;
        }
        if (!cf && !cv) {
            type->Loaded = TRUE;
            goto done;
        }
        const BYTE *mem = image_at(lib, memoff, 4);
        if (!mem) {
            hr = TYPE_E_INVDATAREAD;
            goto done;
        }
        ULONG infolen = rd32(mem);
        ULONG total = (ULONG)cf + cv;
        const BYTE *recs = image_at(lib, (ULONGLONG)memoff + 4, (ULONGLONG)infolen + (ULONGLONG)total * 12);
        if (!recs) {
            hr = TYPE_E_INVDATAREAD;
            goto done;
        }
        const BYTE *memids = recs + infolen, *names = memids + total * 4, *offsets = names + total * 4;
        type->Funcs = TlAlloc(lib, (SIZE_T)(cf ? cf : 1) * sizeof(TL_FUNC));
        type->Vars = TlAlloc(lib, (SIZE_T)(cv ? cv : 1) * sizeof(TL_VAR));
        if (!type->Funcs || !type->Vars) {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        for (ULONG k = 0; k < total; k++) {
            ULONG at = rd32(offsets + k * 4);
            if (at > infolen || infolen - at < 20) {
                hr = TYPE_E_INVDATAREAD;
                goto done;
            }
            const BYTE *r = recs + at;
            ULONG size = rd16(r);
            if (size < 20 || size > infolen - at) {
                hr = TYPE_E_INVDATAREAD;
                goto done;
            }
            if (k < cf) {
                TL_FUNC *f = &type->Funcs[k];
                if (size < 24) {
                    hr = TYPE_E_INVDATAREAD;
                    goto done;
                }
                ULONG fk = rd32(r + 16);
                USHORT nargs = rd16(r + 20);
                BOOL defaults = (fk & 0x1000) != 0;
                ULONG fixed = 24 + (ULONG)nargs * (defaults ? 16 : 12);
                if (size < fixed) {
                    hr = TYPE_E_INVDATAREAD;
                    goto done;
                }
                f->Id = (MEMBERID)rd32(memids + k * 4);
                f->Kind = (FUNCKIND)(fk & 7);
                f->Invoke = (INVOKEKIND)((fk >> 3) & 15);
                f->Conv = (CALLCONV)((fk >> 8) & 15);
                f->Flags = rd16(r + 8);
                f->VtOffset = (SHORT)(rd16(r + 12) & 0xFFFC);
                f->VtOffsetSet = (rd16(r + 12) & 1) != 0 || TRUE;
                f->ParamCount = nargs;
                f->OptCount = (SHORT)rd16(r + 22);
                f->Name = TlNameAt(lib, rd32(names + k * 4));
                hr = read_typedesc(lib, (LONG)rd32(r + 4), &f->Ret, 0);
                if (FAILED(hr))
                    goto done;
                ULONG slots = size - fixed;
                #define SLOT(off) (slots > (off) - 24 ? rd32(r + (off)) : 0xFFFFFFFF)
                f->HelpContext = slots > 0 ? rd32(r + 24) : 0;
                f->Doc = TlStringAt(lib, SLOT(28));
                if (fk & 0x2000) {
                    f->HasOrdinal = TRUE;
                    f->Ordinal = slots > 8 ? rd16(r + 32) : 0;
                } else {
                    f->EntryName = TlNameAt(lib, SLOT(32));
                    if (!f->EntryName)
                        f->EntryName = TlStringAt(lib, SLOT(32));
                }
                f->HelpFile = TlStringAt(lib, SLOT(36));
                f->DllName = TlStringAt(lib, SLOT(40));
                f->HelpStringContext = slots > 20 ? rd32(r + 44) : 0;
                if ((fk & 0x80) && slots >= 28)
                    f->Cust = read_custom(lib, rd32(r + 48));
                const BYTE *dflt = r + size - (ULONG)nargs * (defaults ? 16 : 12);
                const BYTE *params = r + size - (ULONG)nargs * 12;
                if (nargs) {
                    f->Params = TlAlloc(lib, (SIZE_T)nargs * sizeof(TL_PARAM));
                    if (!f->Params) {
                        hr = E_OUTOFMEMORY;
                        goto done;
                    }
                }
                for (USHORT j = 0; j < nargs; j++) {
                    TL_PARAM *prm = &f->Params[j];
                    hr = read_typedesc(lib, (LONG)rd32(params + j * 12), &prm->Type, 0);
                    if (FAILED(hr))
                        goto done;
                    prm->Name = TlNameAt(lib, rd32(params + j * 12 + 4));
                    prm->Flags = rd16(params + j * 12 + 8);
                    if ((prm->Flags & PARAMFLAG_FHASDEFAULT) && defaults) {
                        hr = const_value(lib, rd32(dflt + j * 4), &prm->Default);
                        if (FAILED(hr))
                            prm->Default.vt = VT_EMPTY, hr = S_OK;
                    }
                    if ((fk & 0x80) && slots >= 28 + 4 * (ULONG)(j + 2) - 4 + 0)
                        prm->Cust = nullptr;
                }
                if (fk & 0x80) {
                    for (USHORT j = 0; j < nargs; j++) {
                        ULONG off = 52 + 4 * (ULONG)j;
                        if (size - fixed > off - 24 + 3)
                            f->Params[j].Cust = read_custom(lib, rd32(r + off));
                    }
                }
            } else {
                TL_VAR *v = &type->Vars[k - cf];
                if (size < 20) {
                    hr = TYPE_E_INVDATAREAD;
                    goto done;
                }
                v->Id = (MEMBERID)rd32(memids + k * 4);
                v->Name = TlNameAt(lib, rd32(names + k * 4));
                v->Flags = rd16(r + 8);
                v->Kind = (VARKIND)rd16(r + 12);
                hr = read_typedesc(lib, (LONG)rd32(r + 4), &v->Type, 0);
                if (FAILED(hr))
                    goto done;
                ULONG value = rd32(r + 16);
                if (v->Kind == VAR_CONST) {
                    hr = const_value(lib, value, &v->Value);
                    if (FAILED(hr)) {
                        v->Value.vt = VT_EMPTY;
                        hr = S_OK;
                    }
                } else {
                    v->Offset = value;
                }
                v->HelpContext = size > 20 ? rd32(r + 20) : 0;
                v->Doc = TlStringAt(lib, size > 24 ? rd32(r + 24) : 0xFFFFFFFF);
                if (size > 32)
                    v->Cust = read_custom(lib, rd32(r + 32));
                v->HelpStringContext = size > 36 ? rd32(r + 36) : 0;
            }
        }
        #undef SLOT
        type->Loaded = TRUE;
    }
done:
    LeaveCriticalSection(&lib->Lock);
    return hr;
}
