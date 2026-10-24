/* winrt type signatures and namespace uuid version 5 identifiers. */
#include "combase_private.h"
#include "parameterized.h"
#include <mime/win32/winnls.h>
#include <crypto/crypto.h>

typedef struct MIME_TYPE_SIGNATURE {
    char *Data;
    SIZE_T Length, Capacity;
} MIME_TYPE_SIGNATURE;

typedef struct MIME_TYPE_PATH {
    const struct MIME_TYPE_PATH *Next;
    PCWSTR Name;
} MIME_TYPE_PATH;

typedef struct MIME_TYPE_CONTEXT {
    MIME_TYPE_SIGNATURE *Signature;
    const MIME_RO_LOCATOR *Locator;
    const MIME_TYPE_PATH *Path;
    ULONG Recursion, Nesting;
} MIME_TYPE_CONTEXT;

typedef struct MIME_TYPE_BUILDER {
    MIME_RO_BUILDER Interface;
    MIME_TYPE_CONTEXT *Context;
    BOOL Assigned, Generic;
    UINT32 Arguments;
} MIME_TYPE_BUILDER;

static const MIME_RO_BUILDER_VTBL builder_vtable;
static HRESULT write_type(MIME_TYPE_CONTEXT *context, UINT32 count, PCWSTR const *names, UINT32 *cursor);

static HRESULT reserve(MIME_TYPE_SIGNATURE *signature, SIZE_T extra)
{
    if (extra > SIZE_MAX - signature->Length - 1)
        return E_OUTOFMEMORY;
    SIZE_T need = signature->Length + extra + 1;
    if (need <= signature->Capacity)
        return S_OK;
    SIZE_T capacity = signature->Capacity ? signature->Capacity : 256;
    while (capacity < need) {
        if (capacity > SIZE_MAX / 2) {
            capacity = need;
            break;
        }
        capacity *= 2;
    }
    char *grown = CbAlloc(capacity);
    if (!grown)
        return E_OUTOFMEMORY;
    if (signature->Length)
        memcpy(grown, signature->Data, signature->Length);
    grown[signature->Length] = 0;
    CbFree(signature->Data);
    signature->Data = grown;
    signature->Capacity = capacity;
    return S_OK;
}

static HRESULT write_text(MIME_TYPE_CONTEXT *context, const char *text)
{
    MIME_TYPE_SIGNATURE *s = context->Signature;
    SIZE_T length = strlen(text);
    HRESULT hr = reserve(s, length);
    if (SUCCEEDED(hr)) {
        memcpy(s->Data + s->Length, text, length + 1);
        s->Length += length;
    }
    return hr;
}

static HRESULT write_name(MIME_TYPE_CONTEXT *context, PCWSTR name)
{
    if (!name || !*name)
        return E_INVALIDARG;
    int length = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
    if (!length)
        return HRESULT_FROM_WIN32(GetLastError());
    MIME_TYPE_SIGNATURE *s = context->Signature;
    HRESULT hr = reserve(s, (SIZE_T)length - 1);
    if (FAILED(hr))
        return hr;
    if (!WideCharToMultiByte(CP_UTF8, 0, name, -1, s->Data + s->Length, length, nullptr, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());
    s->Length += (SIZE_T)length - 1;
    return S_OK;
}

static HRESULT write_guid(MIME_TYPE_CONTEXT *context, const GUID *guid)
{
    char text[39];
    _snprintf(text, sizeof(text), "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
              guid->Data1, guid->Data2, guid->Data3, guid->Data4[0], guid->Data4[1], guid->Data4[2],
              guid->Data4[3], guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
    return write_text(context, text);
}

static HRESULT assign_type(MIME_TYPE_BUILDER *builder)
{
    if (builder->Assigned)
        return E_INVALIDARG;
    builder->Assigned = TRUE;
    return S_OK;
}

static HRESULT WINAPI set_interface(MIME_RO_BUILDER *self, GUID iid)
{
    MIME_TYPE_BUILDER *b = (void *)self;
    HRESULT hr = assign_type(b);
    return FAILED(hr) ? hr : write_guid(b->Context, &iid);
}

static HRESULT WINAPI set_delegate(MIME_RO_BUILDER *self, GUID iid)
{
    MIME_TYPE_BUILDER *b = (void *)self;
    HRESULT hr = assign_type(b);
    if (SUCCEEDED(hr)) hr = write_text(b->Context, "delegate(");
    if (SUCCEEDED(hr)) hr = write_guid(b->Context, &iid);
    if (SUCCEEDED(hr)) hr = write_text(b->Context, ")");
    return hr;
}

static HRESULT WINAPI set_parameterized(MIME_RO_BUILDER *self, GUID piid, UINT32 arguments)
{
    MIME_TYPE_BUILDER *b = (void *)self;
    HRESULT hr = assign_type(b);
    if (SUCCEEDED(hr)) hr = write_text(b->Context, "pinterface(");
    if (SUCCEEDED(hr)) hr = write_guid(b->Context, &piid);
    if (SUCCEEDED(hr)) {
        b->Generic = TRUE;
        b->Arguments = arguments;
    }
    return hr;
}

static HRESULT write_arguments(MIME_TYPE_CONTEXT *context, UINT32 count, PCWSTR const *names, BOOL single)
{
    if (!count)
        return E_UNEXPECTED;
    if (!names)
        return E_INVALIDARG;
    UINT32 cursor = 0, types = 0;
    HRESULT hr = S_OK;
    context->Nesting++;
    while (cursor < count && SUCCEEDED(hr)) {
        if (types++)
            hr = write_text(context, ";");
        if (SUCCEEDED(hr))
            hr = write_type(context, count, names, &cursor);
    }
    context->Nesting--;
    if (SUCCEEDED(hr) && single && types != 1)
        hr = E_INVALIDARG;
    return hr;
}

static HRESULT named_arguments(MIME_TYPE_BUILDER *b, const char *prefix, PCWSTR name, UINT32 count,
                                PCWSTR const *names, BOOL single, BOOL cycle_check)
{
    HRESULT hr = assign_type(b);
    MIME_TYPE_CONTEXT *context = b->Context;
    if (SUCCEEDED(hr)) hr = write_text(context, prefix);
    if (SUCCEEDED(hr)) hr = write_name(context, name);
    if (SUCCEEDED(hr)) hr = write_text(context, ";");
    if (FAILED(hr))
        return hr;
    if (cycle_check) {
        for (const MIME_TYPE_PATH *p = context->Path; p; p = p->Next)
            if (!wcscmp(p->Name, name))
                return write_text(context, "*)");
    }
    MIME_TYPE_PATH path = {context->Path, name};
    if (cycle_check)
        context->Path = &path;
    hr = write_arguments(context, count, names, single);
    if (cycle_check)
        context->Path = path.Next;
    if (SUCCEEDED(hr)) hr = write_text(context, ")");
    return hr;
}

static HRESULT simple_default(MIME_TYPE_BUILDER *b, const char *prefix, PCWSTR name, PCWSTR interface_name,
                               const GUID *iid)
{
    HRESULT hr = assign_type(b);
    if (SUCCEEDED(hr)) hr = write_text(b->Context, prefix);
    if (SUCCEEDED(hr)) hr = write_name(b->Context, name);
    if (SUCCEEDED(hr)) hr = write_text(b->Context, ";");
    if (SUCCEEDED(hr)) {
        if (iid)
            hr = write_guid(b->Context, iid);
        else
            hr = write_arguments(b->Context, 1, &interface_name, TRUE);
    }
    if (SUCCEEDED(hr)) hr = write_text(b->Context, ")");
    return hr;
}

static HRESULT WINAPI set_group(MIME_RO_BUILDER *self, PCWSTR name, PCWSTR interface_name, const GUID *iid)
{
    return simple_default((void *)self, "ig(", name, interface_name, iid);
}

static HRESULT WINAPI set_class(MIME_RO_BUILDER *self, PCWSTR name, PCWSTR interface_name, const GUID *iid)
{
    return simple_default((void *)self, "rc(", name, interface_name, iid);
}

static HRESULT WINAPI set_generic_group(MIME_RO_BUILDER *self, PCWSTR name, UINT32 count, PCWSTR const *names)
{
    return named_arguments((void *)self, "ig(", name, count, names, TRUE, TRUE);
}

static HRESULT WINAPI set_generic_class(MIME_RO_BUILDER *self, PCWSTR name, UINT32 count, PCWSTR const *names)
{
    return named_arguments((void *)self, "rc(", name, count, names, TRUE, TRUE);
}

static HRESULT WINAPI set_struct(MIME_RO_BUILDER *self, PCWSTR name, UINT32 count, PCWSTR const *names)
{
    return named_arguments((void *)self, "struct(", name, count, names, FALSE, FALSE);
}

static HRESULT WINAPI set_enum(MIME_RO_BUILDER *self, PCWSTR name, PCWSTR base)
{
    return named_arguments((void *)self, "enum(", name, 1, &base, TRUE, FALSE);
}

static const MIME_RO_BUILDER_VTBL builder_vtable = {
    set_interface, set_delegate, set_group, set_generic_group, set_class, set_generic_class,
    set_struct, set_enum, set_parameterized, set_parameterized,
};

static HRESULT write_type(MIME_TYPE_CONTEXT *context, UINT32 count, PCWSTR const *names, UINT32 *cursor)
{
    if (*cursor >= count || !names[*cursor])
        return E_INVALIDARG;
    PCWSTR name = names[(*cursor)++];
    static const struct {
        PCWSTR Name;
        const char *Signature;
    } builtins[] = {
        {L"UInt8", "u1"}, {L"Int16", "i2"}, {L"UInt16", "u2"}, {L"Int32", "i4"},
        {L"UInt32", "u4"}, {L"Int64", "i8"}, {L"UInt64", "u8"}, {L"Single", "f4"},
        {L"Double", "f8"}, {L"Boolean", "b1"}, {L"Char16", "c2"}, {L"String", "string"},
        {L"Guid", "g16"}, {L"Object", "cinterface(IInspectable)"},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(builtins); i++)
        if (!wcscmp(name, builtins[i].Name))
            return write_text(context, builtins[i].Signature);
    if (!context->Locator || !context->Locator->lpVtbl || !context->Locator->lpVtbl->Locate)
        return E_INVALIDARG;
    if (context->Recursion + context->Nesting >= 64)
        return E_UNEXPECTED;
    MIME_TYPE_BUILDER builder = {{&builder_vtable}, context};
    context->Recursion++;
    HRESULT hr = context->Locator->lpVtbl->Locate(context->Locator, name, &builder.Interface);
    context->Recursion--;
    if (FAILED(hr))
        return hr;
    if (!builder.Assigned)
        return E_INVALIDARG;
    if (builder.Generic) {
        context->Nesting++;
        for (UINT32 i = 0; i < builder.Arguments && SUCCEEDED(hr); i++) {
            hr = write_text(context, ";");
            if (SUCCEEDED(hr)) hr = write_type(context, count, names, cursor);
        }
        context->Nesting--;
        if (SUCCEEDED(hr)) hr = write_text(context, ")");
    }
    return hr;
}

ROAPI HRESULT WINAPI RoGetParameterizedTypeInstanceIID(UINT32 count, PCWSTR const *names,
                                                       const MIME_RO_LOCATOR *locator, GUID *iid, HANDLE *extra)
{
    if (!iid)
        return E_POINTER;
    memset(iid, 0, sizeof(*iid));
    if (extra)
        *extra = nullptr;
    MIME_TYPE_SIGNATURE local = {}, *signature = &local;
    if (extra) {
        signature = CbAlloc(sizeof(*signature));
        if (!signature)
            return E_OUTOFMEMORY;
        memset(signature, 0, sizeof(*signature));
        *extra = signature;
    }
    MIME_TYPE_CONTEXT context = {.Signature = signature, .Locator = locator};
    HRESULT hr = reserve(signature, 0);
    if (SUCCEEDED(hr)) {
        if (!count)
            hr = E_UNEXPECTED;
        else if (!names)
            hr = E_INVALIDARG;
        else {
            UINT32 cursor = 0;
            hr = write_type(&context, count, names, &cursor);
            if (SUCCEEDED(hr) && cursor != count)
                hr = E_INVALIDARG;
        }
    }
    if (SUCCEEDED(hr)) {
        static const UCHAR namespace_bytes[16] = {
            0x11, 0xf4, 0x7a, 0xd5, 0x7b, 0x73, 0x42, 0xc0,
            0xab, 0xae, 0x87, 0x8b, 0x1e, 0x16, 0xad, 0xee,
        };
        MimeHashCtx hash;
        UCHAR digest[20];
        mime_sha1.init(&hash);
        mime_sha1.update(&hash, namespace_bytes, sizeof(namespace_bytes));
        mime_sha1.update(&hash, signature->Data, signature->Length);
        mime_sha1.final(&hash, digest);
        iid->Data1 = (ULONG)digest[0] << 24 | (ULONG)digest[1] << 16 | (ULONG)digest[2] << 8 | digest[3];
        iid->Data2 = (USHORT)digest[4] << 8 | digest[5];
        iid->Data3 = (((USHORT)digest[6] << 8 | digest[7]) & 0x0fff) | 0x5000;
        memcpy(iid->Data4, digest + 8, sizeof(iid->Data4));
        iid->Data4[0] = (iid->Data4[0] & 0x3f) | 0x80;
    }
    if (!extra)
        CbFree(signature->Data);
    return hr;
}

ROAPI void WINAPI RoFreeParameterizedTypeExtra(HANDLE extra)
{
    MIME_TYPE_SIGNATURE *signature = extra;
    if (signature) {
        CbFree(signature->Data);
        CbFree(signature);
    }
}

ROAPI PCSTR WINAPI RoParameterizedTypeExtraGetTypeSignature(HANDLE extra)
{
    const MIME_TYPE_SIGNATURE *signature = extra;
    return signature ? signature->Data : nullptr;
}
