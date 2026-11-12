#include "devices.h"

#include <mime/ntuser.h>
#include <mime/win32/winnls.h>
#include <rtl.h>

static UINT64 hash(const char *text, UINT64 seed)
{
    UINT64 h = seed;
    for (; *text; text++)
        h = (h ^ (BYTE)*text) * 0x100000001B3ull;
    return h;
}

/* devices of the same name get different guids by their place among them. */
static void make_guid(GUID *guid, const char *name, BOOL capture, UINT32 twin)
{
    UINT64 low = hash(name, 0xCBF29CE484222325ull + twin), high = hash(name, 0x84222325CBF29CE4ull + twin * 31 + capture);
    memcpy(guid, &low, sizeof(low));
    memcpy((BYTE *)guid + sizeof(low), &high, sizeof(high));
    guid->Data3 = (guid->Data3 & 0x0FFF) | 0x4000;
    guid->Data4[0] = (guid->Data4[0] & 0x3F) | 0x80;
}

static BOOL host_devices(BOOL capture, MimeAudioDeviceList *list)
{
    PVOID base;
    ULONG slot;
    if (!NT_SUCCESS(NtGdiMimeAudioCreate(&base, &slot)))
        return FALSE;
    BOOL ok = NT_SUCCESS(NtGdiMimeAudioControl(slot, capture ? MIME_AUDIO_INPUTS : MIME_AUDIO_OUTPUTS));
    if (ok)
        memcpy(list, base, sizeof(*list));
    /* the next stream of the slot starts from a clean header. */
    memset(base, 0, sizeof(*list));
    NtGdiMimeAudioControl(slot, MIME_AUDIO_CLOSE);
    return ok;
}

#if defined(__aarch64__)
#define NO_DEVICE_BRAND L"Qualcomm Audio"
#else
#define NO_DEVICE_BRAND L"Realtek(R) Audio"
#endif

/* a host without a device of the direction: programs get one whose sound the host discards or keeps silent. */
static void no_device(BOOL capture, MimeAudioEndpoint *e)
{
    static const GUID guids[2] = {
        {0x2c9f1a4b, 0x8d3e, 0x4f6a, {0x9b, 0x1c, 0x7e, 0x5d, 0x2a, 0x8c, 0x4f, 0x01}},
        {0x2c9f1a4b, 0x8d3e, 0x4f6a, {0x9b, 0x1c, 0x7e, 0x5d, 0x2a, 0x8c, 0x4f, 0x02}},
    };
    memset(e, 0, sizeof(*e));
    e->is_default = TRUE;
    e->guid = guids[capture ? 1 : 0];
    e->rate = 48000;
    e->channels = 2;
    e->bits = 16;
    lstrcpyW(e->name, capture ? L"Microphone (" NO_DEVICE_BRAND L")" : L"Speakers (" NO_DEVICE_BRAND L")");
}

UINT32 MimeAudioEndpoints(BOOL capture, MimeAudioEndpoint *out)
{
    MimeAudioDeviceList list;
    UINT32 count = 0;
    if (host_devices(capture, &list)) {
        if (list.count > MIME_AUDIO_MAX_DEVICES)
            list.count = MIME_AUDIO_MAX_DEVICES;
        for (UINT32 i = 0; i < list.count; i++) {
            MimeAudioDevice *d = &list.devices[i];
            d->name[sizeof(d->name) - 1] = 0;
            MimeAudioEndpoint e = {0};
            if (!d->id || !MultiByteToWideChar(CP_UTF8, 0, d->name, -1, e.name, RTL_NUMBER_OF(e.name)))
                continue;
            UINT32 twin = 0;
            for (UINT32 j = 0; j < i; j++)
                twin += !strcmp(list.devices[j].name, d->name);
            e.host = d->id;
            make_guid(&e.guid, d->name, capture, twin);
            e.rate = d->rate >= 8000 && d->rate <= 192000 ? d->rate : 48000;
            e.channels = d->channels >= 1 && d->channels <= 8 ? d->channels : 2;
            e.is_float = d->is_float != 0;
            e.bits = e.is_float ? 32 : d->bits == 24 || d->bits == 32 ? d->bits : 16;
            /* one default device, moved to the front. */
            e.is_default = (d->flags & MIME_AUDIO_DEVICE_DEFAULT) && !(count && out[0].is_default);
            if (e.is_default && count) {
                memmove(&out[1], &out[0], count * sizeof(e));
                out[0] = e;
            } else {
                out[count] = e;
            }
            count++;
        }
    }
    if (!count)
        no_device(capture, &out[count++]);
    out[0].is_default = TRUE;
    return count;
}

BOOL MimeAudioEndpointHost(BOOL capture, const GUID *guid, UINT32 *host)
{
    MimeAudioEndpoint *all = HeapAlloc(GetProcessHeap(), 0, sizeof(MimeAudioEndpoint) * MIME_AUDIO_ENDPOINTS);
    if (!all)
        return FALSE;
    UINT32 count = MimeAudioEndpoints(capture, all);
    BOOL found = FALSE;
    for (UINT32 i = 0; i < count && !found; i++) {
        if (!memcmp(&all[i].guid, guid, sizeof(GUID))) {
            *host = all[i].host;
            found = TRUE;
        }
    }
    HeapFree(GetProcessHeap(), 0, all);
    return found;
}

void MimeAudioDefaultEndpoint(BOOL capture, MimeAudioEndpoint *out)
{
    MimeAudioEndpoint *all = HeapAlloc(GetProcessHeap(), 0, sizeof(MimeAudioEndpoint) * MIME_AUDIO_ENDPOINTS);
    if (all) {
        MimeAudioEndpoints(capture, all);
        *out = all[0];
        HeapFree(GetProcessHeap(), 0, all);
    } else {
        no_device(capture, out);
    }
}

void MimeAudioEndpointId(BOOL capture, const GUID *guid, WCHAR *out)
{
    static const WCHAR digits[] = L"0123456789abcdef";
    static const BYTE order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    const BYTE *bytes = (const BYTE *)guid;
    for (const WCHAR *prefix = capture ? L"{0.0.1.00000000}.{" : L"{0.0.0.00000000}.{"; *prefix; prefix++)
        *out++ = *prefix;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            *out++ = L'-';
        *out++ = digits[bytes[order[i]] >> 4];
        *out++ = digits[bytes[order[i]] & 15];
    }
    *out++ = L'}';
    *out = 0;
}
