#define MIME_KERNEL_PNP
#include "../ntos.h"
#include <mime/abi.h>
#include <mime/audio.h>
#include <mime/pnp.h>

#define INSTANCE_CHARS 72
#define MAX_ENDPOINTS (MIME_AUDIO_MAX_DEVICES * 2)

static const GUID DEVINTERFACE_AUDIO_RENDER = {0xE6327CAD, 0xDCEC, 0x4949, {0xAE, 0x8A, 0x99, 0x1E, 0x97, 0x6A, 0x79, 0xD2}};
static const GUID DEVINTERFACE_AUDIO_CAPTURE = {0x2EEF81BE, 0x33FA, 0x4800, {0x96, 0x70, 0x1C, 0xD4, 0x74, 0x97, 0x2C, 0x3F}};

/* only the thread of the host's changes runs here. */
static MimeAudioDeviceList *g_list;
static WCHAR g_known[MAX_ENDPOINTS][INSTANCE_CHARS], g_now[MAX_ENDPOINTS][INSTANCE_CHARS];
static ULONG g_known_count;

static void widen(const char *utf8, WCHAR *out, ULONG chars)
{
    ULONG written = 0;
    out[0] = 0;
    RtlUTF8ToUnicodeN(out, (chars - 1) * sizeof(WCHAR), &written, utf8, (ULONG)strlen(utf8));
    out[written / sizeof(WCHAR)] = 0;
}

static BOOLEAN known(WCHAR list[][INSTANCE_CHARS], ULONG count, PCWSTR instance)
{
    for (ULONG i = 0; i < count; i++)
        if (!wcscmp(list[i], instance))
            return TRUE;
    return FALSE;
}

/*
 * windows shows an audio endpoint as a software device named by the id mmdevapi gives it, with the device interface
 * of its direction. the interface is there to be listed and watched: sound goes through the audio streams.
 */
static void add_endpoint(PCWSTR instance, const MimeAudioDevice *d, BOOLEAN capture)
{
    WCHAR name[RTL_NUMBER_OF(d->name)], port[RTL_NUMBER_OF(d->port)];
    widen(d->name, name, RTL_NUMBER_OF(name));
    widen(d->port, port, RTL_NUMBER_OF(port));
    MIME_DEVICE_PROPERTIES props = {
        .DeviceDesc = port[0] ? port : name,
        .HardwareIds = L"MMDEVAPI\\AudioEndpoints\0",
        .Class = L"AudioEndpoint",
        .ClassGuid = L"{c166523c-fe0c-4a94-a586-f1a80cfbbf3e}",
        .Manufacturer = L"Microsoft",
        .FriendlyName = name,
    };
    if (NT_SUCCESS(MimePnpAddDevice(instance, nullptr, &props)))
        MimePnpAddInterface(instance, capture ? &DEVINTERFACE_AUDIO_CAPTURE : &DEVINTERFACE_AUDIO_RENDER, L"\\Device\\Null");
}

void IopSyncAudioEndpoints(void)
{
    SIZE_T bytes = ROUND_TO_PAGES(sizeof(*g_list));
    if (!g_list)
        g_list = MmAllocateContiguousPages(bytes);
    if (!g_list)
        return;
    ULONG count = 0;
    for (ULONG capture = 0; capture < 2; capture++) {
        memset(g_list, 0, sizeof(*g_list));
        if (HalMimeHypercall(MIME_HC_AUDIO_DEVICES, HalPhysicalOf(g_list), bytes, capture, 0, 0, 0, nullptr) != MIME_HC_STATUS_OK)
            continue;
        ULONG listed = g_list->count < MIME_AUDIO_MAX_DEVICES ? g_list->count : MIME_AUDIO_MAX_DEVICES;
        for (ULONG i = 0; i < listed; i++) {
            MimeAudioDevice *d = &g_list->devices[i];
            d->name[sizeof(d->name) - 1] = 0;
            d->port[sizeof(d->port) - 1] = 0;
            if (!d->id || !d->name[0])
                continue;
            ULONG twin = 0;
            for (ULONG j = 0; j < i; j++)
                twin += !strcmp(g_list->devices[j].name, d->name);
            UCHAR g[16];
            mime_audio_device_guid(g, d->name, capture, twin);
            WCHAR *instance = g_now[count];
            _snwprintf(instance, INSTANCE_CHARS,
                       L"SWD\\MMDEVAPI\\{0.0.%u.00000000}.{%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                       capture, g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13], g[14],
                       g[15]);
            if (known(g_now, count, instance))
                continue;
            count++;
            if (!known(g_known, g_known_count, instance))
                add_endpoint(instance, d, capture != 0);
        }
    }
    for (ULONG i = 0; i < g_known_count; i++)
        if (!known(g_now, count, g_known[i]))
            MimePnpRemoveDevice(g_known[i]);
    memcpy(g_known, g_now, sizeof(g_known[0]) * count);
    g_known_count = count;
}
