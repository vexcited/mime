/*
 * effect files: a RIFF 'FORC' form starting with a guid, then a LIST 'efct'
 * per effect holding its name, a 32-bit DIEFFECT without its pointers, its
 * guid, a 1, and 'data' chunks for the axes, the directions, the envelope
 * and the type specific parameters. like windows, the reader takes the next
 * chunk for each of those four, whatever the writer left out.
 */
#include "dinput_private.h"

static const GUID GUID_FileEffect = {0x197E775C, 0x34BA, 0x11D3, {0xAB, 0xD5, 0x00, 0xC0, 0x4F, 0x8E, 0xC6, 0x27}};

#define FOURCC(a, b, c, d) ((DWORD)(a) | (DWORD)(b) << 8 | (DWORD)(c) << 16 | (DWORD)(d) << 24)

static BOOL standard_effect(const GUID *guid)
{
    const GUID *const STANDARD[] = {&GUID_ConstantForce, &GUID_RampForce,    &GUID_Square,  &GUID_Sine,
                                    &GUID_Triangle,      &GUID_SawtoothUp,   &GUID_SawtoothDown,
                                    &GUID_Spring,        &GUID_Damper,       &GUID_Inertia, &GUID_Friction,
                                    &GUID_CustomForce};
    for (DWORD i = 0; i < RTL_NUMBER_OF(STANDARD); i++)
        if (IsEqualGUID(guid, STANDARD[i]))
            return TRUE;
    return FALSE;
}

static HRESULT last_error(void)
{
    return (HRESULT)(0x80070000u | (GetLastError() & 0xFFFF));
}

typedef struct READER {
    const BYTE *data;
    SIZE_T size, pos;
} READER;

/* the next chunk's content range, past a list's type. */
static BOOL descend(READER *r, SIZE_T *start, SIZE_T *end)
{
    if (r->pos + 8 > r->size)
        return FALSE;
    DWORD id, length;
    memcpy(&id, r->data + r->pos, 4);
    memcpy(&length, r->data + r->pos + 4, 4);
    *start = r->pos + 8;
    if (id == FOURCC('R', 'I', 'F', 'F') || id == FOURCC('L', 'I', 'S', 'T'))
        *start += 4;
    *end = r->pos + 8 + length;
    if (*end > r->size || *start > *end)
        return FALSE;
    r->pos = *start;
    return TRUE;
}

static void ascend(READER *r, SIZE_T end)
{
    r->pos = end + (end & 1);
}

static void read_bytes(READER *r, SIZE_T end, void *out, SIZE_T n)
{
    SIZE_T have = r->pos < end ? end - r->pos : 0;
    if (have > n)
        have = n;
    memcpy(out, r->data + r->pos, have);
    r->pos += have;
}

HRESULT DiEnumEffectsInFile(LPCWSTR file, LPDIENUMEFFECTSINFILECALLBACK callback, void *context, DWORD flags)
{
    if (!file || lstrlenW(file) >= 256)
        return E_INVALIDARG;
    if (!callback)
        return E_INVALIDARG;
    if (flags & ~(DIFEF_INCLUDENONSTANDARD | DIFEF_MODIFYIFNEEDED))
        return E_INVALIDARG;
    HANDLE h = CreateFileW(file, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return last_error();
    LARGE_INTEGER length;
    BYTE *bytes = nullptr;
    DWORD got = 0;
    if (GetFileSizeEx(h, &length) && length.QuadPart < 0x4000000)
        bytes = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length.QuadPart + 1);
    BOOL ok = bytes && ReadFile(h, bytes, (DWORD)length.QuadPart, &got, nullptr);
    CloseHandle(h);
    if (!ok) {
        HeapFree(GetProcessHeap(), 0, bytes);
        return E_OUTOFMEMORY;
    }
    READER r = {bytes, got, 0};
    SIZE_T form_start, form_end;
    GUID marker;
    HRESULT hr = DI_OK;
    if (!descend(&r, &form_start, &form_end) || memcmp(bytes + 8, "FORC", 4)) {
        hr = (HRESULT)0x8007000B;
    } else {
        read_bytes(&r, form_end, &marker, sizeof(marker));
        if (!IsEqualGUID(&marker, &GUID_FileEffect))
            hr = (HRESULT)0x8007000B;
    }
    while (SUCCEEDED(hr)) {
        SIZE_T start, end;
        if (!descend(&r, &start, &end))
            break;
        DWORD axes[32] = {};
        LONG directions[32] = {};
        DIENVELOPE envelope = {};
        DIEFFECT effect = {sizeof(effect)};
        effect.rgdwAxes = axes;
        effect.rglDirection = directions;
        effect.lpEnvelope = &envelope;
        DIFILEEFFECT entry = {sizeof(entry)};
        entry.lpDiEffect = &effect;
        read_bytes(&r, end, entry.szFriendlyName, 64);
        DWORD saved[14] = {};
        read_bytes(&r, end, saved, sizeof(saved));
        if (saved[0] == sizeof(saved)) {
            effect.dwFlags = saved[1];
            effect.dwDuration = saved[2];
            effect.dwSamplePeriod = saved[3];
            effect.dwGain = saved[4];
            effect.dwTriggerButton = saved[5];
            effect.dwTriggerRepeatInterval = saved[6];
            effect.cAxes = saved[7];
            effect.cbTypeSpecificParams = saved[11];
            effect.dwStartDelay = saved[13];
        }
        read_bytes(&r, end, &entry.GuidEffect, sizeof(GUID));
        DWORD one;
        read_bytes(&r, end, &one, sizeof(one));
        DWORD n = effect.cAxes < 32 ? effect.cAxes : 32;
        void *params = nullptr;
        SIZE_T cs, ce;
        if (descend(&r, &cs, &ce)) {
            read_bytes(&r, ce, axes, n * sizeof(DWORD));
            ascend(&r, ce);
        }
        if (descend(&r, &cs, &ce)) {
            read_bytes(&r, ce, directions, n * sizeof(LONG));
            ascend(&r, ce);
        }
        if (descend(&r, &cs, &ce)) {
            read_bytes(&r, ce, &envelope, sizeof(envelope));
            ascend(&r, ce);
        }
        if (effect.cbTypeSpecificParams) {
            params = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, effect.cbTypeSpecificParams);
            if (params && descend(&r, &cs, &ce)) {
                read_bytes(&r, ce, params, effect.cbTypeSpecificParams);
                ascend(&r, ce);
            }
            effect.lpvTypeSpecificParams = params;
        }
        ascend(&r, end);
        BOOL more = TRUE;
        if ((flags & DIFEF_INCLUDENONSTANDARD) || standard_effect(&entry.GuidEffect))
            more = callback(&entry, context) == DIENUM_CONTINUE;
        HeapFree(GetProcessHeap(), 0, params);
        if (!more)
            break;
    }
    HeapFree(GetProcessHeap(), 0, bytes);
    return hr;
}

typedef struct WRITER {
    BYTE *data;
    SIZE_T size, capacity;
    BOOL failed;
} WRITER;

static void put(WRITER *w, const void *bytes, SIZE_T n)
{
    if (w->failed)
        return;
    if (w->size + n > w->capacity) {
        SIZE_T capacity = (w->size + n) * 2 + 256;
        BYTE *grown = w->data ? HeapReAlloc(GetProcessHeap(), 0, w->data, capacity)
                              : HeapAlloc(GetProcessHeap(), 0, capacity);
        if (!grown) {
            w->failed = TRUE;
            return;
        }
        w->data = grown;
        w->capacity = capacity;
    }
    memcpy(w->data + w->size, bytes, n);
    w->size += n;
}

static void put32(WRITER *w, DWORD v)
{
    put(w, &v, 4);
}

static SIZE_T open_chunk(WRITER *w, DWORD id, DWORD type)
{
    put32(w, id);
    SIZE_T at = w->size;
    put32(w, 0);
    if (type)
        put32(w, type);
    return at;
}

static void close_chunk(WRITER *w, SIZE_T at)
{
    if (w->failed)
        return;
    DWORD length = (DWORD)(w->size - at - 4);
    memcpy(w->data + at, &length, 4);
    if (length & 1)
        put(w, "", 1);
}

static void data_chunk(WRITER *w, const void *bytes, SIZE_T n)
{
    SIZE_T at = open_chunk(w, FOURCC('d', 'a', 't', 'a'), 0);
    put(w, bytes, n);
    close_chunk(w, at);
}

static HRESULT write_effect(WRITER *w, const DIFILEEFFECT *entry)
{
    const DIEFFECT *e = entry->lpDiEffect;
    if (strnlen(entry->szFriendlyName, 256) >= 256)
        return E_INVALIDARG;
    SIZE_T list = open_chunk(w, FOURCC('L', 'I', 'S', 'T'), FOURCC('e', 'f', 'c', 't'));
    CHAR name[64] = {};
    memcpy(name, entry->szFriendlyName, strnlen(entry->szFriendlyName, sizeof(name)));
    put(w, name, sizeof(name));
    DWORD saved[14] = {sizeof(saved), e->dwFlags, e->dwDuration, e->dwSamplePeriod, e->dwGain, e->dwTriggerButton,
                       e->dwTriggerRepeatInterval, e->cAxes, 0, 0, 0, e->cbTypeSpecificParams,
                       e->cbTypeSpecificParams && e->lpvTypeSpecificParams ? 256u : 0u, e->dwStartDelay};
    put(w, saved, sizeof(saved));
    put(w, &entry->GuidEffect, sizeof(GUID));
    put32(w, 1);
    if (e->rgdwAxes)
        data_chunk(w, e->rgdwAxes, (SIZE_T)e->cAxes * 4);
    if (e->rglDirection)
        data_chunk(w, e->rglDirection, (SIZE_T)e->cAxes * 4);
    if (e->lpEnvelope)
        data_chunk(w, e->lpEnvelope, e->lpEnvelope->dwSize);
    if (e->cbTypeSpecificParams && e->lpvTypeSpecificParams)
        data_chunk(w, e->lpvTypeSpecificParams, e->cbTypeSpecificParams);
    close_chunk(w, list);
    return DI_OK;
}

HRESULT DiWriteEffectToFile(LPCWSTR file, DWORD count, LPDIFILEEFFECT effects, DWORD flags)
{
    if (!file || lstrlenW(file) >= 256)
        return E_INVALIDARG;
    if (flags & ~(DIFEF_INCLUDENONSTANDARD | DIFEF_MODIFYIFNEEDED))
        return E_INVALIDARG;
    if (!effects)
        return E_POINTER;
    WRITER w = {};
    SIZE_T form = open_chunk(&w, FOURCC('R', 'I', 'F', 'F'), FOURCC('F', 'O', 'R', 'C'));
    put(&w, &GUID_FileEffect, sizeof(GUID));
    HRESULT hr = DI_OK;
    for (DWORD i = 0; i < count && SUCCEEDED(hr); i++) {
        if (!(flags & DIFEF_INCLUDENONSTANDARD) && !standard_effect(&effects[i].GuidEffect))
            continue;
        hr = write_effect(&w, &effects[i]);
    }
    close_chunk(&w, form);
    if (w.failed)
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) {
        HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD done;
        if (h == INVALID_HANDLE_VALUE)
            hr = last_error();
        else if (!WriteFile(h, w.data, (DWORD)w.size, &done, nullptr))
            hr = last_error();
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
    HeapFree(GetProcessHeap(), 0, w.data);
    return hr;
}
