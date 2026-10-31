/*
 * object rpc: how COM reaches an object in another process.
 *
 * an object leaves its process as an OBJREF naming the apartment holding it by OXID, the object by OID, the interface by
 * IPID, and where the apartment's calls are served. identity managers keep the interface proxies a client makes and the
 * stubs a server makes for the interfaces it hands out. calls travel over rpcrt4 on one ncalrpc endpoint per process,
 * as rpc calls on the COM interface with the ipid as object id and the method number as procedure number. request
 * stub data begins with the 32 byte ORPCTHIS and replies with the 8 byte ORPCTHAT. ole32 serves the reference counting
 * interfaces itself. streams follow windows' standard marshaling except where noted.
 */
#ifndef MIME_OLE32_ORPC_H
#define MIME_OLE32_ORPC_H

#include "ole32_private.h"
#include <mime/win32/objidl.h>
#include <mime/win32/rpcproxy.h>

#define OBJREF_SIGNATURE 0x574F454D /* "MEOW" */
#define OBJREF_STANDARD 0x1
#define OBJREF_CUSTOM 0x4
/* a private form for marshaling inside a process: the pointer itself. */
#define OBJREF_MIME_DIRECT 0x10

#define ORPC_THIS_SIZE 32
#define ORPC_THAT_SIZE 8
/* interface references an object reference carries. */
#define ORPC_PUBLIC_REFS 5

/* ncalrpc's tower id in an object reference's string binding. */
#define NCALRPC_TOWER_ID 0x10

static const IID IID_IRemUnknown = {0x00000131, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
/* tells proxies apart: a proxy's identity manager answers with itself. */
static const IID IID_IMimeProxyManager = {0x4d494d45, 0x5052, 0x4f58, {0x59, 0x4d, 0x47, 0x52, 0x00, 0x00, 0x00, 0x01}};

#define RPC_E_DISCONNECTED_HR ((HRESULT)0x80010108L)
#define RPC_E_INVALID_OBJREF_HR ((HRESULT)0x8001011DL)
#define RPC_E_INVALID_IPID_HR ((HRESULT)0x80010113L)
#define RPC_E_SERVER_DIED_DNE_HR ((HRESULT)0x80010012L)
#define RPC_E_INVALID_OBJECT_HR ((HRESULT)0x80010114L)
#define CO_E_OBJNOTCONNECTED_HR ((HRESULT)0x800401FDL)
#define RPC_E_INVALID_DATA_HR ((HRESULT)0x8001000FL)
#define RPC_E_INVALIDMETHOD_HR ((HRESULT)0x80010107L)

#pragma pack(push, 1)
typedef struct STDOBJREF {
    ULONG Flags;
    ULONG PublicRefs;
    ULONG64 Oxid;
    ULONG64 Oid;
    GUID Ipid;
} STDOBJREF;

typedef struct OBJREF_HEAD {
    DWORD Signature;
    DWORD Flags;
    IID Iid;
} OBJREF_HEAD;
#pragma pack(pop)

/* an apartment's reference counting ipid: zeros, then the oxid. */
void OrpcRemUnknownIpid(ULONG64 oxid, GUID *ipid);
BOOL OrpcIsRemUnknownIpid(const GUID *ipid, ULONG64 *oxid);

/* ------------------------------------------------------------------ apartments */

typedef struct APARTMENT {
    struct APARTMENT *Next;
    ULONG64 Oxid;
    BOOL Mta;
    DWORD ThreadId;
    HANDLE Thread;
    HWND Window;
    /* set when the apartment ends, its exports are gone. */
    volatile LONG Dead;
} APARTMENT;

/* the multithreaded apartment for a thread that has none. */
APARTMENT *OrpcCurrentApartment(void);
/* null when no apartment of this process has the oxid. */
APARTMENT *OrpcApartmentOf(ULONG64 oxid);
/* disconnects the objects of the thread's ending apartment. */
void OrpcApartmentLeave(OLE_THREAD *t);
/* runs on the apartment's thread for a single threaded apartment, in place for the multithreaded one. */
typedef void (*ORPC_RUN)(void *context);
HRESULT OrpcRunInApartment(APARTMENT *apartment, ORPC_RUN function, void *context);
/* the event that wakes the thread of a single threaded apartment for a queued call, and the run of the queued calls. */
HANDLE OrpcApartmentWake(APARTMENT *apartment);
void OrpcApartmentRun(APARTMENT *apartment);
/* waits for a reply while serving the calls that come to the thread's apartment. */
DWORD WINAPI OrpcWait(DWORD count, const HANDLE *handles, DWORD timeout, void *context);

/* ------------------------------------------------------------------ server side */

/* started when the first object leaves the process. */
HRESULT OrpcServerStart(const WCHAR **endpoint);
/* dispatches every COM interface served over rpc. */
void WINAPI OrpcDispatch(PRPC_MESSAGE message);

HRESULT OrpcMarshal(IStream *stream, REFIID iid, IUnknown *object, DWORD context, DWORD flags);
HRESULT OrpcMarshalSizeMax(ULONG *size, REFIID iid, IUnknown *object, DWORD context, DWORD flags);
/* both read what follows an object reference head the caller already read. */
HRESULT OrpcUnmarshalAfterHead(IStream *stream, const OBJREF_HEAD *head, REFIID iid, void **object);
HRESULT OrpcReleaseMarshalData(IStream *stream);
/* the proxy and stub factory of an interface, referenced, or null. */
IPSFactoryBuffer *OrpcFactoryFor(REFIID iid);
BOOL OrpcAutomationClass(REFIID iid, CLSID *clsid);
BOOL OrpcBuiltinServer(REFCLSID clsid, const WCHAR **dll);
/* the marshaling class of an interface ole32 carries itself, and its class object, which lives in no dll. */
BOOL OrpcBuiltinClass(REFIID iid, CLSID *clsid);
BOOL OrpcBuiltinClassObject(REFCLSID clsid, REFIID iid, void **object, HRESULT *hr);
/* the oleaut32 module that registered itself with ole32, or null. */
HMODULE OleOleautModule(void);
/* backs CoDisconnectObject. */
HRESULT OrpcDisconnectObject(IUnknown *object);
/* the referenced proxy manager of an interface pointer, or null for any other pointer. */
struct PROXYMGR;
struct PROXYMGR *OrpcProxyManagerOf(IUnknown *object);
HRESULT OrpcMarshalProxy(struct PROXYMGR *manager, IStream *stream, REFIID iid, DWORD flags);
void OrpcProxyManagerRelease(struct PROXYMGR *manager);

/* the thread impersonates the caller of the call it serves, or reverts to itself. */
HRESULT OrpcImpersonate(void *served, BOOL impersonate);

/* what an ipid resolves to for a call. */
typedef struct IPID_TARGET {
    IRpcStubBuffer *Stub;
    APARTMENT *Apartment;
    IID Iid;
} IPID_TARGET;
/* references the stub it finds. */
HRESULT OrpcFindStub(const GUID *ipid, IPID_TARGET *target);
HRESULT OrpcRemUnknownCall(ULONG64 oxid, ULONG method, const BYTE *in, ULONG in_size, BYTE **out, ULONG *out_size);

/* ------------------------------------------------------------------ client side */

/* where calls to an apartment of another process are sent. */
typedef struct OXID_ENTRY {
    struct OXID_ENTRY *Next;
    ULONG64 Oxid;
    WCHAR Endpoint[48];
    RPC_BINDING_HANDLE Binding;
} OXID_ENTRY;
OXID_ENTRY *OrpcOxidEntry(ULONG64 oxid, const WCHAR *endpoint);

/* one channel per interface proxy. */
HRESULT OrpcChannelCreate(OXID_ENTRY *server, const GUID *ipid, REFIID iid, IRpcChannelBuffer **channel);
/* reference counting calls to an apartment of another process. */
HRESULT OrpcRemCall(OXID_ENTRY *server, ULONG method, const BYTE *in, ULONG in_size, BYTE **out, ULONG *out_size);

/* ------------------------------------------------------------------ proxy and stub classes of ole32's own interfaces */

HRESULT OrpcBuiltinFactory(REFIID iid, IPSFactoryBuffer **factory);

/* ------------------------------------------------------------------ machine wide tables */

/* the machine wide running object table, see orpc_rot.c. */
HRESULT OrpcRotPublish(const WCHAR *key, IUnknown *object, DWORD flags, DWORD cookie, const FILETIME *time);
void OrpcRotRevoke(DWORD cookie);
void OrpcRotNoteChange(DWORD cookie, const FILETIME *time);
BOOL OrpcRotLookup(const WCHAR *key, BYTE *objref, ULONG *size, FILETIME *time);
HRESULT OrpcRotUnmarshal(const BYTE *objref, ULONG size, REFIID iid, void **object);
#define ROT_OBJREF_BYTES 320

HRESULT OrpcClassPublish(REFCLSID clsid, IUnknown *factory, DWORD flags, DWORD cookie);
void OrpcClassRevoke(DWORD cookie);
HRESULT OrpcClassGet(REFCLSID clsid, REFIID iid, void **object);

#endif
