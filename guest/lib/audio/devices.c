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

UINT32 MimeAudioEndpoints(BOOL capture, MimeAudioEndpoint *out)
{
    MimeAudioDeviceList list;
    const WCHAR *kind = capture ? L"Microphone" : L"Speakers";
    memset(&out[0], 0, sizeof(out[0]));
    lstrcpyW(out[0].name, kind);
    lstrcatW(out[0].name, L" (" MIME_AUDIO_BRAND L")");
    lstrcpyW(out[0].interface, MIME_AUDIO_BRAND);
    UINT32 count = 1;
    if (host_devices(capture, &list)) {
        if (list.count > MIME_AUDIO_MAX_DEVICES)
            list.count = MIME_AUDIO_MAX_DEVICES;
        for (UINT32 i = 0; i < list.count; i++) {
            MimeAudioDevice *d = &list.devices[i];
            d->name[sizeof(d->name) - 1] = 0;
            MimeAudioEndpoint *e = &out[count];
            memset(e, 0, sizeof(*e));
            if (!d->id || !MultiByteToWideChar(CP_UTF8, 0, d->name, -1, e->interface, RTL_NUMBER_OF(e->interface)))
                continue;
            UINT32 twin = 0;
            for (UINT32 j = 0; j < i; j++)
                twin += !strcmp(list.devices[j].name, d->name);
            e->host = d->id;
            make_guid(&e->guid, d->name, capture, twin);
            lstrcpyW(e->name, kind);
            lstrcatW(e->name, L" (");
            lstrcatW(e->name, e->interface);
            lstrcatW(e->name, L")");
            count++;
        }
    }
    return count;
}

BOOL MimeAudioEndpointHost(BOOL capture, const GUID *guid, UINT32 *host)
{
    MimeAudioEndpoint *all = HeapAlloc(GetProcessHeap(), 0, sizeof(MimeAudioEndpoint) * MIME_AUDIO_ENDPOINTS);
    if (!all)
        return FALSE;
    UINT32 count = MimeAudioEndpoints(capture, all);
    BOOL found = FALSE;
    for (UINT32 i = 1; i < count && !found; i++) {
        if (!memcmp(&all[i].guid, guid, sizeof(GUID))) {
            *host = all[i].host;
            found = TRUE;
        }
    }
    HeapFree(GetProcessHeap(), 0, all);
    return found;
}
