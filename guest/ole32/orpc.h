/*
 * the object remote procedure call of com: how an object of one process is reached from another.
 *
 * An object leaves its process as an OBJREF: the identifier of the apartment that holds it (an OXID), the identifier
 * of the object (an OID), the identifier of the interface (an IPID) and where the apartment's calls are served. The
 * interface proxies a client makes for it, with the proxy and stub classes of the interfaces, and the stubs a server
 * makes for the interfaces it hands out, are kept by identity managers. The calls travel over the rpc runtime of rpcrt4
 * on an ncalrpc endpoint of each process, as rpc calls on the interface of the com interface, to the object identifier
 * that is the ipid, with the method number as procedure number. The stub data of a call begins with the 32 bytes of
 * ORPCTHIS and a reply with the 8 bytes of ORPCTHAT; the interfaces of the reference counting are served by ole32 itself.
 * The streams are those of windows' standard marshaling, except where noted.
 */
#ifndef MIME_OLE32_ORPC_H
#define MIME_OLE32_ORPC_H

#include "ole32_private.h"
#include <mime/win32/objidl.h>
#include <mime/win32/rpcproxy.h>

#define OBJREF_SIGNATURE 0x574F454D /* "MEOW" */
#define OBJREF_STANDARD 0x1
#define OBJREF_CUSTOM 0x4
/* Mime's own form, for marshaling inside a process: the pointer itself. */
#define OBJREF_MIME_DIRECT 0x10

#define ORPC_THIS_SIZE 32
#define ORPC_THAT_SIZE 8
/* the references to an interface that an object reference carries. */
#define ORPC_PUBLIC_REFS 5

/* the tower identifier of ncalrpc in a string binding of an object reference. */
#define NCALRPC_TOWER_ID 0x10

static const IID IID_IRemUnknown = {0x00000131, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
/* asked of a proxy to find out if it is one: the identity manager answers with itself. */
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

/* the ipid of the reference counting interface of an apartment: zeros, then the oxid. */
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
    /* the apartment is over: what it exported is gone. */
    volatile LONG Dead;
} APARTMENT;

/* the apartment of the calling thread; the multithreaded one for a thread that has none. */
APARTMENT *OrpcCurrentApartment(void);
/* the apartment of an oxid of this process, or null. */
APARTMENT *OrpcApartmentOf(ULONG64 oxid);
/* the apartment of a thread ends: its objects are disconnected. */
void OrpcApartmentLeave(OLE_THREAD *t);
/* runs a call in the apartment: on the thread of an apartment of single threads, in place for the multithreaded one. */
typedef void (*ORPC_RUN)(void *context);
HRESULT OrpcRunInApartment(APARTMENT *apartment, ORPC_RUN function, void *context);
/* waits for the reply of a call while serving the calls that come to the thread's apartment. */
DWORD WINAPI OrpcWait(DWORD count, const HANDLE *handles, DWORD timeout, void *context);

/* ------------------------------------------------------------------ the server side */

/* the endpoint of this process, started when the first object leaves it. */
HRESULT OrpcServerStart(const WCHAR **endpoint);
/* the dispatch function for every com interface served over rpc. */
void WINAPI OrpcDispatch(PRPC_MESSAGE message);

HRESULT OrpcMarshal(IStream *stream, REFIID iid, IUnknown *object, DWORD context, DWORD flags);
HRESULT OrpcMarshalSizeMax(ULONG *size, REFIID iid, IUnknown *object, DWORD context, DWORD flags);
/* both read what follows the head of an object reference, which the caller has read. */
HRESULT OrpcUnmarshalAfterHead(IStream *stream, const OBJREF_HEAD *head, REFIID iid, void **object);
HRESULT OrpcReleaseMarshalData(IStream *stream);
/* the factory of the proxies and stubs of an interface, referenced, or null. */
IPSFactoryBuffer *OrpcFactoryFor(REFIID iid);
BOOL OrpcAutomationClass(REFIID iid, CLSID *clsid);
BOOL OrpcBuiltinServer(REFCLSID clsid, const WCHAR **dll);
/* the class that marshals an interface ole32 carries itself, and its class object, which is not in a dll. */
BOOL OrpcBuiltinClass(REFIID iid, CLSID *clsid);
BOOL OrpcBuiltinClassObject(REFCLSID clsid, REFIID iid, void **object, HRESULT *hr);
/* the oleaut32 that told ole32 where it is, or null. */
HMODULE OleOleautModule(void);
/* CoDisconnectObject: the object is no longer served. */
HRESULT OrpcDisconnectObject(IUnknown *object);
/* the proxy manager an interface pointer belongs to, referenced, or null for any other pointer. */
struct PROXYMGR;
struct PROXYMGR *OrpcProxyManagerOf(IUnknown *object);
HRESULT OrpcMarshalProxy(struct PROXYMGR *manager, IStream *stream, REFIID iid, DWORD flags);
void OrpcProxyManagerRelease(struct PROXYMGR *manager);

/* the thread takes on the identity of the caller of the call it serves, or goes back to its own. */
HRESULT OrpcImpersonate(void *served, BOOL impersonate);

/* the ipid entry for a call: the stub to run, the object's apartment and the interface. */
typedef struct IPID_TARGET {
    IRpcStubBuffer *Stub;
    APARTMENT *Apartment;
    IID Iid;
} IPID_TARGET;
/* finds and references the stub of an ipid. */
HRESULT OrpcFindStub(const GUID *ipid, IPID_TARGET *target);
HRESULT OrpcRemUnknownCall(ULONG64 oxid, ULONG method, const BYTE *in, ULONG in_size, BYTE **out, ULONG *out_size);

/* ------------------------------------------------------------------ the client side */

/* the oxid entries: where the calls to an apartment of another process are sent. */
typedef struct OXID_ENTRY {
    struct OXID_ENTRY *Next;
    ULONG64 Oxid;
    WCHAR Endpoint[48];
    RPC_BINDING_HANDLE Binding;
} OXID_ENTRY;
OXID_ENTRY *OrpcOxidEntry(ULONG64 oxid, const WCHAR *endpoint);

/* a channel for the calls of one interface proxy. */
HRESULT OrpcChannelCreate(OXID_ENTRY *server, const GUID *ipid, REFIID iid, IRpcChannelBuffer **channel);
/* the reference counting calls to an apartment of another process. */
HRESULT OrpcRemCall(OXID_ENTRY *server, ULONG method, const BYTE *in, ULONG in_size, BYTE **out, ULONG *out_size);

/* ------------------------------------------------------------------ the ps classes of ole32's own interfaces */

/* the factory of the proxies and stubs of the interfaces ole32 knows. */
HRESULT OrpcBuiltinFactory(REFIID iid, IPSFactoryBuffer **factory);

/* ------------------------------------------------------------------ classes of local servers */

/* the running object table across processes, see orpc_rot.c. */
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
