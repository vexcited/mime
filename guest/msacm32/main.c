/*
 * msacm32's process state: the lock of the driver list, the checks of
 * handles and pointers, the version and the metrics.
 */
#include "msacm32_private.h"

HINSTANCE AcmModule;
RTL_CRITICAL_SECTION AcmLock;

/* how deep the thread holding the list is in enumerations, which forbid changing the list. */
static DWORD g_enum_depth;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        AcmModule = module;
        RtlInitializeCriticalSection(&AcmLock);
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

BOOL AcmEnumerating(void)
{
    BOOL inside = FALSE;
    if (RtlTryEnterCriticalSection(&AcmLock)) {
        inside = g_enum_depth != 0;
        RtlLeaveCriticalSection(&AcmLock);
    }
    return inside;
}

void AcmEnterEnum(void)
{
    RtlEnterCriticalSection(&AcmLock);
    g_enum_depth++;
}

void AcmLeaveEnum(void)
{
    g_enum_depth--;
    RtlLeaveCriticalSection(&AcmLock);
}

/* a handle of the given kind, or of any kind, still open. */
BOOL AcmValid(const void *handle, DWORD type)
{
    if (!handle || !AcmBoot())
        return FALSE;
    BOOL found = FALSE;
    RtlEnterCriticalSection(&AcmLock);
    for (ACMDRIVERID_ *id = AcmNextId(nullptr, ENUM_ALL); id && !found; id = AcmNextId(id, ENUM_ALL)) {
        if (handle == id) {
            found = type == ACM_ANY || type == ACM_ID;
            break;
        }
        for (ACMDRIVER_ *d = id->drivers; d && !found; d = d->next) {
            if (handle == d) {
                found = type == ACM_ANY || type == ACM_DRIVER;
                goto out;
            }
            for (ACMSTREAM_ *s = d->streams; s; s = s->next)
                if (handle == s) {
                    found = type == ACM_ANY || type == ACM_STREAM;
                    goto out;
                }
        }
    }
out:
    RtlLeaveCriticalSection(&AcmLock);
    return found;
}

BOOL AcmReadable(const void *p, SIZE_T size)
{
    return p && !IsBadReadPtr(p, size ? size : 1);
}

BOOL AcmWritable(void *p, SIZE_T size)
{
    return p && !IsBadWritePtr(p, size ? size : 1);
}

/* the size of a format: pcm has no cbSize. */
DWORD AcmFormatSize(const WAVEFORMATEX *format)
{
    return format->wFormatTag == WAVE_FORMAT_PCM ? sizeof(PCMWAVEFORMAT) : sizeof(WAVEFORMATEX) + format->cbSize;
}

BOOL AcmReadableFormat(const WAVEFORMATEX *format)
{
    if (!AcmReadable(format, sizeof(PCMWAVEFORMAT)))
        return FALSE;
    return format->wFormatTag == WAVE_FORMAT_PCM || AcmReadable(format, AcmFormatSize(format));
}

BOOL AcmReadableFilter(const WAVEFILTER *filter)
{
    return AcmReadable(filter, sizeof(DWORD)) && filter->cbStruct >= sizeof(WAVEFILTER) &&
           AcmReadable(filter, filter->cbStruct);
}

void AcmCopyString(WCHAR *to, const WCHAR *from, SIZE_T count)
{
    SIZE_T i = 0;
    for (; i + 1 < count && from[i]; i++)
        to[i] = from[i];
    if (count)
        to[i] = 0;
}

void AcmToAnsi(char *to, const WCHAR *from, int count)
{
    if (!WideCharToMultiByte(CP_ACP, 0, from, -1, to, count, nullptr, nullptr) && count)
        to[count - 1] = 0;
}

void AcmToWide(WCHAR *to, const char *from, int count)
{
    if (!MultiByteToWideChar(CP_ACP, 0, from, -1, to, count) && count)
        to[count - 1] = 0;
}

MSACMAPI DWORD WINAPI acmGetVersion(void)
{
    return 0x05000000;
}

/* the drivers the thread sees whose support has every bit of mask. */
static MMRESULT count_drivers(DWORD *out, DWORD mask, DWORD flags)
{
    DWORD n = 0;
    AcmEnterEnum();
    for (ACMDRIVERID_ *id = AcmNextId(nullptr, flags); id; id = AcmNextId(id, flags))
        if ((AcmIdSupport(id) & mask) == mask)
            n++;
    AcmLeaveEnum();
    *out = n;
    return MMSYSERR_NOERROR;
}

/* the wave device a hardware driver stands for, found by the name both give. */
static MMRESULT wave_identifier(ACMDRIVERID_ *id, DWORD *out, BOOL input)
{
    *out = (DWORD)-1;
    if (!(id->support & ACMDRIVERDETAILS_SUPPORTF_HARDWARE))
        return MMSYSERR_INVALHANDLE;
    WAVEOUTCAPSW out_caps = {};
    WAVEINCAPSW in_caps = {};
    WCHAR *name = input ? in_caps.szPname : out_caps.szPname;
    UINT message = input ? ACMDM_HARDWARE_WAVE_CAPS_INPUT : ACMDM_HARDWARE_WAVE_CAPS_OUTPUT;
    MMRESULT r = (MMRESULT)AcmIdMessage(id, message, input ? (LPARAM)&in_caps : (LPARAM)&out_caps,
                                        input ? sizeof(in_caps) : sizeof(out_caps));
    if (r)
        return r;
    name[MAXPNAMELEN - 1] = 0;
    UINT devices = input ? waveInGetNumDevs() : waveOutGetNumDevs();
    for (UINT i = 0; i < devices; i++) {
        WCHAR other[MAXPNAMELEN];
        if (input) {
            WAVEINCAPSW caps;
            if (waveInGetDevCapsW(i, &caps, sizeof(caps)))
                continue;
            AcmCopyString(other, caps.szPname, MAXPNAMELEN);
        } else {
            WAVEOUTCAPSW caps;
            if (waveOutGetDevCapsW(i, &caps, sizeof(caps)))
                continue;
            AcmCopyString(other, caps.szPname, MAXPNAMELEN);
        }
        if (!lstrcmpW(name, other)) {
            *out = i;
            return MMSYSERR_NOERROR;
        }
    }
    return MMSYSERR_NODRIVER;
}

MSACMAPI MMRESULT WINAPI acmMetrics(HACMOBJ hao, UINT metric, LPVOID output)
{
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    if (hao && !AcmValid(hao, ACM_ANY))
        return MMSYSERR_INVALHANDLE;
    DWORD *value = output;
    DWORD mask, flags = 0;
    switch (metric) {
    case ACM_METRIC_COUNT_DRIVERS: mask = 0; break;
    case ACM_METRIC_COUNT_CODECS: mask = ACMDRIVERDETAILS_SUPPORTF_CODEC; break;
    case ACM_METRIC_COUNT_CONVERTERS: mask = ACMDRIVERDETAILS_SUPPORTF_CONVERTER; break;
    case ACM_METRIC_COUNT_FILTERS: mask = ACMDRIVERDETAILS_SUPPORTF_FILTER; break;
    case ACM_METRIC_COUNT_DISABLED: mask = flags = ACMDRIVERDETAILS_SUPPORTF_DISABLED; break;
    case ACM_METRIC_COUNT_HARDWARE: mask = ACMDRIVERDETAILS_SUPPORTF_HARDWARE; break;
    case ACM_METRIC_COUNT_LOCAL_DRIVERS: mask = ACMDRIVERDETAILS_SUPPORTF_LOCAL; break;
    case ACM_METRIC_COUNT_LOCAL_CODECS: mask = ACMDRIVERDETAILS_SUPPORTF_LOCAL | ACMDRIVERDETAILS_SUPPORTF_CODEC; break;
    case ACM_METRIC_COUNT_LOCAL_CONVERTERS:
        mask = ACMDRIVERDETAILS_SUPPORTF_LOCAL | ACMDRIVERDETAILS_SUPPORTF_CONVERTER;
        break;
    case ACM_METRIC_COUNT_LOCAL_FILTERS: mask = ACMDRIVERDETAILS_SUPPORTF_LOCAL | ACMDRIVERDETAILS_SUPPORTF_FILTER; break;
    case ACM_METRIC_COUNT_LOCAL_DISABLED:
        mask = ACMDRIVERDETAILS_SUPPORTF_LOCAL | ACMDRIVERDETAILS_SUPPORTF_DISABLED;
        flags = ACM_DRIVERENUMF_DISABLED;
        break;
    case ACM_METRIC_HARDWARE_WAVE_INPUT:
    case ACM_METRIC_HARDWARE_WAVE_OUTPUT:
        if (!AcmWritable(value, sizeof(DWORD)))
            return MMSYSERR_INVALPARAM;
        *value = (DWORD)-1;
        if (!AcmValid(hao, ACM_ID))
            return MMSYSERR_INVALHANDLE;
        return wave_identifier((ACMDRIVERID_ *)hao, value, metric == ACM_METRIC_HARDWARE_WAVE_INPUT);
    case ACM_METRIC_MAX_SIZE_FORMAT:
        if (!AcmWritable(value, sizeof(DWORD)))
            return MMSYSERR_INVALPARAM;
        return AcmMaxFormatSize(hao, value);
    case ACM_METRIC_MAX_SIZE_FILTER:
        if (!AcmWritable(value, sizeof(DWORD)))
            return MMSYSERR_INVALPARAM;
        return AcmMaxFilterSize(hao, value);
    case ACM_METRIC_DRIVER_SUPPORT:
        if (!AcmWritable(value, sizeof(DWORD)))
            return MMSYSERR_INVALPARAM;
        *value = 0;
        if (!AcmValid(hao, ACM_ID))
            return MMSYSERR_INVALHANDLE;
        *value = AcmIdSupport((ACMDRIVERID_ *)hao);
        return MMSYSERR_NOERROR;
    case ACM_METRIC_DRIVER_PRIORITY:
        if (!AcmWritable(value, sizeof(DWORD)))
            return MMSYSERR_INVALPARAM;
        *value = 0;
        if (!AcmValid(hao, ACM_ID))
            return MMSYSERR_INVALHANDLE;
        *value = ((ACMDRIVERID_ *)hao)->priority;
        return MMSYSERR_NOERROR;
    default:
        return MMSYSERR_NOTSUPPORTED;
    }
    if (!AcmWritable(value, sizeof(DWORD)))
        return MMSYSERR_INVALPARAM;
    if (hao) {
        *value = 0;
        return MMSYSERR_INVALHANDLE;
    }
    return count_drivers(value, mask, flags);
}
