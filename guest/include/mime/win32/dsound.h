/* directsound: devices, buffers, 3d, notifications, capture and full duplex, in windows' vtable order. */
#ifndef MIME_WIN32_DSOUND_H
#define MIME_WIN32_DSOUND_H

#include <mime/win32/objbase.h>
#include <mime/win32/mmsystem.h>
#include <mime/win32/mmreg.h>

#ifdef MIME_BUILD_DSOUND
#define DSOUNDAPI DECLSPEC_EXPORT
#else
#define DSOUNDAPI DECLSPEC_IMPORT
#endif

#ifndef D3DVECTOR_DEFINED
#define D3DVECTOR_DEFINED
typedef struct _D3DVECTOR {
    float x, y, z;
} D3DVECTOR;
#endif
typedef D3DVECTOR *LPD3DVECTOR;
typedef const D3DVECTOR *LPCD3DVECTOR;
typedef float D3DVALUE;

#define _FACDS 0x878
#define MAKE_DSHRESULT(code) ((HRESULT)(0x88780000 | (code)))

#define DS_OK S_OK
#define DS_NO_VIRTUALIZATION ((HRESULT)0x0878000A)
#define DS_INCOMPLETE ((HRESULT)0x08780014)
#define DSERR_ALLOCATED MAKE_DSHRESULT(10)
#define DSERR_CONTROLUNAVAIL MAKE_DSHRESULT(30)
#define DSERR_INVALIDPARAM E_INVALIDARG
#define DSERR_INVALIDCALL MAKE_DSHRESULT(50)
#define DSERR_GENERIC E_FAIL
#define DSERR_PRIOLEVELNEEDED MAKE_DSHRESULT(70)
#define DSERR_OUTOFMEMORY E_OUTOFMEMORY
#define DSERR_BADFORMAT MAKE_DSHRESULT(100)
#define DSERR_UNSUPPORTED E_NOTIMPL
#define DSERR_NODRIVER MAKE_DSHRESULT(120)
#define DSERR_ALREADYINITIALIZED MAKE_DSHRESULT(130)
#define DSERR_NOAGGREGATION CLASS_E_NOAGGREGATION
#define DSERR_BUFFERLOST MAKE_DSHRESULT(150)
#define DSERR_OTHERAPPHASPRIO MAKE_DSHRESULT(160)
#define DSERR_UNINITIALIZED MAKE_DSHRESULT(170)
#define DSERR_NOINTERFACE E_NOINTERFACE
#define DSERR_ACCESSDENIED E_ACCESSDENIED
#define DSERR_BUFFERTOOSMALL MAKE_DSHRESULT(180)
#define DSERR_DS8_REQUIRED MAKE_DSHRESULT(190)
#define DSERR_SENDLOOP MAKE_DSHRESULT(200)
#define DSERR_BADSENDBUFFERGUID MAKE_DSHRESULT(210)
#define DSERR_OBJECTNOTFOUND MAKE_DSHRESULT(4449)
#define DSERR_FXUNAVAILABLE MAKE_DSHRESULT(220)

#define DSCAPS_PRIMARYMONO 0x00000001
#define DSCAPS_PRIMARYSTEREO 0x00000002
#define DSCAPS_PRIMARY8BIT 0x00000004
#define DSCAPS_PRIMARY16BIT 0x00000008
#define DSCAPS_CONTINUOUSRATE 0x00000010
#define DSCAPS_EMULDRIVER 0x00000020
#define DSCAPS_CERTIFIED 0x00000040
#define DSCAPS_SECONDARYMONO 0x00000100
#define DSCAPS_SECONDARYSTEREO 0x00000200
#define DSCAPS_SECONDARY8BIT 0x00000400
#define DSCAPS_SECONDARY16BIT 0x00000800

#define DSSCL_NORMAL 0x00000001
#define DSSCL_PRIORITY 0x00000002
#define DSSCL_EXCLUSIVE 0x00000003
#define DSSCL_WRITEPRIMARY 0x00000004

#define DSSPEAKER_DIRECTOUT 0x00000000
#define DSSPEAKER_HEADPHONE 0x00000001
#define DSSPEAKER_MONO 0x00000002
#define DSSPEAKER_QUAD 0x00000003
#define DSSPEAKER_STEREO 0x00000004
#define DSSPEAKER_SURROUND 0x00000005
#define DSSPEAKER_5POINT1 0x00000006
#define DSSPEAKER_7POINT1 0x00000007
#define DSSPEAKER_7POINT1_SURROUND 0x00000008
#define DSSPEAKER_5POINT1_SURROUND 0x00000009
#define DSSPEAKER_7POINT1_WIDE DSSPEAKER_7POINT1
#define DSSPEAKER_5POINT1_BACK DSSPEAKER_5POINT1
#define DSSPEAKER_GEOMETRY_MIN 0x00000005
#define DSSPEAKER_GEOMETRY_NARROW 0x0000000A
#define DSSPEAKER_GEOMETRY_WIDE 0x00000014
#define DSSPEAKER_GEOMETRY_MAX 0x000000B4
#define DSSPEAKER_COMBINED(c, g) ((DWORD)(((BYTE)(c)) | ((DWORD)((BYTE)(g))) << 16))
#define DSSPEAKER_CONFIG(a) ((BYTE)(a))
#define DSSPEAKER_GEOMETRY(a) ((BYTE)(((DWORD)(a) >> 16) & 0x00FF))

#define DSBCAPS_PRIMARYBUFFER 0x00000001
#define DSBCAPS_STATIC 0x00000002
#define DSBCAPS_LOCHARDWARE 0x00000004
#define DSBCAPS_LOCSOFTWARE 0x00000008
#define DSBCAPS_CTRL3D 0x00000010
#define DSBCAPS_CTRLFREQUENCY 0x00000020
#define DSBCAPS_CTRLPAN 0x00000040
#define DSBCAPS_CTRLVOLUME 0x00000080
#define DSBCAPS_CTRLPOSITIONNOTIFY 0x00000100
#define DSBCAPS_CTRLFX 0x00000200
#define DSBCAPS_STICKYFOCUS 0x00004000
#define DSBCAPS_GLOBALFOCUS 0x00008000
#define DSBCAPS_GETCURRENTPOSITION2 0x00010000
#define DSBCAPS_MUTE3DATMAXDISTANCE 0x00020000
#define DSBCAPS_LOCDEFER 0x00040000
#define DSBCAPS_TRUEPLAYPOSITION 0x00080000

#define DSBPLAY_LOOPING 0x00000001
#define DSBPLAY_LOCHARDWARE 0x00000002
#define DSBPLAY_LOCSOFTWARE 0x00000004
#define DSBPLAY_TERMINATEBY_TIME 0x00000008
#define DSBPLAY_TERMINATEBY_DISTANCE 0x000000010
#define DSBPLAY_TERMINATEBY_PRIORITY 0x000000020

#define DSBSTATUS_PLAYING 0x00000001
#define DSBSTATUS_BUFFERLOST 0x00000002
#define DSBSTATUS_LOOPING 0x00000004
#define DSBSTATUS_LOCHARDWARE 0x00000008
#define DSBSTATUS_LOCSOFTWARE 0x00000010
#define DSBSTATUS_TERMINATED 0x00000020

#define DSBLOCK_FROMWRITECURSOR 0x00000001
#define DSBLOCK_ENTIREBUFFER 0x00000002

#define DSBFREQUENCY_ORIGINAL 0
#define DSBFREQUENCY_MIN 100
#define DSBFREQUENCY_MAX 200000
#define DSBPAN_LEFT -10000
#define DSBPAN_CENTER 0
#define DSBPAN_RIGHT 10000
#define DSBVOLUME_MIN -10000
#define DSBVOLUME_MAX 0
#define DSBSIZE_MIN 4
#define DSBSIZE_MAX 0x0FFFFFFF
#define DSBSIZE_FX_MIN 150

#define DSBNOTIFICATIONS_MAX 100000UL
#define DSBPN_OFFSETSTOP 0xFFFFFFFF

#define DS3DMODE_NORMAL 0x00000000
#define DS3DMODE_HEADRELATIVE 0x00000001
#define DS3DMODE_DISABLE 0x00000002
#define DS3D_IMMEDIATE 0x00000000
#define DS3D_DEFERRED 0x00000001
#define DS3D_MINDISTANCEFACTOR FLT_MIN
#define DS3D_MAXDISTANCEFACTOR FLT_MAX
#define DS3D_DEFAULTDISTANCEFACTOR 1.0f
#define DS3D_MINROLLOFFFACTOR 0.0f
#define DS3D_MAXROLLOFFFACTOR 10.0f
#define DS3D_DEFAULTROLLOFFFACTOR 1.0f
#define DS3D_MINDOPPLERFACTOR 0.0f
#define DS3D_MAXDOPPLERFACTOR 10.0f
#define DS3D_DEFAULTDOPPLERFACTOR 1.0f
#define DS3D_DEFAULTMINDISTANCE 1.0f
#define DS3D_DEFAULTMAXDISTANCE 1000000000.0f
#define DS3D_MINCONEANGLE 0
#define DS3D_MAXCONEANGLE 360
#define DS3D_DEFAULTCONEANGLE 360
#define DS3D_DEFAULTCONEOUTSIDEVOLUME DSBVOLUME_MAX

#define DSCCAPS_EMULDRIVER DSCAPS_EMULDRIVER
#define DSCCAPS_CERTIFIED DSCAPS_CERTIFIED
#define DSCCAPS_MULTIPLECAPTURE 0x00000001
#define DSCBCAPS_WAVEMAPPED 0x80000000
#define DSCBCAPS_CTRLFX 0x00000200
#define DSCBLOCK_ENTIREBUFFER 0x00000001
#define DSCBSTATUS_CAPTURING 0x00000001
#define DSCBSTATUS_LOOPING 0x00000002
#define DSCBSTART_LOOPING 0x00000001
#define DSCBPN_OFFSETSTOP 0xFFFFFFFF

#define DSFX_LOCHARDWARE 0x00000001
#define DSFX_LOCSOFTWARE 0x00000002
#define DSFXR_PRESENT 0
#define DSFXR_LOCHARDWARE 1
#define DSFXR_LOCSOFTWARE 2
#define DSFXR_UNALLOCATED 3
#define DSFXR_FAILED 4
#define DSFXR_UNKNOWN 5
#define DSFXR_SENDLOOP 6

typedef struct _DSCAPS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwMinSecondarySampleRate;
    DWORD dwMaxSecondarySampleRate;
    DWORD dwPrimaryBuffers;
    DWORD dwMaxHwMixingAllBuffers;
    DWORD dwMaxHwMixingStaticBuffers;
    DWORD dwMaxHwMixingStreamingBuffers;
    DWORD dwFreeHwMixingAllBuffers;
    DWORD dwFreeHwMixingStaticBuffers;
    DWORD dwFreeHwMixingStreamingBuffers;
    DWORD dwMaxHw3DAllBuffers;
    DWORD dwMaxHw3DStaticBuffers;
    DWORD dwMaxHw3DStreamingBuffers;
    DWORD dwFreeHw3DAllBuffers;
    DWORD dwFreeHw3DStaticBuffers;
    DWORD dwFreeHw3DStreamingBuffers;
    DWORD dwTotalHwMemBytes;
    DWORD dwFreeHwMemBytes;
    DWORD dwMaxContigFreeHwMemBytes;
    DWORD dwUnlockTransferRateHwBuffers;
    DWORD dwPlayCpuOverheadSwBuffers;
    DWORD dwReserved1;
    DWORD dwReserved2;
} DSCAPS, *LPDSCAPS;
typedef const DSCAPS *LPCDSCAPS;

typedef struct _DSBCAPS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwUnlockTransferRate;
    DWORD dwPlayCpuOverhead;
} DSBCAPS, *LPDSBCAPS;
typedef const DSBCAPS *LPCDSBCAPS;

typedef struct _DSBUFFERDESC {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwReserved;
    LPWAVEFORMATEX lpwfxFormat;
    GUID guid3DAlgorithm;
} DSBUFFERDESC, *LPDSBUFFERDESC;
typedef const DSBUFFERDESC *LPCDSBUFFERDESC;

typedef struct _DSBUFFERDESC1 {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwReserved;
    LPWAVEFORMATEX lpwfxFormat;
} DSBUFFERDESC1, *LPDSBUFFERDESC1;
typedef const DSBUFFERDESC1 *LPCDSBUFFERDESC1;

typedef struct _DS3DBUFFER {
    DWORD dwSize;
    D3DVECTOR vPosition;
    D3DVECTOR vVelocity;
    DWORD dwInsideConeAngle;
    DWORD dwOutsideConeAngle;
    D3DVECTOR vConeOrientation;
    LONG lConeOutsideVolume;
    D3DVALUE flMinDistance;
    D3DVALUE flMaxDistance;
    DWORD dwMode;
} DS3DBUFFER, *LPDS3DBUFFER;
typedef const DS3DBUFFER *LPCDS3DBUFFER;

typedef struct _DS3DLISTENER {
    DWORD dwSize;
    D3DVECTOR vPosition;
    D3DVECTOR vVelocity;
    D3DVECTOR vOrientFront;
    D3DVECTOR vOrientTop;
    D3DVALUE flDistanceFactor;
    D3DVALUE flRolloffFactor;
    D3DVALUE flDopplerFactor;
} DS3DLISTENER, *LPDS3DLISTENER;
typedef const DS3DLISTENER *LPCDS3DLISTENER;

typedef struct _DSCCAPS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFormats;
    DWORD dwChannels;
} DSCCAPS, *LPDSCCAPS;
typedef const DSCCAPS *LPCDSCCAPS;

typedef struct _DSCEFFECTDESC {
    DWORD dwSize;
    DWORD dwFlags;
    GUID guidDSCFXClass;
    GUID guidDSCFXInstance;
    DWORD dwReserved1;
    DWORD dwReserved2;
} DSCEFFECTDESC, *LPDSCEFFECTDESC;
typedef const DSCEFFECTDESC *LPCDSCEFFECTDESC;

typedef struct _DSCBUFFERDESC1 {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwReserved;
    LPWAVEFORMATEX lpwfxFormat;
} DSCBUFFERDESC1, *LPDSCBUFFERDESC1;

typedef struct _DSCBUFFERDESC {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwReserved;
    LPWAVEFORMATEX lpwfxFormat;
    DWORD dwFXCount;
    LPDSCEFFECTDESC lpDSCFXDesc;
} DSCBUFFERDESC, *LPDSCBUFFERDESC;
typedef const DSCBUFFERDESC *LPCDSCBUFFERDESC;

typedef struct _DSCBCAPS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwReserved;
} DSCBCAPS, *LPDSCBCAPS;
typedef const DSCBCAPS *LPCDSCBCAPS;

typedef struct _DSBPOSITIONNOTIFY {
    DWORD dwOffset;
    HANDLE hEventNotify;
} DSBPOSITIONNOTIFY, *LPDSBPOSITIONNOTIFY;
typedef const DSBPOSITIONNOTIFY *LPCDSBPOSITIONNOTIFY;

typedef struct _DSEFFECTDESC {
    DWORD dwSize;
    DWORD dwFlags;
    GUID guidDSFXClass;
    DWORD_PTR dwReserved1;
    DWORD_PTR dwReserved2;
} DSEFFECTDESC, *LPDSEFFECTDESC;
typedef const DSEFFECTDESC *LPCDSEFFECTDESC;

typedef BOOL(CALLBACK *LPDSENUMCALLBACKA)(LPGUID guid, LPCSTR description, LPCSTR module, LPVOID context);
typedef BOOL(CALLBACK *LPDSENUMCALLBACKW)(LPGUID guid, LPCWSTR description, LPCWSTR module, LPVOID context);

typedef struct IDirectSound IDirectSound, *LPDIRECTSOUND;
typedef struct IDirectSound8 IDirectSound8, *LPDIRECTSOUND8;
typedef struct IDirectSoundBuffer IDirectSoundBuffer, *LPDIRECTSOUNDBUFFER;
typedef struct IDirectSoundBuffer8 IDirectSoundBuffer8, *LPDIRECTSOUNDBUFFER8;
typedef struct IDirectSound3DListener IDirectSound3DListener, *LPDIRECTSOUND3DLISTENER;
typedef struct IDirectSound3DBuffer IDirectSound3DBuffer, *LPDIRECTSOUND3DBUFFER;
typedef struct IDirectSoundCapture IDirectSoundCapture, *LPDIRECTSOUNDCAPTURE;
typedef struct IDirectSoundCaptureBuffer IDirectSoundCaptureBuffer, *LPDIRECTSOUNDCAPTUREBUFFER;
typedef struct IDirectSoundCaptureBuffer8 IDirectSoundCaptureBuffer8, *LPDIRECTSOUNDCAPTUREBUFFER8;
typedef struct IDirectSoundNotify IDirectSoundNotify, *LPDIRECTSOUNDNOTIFY;
typedef struct IDirectSoundFullDuplex IDirectSoundFullDuplex, *LPDIRECTSOUNDFULLDUPLEX;
typedef struct IKsPropertySet IKsPropertySet, *LPKSPROPERTYSET;
typedef IDirectSoundCapture IDirectSoundCapture8, *LPDIRECTSOUNDCAPTURE8;
typedef IDirectSound3DListener IDirectSound3DListener8, *LPDIRECTSOUND3DLISTENER8;
typedef IDirectSound3DBuffer IDirectSound3DBuffer8, *LPDIRECTSOUND3DBUFFER8;
typedef IDirectSoundNotify IDirectSoundNotify8, *LPDIRECTSOUNDNOTIFY8;

#define IDirectSound_METHODS(T)                                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateSoundBuffer)(T * This, LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER *buffer,         \
                                                  IUnknown *outer);                                                    \
    HRESULT(STDMETHODCALLTYPE *GetCaps)(T * This, LPDSCAPS caps);                                                      \
    HRESULT(STDMETHODCALLTYPE *DuplicateSoundBuffer)(T * This, LPDIRECTSOUNDBUFFER original,                           \
                                                     LPDIRECTSOUNDBUFFER *duplicate);                                  \
    HRESULT(STDMETHODCALLTYPE *SetCooperativeLevel)(T * This, HWND window, DWORD level);                               \
    HRESULT(STDMETHODCALLTYPE *Compact)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *GetSpeakerConfig)(T * This, LPDWORD config);                                            \
    HRESULT(STDMETHODCALLTYPE *SetSpeakerConfig)(T * This, DWORD config);                                              \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, LPCGUID device);

#define IDirectSound8_METHODS(T)                                                                                       \
    IDirectSound_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *VerifyCertification)(T * This, LPDWORD certified);

#define IDirectSoundBuffer_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetCaps)(T * This, LPDSBCAPS caps);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetCurrentPosition)(T * This, LPDWORD play, LPDWORD write);                            \
    HRESULT(STDMETHODCALLTYPE *GetFormat)(T * This, LPWAVEFORMATEX format, DWORD allocated, LPDWORD written);          \
    HRESULT(STDMETHODCALLTYPE *GetVolume)(T * This, LPLONG volume);                                                    \
    HRESULT(STDMETHODCALLTYPE *GetPan)(T * This, LPLONG pan);                                                          \
    HRESULT(STDMETHODCALLTYPE *GetFrequency)(T * This, LPDWORD frequency);                                             \
    HRESULT(STDMETHODCALLTYPE *GetStatus)(T * This, LPDWORD status);                                                   \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, LPDIRECTSOUND sound, LPCDSBUFFERDESC desc);                       \
    HRESULT(STDMETHODCALLTYPE *Lock)(T * This, DWORD offset, DWORD bytes, LPVOID * audio1, LPDWORD bytes1,             \
                                     LPVOID * audio2, LPDWORD bytes2, DWORD flags);                                    \
    HRESULT(STDMETHODCALLTYPE *Play)(T * This, DWORD reserved, DWORD priority, DWORD flags);                           \
    HRESULT(STDMETHODCALLTYPE *SetCurrentPosition)(T * This, DWORD position);                                          \
    HRESULT(STDMETHODCALLTYPE *SetFormat)(T * This, LPCWAVEFORMATEX format);                                           \
    HRESULT(STDMETHODCALLTYPE *SetVolume)(T * This, LONG volume);                                                      \
    HRESULT(STDMETHODCALLTYPE *SetPan)(T * This, LONG pan);                                                            \
    HRESULT(STDMETHODCALLTYPE *SetFrequency)(T * This, DWORD frequency);                                               \
    HRESULT(STDMETHODCALLTYPE *Stop)(T * This);                                                                        \
    HRESULT(STDMETHODCALLTYPE *Unlock)(T * This, LPVOID audio1, DWORD bytes1, LPVOID audio2, DWORD bytes2);           \
    HRESULT(STDMETHODCALLTYPE *Restore)(T * This);

#define IDirectSoundBuffer8_METHODS(T)                                                                                 \
    IDirectSoundBuffer_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *SetFX)(T * This, DWORD count, LPDSEFFECTDESC effects, LPDWORD results);                 \
    HRESULT(STDMETHODCALLTYPE *AcquireResources)(T * This, DWORD flags, DWORD count, LPDWORD results);                 \
    HRESULT(STDMETHODCALLTYPE *GetObjectInPath)(T * This, REFGUID object, DWORD index, REFGUID iid, LPVOID * out);

#define IDirectSound3DListener_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetAllParameters)(T * This, LPDS3DLISTENER listener);                                   \
    HRESULT(STDMETHODCALLTYPE *GetDistanceFactor)(T * This, D3DVALUE * factor);                                        \
    HRESULT(STDMETHODCALLTYPE *GetDopplerFactor)(T * This, D3DVALUE * factor);                                         \
    HRESULT(STDMETHODCALLTYPE *GetOrientation)(T * This, D3DVECTOR * front, D3DVECTOR * top);                          \
    HRESULT(STDMETHODCALLTYPE *GetPosition)(T * This, D3DVECTOR * position);                                           \
    HRESULT(STDMETHODCALLTYPE *GetRolloffFactor)(T * This, D3DVALUE * factor);                                         \
    HRESULT(STDMETHODCALLTYPE *GetVelocity)(T * This, D3DVECTOR * velocity);                                           \
    HRESULT(STDMETHODCALLTYPE *SetAllParameters)(T * This, LPCDS3DLISTENER listener, DWORD apply);                     \
    HRESULT(STDMETHODCALLTYPE *SetDistanceFactor)(T * This, D3DVALUE factor, DWORD apply);                             \
    HRESULT(STDMETHODCALLTYPE *SetDopplerFactor)(T * This, D3DVALUE factor, DWORD apply);                              \
    HRESULT(STDMETHODCALLTYPE *SetOrientation)(T * This, D3DVALUE xfront, D3DVALUE yfront, D3DVALUE zfront,            \
                                               D3DVALUE xtop, D3DVALUE ytop, D3DVALUE ztop, DWORD apply);              \
    HRESULT(STDMETHODCALLTYPE *SetPosition)(T * This, D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply);                \
    HRESULT(STDMETHODCALLTYPE *SetRolloffFactor)(T * This, D3DVALUE factor, DWORD apply);                              \
    HRESULT(STDMETHODCALLTYPE *SetVelocity)(T * This, D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply);                \
    HRESULT(STDMETHODCALLTYPE *CommitDeferredSettings)(T * This);

#define IDirectSound3DBuffer_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetAllParameters)(T * This, LPDS3DBUFFER buffer);                                       \
    HRESULT(STDMETHODCALLTYPE *GetConeAngles)(T * This, LPDWORD inside, LPDWORD outside);                              \
    HRESULT(STDMETHODCALLTYPE *GetConeOrientation)(T * This, D3DVECTOR * orientation);                                 \
    HRESULT(STDMETHODCALLTYPE *GetConeOutsideVolume)(T * This, LPLONG volume);                                         \
    HRESULT(STDMETHODCALLTYPE *GetMaxDistance)(T * This, D3DVALUE * distance);                                         \
    HRESULT(STDMETHODCALLTYPE *GetMinDistance)(T * This, D3DVALUE * distance);                                         \
    HRESULT(STDMETHODCALLTYPE *GetMode)(T * This, LPDWORD mode);                                                       \
    HRESULT(STDMETHODCALLTYPE *GetPosition)(T * This, D3DVECTOR * position);                                           \
    HRESULT(STDMETHODCALLTYPE *GetVelocity)(T * This, D3DVECTOR * velocity);                                           \
    HRESULT(STDMETHODCALLTYPE *SetAllParameters)(T * This, LPCDS3DBUFFER buffer, DWORD apply);                         \
    HRESULT(STDMETHODCALLTYPE *SetConeAngles)(T * This, DWORD inside, DWORD outside, DWORD apply);                     \
    HRESULT(STDMETHODCALLTYPE *SetConeOrientation)(T * This, D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply);         \
    HRESULT(STDMETHODCALLTYPE *SetConeOutsideVolume)(T * This, LONG volume, DWORD apply);                              \
    HRESULT(STDMETHODCALLTYPE *SetMaxDistance)(T * This, D3DVALUE distance, DWORD apply);                              \
    HRESULT(STDMETHODCALLTYPE *SetMinDistance)(T * This, D3DVALUE distance, DWORD apply);                              \
    HRESULT(STDMETHODCALLTYPE *SetMode)(T * This, DWORD mode, DWORD apply);                                            \
    HRESULT(STDMETHODCALLTYPE *SetPosition)(T * This, D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply);                \
    HRESULT(STDMETHODCALLTYPE *SetVelocity)(T * This, D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply);

#define IDirectSoundCapture_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateCaptureBuffer)(T * This, LPCDSCBUFFERDESC desc,                                   \
                                                    LPDIRECTSOUNDCAPTUREBUFFER *buffer, IUnknown *outer);              \
    HRESULT(STDMETHODCALLTYPE *GetCaps)(T * This, LPDSCCAPS caps);                                                     \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, LPCGUID device);

#define IDirectSoundCaptureBuffer_METHODS(T)                                                                           \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetCaps)(T * This, LPDSCBCAPS caps);                                                    \
    HRESULT(STDMETHODCALLTYPE *GetCurrentPosition)(T * This, LPDWORD capture, LPDWORD read);                          \
    HRESULT(STDMETHODCALLTYPE *GetFormat)(T * This, LPWAVEFORMATEX format, DWORD allocated, LPDWORD written);          \
    HRESULT(STDMETHODCALLTYPE *GetStatus)(T * This, LPDWORD status);                                                   \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, LPDIRECTSOUNDCAPTURE capture, LPCDSCBUFFERDESC desc);             \
    HRESULT(STDMETHODCALLTYPE *Lock)(T * This, DWORD offset, DWORD bytes, LPVOID * audio1, LPDWORD bytes1,             \
                                     LPVOID * audio2, LPDWORD bytes2, DWORD flags);                                    \
    HRESULT(STDMETHODCALLTYPE *Start)(T * This, DWORD flags);                                                          \
    HRESULT(STDMETHODCALLTYPE *Stop)(T * This);                                                                        \
    HRESULT(STDMETHODCALLTYPE *Unlock)(T * This, LPVOID audio1, DWORD bytes1, LPVOID audio2, DWORD bytes2);

#define IDirectSoundCaptureBuffer8_METHODS(T)                                                                          \
    IDirectSoundCaptureBuffer_METHODS(T)                                                                               \
    HRESULT(STDMETHODCALLTYPE *GetObjectInPath)(T * This, REFGUID object, DWORD index, REFGUID iid, LPVOID * out);     \
    HRESULT(STDMETHODCALLTYPE *GetFXStatus)(T * This, DWORD count, LPDWORD results);

#define IDirectSoundNotify_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetNotificationPositions)(T * This, DWORD count, LPCDSBPOSITIONNOTIFY notifies);

#define IKsPropertySet_METHODS(T)                                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Get)(T * This, REFGUID set, ULONG id, LPVOID instance, ULONG instance_size,             \
                                    LPVOID data, ULONG data_size, PULONG returned);                                    \
    HRESULT(STDMETHODCALLTYPE *Set)(T * This, REFGUID set, ULONG id, LPVOID instance, ULONG instance_size,             \
                                    LPVOID data, ULONG data_size);                                                     \
    HRESULT(STDMETHODCALLTYPE *QuerySupport)(T * This, REFGUID set, ULONG id, PULONG support);

#define IDirectSoundFullDuplex_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, LPCGUID capture_device, LPCGUID render_device,                    \
                                           LPCDSCBUFFERDESC capture_desc, LPCDSBUFFERDESC render_desc, HWND window,    \
                                           DWORD level, LPDIRECTSOUNDCAPTUREBUFFER8 *capture_buffer,                    \
                                           LPDIRECTSOUNDBUFFER8 *render_buffer);

MIME_COM_INTERFACE(IDirectSound)
MIME_COM_INTERFACE(IDirectSound8)
MIME_COM_INTERFACE(IDirectSoundBuffer)
MIME_COM_INTERFACE(IDirectSoundBuffer8)
MIME_COM_INTERFACE(IDirectSound3DListener)
MIME_COM_INTERFACE(IDirectSound3DBuffer)
MIME_COM_INTERFACE(IDirectSoundCapture)
MIME_COM_INTERFACE(IDirectSoundCaptureBuffer)
MIME_COM_INTERFACE(IDirectSoundCaptureBuffer8)
MIME_COM_INTERFACE(IDirectSoundNotify)
MIME_COM_INTERFACE(IKsPropertySet)
MIME_COM_INTERFACE(IDirectSoundFullDuplex)

#define KSPROPERTY_SUPPORT_GET 0x00000001
#define E_PROP_ID_UNSUPPORTED ((HRESULT)0x80070490)
#define E_PROP_SET_UNSUPPORTED ((HRESULT)0x80070492)
#define KSPROPERTY_SUPPORT_SET 0x00000002

MIME_DEFINE_GUID(CLSID_DirectSound, 0x47D4D946, 0x62E8, 0x11CF, 0x93, 0xBC, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(CLSID_DirectSound8, 0x3901CC3F, 0x84B5, 0x4FA4, 0xBA, 0x35, 0xAA, 0x81, 0x72, 0xB8, 0xA0, 0x9B);
MIME_DEFINE_GUID(CLSID_DirectSoundCapture, 0xB0210780, 0x89CD, 0x11D0, 0xAF, 0x08, 0x00, 0xA0, 0xC9, 0x25, 0xCD, 0x16);
MIME_DEFINE_GUID(CLSID_DirectSoundCapture8, 0xE4BCAC13, 0x7F99, 0x4908, 0x9A, 0x8E, 0x74, 0xE3, 0xBF, 0x24, 0xB6, 0xE1);
MIME_DEFINE_GUID(CLSID_DirectSoundFullDuplex, 0xFEA4300C, 0x7959, 0x4147, 0xB2, 0x6A, 0x23, 0x77, 0xB9, 0xE7, 0xA9, 0x1D);
MIME_DEFINE_GUID(CLSID_DirectSoundPrivate, 0x11AB3EC0, 0x25EC, 0x11D1, 0xA4, 0xD8, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA);
MIME_DEFINE_GUID(DSDEVID_DefaultPlayback, 0xDEF00000, 0x9C6D, 0x47ED, 0xAA, 0xF1, 0x4D, 0xDA, 0x8F, 0x2B, 0x5C, 0x03);
MIME_DEFINE_GUID(DSDEVID_DefaultCapture, 0xDEF00001, 0x9C6D, 0x47ED, 0xAA, 0xF1, 0x4D, 0xDA, 0x8F, 0x2B, 0x5C, 0x03);
MIME_DEFINE_GUID(DSDEVID_DefaultVoicePlayback, 0xDEF00002, 0x9C6D, 0x47ED, 0xAA, 0xF1, 0x4D, 0xDA, 0x8F, 0x2B, 0x5C, 0x03);
MIME_DEFINE_GUID(DSDEVID_DefaultVoiceCapture, 0xDEF00003, 0x9C6D, 0x47ED, 0xAA, 0xF1, 0x4D, 0xDA, 0x8F, 0x2B, 0x5C, 0x03);
MIME_DEFINE_GUID(IID_IDirectSound, 0x279AFA83, 0x4981, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);
MIME_DEFINE_GUID(IID_IDirectSound8, 0xC50A7E93, 0xF395, 0x4834, 0x9E, 0xF6, 0x7F, 0xA9, 0x9D, 0xE5, 0x09, 0x66);
MIME_DEFINE_GUID(IID_IDirectSoundBuffer, 0x279AFA85, 0x4981, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);
MIME_DEFINE_GUID(IID_IDirectSoundBuffer8, 0x6825A449, 0x7524, 0x4D82, 0x92, 0x0F, 0x50, 0xE3, 0x6A, 0xB3, 0xAB, 0x1E);
MIME_DEFINE_GUID(IID_IDirectSound3DListener, 0x279AFA84, 0x4981, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5,
                 0x60);
MIME_DEFINE_GUID(IID_IDirectSound3DBuffer, 0x279AFA86, 0x4981, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);
MIME_DEFINE_GUID(IID_IDirectSoundCapture, 0xB0210781, 0x89CD, 0x11D0, 0xAF, 0x08, 0x00, 0xA0, 0xC9, 0x25, 0xCD, 0x16);
MIME_DEFINE_GUID(IID_IDirectSoundCaptureBuffer, 0xB0210782, 0x89CD, 0x11D0, 0xAF, 0x08, 0x00, 0xA0, 0xC9, 0x25, 0xCD,
                 0x16);
MIME_DEFINE_GUID(IID_IDirectSoundCaptureBuffer8, 0x00990DF4, 0x0DBB, 0x4872, 0x83, 0x3E, 0x6D, 0x30, 0x3E, 0x80, 0xAE,
                 0xB6);
MIME_DEFINE_GUID(IID_IDirectSoundNotify, 0xB0210783, 0x89CD, 0x11D0, 0xAF, 0x08, 0x00, 0xA0, 0xC9, 0x25, 0xCD, 0x16);
MIME_DEFINE_GUID(IID_IKsPropertySet, 0x31EFAC30, 0x515C, 0x11D0, 0xA9, 0xAA, 0x00, 0xAA, 0x00, 0x61, 0xBE, 0x93);
MIME_DEFINE_GUID(IID_IDirectSoundFullDuplex, 0xEDCB4C7A, 0xDAAB, 0x4216, 0xA4, 0x2E, 0x6C, 0x50, 0x59, 0x6D, 0xDC, 0x1D);
MIME_DEFINE_GUID(DS3DALG_NO_VIRTUALIZATION, 0xC241333F, 0x1C1B, 0x11D2, 0x94, 0xF5, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA);
MIME_DEFINE_GUID(DS3DALG_HRTF_FULL, 0xC2413340, 0x1C1B, 0x11D2, 0x94, 0xF5, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA);
MIME_DEFINE_GUID(DS3DALG_HRTF_LIGHT, 0xC2413342, 0x1C1B, 0x11D2, 0x94, 0xF5, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA);
#define DS3DALG_DEFAULT GUID_NULL
MIME_DEFINE_GUID(GUID_All_Objects, 0xAA114DE5, 0xC262, 0x4169, 0xA1, 0xC8, 0x23, 0xD6, 0x98, 0xCC, 0x73, 0xB5);
MIME_DEFINE_GUID(DSPROPSETID_DirectSoundDevice, 0x84624F82, 0x25EC, 0x11D1, 0xA4, 0xD8, 0x00, 0xC0, 0x4F, 0xC2, 0x8A,
                 0xCA);

/* DSPROPSETID_DirectSoundDevice, a private property set listing devices and their mappings. */
typedef enum {
    DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A = 1,
    DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1 = 2,
    DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1 = 3,
    DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W = 4,
    DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A = 5,
    DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W = 6,
    DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A = 7,
    DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W = 8,
} DSPROPERTY_DIRECTSOUNDDEVICE;

typedef enum {
    DIRECTSOUNDDEVICE_TYPE_EMULATED,
    DIRECTSOUNDDEVICE_TYPE_VXD,
    DIRECTSOUNDDEVICE_TYPE_WDM,
} DIRECTSOUNDDEVICE_TYPE;

typedef enum {
    DIRECTSOUNDDEVICE_DATAFLOW_RENDER,
    DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE,
} DIRECTSOUNDDEVICE_DATAFLOW;

typedef struct _DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A_DATA {
    LPSTR DeviceName;
    DIRECTSOUNDDEVICE_DATAFLOW DataFlow;
    GUID DeviceId;
} DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A_DATA, *PDSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_A_DATA;

typedef struct _DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA {
    LPWSTR DeviceName;
    DIRECTSOUNDDEVICE_DATAFLOW DataFlow;
    GUID DeviceId;
} DSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA, *PDSPROPERTY_DIRECTSOUNDDEVICE_WAVEDEVICEMAPPING_W_DATA;

typedef struct _DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA {
    GUID DeviceId;
    CHAR DescriptionA[0x100];
    WCHAR DescriptionW[0x100];
    CHAR ModuleA[MAX_PATH];
    WCHAR ModuleW[MAX_PATH];
    DIRECTSOUNDDEVICE_TYPE Type;
    DIRECTSOUNDDEVICE_DATAFLOW DataFlow;
    ULONG WaveDeviceId;
    ULONG Devnode;
} DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA, *PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA;

typedef struct _DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA {
    DIRECTSOUNDDEVICE_TYPE Type;
    DIRECTSOUNDDEVICE_DATAFLOW DataFlow;
    GUID DeviceId;
    LPSTR Description;
    LPSTR Module;
    LPSTR Interface;
    ULONG WaveDeviceId;
} DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA, *PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA;

typedef struct _DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA {
    DIRECTSOUNDDEVICE_TYPE Type;
    DIRECTSOUNDDEVICE_DATAFLOW DataFlow;
    GUID DeviceId;
    LPWSTR Description;
    LPWSTR Module;
    LPWSTR Interface;
    ULONG WaveDeviceId;
} DSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA, *PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA;

typedef BOOL(CALLBACK *LPFNDIRECTSOUNDDEVICEENUMERATECALLBACK1)(PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_1_DATA,
                                                                LPVOID);
typedef BOOL(CALLBACK *LPFNDIRECTSOUNDDEVICEENUMERATECALLBACKA)(PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_A_DATA,
                                                                LPVOID);
typedef BOOL(CALLBACK *LPFNDIRECTSOUNDDEVICEENUMERATECALLBACKW)(PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA,
                                                                LPVOID);

typedef struct _DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1_DATA {
    LPFNDIRECTSOUNDDEVICEENUMERATECALLBACK1 Callback;
    LPVOID Context;
} DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1_DATA, *PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_1_DATA;

typedef struct _DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A_DATA {
    LPFNDIRECTSOUNDDEVICEENUMERATECALLBACKA Callback;
    LPVOID Context;
} DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A_DATA, *PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_A_DATA;

typedef struct _DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA {
    LPFNDIRECTSOUNDDEVICEENUMERATECALLBACKW Callback;
    LPVOID Context;
} DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA, *PDSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA;

DSOUNDAPI HRESULT WINAPI DirectSoundCreate(LPCGUID device, LPDIRECTSOUND *sound, IUnknown *outer);
DSOUNDAPI HRESULT WINAPI DirectSoundCreate8(LPCGUID device, LPDIRECTSOUND8 *sound, IUnknown *outer);
DSOUNDAPI HRESULT WINAPI DirectSoundEnumerateA(LPDSENUMCALLBACKA callback, LPVOID context);
DSOUNDAPI HRESULT WINAPI DirectSoundEnumerateW(LPDSENUMCALLBACKW callback, LPVOID context);
DSOUNDAPI HRESULT WINAPI DirectSoundCaptureCreate(LPCGUID device, LPDIRECTSOUNDCAPTURE *capture, IUnknown *outer);
DSOUNDAPI HRESULT WINAPI DirectSoundCaptureCreate8(LPCGUID device, LPDIRECTSOUNDCAPTURE8 *capture, IUnknown *outer);
DSOUNDAPI HRESULT WINAPI DirectSoundCaptureEnumerateA(LPDSENUMCALLBACKA callback, LPVOID context);
DSOUNDAPI HRESULT WINAPI DirectSoundCaptureEnumerateW(LPDSENUMCALLBACKW callback, LPVOID context);
DSOUNDAPI HRESULT WINAPI GetDeviceID(LPCGUID src, LPGUID dst);
DSOUNDAPI HRESULT WINAPI DirectSoundFullDuplexCreate(LPCGUID capture_device, LPCGUID render_device,
                                                     LPCDSCBUFFERDESC capture_desc, LPCDSBUFFERDESC render_desc,
                                                     HWND window, DWORD level, LPDIRECTSOUNDFULLDUPLEX *duplex,
                                                     LPDIRECTSOUNDCAPTUREBUFFER8 *capture_buffer,
                                                     LPDIRECTSOUNDBUFFER8 *render_buffer, IUnknown *outer);

#endif
