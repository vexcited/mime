#ifndef DSOUND_PRIVATE_H
#define DSOUND_PRIVATE_H

#define MIME_BUILD_DSOUND
#include <mime/nt.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/dsound.h>
#include "../lib/audio/output.h"

/* every buffer mixes into this, like windows' engine mixes into the endpoint's format. */
#define MIX_RATE 48000
#define MIX_CHANNELS 2
/* one engine pass, and the passes kept ahead of the host. */
#define MIX_PERIOD 480
#define MIX_AHEAD 3
/* source channels a buffer's matrix spreads over the speakers; extra ones are dropped. */
#define MAX_CHANNELS 32

typedef struct DSDEVICE DSDEVICE;
typedef struct DSBUFFER DSBUFFER;

typedef struct NOTIFY {
    DWORD offset;
    HANDLE event;
} NOTIFY;

/* 3d state of a buffer or listener: what applies, and what DS3D_DEFERRED set, per field. */
typedef struct SOUND3D {
    DS3DBUFFER params;
    DS3DBUFFER deferred;
    DWORD dirty;
} SOUND3D;

typedef struct LISTENER3D {
    DS3DLISTENER params;
    DS3DLISTENER deferred;
    DWORD dirty;
} LISTENER3D;

struct DSBUFFER {
    IDirectSoundBuffer8 iface;
    IDirectSound3DBuffer iface3d;
    IDirectSound3DListener listener;
    IDirectSoundNotify notify_iface;
    IKsPropertySet props;
    DWORD magic;
    LONG ref;
    DSDEVICE *device;
    DSBUFFER *next;         /* in the device's list. */
    BOOL primary;
    DWORD flags;            /* DSBCAPS_ flags. */
    WAVEFORMATEX *format;
    BYTE *data;             /* shared by duplicates. */
    LONG *data_ref;
    DWORD size;
    DWORD original_rate;
    /* what the application set. */
    LONG volume, pan;
    DWORD frequency;
    /* playback, under the engine's lock. */
    BOOL playing, looping;
    BOOL lost, muted;
    DWORD position;         /* the next byte the mixer reads: windows' write cursor. */
    ULONG64 fraction;       /* of the next frame, 32.32 fixed point. */
    DWORD last_play;        /* the play cursor last reported, kept monotonic. */
    DWORD lead;             /* how far the play cursor trails the write cursor. */
    BOOL ended;             /* stopped by reaching its end. */
    /* where the play cursor was at the end of the last pass, for notifications. */
    DWORD notify_play;
    NOTIFY *notifies;
    DWORD notify_count;
    /* per channel gains from volume, pan and 3d, applied by the mixer. */
    float gain[MAX_CHANNELS][2]; /* [source channel][output channel] */
    DWORD rate;             /* the rate the mixer reads at, doppler included. */
    SOUND3D sound3d;
    BOOL has3d;
    /* currently locked regions. */
    struct {
        DWORD offset, bytes;
    } locks[16];
    DWORD lock_count;
    DWORD priority;
};

struct DSDEVICE {
    IDirectSound8 iface;
    IUnknown unknown;
    DWORD magic;
    LONG ref;
    BOOL initialized;
    DWORD version;          /* 0x700 for IDirectSound, 0x800 for IDirectSound8. */
    GUID guid;
    DWORD level;
    DWORD thread;           /* of the cooperative window. */
    HWND window;
    DWORD speaker_config;
    DSBUFFER *primary;
    LONG primary_users;
    DSBUFFER *buffers;
    DSDEVICE *next;
    LISTENER3D listener;
};

#define DEVICE_MAGIC 0x44534F44
#define BUFFER_MAGIC 0x46425344

extern RTL_CRITICAL_SECTION DsLock;
extern HINSTANCE DsModule;
extern const GUID DsDeviceGuid;
extern const WCHAR DsDeviceName[];
extern const WCHAR DsDeviceModule[];
extern DSDEVICE *DsDevices;

/* main.c */
HRESULT DsCreateDevice(LPCGUID guid, DWORD version, IDirectSound8 **out);
BOOL DsResolveDevice(LPCGUID requested, GUID *out, BOOL capture);
void DsCopyFormat(WAVEFORMATEX *to, const WAVEFORMATEX *from);
DWORD DsFormatSize(const WAVEFORMATEX *f);
WORD DsFormatTag(const WAVEFORMATEX *f);
BOOL DsValidFormat(const WAVEFORMATEX *f);
float DsAmplitude(LONG hundredths);

/* private.c */
HRESULT DsCreatePrivate(REFIID riid, void **out);

/* device.c */
extern const IDirectSound8Vtbl DsDeviceVtbl;
DSDEVICE *DsDeviceFrom(void *iface);
HRESULT DsCreateBuffer(DSDEVICE *d, LPCDSBUFFERDESC desc, DSBUFFER **out);
void DsDeviceDestroy(DSDEVICE *d);

/* buffer.c */
DSBUFFER *DsBufferFrom(void *iface);
HRESULT DsBufferInit(DSBUFFER *b, DSDEVICE *d, const DSBUFFERDESC *desc);
HRESULT DsBufferDuplicate(DSBUFFER *original, DSBUFFER **out);
void DsBufferKill(DSBUFFER *b);
void DsBufferUpdateGains(DSBUFFER *b);
void DsPrimaryGains(DSDEVICE *d, float gains[2]);
HRESULT DsBufferQuery(DSBUFFER *b, REFIID riid, void **out);
ULONG DsBufferRelease(DSBUFFER *b);

/* mixer.c */
void DsMixerStart(void);
void DsMixerAdd(DSBUFFER *b);
void DsMixerRemove(DSBUFFER *b);
void DsMixerCursors(DSBUFFER *b, DWORD *play, DWORD *write);
void DsMixerStop(DSBUFFER *b);
void DsUpdateFocus(void);

/* sound3d.c */
extern const IDirectSound3DBufferVtbl DsBuffer3dVtbl;
extern const IDirectSound3DListenerVtbl DsListenerVtbl;
void Ds3dDefaults(DSBUFFER *b);
void DsListenerDefaults(LISTENER3D *l);
void Ds3dApply(DSBUFFER *b);
void Ds3dApplyAll(DSDEVICE *d);

/* capture.c */
HRESULT DsCreateCapture(LPCGUID guid, IDirectSoundCapture **out, BOOL v8, BOOL initialize);

#endif
