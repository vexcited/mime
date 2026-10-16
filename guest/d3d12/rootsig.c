/*
 * root signatures: their serialized form, a dxbc container with an RTS0
 * part as D3D12Core.dll writes it (see docs/d3d12.md), their descriptions
 * in each version, and the root signature object: where each parameter
 * lies in the push constants and what the compiler finds through them.
 */
#include "d3d12_private.h"

#define TAG_RTS0 0x30535452

/*
 * a description in its widest form, version 1.2's structures, with the
 * version it came in: what parsing, conversion and serialization share.
 */
typedef struct RootDesc {
    D3D_ROOT_SIGNATURE_VERSION version;
    D3D12_ROOT_SIGNATURE_FLAGS flags;
    UINT param_count, sampler_count, range_count;
    D3D12_ROOT_PARAMETER1 *params;
    D3D12_DESCRIPTOR_RANGE1 *ranges;
    D3D12_STATIC_SAMPLER_DESC1 *samplers;
} RootDesc;

static void root_desc_free(RootDesc *d)
{
    HeapFree(GetProcessHeap(), 0, d->params);
    HeapFree(GetProcessHeap(), 0, d->ranges);
    HeapFree(GetProcessHeap(), 0, d->samplers);
}

static HRESULT root_desc_alloc(RootDesc *d)
{
    d->params = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (d->param_count + 1) * sizeof(*d->params));
    d->ranges = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (d->range_count + 1) * sizeof(*d->ranges));
    d->samplers = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (d->sampler_count + 1) * sizeof(*d->samplers));
    if (!d->params || !d->ranges || !d->samplers) {
        root_desc_free(d);
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

/* version 1.0 says nothing of volatility: its descriptors and data are volatile, samplers' descriptors alone. */
static D3D12_DESCRIPTOR_RANGE_FLAGS default_range_flags(D3D12_DESCRIPTOR_RANGE_TYPE type)
{
    if (type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
        return D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    return D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
}

static HRESULT root_desc_from(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *v, RootDesc *d)
{
    memset(d, 0, sizeof(*d));
    d->version = v->Version;
    if (v->Version < D3D_ROOT_SIGNATURE_VERSION_1_0 || v->Version > D3D_ROOT_SIGNATURE_VERSION_1_2)
        return E_INVALIDARG;
    BOOL v10 = v->Version == D3D_ROOT_SIGNATURE_VERSION_1_0;
    /* the three versions' descriptions share their layout but for the structures they point to. */
    d->flags = v->Desc_1_0.Flags;
    d->param_count = v->Desc_1_0.NumParameters;
    d->sampler_count = v->Desc_1_0.NumStaticSamplers;
    if ((d->param_count && !v->Desc_1_0.pParameters) || (d->sampler_count && !v->Desc_1_0.pStaticSamplers))
        return E_INVALIDARG;
    for (UINT i = 0; i < d->param_count; i++) {
        D3D12_ROOT_PARAMETER_TYPE type = v10 ? v->Desc_1_0.pParameters[i].ParameterType
                                             : v->Desc_1_1.pParameters[i].ParameterType;
        if (type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            d->range_count += v10 ? v->Desc_1_0.pParameters[i].DescriptorTable.NumDescriptorRanges
                                  : v->Desc_1_1.pParameters[i].DescriptorTable.NumDescriptorRanges;
    }
    HRESULT hr = root_desc_alloc(d);
    if (FAILED(hr))
        return hr;
    UINT range = 0;
    for (UINT i = 0; i < d->param_count; i++) {
        D3D12_ROOT_PARAMETER1 *p = &d->params[i];
        if (v10) {
            const D3D12_ROOT_PARAMETER *s = &v->Desc_1_0.pParameters[i];
            p->ParameterType = s->ParameterType;
            p->ShaderVisibility = s->ShaderVisibility;
            switch (s->ParameterType) {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                p->DescriptorTable.NumDescriptorRanges = s->DescriptorTable.NumDescriptorRanges;
                p->DescriptorTable.pDescriptorRanges = &d->ranges[range];
                for (UINT j = 0; j < s->DescriptorTable.NumDescriptorRanges; j++, range++) {
                    const D3D12_DESCRIPTOR_RANGE *r = &s->DescriptorTable.pDescriptorRanges[j];
                    d->ranges[range] = (D3D12_DESCRIPTOR_RANGE1){r->RangeType, r->NumDescriptors,
                                                                 r->BaseShaderRegister, r->RegisterSpace,
                                                                 default_range_flags(r->RangeType),
                                                                 r->OffsetInDescriptorsFromTableStart};
                }
                break;
            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                p->Constants = s->Constants;
                break;
            default:
                p->Descriptor.ShaderRegister = s->Descriptor.ShaderRegister;
                p->Descriptor.RegisterSpace = s->Descriptor.RegisterSpace;
                p->Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
                break;
            }
        } else {
            const D3D12_ROOT_PARAMETER1 *s = &v->Desc_1_1.pParameters[i];
            *p = *s;
            if (s->ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
                p->DescriptorTable.pDescriptorRanges = &d->ranges[range];
                for (UINT j = 0; j < s->DescriptorTable.NumDescriptorRanges; j++)
                    d->ranges[range++] = s->DescriptorTable.pDescriptorRanges[j];
            }
        }
        if (p->ParameterType > D3D12_ROOT_PARAMETER_TYPE_UAV ||
            (p->ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE && p->DescriptorTable.NumDescriptorRanges &&
             !(v10 ? (const void *)v->Desc_1_0.pParameters[i].DescriptorTable.pDescriptorRanges
                   : (const void *)v->Desc_1_1.pParameters[i].DescriptorTable.pDescriptorRanges))) {
            root_desc_free(d);
            return E_INVALIDARG;
        }
    }
    for (UINT i = 0; i < d->sampler_count; i++) {
        if (v->Version == D3D_ROOT_SIGNATURE_VERSION_1_2)
            d->samplers[i] = v->Desc_1_2.pStaticSamplers[i];
        else
            memcpy(&d->samplers[i], &v->Desc_1_0.pStaticSamplers[i], sizeof(D3D12_STATIC_SAMPLER_DESC));
    }
    return S_OK;
}

/* the description at a version, in one block: the description, its parameters, ranges and samplers. */
static HRESULT root_desc_to(const RootDesc *d, D3D_ROOT_SIGNATURE_VERSION version,
                            D3D12_VERSIONED_ROOT_SIGNATURE_DESC **out)
{
    BOOL v10 = version == D3D_ROOT_SIGNATURE_VERSION_1_0;
    SIZE_T param_size = v10 ? sizeof(D3D12_ROOT_PARAMETER) : sizeof(D3D12_ROOT_PARAMETER1);
    SIZE_T range_size = v10 ? sizeof(D3D12_DESCRIPTOR_RANGE) : sizeof(D3D12_DESCRIPTOR_RANGE1);
    SIZE_T sampler_size = version == D3D_ROOT_SIGNATURE_VERSION_1_2 ? sizeof(D3D12_STATIC_SAMPLER_DESC1)
                                                                   : sizeof(D3D12_STATIC_SAMPLER_DESC);
    SIZE_T size = D12_ALIGN(sizeof(D3D12_VERSIONED_ROOT_SIGNATURE_DESC), 8) + d->param_count * param_size +
                  d->range_count * range_size + d->sampler_count * sampler_size;
    BYTE *block = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!block)
        return E_OUTOFMEMORY;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC *v = (void *)block;
    BYTE *params = block + D12_ALIGN(sizeof(*v), 8);
    BYTE *ranges = params + d->param_count * param_size;
    BYTE *samplers = ranges + d->range_count * range_size;
    v->Version = version;
    v->Desc_1_0.NumParameters = d->param_count;
    v->Desc_1_0.pParameters = d->param_count ? (void *)params : NULL;
    v->Desc_1_0.NumStaticSamplers = d->sampler_count;
    v->Desc_1_0.pStaticSamplers = d->sampler_count ? (void *)samplers : NULL;
    v->Desc_1_0.Flags = d->flags;
    UINT range = 0;
    for (UINT i = 0; i < d->param_count; i++) {
        const D3D12_ROOT_PARAMETER1 *s = &d->params[i];
        if (!v10) {
            D3D12_ROOT_PARAMETER1 *p = (D3D12_ROOT_PARAMETER1 *)params + i;
            *p = *s;
            if (s->ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
                D3D12_DESCRIPTOR_RANGE1 *r = (D3D12_DESCRIPTOR_RANGE1 *)ranges + range;
                memcpy(r, s->DescriptorTable.pDescriptorRanges, s->DescriptorTable.NumDescriptorRanges * sizeof(*r));
                p->DescriptorTable.pDescriptorRanges = s->DescriptorTable.NumDescriptorRanges ? r : NULL;
                range += s->DescriptorTable.NumDescriptorRanges;
            }
            continue;
        }
        D3D12_ROOT_PARAMETER *p = (D3D12_ROOT_PARAMETER *)params + i;
        p->ParameterType = s->ParameterType;
        p->ShaderVisibility = s->ShaderVisibility;
        switch (s->ParameterType) {
        case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
            D3D12_DESCRIPTOR_RANGE *r = (D3D12_DESCRIPTOR_RANGE *)ranges + range;
            for (UINT j = 0; j < s->DescriptorTable.NumDescriptorRanges; j++) {
                const D3D12_DESCRIPTOR_RANGE1 *f = &s->DescriptorTable.pDescriptorRanges[j];
                r[j] = (D3D12_DESCRIPTOR_RANGE){f->RangeType, f->NumDescriptors, f->BaseShaderRegister,
                                                f->RegisterSpace, f->OffsetInDescriptorsFromTableStart};
            }
            p->DescriptorTable.NumDescriptorRanges = s->DescriptorTable.NumDescriptorRanges;
            p->DescriptorTable.pDescriptorRanges = s->DescriptorTable.NumDescriptorRanges ? r : NULL;
            range += s->DescriptorTable.NumDescriptorRanges;
            break;
        }
        case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            p->Constants = s->Constants;
            break;
        default:
            p->Descriptor.ShaderRegister = s->Descriptor.ShaderRegister;
            p->Descriptor.RegisterSpace = s->Descriptor.RegisterSpace;
            break;
        }
    }
    for (UINT i = 0; i < d->sampler_count; i++)
        memcpy(samplers + i * sampler_size, &d->samplers[i], sampler_size);
    *out = v;
    return S_OK;
}

HRESULT D12ConvertRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc, D3D_ROOT_SIGNATURE_VERSION version,
                                D3D12_VERSIONED_ROOT_SIGNATURE_DESC **out)
{
    RootDesc d;
    HRESULT hr = root_desc_from(desc, &d);
    if (FAILED(hr))
        return hr;
    hr = root_desc_to(&d, version, out);
    root_desc_free(&d);
    return hr;
}

/* ------------------------------------------------------------------ the RTS0 part */

static UINT read32(const BYTE *data, SIZE_T size, UINT offset, BOOL *ok)
{
    if ((SIZE_T)offset + 4 > size) {
        *ok = FALSE;
        return 0;
    }
    UINT v;
    memcpy(&v, data + offset, 4);
    return v;
}

static HRESULT parse_rts0(const BYTE *data, SIZE_T size, RootDesc *d)
{
    BOOL ok = TRUE;
    memset(d, 0, sizeof(*d));
    d->version = read32(data, size, 0, &ok);
    d->param_count = read32(data, size, 4, &ok);
    UINT param_offset = read32(data, size, 8, &ok);
    d->sampler_count = read32(data, size, 12, &ok);
    UINT sampler_offset = read32(data, size, 16, &ok);
    d->flags = read32(data, size, 20, &ok);
    if (!ok || d->version < D3D_ROOT_SIGNATURE_VERSION_1_0 || d->version > D3D_ROOT_SIGNATURE_VERSION_1_2 ||
        d->param_count > size / 12 || d->sampler_count > size / 52)
        return E_INVALIDARG;
    BOOL v10 = d->version == D3D_ROOT_SIGNATURE_VERSION_1_0;
    for (UINT i = 0; i < d->param_count; i++) {
        UINT at = param_offset + 12 * i;
        if (read32(data, size, at, &ok) == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            d->range_count += read32(data, size, read32(data, size, at + 8, &ok), &ok);
        if (!ok || d->range_count > size / 20)
            return E_INVALIDARG;
    }
    HRESULT hr = root_desc_alloc(d);
    if (FAILED(hr))
        return hr;
    UINT range = 0;
    for (UINT i = 0; i < d->param_count && ok; i++) {
        UINT at = param_offset + 12 * i;
        D3D12_ROOT_PARAMETER1 *p = &d->params[i];
        p->ParameterType = read32(data, size, at, &ok);
        p->ShaderVisibility = read32(data, size, at + 4, &ok);
        UINT payload = read32(data, size, at + 8, &ok);
        switch (p->ParameterType) {
        case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
            UINT count = read32(data, size, payload, &ok);
            UINT ranges = read32(data, size, payload + 4, &ok);
            p->DescriptorTable.NumDescriptorRanges = count;
            p->DescriptorTable.pDescriptorRanges = &d->ranges[range];
            for (UINT j = 0; j < count && ok; j++, range++) {
                UINT r = ranges + j * (v10 ? 20 : 24);
                D3D12_DESCRIPTOR_RANGE1 *out = &d->ranges[range];
                out->RangeType = read32(data, size, r, &ok);
                out->NumDescriptors = read32(data, size, r + 4, &ok);
                out->BaseShaderRegister = read32(data, size, r + 8, &ok);
                out->RegisterSpace = read32(data, size, r + 12, &ok);
                if (v10) {
                    out->Flags = default_range_flags(out->RangeType);
                    out->OffsetInDescriptorsFromTableStart = read32(data, size, r + 16, &ok);
                } else {
                    out->Flags = read32(data, size, r + 16, &ok);
                    out->OffsetInDescriptorsFromTableStart = read32(data, size, r + 20, &ok);
                }
            }
            break;
        }
        case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            p->Constants.ShaderRegister = read32(data, size, payload, &ok);
            p->Constants.RegisterSpace = read32(data, size, payload + 4, &ok);
            p->Constants.Num32BitValues = read32(data, size, payload + 8, &ok);
            break;
        case D3D12_ROOT_PARAMETER_TYPE_CBV:
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
            p->Descriptor.ShaderRegister = read32(data, size, payload, &ok);
            p->Descriptor.RegisterSpace = read32(data, size, payload + 4, &ok);
            p->Descriptor.Flags = v10 ? D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE : read32(data, size, payload + 8, &ok);
            break;
        default:
            ok = FALSE;
            break;
        }
    }
    UINT sampler_size = d->version == D3D_ROOT_SIGNATURE_VERSION_1_2 ? 56 : 52;
    for (UINT i = 0; i < d->sampler_count && ok; i++) {
        UINT at = sampler_offset + i * sampler_size;
        if ((SIZE_T)at + sampler_size > size)
            ok = FALSE;
        else
            memcpy(&d->samplers[i], data + at, sampler_size);
    }
    if (!ok) {
        root_desc_free(d);
        return E_INVALIDARG;
    }
    return S_OK;
}

/* the root signature of a serialized one or of a shader that carries it. */
static HRESULT parse_blob(const void *blob, SIZE_T size, RootDesc *d)
{
    if (!blob || size < 32 || dxbc_check(blob, size))
        return E_INVALIDARG;
    uint32_t part_size;
    const void *part = dxbc_find_chunk(blob, size, TAG_RTS0, &part_size);
    if (!part)
        return E_INVALIDARG;
    return parse_rts0(part, part_size, d);
}

HRESULT D12ParseRootSignature(const void *data, SIZE_T size, D3D12_VERSIONED_ROOT_SIGNATURE_DESC **out)
{
    RootDesc d;
    HRESULT hr = parse_blob(data, size, &d);
    if (FAILED(hr))
        return hr;
    hr = root_desc_to(&d, d.version, out);
    root_desc_free(&d);
    return hr;
}

/* the part's bytes, laid out as windows lays them: the header, the parameters, their payloads, the samplers. */
typedef struct Writer {
    BYTE *data;
    UINT size, capacity;
} Writer;

static UINT reserve(Writer *w, UINT size)
{
    UINT at = (w->size + 3) & ~3u;
    if (at + size > w->capacity) {
        UINT capacity = D12_MAX(w->capacity * 2, at + size + 256);
        BYTE *data = w->data ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, w->data, capacity)
                             : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity);
        if (!data)
            return ~0u;
        w->data = data;
        w->capacity = capacity;
    }
    w->size = at + size;
    return at;
}

static void put32(Writer *w, UINT at, UINT v) { memcpy(w->data + at, &v, 4); }

static HRESULT error_blob(ID3DBlob **error, const char *text)
{
    if (error)
        D12CreateBlob(text, strlen(text) + 1, error);
    return E_INVALIDARG;
}

/* checks what windows refuses to serialize. */
static HRESULT validate(const RootDesc *d, ID3DBlob **error)
{
    UINT cost = 0;
    for (UINT i = 0; i < d->param_count; i++) {
        const D3D12_ROOT_PARAMETER1 *p = &d->params[i];
        if (p->ShaderVisibility > D3D12_SHADER_VISIBILITY_MESH)
            return error_blob(error, "D3DSerializeRootSignature: invalid shader visibility.\n");
        switch (p->ParameterType) {
        case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
            cost += 1;
            BOOL sampler = FALSE, other = FALSE;
            for (UINT j = 0; j < p->DescriptorTable.NumDescriptorRanges; j++) {
                const D3D12_DESCRIPTOR_RANGE1 *r = &p->DescriptorTable.pDescriptorRanges[j];
                if (r->RangeType > D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
                    return error_blob(error, "D3DSerializeRootSignature: invalid descriptor range type.\n");
                if (!r->NumDescriptors)
                    return error_blob(error, "D3DSerializeRootSignature: a descriptor range is empty.\n");
                if (r->RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
                    sampler = TRUE;
                else
                    other = TRUE;
            }
            if (sampler && other)
                return error_blob(error,
                                  "D3DSerializeRootSignature: a descriptor table mixes samplers with other views.\n");
            break;
        }
        case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            cost += p->Constants.Num32BitValues;
            break;
        case D3D12_ROOT_PARAMETER_TYPE_CBV:
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
            cost += 2;
            break;
        default:
            return error_blob(error, "D3DSerializeRootSignature: unknown root parameter type.\n");
        }
    }
    if (cost > D3D12_MAX_ROOT_COST)
        return error_blob(error, "D3DSerializeRootSignature: the root signature takes more than 64 DWORDs.\n");
    return S_OK;
}

static HRESULT serialize(const RootDesc *d, Writer *w)
{
    BOOL v10 = d->version == D3D_ROOT_SIGNATURE_VERSION_1_0;
    UINT header = reserve(w, 24);
    UINT params = reserve(w, 12 * d->param_count);
    if (header == ~0u || params == ~0u)
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < d->param_count; i++) {
        const D3D12_ROOT_PARAMETER1 *p = &d->params[i];
        UINT at = params + 12 * i, payload;
        switch (p->ParameterType) {
        case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
            UINT count = p->DescriptorTable.NumDescriptorRanges, range_size = v10 ? 20 : 24;
            payload = reserve(w, 8);
            UINT ranges = payload == ~0u ? ~0u : reserve(w, range_size * count);
            if (ranges == ~0u)
                return E_OUTOFMEMORY;
            put32(w, payload, count);
            put32(w, payload + 4, ranges);
            for (UINT j = 0; j < count; j++) {
                const D3D12_DESCRIPTOR_RANGE1 *r = &p->DescriptorTable.pDescriptorRanges[j];
                UINT o = ranges + j * range_size;
                put32(w, o, r->RangeType);
                put32(w, o + 4, r->NumDescriptors);
                put32(w, o + 8, r->BaseShaderRegister);
                put32(w, o + 12, r->RegisterSpace);
                if (v10) {
                    put32(w, o + 16, r->OffsetInDescriptorsFromTableStart);
                } else {
                    put32(w, o + 16, r->Flags);
                    put32(w, o + 20, r->OffsetInDescriptorsFromTableStart);
                }
            }
            break;
        }
        case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            if ((payload = reserve(w, 12)) == ~0u)
                return E_OUTOFMEMORY;
            put32(w, payload, p->Constants.ShaderRegister);
            put32(w, payload + 4, p->Constants.RegisterSpace);
            put32(w, payload + 8, p->Constants.Num32BitValues);
            break;
        default:
            if ((payload = reserve(w, v10 ? 8 : 12)) == ~0u)
                return E_OUTOFMEMORY;
            put32(w, payload, p->Descriptor.ShaderRegister);
            put32(w, payload + 4, p->Descriptor.RegisterSpace);
            if (!v10)
                put32(w, payload + 8, p->Descriptor.Flags);
            break;
        }
        put32(w, at, p->ParameterType);
        put32(w, at + 4, p->ShaderVisibility);
        put32(w, at + 8, payload);
    }
    UINT sampler_size = d->version == D3D_ROOT_SIGNATURE_VERSION_1_2 ? 56 : 52;
    UINT samplers = reserve(w, sampler_size * d->sampler_count);
    if (samplers == ~0u)
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < d->sampler_count; i++)
        memcpy(w->data + samplers + i * sampler_size, &d->samplers[i], sampler_size);
    put32(w, header, d->version);
    put32(w, header + 4, d->param_count);
    put32(w, header + 8, params);
    put32(w, header + 12, d->sampler_count);
    put32(w, header + 16, samplers);
    put32(w, header + 20, d->flags);
    return S_OK;
}

HRESULT D12SerializeRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc, ID3DBlob **blob, ID3DBlob **error)
{
    if (blob)
        *blob = NULL;
    if (error)
        *error = NULL;
    if (!desc || !blob)
        return E_INVALIDARG;
    RootDesc d;
    HRESULT hr = root_desc_from(desc, &d);
    if (FAILED(hr))
        return hr;
    Writer w = {0};
    if (SUCCEEDED(hr = validate(&d, error)))
        hr = serialize(&d, &w);
    root_desc_free(&d);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, w.data);
        return hr;
    }
    /* the container: its header, the one part's offset, the part's tag and size, then the part. */
    UINT part_size = (w.size + 3) & ~3u, total = 32 + 4 + 8 + part_size;
    ID3DBlob *out;
    if (FAILED(hr = D12CreateBlob(NULL, total, &out))) {
        HeapFree(GetProcessHeap(), 0, w.data);
        return hr;
    }
    BYTE *p = out->lpVtbl->GetBufferPointer(out);
    memset(p, 0, total);
    UINT words[] = {0x43425844, 0, 0, 0, 0, 1, total, 1, 36, TAG_RTS0, part_size};
    memcpy(p, words, sizeof(words));
    memcpy(p + 44, w.data, w.size);
    HeapFree(GetProcessHeap(), 0, w.data);
    uint32_t checksum[4];
    dxbc_checksum(p, total, checksum);
    memcpy(p + 4, checksum, 16);
    *blob = out;
    return S_OK;
}

/* ------------------------------------------------------------------ the object */

D12RootSignature *D12RootSignatureFrom(ID3D12RootSignature *iface)
{
    return iface ? CONTAINING_RECORD(iface, D12RootSignature, iface) : NULL;
}

static D12RootSignature *impl(ID3D12RootSignature *iface) { return CONTAINING_RECORD(iface, D12RootSignature, iface); }

static HRESULT STDMETHODCALLTYPE root_query(ID3D12RootSignature *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Object) ||
        IsEqualGUID(riid, &IID_ID3D12DeviceChild) || IsEqualGUID(riid, &IID_ID3D12RootSignature)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE root_add_ref(ID3D12RootSignature *iface)
{
    return InterlockedIncrement(&impl(iface)->child.refs);
}

static ULONG STDMETHODCALLTYPE root_release(ID3D12RootSignature *iface)
{
    D12RootSignature *root = impl(iface);
    ULONG refs = InterlockedDecrement(&root->child.refs);
    if (!refs) {
        D12Device *device = root->child.device;
        D12Defer(device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (UINT64)root->layout);
        if (root->static_layout)
            D12Defer(device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (UINT64)root->static_layout);
        HeapFree(GetProcessHeap(), 0, root->params);
        HeapFree(GetProcessHeap(), 0, root->bindings);
        HeapFree(GetProcessHeap(), 0, root->binding_stages);
        HeapFree(GetProcessHeap(), 0, root->blob);
        D12ChildRelease(&root->child);
        HeapFree(GetProcessHeap(), 0, root);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE root_get_private_data(ID3D12RootSignature *iface, REFGUID guid, UINT *size,
                                                       void *data)
{
    return MimePrivateGet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE root_set_private_data(ID3D12RootSignature *iface, REFGUID guid, UINT size,
                                                       const void *data)
{
    return MimePrivateSet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE root_set_private_data_interface(ID3D12RootSignature *iface, REFGUID guid,
                                                                 const IUnknown *data)
{
    return MimePrivateSetInterface(&impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE root_set_name(ID3D12RootSignature *iface, LPCWSTR name) { return S_OK; }

static HRESULT STDMETHODCALLTYPE root_get_device(ID3D12RootSignature *iface, REFIID riid, void **device)
{
    return D12ChildGetDevice(&impl(iface)->child, riid, device);
}

static const struct ID3D12RootSignatureVtbl g_root_vtbl = {
    root_query,
    root_add_ref,
    root_release,
    root_get_private_data,
    root_set_private_data,
    root_set_private_data_interface,
    root_set_name,
    root_get_device,
};

/* the stages a visibility shows a parameter to, as the bits of D12Stage. */
static UINT visibility_stages(D3D12_SHADER_VISIBILITY visibility)
{
    switch (visibility) {
    case D3D12_SHADER_VISIBILITY_VERTEX:
        return 1u << D12_VS;
    case D3D12_SHADER_VISIBILITY_HULL:
        return 1u << D12_HS;
    case D3D12_SHADER_VISIBILITY_DOMAIN:
        return 1u << D12_DS;
    case D3D12_SHADER_VISIBILITY_GEOMETRY:
        return 1u << D12_GS;
    case D3D12_SHADER_VISIBILITY_PIXEL:
        return 1u << D12_PS;
    case D3D12_SHADER_VISIBILITY_AMPLIFICATION:
        return 1u << D12_AS;
    case D3D12_SHADER_VISIBILITY_MESH:
        return 1u << D12_MS;
    case D3D12_SHADER_VISIBILITY_ALL:
        return (1u << D12_STAGES) - 1;
    default:
        return 0;
    }
}

static const BYTE g_range_bind[] = {
    [D3D12_DESCRIPTOR_RANGE_TYPE_SRV] = DXBC_BIND_SRV,
    [D3D12_DESCRIPTOR_RANGE_TYPE_UAV] = DXBC_BIND_UAV,
    [D3D12_DESCRIPTOR_RANGE_TYPE_CBV] = DXBC_BIND_CBV,
    [D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER] = DXBC_BIND_SAMPLER,
};

/* the bindings of the root signature visible to a stage, for the compiler; the caller frees them. */
DxbcRootBinding *D12RootBindings(D12RootSignature *root, D12Stage stage, UINT *count)
{
    DxbcRootBinding *out = HeapAlloc(GetProcessHeap(), 0, (root->binding_count + 1) * sizeof(*out));
    *count = 0;
    if (!out)
        return NULL;
    for (UINT i = 0; i < root->binding_count; i++)
        if (root->binding_stages[i] & (1u << stage))
            out[(*count)++] = root->bindings[i];
    return out;
}

/* a static sampler as the sampler description it stands for. */
static void static_sampler_desc(const D3D12_STATIC_SAMPLER_DESC1 *s, D3D12_SAMPLER_DESC2 *d)
{
    static const FLOAT colors[][4] = {
        [D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK] = {0, 0, 0, 0},
        [D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK] = {0, 0, 0, 1},
        [D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE] = {1, 1, 1, 1},
        [D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT] = {0, 0, 0, 1},
        [D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE_UINT] = {1, 1, 1, 1},
    };
    memset(d, 0, sizeof(*d));
    d->Filter = s->Filter;
    d->AddressU = s->AddressU;
    d->AddressV = s->AddressV;
    d->AddressW = s->AddressW;
    d->MipLODBias = s->MipLODBias;
    d->MaxAnisotropy = s->MaxAnisotropy;
    d->ComparisonFunc = s->ComparisonFunc;
    UINT color = s->BorderColor <= D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE_UINT ? s->BorderColor : 0;
    if (color >= D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT) {
        for (UINT i = 0; i < 4; i++)
            d->UintBorderColor[i] = (UINT)colors[color][i];
        d->Flags = s->Flags | D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR;
    } else {
        memcpy(d->FloatBorderColor, colors[color], sizeof(d->FloatBorderColor));
        d->Flags = s->Flags;
    }
    d->MinLOD = s->MinLOD;
    d->MaxLOD = s->MaxLOD;
}

HRESULT D12CreateRootSignature(D12Device *device, const void *blob, SIZE_T size, D12RootSignature **out)
{
    D12Vk *vk = &device->vk;
    RootDesc d;
    HRESULT hr = parse_blob(blob, size, &d);
    if (FAILED(hr))
        return hr;
    D12RootSignature *root = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*root));
    UINT binding_capacity = d.range_count + d.param_count + d.sampler_count + 1;
    if (root) {
        root->params = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (d.param_count + 1) * sizeof(*root->params));
        root->bindings = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, binding_capacity * sizeof(*root->bindings));
        root->binding_stages =
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, binding_capacity * sizeof(*root->binding_stages));
        root->blob = HeapAlloc(GetProcessHeap(), 0, size);
    }
    if (!root || !root->params || !root->bindings || !root->binding_stages || !root->blob) {
        if (root) {
            HeapFree(GetProcessHeap(), 0, root->params);
            HeapFree(GetProcessHeap(), 0, root->bindings);
            HeapFree(GetProcessHeap(), 0, root->binding_stages);
            HeapFree(GetProcessHeap(), 0, root->blob);
            HeapFree(GetProcessHeap(), 0, root);
        }
        root_desc_free(&d);
        return E_OUTOFMEMORY;
    }
    root->iface.lpVtbl = &g_root_vtbl;
    memcpy(root->blob, blob, size);
    root->blob_size = size;
    root->flags = d.flags;
    root->param_count = d.param_count;

    /* each parameter's place in the push constants: a table's heap index, the constants, a buffer's address. */
    UINT push = 0;
    for (UINT i = 0; i < d.param_count; i++) {
        const D3D12_ROOT_PARAMETER1 *p = &d.params[i];
        D12RootParam *rp = &root->params[i];
        UINT stages = visibility_stages(p->ShaderVisibility);
        rp->type = p->ParameterType;
        rp->push_offset = push;
        switch (p->ParameterType) {
        case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
            UINT offset = 0;
            for (UINT j = 0; j < p->DescriptorTable.NumDescriptorRanges; j++) {
                const D3D12_DESCRIPTOR_RANGE1 *r = &p->DescriptorTable.pDescriptorRanges[j];
                if (r->OffsetInDescriptorsFromTableStart != D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
                    offset = r->OffsetInDescriptorsFromTableStart;
                DxbcRootBinding *b = &root->bindings[root->binding_count];
                b->type = g_range_bind[r->RangeType & 3];
                b->source = DXBC_SOURCE_TABLE;
                b->space = r->RegisterSpace;
                b->base = r->BaseShaderRegister;
                b->count = r->NumDescriptors == ~0u ? ~0u : r->NumDescriptors;
                b->push_offset = push;
                b->offset = offset;
                root->binding_stages[root->binding_count++] = stages;
                if (r->NumDescriptors != ~0u)
                    offset += r->NumDescriptors;
            }
            rp->constants = 1;
            push += 4;
            break;
        }
        case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: {
            DxbcRootBinding *b = &root->bindings[root->binding_count];
            b->type = DXBC_BIND_CBV;
            b->source = DXBC_SOURCE_CONSTANTS;
            b->space = p->Constants.RegisterSpace;
            b->base = p->Constants.ShaderRegister;
            b->count = 1;
            b->push_offset = push;
            root->binding_stages[root->binding_count++] = stages;
            rp->constants = p->Constants.Num32BitValues;
            push += 4 * p->Constants.Num32BitValues;
            break;
        }
        default: {
            DxbcRootBinding *b = &root->bindings[root->binding_count];
            b->type = p->ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV   ? DXBC_BIND_CBV
                      : p->ParameterType == D3D12_ROOT_PARAMETER_TYPE_SRV ? DXBC_BIND_SRV
                                                                          : DXBC_BIND_UAV;
            b->source = DXBC_SOURCE_ADDRESS;
            b->space = p->Descriptor.RegisterSpace;
            b->base = p->Descriptor.ShaderRegister;
            b->count = 1;
            b->push_offset = push;
            root->binding_stages[root->binding_count++] = stages;
            rp->constants = 2;
            push += 8;
            break;
        }
        }
    }
    root->push_size = push;
    if (push > D12_PUSH_SIZE) {
        D12Log("d3d12: a root signature takes %u bytes of push constants, more than the gpu has", push);
        hr = E_INVALIDARG;
    }

    /* static samplers are immutable samplers of their own set, embedded in the layout. */
    VkSampler samplers[64];
    UINT sampler_count = D12_MIN(d.sampler_count, 64u);
    for (UINT i = 0; i < sampler_count; i++) {
        const D3D12_STATIC_SAMPLER_DESC1 *s = &d.samplers[i];
        D3D12_SAMPLER_DESC2 desc;
        static_sampler_desc(s, &desc);
        samplers[i] = D12Sampler(device, &desc, NULL);
        DxbcRootBinding *b = &root->bindings[root->binding_count];
        b->type = DXBC_BIND_SAMPLER;
        b->source = DXBC_SOURCE_STATIC_SAMPLER;
        b->space = s->RegisterSpace;
        b->base = s->ShaderRegister;
        b->count = 1;
        b->offset = i;
        root->binding_stages[root->binding_count++] = visibility_stages(s->ShaderVisibility);
    }
    root_desc_free(&d);
    if (SUCCEEDED(hr) && sampler_count) {
        VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_SAMPLER, sampler_count, VK_SHADER_STAGE_ALL,
                                                samplers};
        VkDescriptorSetLayoutCreateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT |
                     VK_DESCRIPTOR_SET_LAYOUT_CREATE_EMBEDDED_IMMUTABLE_SAMPLERS_BIT_EXT;
        info.bindingCount = 1;
        info.pBindings = &binding;
        if (vk->vkCreateDescriptorSetLayout(vk->device, &info, NULL, &root->static_layout) != VK_SUCCESS)
            hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr)) {
        VkDescriptorSetLayout sets[4] = {device->resource_layout, device->sampler_layout,
                                         root->static_layout ? root->static_layout : device->empty_layout,
                                         device->record_layout};
        /* the shaders' block of root arguments spans the 64 dwords a root signature may take. */
        VkPushConstantRange range = {VK_SHADER_STAGE_ALL, 0, D12_PUSH_SIZE};
        VkPipelineLayoutCreateInfo info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        info.setLayoutCount = 4;
        info.pSetLayouts = sets;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &range;
        if (vk->vkCreatePipelineLayout(vk->device, &info, NULL, &root->layout) != VK_SUCCESS)
            hr = E_OUTOFMEMORY;
    }
    if (FAILED(hr)) {
        if (root->static_layout)
            vk->vkDestroyDescriptorSetLayout(vk->device, root->static_layout, NULL);
        HeapFree(GetProcessHeap(), 0, root->params);
        HeapFree(GetProcessHeap(), 0, root->bindings);
        HeapFree(GetProcessHeap(), 0, root->binding_stages);
        HeapFree(GetProcessHeap(), 0, root->blob);
        HeapFree(GetProcessHeap(), 0, root);
        return hr;
    }
    root->static_count = sampler_count;
    D12ChildInit(&root->child, device);
    *out = root;
    return S_OK;
}
