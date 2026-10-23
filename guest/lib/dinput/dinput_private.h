/*
 * shared by the parts of directinput. one engine serves dinput8.dll and
 * dinput.dll: each object or device knows which dll it belongs to and the
 * version it was initialized with.
 */
#ifndef DINPUT_PRIVATE_H
#define DINPUT_PRIVATE_H

#include <mime/nt.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/dinput.h>
#include <mime/win32/dinputd.h>

#define DI8_VERSION 0x0800
#define DI_MAX_PATH MAX_PATH

/* TRUE in dinput8.dll, FALSE in dinput.dll; set by DllMain. */
extern BOOL DiIs8;
extern HINSTANCE DiModule;
extern LONG DiLocks;
extern RTL_CRITICAL_SECTION DiLock;

HRESULT DiCheckVersion(HINSTANCE instance, DWORD version, BOOL di8);
HRESULT DiGetClassObject(REFCLSID clsid, REFIID riid, void **out);
extern const DIDATAFORMAT DiJoystickFormat;

typedef struct DIOBJ {
    const void *vtbl_w;
    const void *vtbl_a;
    LONG ref;
    BOOL di8;
    DWORD version; /* 0 until initialized. */
    void *joycfg;  /* dinput.dll's IDirectInputJoyConfig, made on first query. */
} DIOBJ;

HRESULT DiCreateObject(REFIID riid, void **out, BOOL di8);
/* joycfg.c */
void *DiJoyConfigFor(DIOBJ *obj);
void DiJoyConfigFree(DIOBJ *obj);
HRESULT DiCreateHelper(HINSTANCE instance, DWORD version, REFIID riid, void **out, IUnknown *outer, BOOL di8);

typedef struct DIDEV DIDEV;

/* a key, axis or button, at its place in the native state. */
typedef struct DIOBJECT {
    const GUID *guid;
    DWORD ofs;   /* offset in the native state. */
    DWORD type;  /* DIDFT_* plus the instance. */
    DWORD flags; /* DIDOI_* */
    /* hid objects only. */
    WORD page, usage, collection, designator;
    DWORD dimension;
    WORD exponent, report;
} DIOBJECT;

/* per class behavior of keyboards, mice and game controllers. */
typedef struct DICLASS {
    BOOL keyboard;
    HRESULT (*init)(DIDEV *dev);
    HRESULT (*cooperate)(DIDEV *dev, HWND window, DWORD flags);
    void (*acquire)(DIDEV *dev);
    void (*unacquire)(DIDEV *dev);
    void (*state)(DIDEV *dev, BYTE *native);
    /* false when the object has no name. */
    BOOL (*name)(DIDEV *dev, const DIOBJECT *object, WCHAR *name);
    /* E_NOTIMPL leaves the property to the device. */
    HRESULT (*get_property)(DIDEV *dev, DWORD id, const DIOBJECT *object, DIPROPHEADER *header);
    HRESULT (*set_property)(DIDEV *dev, DWORD id, const DIOBJECT *object, const DIPROPHEADER *header);
    /* starts the data source on acquisition; null uses the keyboard and mouse one. */
    HRESULT (*start)(DIDEV *dev);
    void (*stop)(DIDEV *dev);
    /* the object for a DIPH_BYUSAGE id or -1; null when the class has no usages. */
    int (*find_usage)(DIDEV *dev, DWORD usage);
    void (*release)(DIDEV *dev);
    const WCHAR *control_panel;
} DICLASS;

extern const DICLASS DiKeyboardClass;
extern const DICLASS DiMouseClass;
extern const DICLASS DiHidClass;

/* one buffered event, as GetDeviceData hands it out. */
typedef struct DIEVENT {
    DWORD ofs;
    DWORD data;
    DWORD time;
    DWORD sequence;
    UINT_PTR appdata;
} DIEVENT;

#define DI_MAX_OBJECTS 256
#define DI_MAX_STATE 1024

/* per instance emulation flags. */
#define DIEM_EXCLUSIVE 0x01
#define DIEM_NOWINKEY 0x20

struct DIDEV {
    const void *vtbl_w;
    const void *vtbl_a;
    LONG ref;
    BOOL di8;
    DWORD version;      /* 0 before Initialize. */
    const DICLASS *cls; /* null before Initialize. */
    RTL_CRITICAL_SECTION lock;
    GUID instance;
    GUID product;
    DWORD devtype;
    DWORD caps;         /* DIDC_* */
    DWORD axes, buttons, povs;
    WCHAR name[DI_MAX_PATH];
    DIOBJECT objects[DI_MAX_OBJECTS];
    DWORD count;
    DWORD native_size;
    /* the application's data format. */
    BOOL formatted;
    DWORD app_size;
    DWORD app_ofs[DI_MAX_OBJECTS]; /* per object, ~0 when not in the format. */
    UINT_PTR appdata[DI_MAX_OBJECTS];
    DWORD missing_povs[16];        /* offsets of optional povs the device lacks. */
    DWORD missing_count;
    BOOL relative;                 /* DIPROPAXISMODE_REL */
    /* cooperation and acquisition. */
    HWND window;
    DWORD coop;
    DWORD em;                      /* DIEM_* */
    BOOL acquired;
    HRESULT status;                /* what reads report while not acquired. */
    BOOL lost_iconic;
    LONG baseline[3];              /* relative axis values at the last read. */
    POINT saved_cursor;
    BOOL hid_cursor;
    /* buffered input. */
    DWORD buffer_prop;             /* as set. */
    DWORD max_buffer;
    DIEVENT *ring;
    DWORD slots, head, tail;
    BOOL overflow;
    HANDLE event;
    DWORD ffgain;
    WCHAR user[DI_MAX_PATH];
    LIST_ENTRY links;              /* in the list of acquired devices. */
    /* hid only: class data, top level usage, and the c_dfDIJoystick offsets old versions report. */
    void *hid;
    WORD usage_page, usage;
    BOOL legacy;
    DWORD legacy_ofs[DI_MAX_OBJECTS];
};

HRESULT DiCreateDevice(DIOBJ *obj, REFGUID guid, REFIID riid, void **out, IUnknown *outer);
HRESULT DiNewDevice(REFIID riid, void **out, BOOL di8);
DIDEV *DiDeviceFrom(void *iface);
BOOL DiDeviceWide(void *iface);
HRESULT DiDeviceInfo(DIDEV *dev, DIDEVICEINSTANCEW *info);
HRESULT DiDeviceCaps(DIDEV *dev, DIDEVCAPS *caps);
void DiDeviceLost(DIDEV *dev);

/* format.c */
HRESULT DiSetDataFormat(DIDEV *dev, const DIDATAFORMAT *format);
void DiReadState(DIDEV *dev, BYTE *out);
int DiFindObject(DIDEV *dev, DWORD id, DWORD how);

/* emul.c: where keyboard and mouse input comes from. */
extern BYTE DiKeyState[256];
typedef struct DIMOUSE {
    LONG x, y, z;
    BYTE buttons[8];
} DIMOUSE;
extern DIMOUSE DiMouseState;
HRESULT DiEmulAcquire(DIDEV *dev);
void DiEmulUnacquire(DIDEV *dev);
void DiEmulForeground(DIDEV *dev);
BOOL DiExclusiveTake(DIDEV *dev, HRESULT *hr);
void DiExclusiveRelease(DIDEV *dev);
DWORD DiNextSequence(void);
BOOL DiStartWorker(void);
extern HANDLE DiWorker;
LONG DiNextUuidCounter(void);
void DiBufferEvent(DIDEV *dev, DWORD native_ofs, DWORD data, DWORD time, DWORD sequence);
void DiSignal(DIDEV *dev);

/* hid.c: the system's hid collections, listed the way windows lists them. */
void DiHidRebuild(BOOL force);
DWORD DiHidCount(void);
BOOL DiHidEntry(DWORD index, GUID *instance, BYTE *type7, BYTE *type8);
BOOL DiHidLookup(const GUID *guid, DWORD *index);
HRESULT DiHidInit(DIDEV *dev, DWORD index);
BOOL DiHidFindPath(const WCHAR *path, GUID *instance);
BOOL DiHidByJoystick(DWORD id, GUID *instance, USHORT *vid, USHORT *pid, USHORT *usage);
const WCHAR *DiHidUsageName(USHORT page, USHORT usage);

/* names.c */
const WCHAR *DiKeyFixedName(DWORD dik);
const WCHAR *DiString(DWORD id);
BOOL DiKeyboardKeyName(DWORD scancode, DWORD dik, WCHAR *name, BOOL di8);

/* effects.c */
HRESULT DiEnumEffectsInFile(LPCWSTR file, LPDIENUMEFFECTSINFILECALLBACK callback, void *context, DWORD flags);
HRESULT DiWriteEffectToFile(LPCWSTR file, DWORD count, LPDIFILEEFFECT effects, DWORD flags);

/* action.c, dinput8 only. */
HRESULT DiBuildActionMap(DIDEV *dev, DIACTIONFORMATW *format, LPCWSTR user, DWORD flags);
HRESULT DiSetActionMap(DIDEV *dev, DIACTIONFORMATW *format, LPCWSTR user, DWORD flags);
HRESULT DiCheckActionFormat(const DIACTIONFORMATW *format, BOOL wide);
HRESULT DiActionFormatToW(const DIACTIONFORMATA *a, DIACTIONFORMATW *w, DIACTIONW **actions);
void DiActionFormatBack(DIACTIONFORMATA *a, const DIACTIONFORMATW *w);
HRESULT DiEnumDevicesBySemantics(DIOBJ *obj, LPCWSTR user, DIACTIONFORMATW *format, void *callback, void *context,
                                 DWORD flags, BOOL wide);

static inline void DiCopyW(WCHAR *dst, const WCHAR *src, SIZE_T size)
{
    SIZE_T i = 0;
    for (; src && i + 1 < size && src[i]; i++)
        dst[i] = src[i];
    if (size)
        dst[i] = 0;
}

static inline void DiToA(CHAR *dst, const WCHAR *src, int size)
{
    if (!WideCharToMultiByte(CP_ACP, 0, src, -1, dst, size, nullptr, nullptr) && size)
        dst[size - 1] = 0;
}

#endif
