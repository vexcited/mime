/*
 * PlaySound loads wave data from memory, resources, files or system aliases. the wave mapper converts compressed
 * formats through msacm32. asynchronous sounds run one at a time; a new sound stops the current one unless requested
 * otherwise.
 */
#define MIME_BUILD_WINMM
#include "winmm_private.h"

#define SND_INVALID_FLAGS 0xFF80DF20u

typedef struct SOUND {
    BYTE *image;      /* the whole RIFF image, ours. */
    const WAVEFORMATEX *format;
    const BYTE *data;
    DWORD size;
    DWORD flags;
} SOUND;

static RTL_CRITICAL_SECTION g_lock = {nullptr, -1, 0, nullptr, nullptr, 0};
static HANDLE g_thread, g_stop;
static volatile BOOL g_playing;

static HANDLE heap(void)
{
    return GetProcessHeap();
}

/* the fmt and data chunks of a RIFF wave image. */
static BOOL parse(SOUND *s, DWORD size)
{
    const BYTE *p = s->image;
    if (size < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4))
        return FALSE;
    DWORD riff = *(const DWORD *)(p + 4) + 8;
    if (riff < size)
        size = riff;
    s->format = nullptr;
    s->data = nullptr;
    for (DWORD at = 12; at + 8 <= size;) {
        DWORD length = *(const DWORD *)(p + at + 4);
        const BYTE *body = p + at + 8;
        DWORD available = size - at - 8;
        if (!memcmp(p + at, "fmt ", 4) && length >= sizeof(PCMWAVEFORMAT) && length <= available)
            s->format = (const WAVEFORMATEX *)body;
        else if (!memcmp(p + at, "data", 4)) {
            s->data = body;
            s->size = length < available ? length : available;
        }
        at += 8 + length + (length & 1);
    }
    return s->format && s->data;
}

static BOOL take_image(SOUND *s, const void *image, DWORD size)
{
    s->image = HeapAlloc(heap(), 0, size);
    if (!s->image)
        return FALSE;
    memcpy(s->image, image, size);
    if (parse(s, size))
        return TRUE;
    HeapFree(heap(), 0, s->image);
    s->image = nullptr;
    return FALSE;
}

static BOOL load_file(SOUND *s, const WCHAR *name)
{
    WCHAR path[MAX_PATH];
    if (!SearchPathW(nullptr, name, L".wav", MAX_PATH, path, nullptr))
        return FALSE;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD size = GetFileSize(f, nullptr), read = 0;
    BOOL ok = FALSE;
    if (size != INVALID_FILE_SIZE && size >= 12 && size < (256u << 20)) {
        s->image = HeapAlloc(heap(), 0, size);
        ok = s->image && ReadFile(f, s->image, size, &read, nullptr) && read == size && parse(s, size);
        if (!ok && s->image) {
            HeapFree(heap(), 0, s->image);
            s->image = nullptr;
        }
    }
    CloseHandle(f);
    return ok;
}

static BOOL load_resource(SOUND *s, HMODULE module, const WCHAR *name)
{
    HRSRC r = FindResourceW(module, name, L"WAVE");
    HGLOBAL g = r ? LoadResource(module, r) : nullptr;
    const void *image = g ? LockResource(g) : nullptr;
    return image && take_image(s, image, SizeofResource(module, r));
}

/* resolves a system event, such as SystemAsterisk, through the current sound scheme. */
static BOOL load_alias(SOUND *s, const WCHAR *alias)
{
    WCHAR key[256], value[MAX_PATH], path[MAX_PATH];
    if (lstrlenW(alias) > 200)
        return FALSE;
    lstrcpyW(key, L"AppEvents\\Schemes\\Apps\\.Default\\");
    lstrcatW(key, alias);
    lstrcatW(key, L"\\.Current");
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, key, nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, value, &size) ||
        !value[0])
        return FALSE;
    if (!ExpandEnvironmentStringsW(value, path, MAX_PATH))
        return FALSE;
    return load_file(s, path);
}

static const WCHAR *alias_of_id(UINT_PTR id)
{
    switch (id) {
    case 'S' | '*' << 8: return L"SystemAsterisk";
    case 'S' | '?' << 8: return L"SystemQuestion";
    case 'S' | 'H' << 8: return L"SystemHand";
    case 'S' | 'E' << 8: return L"SystemExit";
    case 'S' | 'S' << 8: return L"SystemStart";
    case 'S' | 'W' << 8: return L"SystemWelcome";
    case 'S' | '!' << 8: return L"SystemExclamation";
    case 'S' | 'D' << 8: return L".Default";
    default: return nullptr;
    }
}

static BOOL load(SOUND *s, const WCHAR *sound, HMODULE module, DWORD flags)
{
    if ((flags & SND_ALIAS_ID) == SND_ALIAS_ID) {
        const WCHAR *alias = alias_of_id((UINT_PTR)sound);
        return alias && load_alias(s, alias);
    }
    if (flags & SND_MEMORY && !(flags & (SND_RESOURCE & ~SND_MEMORY))) {
        /* the image's size is in its RIFF header. */
        const BYTE *p = (const BYTE *)sound;
        return !memcmp(p, "RIFF", 4) && take_image(s, p, *(const DWORD *)(p + 4) + 8);
    }
    if ((flags & SND_RESOURCE) == SND_RESOURCE)
        return load_resource(s, module ? module : GetModuleHandleW(nullptr), sound);
    if (flags & SND_ALIAS)
        return load_alias(s, sound);
    if (flags & SND_FILENAME)
        return load_file(s, sound);
    /* without a kind, an alias first, then a file. */
    return load_alias(s, sound) || load_file(s, sound);
}

static void unload(SOUND *s)
{
    HeapFree(heap(), 0, s->image);
    s->image = nullptr;
}

static BOOL play(SOUND *s, HANDLE stop)
{
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HWAVEOUT w;
    if (!done || waveOutOpen(&w, WAVE_MAPPER, s->format, (DWORD_PTR)done, 0, CALLBACK_EVENT)) {
        if (done)
            CloseHandle(done);
        return FALSE;
    }
    WAVEHDR h = {(LPSTR)s->data, s->size};
    BOOL ok = !waveOutPrepareHeader(w, &h, sizeof(h));
    HANDLE waits[2] = {done, stop};
    g_playing = TRUE;
    do {
        h.dwFlags &= ~WHDR_DONE;
        if (!ok || waveOutWrite(w, &h, sizeof(h)))
            break;
        while (!(h.dwFlags & WHDR_DONE)) {
            if (WaitForMultipleObjects(stop ? 2 : 1, waits, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
                waveOutReset(w);
                goto out;
            }
        }
    } while (s->flags & SND_LOOP);
out:
    g_playing = FALSE;
    waveOutReset(w);
    waveOutUnprepareHeader(w, &h, sizeof(h));
    waveOutClose(w);
    CloseHandle(done);
    return ok;
}

static DWORD WINAPI sound_thread(void *arg)
{
    SOUND *s = arg;
    play(s, g_stop);
    unload(s);
    HeapFree(heap(), 0, s);
    return 0;
}

/* waits for the asynchronous playback thread to stop. */
static void stop(void)
{
    RtlEnterCriticalSection(&g_lock);
    HANDLE thread = g_thread;
    g_thread = nullptr;
    if (thread) {
        SetEvent(g_stop);
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        ResetEvent(g_stop);
    }
    RtlLeaveCriticalSection(&g_lock);
}

static BOOL busy(void)
{
    RtlEnterCriticalSection(&g_lock);
    BOOL running = g_thread && WaitForSingleObject(g_thread, 0) == WAIT_TIMEOUT;
    RtlLeaveCriticalSection(&g_lock);
    return running;
}

static BOOL play_sound(const WCHAR *sound, HMODULE module, DWORD flags)
{
    if (flags & SND_INVALID_FLAGS)
        return FALSE;
    if (!sound || (flags & SND_PURGE)) {
        stop();
        return TRUE;
    }
    if ((flags & SND_NOSTOP) && busy())
        return FALSE;
    SOUND *s = HeapAlloc(heap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return FALSE;
    s->flags = flags;
    BOOL found = load(s, sound, module, flags);
    if (!found && !(flags & SND_NODEFAULT) && (flags & SND_ALIAS_ID) != SND_ALIAS_ID)
        found = load_alias(s, L".Default");
    if (!found) {
        HeapFree(heap(), 0, s);
        return FALSE;
    }
    stop();
    if (!(flags & SND_ASYNC)) {
        s->flags &= ~SND_LOOP;
        BOOL ok = play(s, nullptr);
        unload(s);
        HeapFree(heap(), 0, s);
        return ok;
    }
    RtlEnterCriticalSection(&g_lock);
    if (!g_stop)
        g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = g_stop ? CreateThread(nullptr, 0, sound_thread, s, 0, nullptr) : nullptr;
    BOOL ok = g_thread != nullptr;
    RtlLeaveCriticalSection(&g_lock);
    if (!ok) {
        unload(s);
        HeapFree(heap(), 0, s);
    }
    return ok;
}

/* memory images and numeric aliases do not take string arguments. */
static BOOL is_string(const void *sound, DWORD flags)
{
    if ((flags & SND_ALIAS_ID) == SND_ALIAS_ID || ((flags & SND_MEMORY) && (flags & SND_RESOURCE) != SND_RESOURCE))
        return FALSE;
    return !IS_INTRESOURCE(sound);
}

WINMMAPI BOOL WINAPI PlaySoundW(LPCWSTR sound, HMODULE module, DWORD flags)
{
    return play_sound(sound, module, flags);
}

WINMMAPI BOOL WINAPI PlaySoundA(LPCSTR sound, HMODULE module, DWORD flags)
{
    if (!sound || !is_string(sound, flags))
        return play_sound((const WCHAR *)sound, module, flags);
    int n = MultiByteToWideChar(CP_ACP, 0, sound, -1, nullptr, 0);
    WCHAR *w = n > 0 ? HeapAlloc(heap(), 0, n * sizeof(WCHAR)) : nullptr;
    if (!w)
        return FALSE;
    MultiByteToWideChar(CP_ACP, 0, sound, -1, w, n);
    BOOL ok = play_sound(w, module, flags);
    HeapFree(heap(), 0, w);
    return ok;
}

WINMMAPI BOOL WINAPI PlaySound(LPCSTR sound, HMODULE module, DWORD flags)
{
    return PlaySoundA(sound, module, flags);
}

WINMMAPI BOOL WINAPI sndPlaySoundW(LPCWSTR sound, UINT flags)
{
    return PlaySoundW(sound, nullptr, flags & (SND_ASYNC | SND_NODEFAULT | SND_MEMORY | SND_LOOP | SND_NOSTOP));
}

WINMMAPI BOOL WINAPI sndPlaySoundA(LPCSTR sound, UINT flags)
{
    return PlaySoundA(sound, nullptr, flags & (SND_ASYNC | SND_NODEFAULT | SND_MEMORY | SND_LOOP | SND_NOSTOP));
}
