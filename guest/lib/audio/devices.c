#include "devices.h"

#include <mime/ntuser.h>
#include <mime/win32/winnls.h>
#include <rtl.h>

DWORD MimeAudioChannelMask(UINT32 channels)
{
    static const DWORD masks[9] = {0, 0x4, 0x3, 0xB, 0x33, 0x3B, 0x3F, 0x70F, 0x63F};
    return channels < RTL_NUMBER_OF(masks) ? masks[channels] : 0;
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

static UINT32 bit_count(DWORD mask)
{
    UINT32 count = 0;
    for (; mask; mask &= mask - 1)
        count++;
    return count;
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
            d->adapter[sizeof(d->adapter) - 1] = 0;
            d->port[sizeof(d->port) - 1] = 0;
            if (!d->adapter[0] || !MultiByteToWideChar(CP_UTF8, 0, d->adapter, -1, e.adapter, RTL_NUMBER_OF(e.adapter)))
                lstrcpynW(e.adapter, e.name, RTL_NUMBER_OF(e.adapter));
            if (!d->port[0] || !MultiByteToWideChar(CP_UTF8, 0, d->port, -1, e.port, RTL_NUMBER_OF(e.port)))
                lstrcpynW(e.port, e.name, RTL_NUMBER_OF(e.port));
            UINT32 twin = 0;
            for (UINT32 j = 0; j < i; j++)
                twin += !strcmp(list.devices[j].name, d->name);
            e.host = d->id;
            mime_audio_device_guid((uint8_t *)&e.guid, d->name, capture, twin);
            e.rate = d->rate >= 8000 && d->rate <= 192000 ? d->rate : 48000;
            e.channels = d->channels >= 1 && d->channels <= 8 ? d->channels : 2;
            e.is_float = d->is_float != 0;
            e.bits = e.is_float ? 32 : d->bits == 24 || d->bits == 32 ? d->bits : 16;
            e.valid_bits = d->valid_bits && d->valid_bits < e.bits && !e.is_float ? d->valid_bits : e.bits;
            e.channel_mask = bit_count(d->channel_mask) == e.channels ? d->channel_mask : MimeAudioChannelMask(e.channels);
            /* windows has no form 0 or 7, and tells what it cannot name as speakers or a microphone too. */
            e.form = d->form >= 1 && d->form <= 9 && d->form != 7 ? d->form : capture ? 4 : 1;
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
    if (count)
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

BOOL MimeAudioDefaultEndpoint(BOOL capture, MimeAudioEndpoint *out)
{
    MimeAudioEndpoint *all = HeapAlloc(GetProcessHeap(), 0, sizeof(MimeAudioEndpoint) * MIME_AUDIO_ENDPOINTS);
    if (!all)
        return FALSE;
    BOOL found = MimeAudioEndpoints(capture, all) != 0;
    if (found)
        *out = all[0];
    HeapFree(GetProcessHeap(), 0, all);
    return found;
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
