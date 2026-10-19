/*
 * shared direct3d resources and fences: the kernel objects behind the nt handles CreateSharedHandle makes, and the
 * services that make, open and synchronize them, in win32k gfxshare.c.
 *
 * an object is a shared resource, of the type DxgkSharedResource, or a synchronization object, DxgkSharedSyncObject. It names
 * the memory or semaphore the host keeps for the other processes by an id, and carries the bytes its creator
 * described the resource with, which an opening device builds its own object from. A resource can also be a keyed
 * mutex, a lock that is taken with a key and released with another.
 *
 * the services take one structure of explicit sizes, so 32-bit processes pass theirs as they are.
 */
#ifndef MIME_GFXSHARE_H
#define MIME_GFXSHARE_H

#include <mime/win32/winbase.h>

#define MIME_GFXSHARE_RESOURCE 1
#define MIME_GFXSHARE_SYNC 2

/* the bytes a creator describes its object with. */
#define MIME_GFXSHARE_MAX_DATA 256

/* services of NtGdiMimeGfxShare. */
#define MIME_GFXSHARE_CREATE 1     /* a new object: kind, data, flags, name; returns handle and id */
#define MIME_GFXSHARE_OPEN 2       /* describes the object of a handle: kind, data, size, flags; returns id */
#define MIME_GFXSHARE_OPEN_NAME 3  /* a handle to the object of a name */
#define MIME_GFXSHARE_OPEN_LEGACY 4 /* a handle to the object of a legacy shared handle */
#define MIME_GFXSHARE_ACQUIRE 5    /* takes a keyed mutex with a key, waiting timeout milliseconds */
#define MIME_GFXSHARE_RELEASE 6    /* releases it, leaving the key */
#define MIME_GFXSHARE_RETIRE 7     /* the creator is gone: its legacy shared handle stops opening */
#define MIME_GFXSHARE_ABANDON 8    /* the device that holds a keyed mutex is gone: the next acquire is told so */

/* flags of an object. */
#define MIME_GFXSHARE_INHERIT 0x1       /* create: the handle is inherited */
#define MIME_GFXSHARE_KEYED_MUTEX 0x2   /* create, and what open says: the resource is a keyed mutex too */
#define MIME_GFXSHARE_LEGACY 0x4        /* create: also reachable through a legacy shared handle, returned in legacy */

typedef struct MimeGfxShare {
    ULONG64 Handle;  /* in for open, acquire and release; out for create and open_name */
    ULONG64 Id;      /* out: the id the host knows the object by */
    ULONG64 Name;    /* in: a pointer to a wide string or 0 */
    ULONG64 Data;    /* in for create, out for open: a pointer to the description */
    ULONG64 Key;     /* the keyed mutex's key */
    ULONG Kind;
    ULONG Size;      /* the bytes of data; capacity in, bytes out when opening */
    ULONG Flags;
    ULONG Legacy;    /* the legacy shared handle: out for create, in for open_legacy and retire */
    ULONG Timeout;   /* milliseconds, 0xFFFFFFFF forever */
    ULONG Reserved;
} MimeGfxShare;

#endif
