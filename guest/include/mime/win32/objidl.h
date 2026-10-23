#ifndef MIME_WIN32_OBJIDL_H
#define MIME_WIN32_OBJIDL_H
#include <mime/win32/objbase.h>

typedef struct IStream IStream;
typedef struct tagSTATSTG {
    LPOLESTR pwcsName;
    DWORD type;
    ULARGE_INTEGER cbSize;
    FILETIME mtime, ctime, atime;
    DWORD grfMode, grfLocksSupported;
    CLSID clsid;
    DWORD grfStateBits, reserved;
} STATSTG;
typedef struct ISequentialStream ISequentialStream;
typedef struct ISequentialStreamVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ISequentialStream *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(ISequentialStream *);
    ULONG(STDMETHODCALLTYPE *Release)(ISequentialStream *);
    HRESULT(STDMETHODCALLTYPE *Read)(ISequentialStream *, void *, ULONG, ULONG *);
    HRESULT(STDMETHODCALLTYPE *Write)(ISequentialStream *, const void *, ULONG, ULONG *);
} ISequentialStreamVtbl;
struct ISequentialStream {
    const ISequentialStreamVtbl *lpVtbl;
};
typedef struct IStreamVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IStream *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IStream *);
    ULONG (STDMETHODCALLTYPE *Release)(IStream *);
    HRESULT (STDMETHODCALLTYPE *Read)(IStream *, void *, ULONG, ULONG *);
    HRESULT (STDMETHODCALLTYPE *Write)(IStream *, const void *, ULONG, ULONG *);
    HRESULT (STDMETHODCALLTYPE *Seek)(IStream *, LARGE_INTEGER, DWORD, ULARGE_INTEGER *);
    HRESULT (STDMETHODCALLTYPE *SetSize)(IStream *, ULARGE_INTEGER);
    HRESULT (STDMETHODCALLTYPE *CopyTo)(IStream *, IStream *, ULARGE_INTEGER, ULARGE_INTEGER *, ULARGE_INTEGER *);
    HRESULT (STDMETHODCALLTYPE *Commit)(IStream *, DWORD);
    HRESULT (STDMETHODCALLTYPE *Revert)(IStream *);
    HRESULT (STDMETHODCALLTYPE *LockRegion)(IStream *, ULARGE_INTEGER, ULARGE_INTEGER, DWORD);
    HRESULT (STDMETHODCALLTYPE *UnlockRegion)(IStream *, ULARGE_INTEGER, ULARGE_INTEGER, DWORD);
    HRESULT (STDMETHODCALLTYPE *Stat)(IStream *, STATSTG *, DWORD);
    HRESULT (STDMETHODCALLTYPE *Clone)(IStream *, IStream **);
} IStreamVtbl;
struct IStream { const IStreamVtbl *lpVtbl; };
MIME_DEFINE_GUID(IID_IStream, 0x0000000c, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_ISequentialStream, 0x0c733a30, 0x2a1c, 0x11ce, 0xad, 0xe5, 0, 0xaa, 0, 0x44, 0x77, 0x3d);

/* ------------------------------------------------------------------ storage errors and flags */

#ifndef STG_E_INVALIDFUNCTION
#define STG_E_INVALIDFUNCTION ((HRESULT)0x80030001L)
#endif
#ifndef STG_E_INVALIDPOINTER
#define STG_E_INVALIDPOINTER ((HRESULT)0x80030009L)
#endif
#ifndef STG_E_MEDIUMFULL
#define STG_E_MEDIUMFULL ((HRESULT)0x80030070L)
#endif
#ifndef STG_E_READFAULT
#define STG_E_READFAULT ((HRESULT)0x8003001EL)
#endif
#ifndef STG_E_WRITEFAULT
#define STG_E_WRITEFAULT ((HRESULT)0x8003001DL)
#endif
#ifndef STG_E_INVALIDNAME
#define STG_E_INVALIDNAME ((HRESULT)0x800300FCL)
#endif
#ifndef STG_E_FILEALREADYEXISTS
#define STG_E_FILEALREADYEXISTS ((HRESULT)0x80030050L)
#endif
#ifndef STG_E_INSUFFICIENTMEMORY
#define STG_E_INSUFFICIENTMEMORY ((HRESULT)0x80030008L)
#endif
#ifndef STG_E_INVALIDFLAG
#define STG_E_INVALIDFLAG ((HRESULT)0x800300FFL)
#endif
#ifndef STG_E_INVALIDPARAMETER
#define STG_E_INVALIDPARAMETER ((HRESULT)0x80030057L)
#endif
#ifndef MK_S_MONIKERALREADYREGISTERED
#define MK_S_MONIKERALREADYREGISTERED ((HRESULT)0x000401E7L)
#endif
#ifndef MK_E_NOTBOUND
#define MK_E_NOTBOUND ((HRESULT)0x800401E9L)
#endif
#ifndef MK_E_UNAVAILABLE
#define MK_E_UNAVAILABLE ((HRESULT)0x800401E3L)
#endif

#define STGM_DIRECT 0x00000000
#define STGM_READ 0x00000000
#define STGM_WRITE 0x00000001
#define STGM_READWRITE 0x00000002
#define STGM_SHARE_EXCLUSIVE 0x00000010
#define STGM_SHARE_DENY_WRITE 0x00000020
#define STGM_SHARE_DENY_READ 0x00000030
#define STGM_SHARE_DENY_NONE 0x00000040
#define STGM_CREATE 0x00001000
#define STGM_TRANSACTED 0x00010000
#define STGM_DELETEONRELEASE 0x04000000

#define STGC_DEFAULT 0
#define STATFLAG_DEFAULT 0
#define STATFLAG_NONAME 1

#define STREAM_SEEK_SET 0
#define STREAM_SEEK_CUR 1
#define STREAM_SEEK_END 2

#define STGTY_STORAGE 1
#define STGTY_STREAM 2
#define STGTY_LOCKBYTES 3
#define STGTY_PROPERTY 4

#define LOCK_WRITE 1
#define LOCK_EXCLUSIVE 2
#define LOCK_ONLYONCE 4

typedef LPOLESTR *SNB;
typedef struct IEnumSTATSTG IEnumSTATSTG;
typedef struct IEnumString IEnumString;
typedef struct IEnumUnknown IEnumUnknown;

/* enumerators of strings, objects and storage elements. */
#define MIME_ENUM_INTERFACE(name, type)                                                                                \
    typedef struct name##Vtbl {                                                                                        \
        HRESULT(STDMETHODCALLTYPE *QueryInterface)(name * This, REFIID riid, void **object);                           \
        ULONG(STDMETHODCALLTYPE *AddRef)(name * This);                                                                 \
        ULONG(STDMETHODCALLTYPE *Release)(name * This);                                                                \
        HRESULT(STDMETHODCALLTYPE *Next)(name * This, ULONG count, type *items, ULONG *fetched);                       \
        HRESULT(STDMETHODCALLTYPE *Skip)(name * This, ULONG count);                                                    \
        HRESULT(STDMETHODCALLTYPE *Reset)(name * This);                                                                \
        HRESULT(STDMETHODCALLTYPE *Clone)(name * This, name * *enumerator);                                            \
    } name##Vtbl;                                                                                                      \
    struct name {                                                                                                      \
        const name##Vtbl *lpVtbl;                                                                                      \
    };
MIME_ENUM_INTERFACE(IEnumSTATSTG, STATSTG)
MIME_ENUM_INTERFACE(IEnumString, LPOLESTR)
MIME_ENUM_INTERFACE(IEnumUnknown, IUnknown *)
MIME_DEFINE_GUID(IID_IEnumSTATSTG, 0x0000000d, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_IEnumString, 0x00000101, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_IEnumUnknown, 0x00000100, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);

/* ------------------------------------------------------------------ IStorage */

typedef struct IStorage IStorage, *LPSTORAGE;
typedef struct IStorageVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IStorage *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IStorage *This);
    ULONG(STDMETHODCALLTYPE *Release)(IStorage *This);
    HRESULT(STDMETHODCALLTYPE *CreateStream)(IStorage *This, LPCOLESTR name, DWORD mode, DWORD reserved1,
                                             DWORD reserved2, IStream **stream);
    HRESULT(STDMETHODCALLTYPE *OpenStream)(IStorage *This, LPCOLESTR name, void *reserved1, DWORD mode,
                                           DWORD reserved2, IStream **stream);
    HRESULT(STDMETHODCALLTYPE *CreateStorage)(IStorage *This, LPCOLESTR name, DWORD mode, DWORD reserved1,
                                              DWORD reserved2, IStorage **storage);
    HRESULT(STDMETHODCALLTYPE *OpenStorage)(IStorage *This, LPCOLESTR name, IStorage *priority, DWORD mode,
                                            SNB exclude, DWORD reserved, IStorage **storage);
    HRESULT(STDMETHODCALLTYPE *CopyTo)(IStorage *This, DWORD exclude_count, const IID *exclude_iids,
                                       SNB exclude_names, IStorage *dest);
    HRESULT(STDMETHODCALLTYPE *MoveElementTo)(IStorage *This, LPCOLESTR name, IStorage *dest, LPCOLESTR new_name,
                                              DWORD flags);
    HRESULT(STDMETHODCALLTYPE *Commit)(IStorage *This, DWORD flags);
    HRESULT(STDMETHODCALLTYPE *Revert)(IStorage *This);
    HRESULT(STDMETHODCALLTYPE *EnumElements)(IStorage *This, DWORD reserved1, void *reserved2, DWORD reserved3,
                                             IEnumSTATSTG **enumerator);
    HRESULT(STDMETHODCALLTYPE *DestroyElement)(IStorage *This, LPCOLESTR name);
    HRESULT(STDMETHODCALLTYPE *RenameElement)(IStorage *This, LPCOLESTR old_name, LPCOLESTR new_name);
    HRESULT(STDMETHODCALLTYPE *SetElementTimes)(IStorage *This, LPCOLESTR name, const FILETIME *created,
                                                const FILETIME *accessed, const FILETIME *modified);
    HRESULT(STDMETHODCALLTYPE *SetClass)(IStorage *This, REFCLSID clsid);
    HRESULT(STDMETHODCALLTYPE *SetStateBits)(IStorage *This, DWORD state, DWORD mask);
    HRESULT(STDMETHODCALLTYPE *Stat)(IStorage *This, STATSTG *stat, DWORD flags);
} IStorageVtbl;
struct IStorage {
    const IStorageVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ ILockBytes */

typedef struct ILockBytes ILockBytes, *LPLOCKBYTES;
typedef struct ILockBytesVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ILockBytes *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(ILockBytes *This);
    ULONG(STDMETHODCALLTYPE *Release)(ILockBytes *This);
    HRESULT(STDMETHODCALLTYPE *ReadAt)(ILockBytes *This, ULARGE_INTEGER offset, void *buffer, ULONG size,
                                       ULONG *read);
    HRESULT(STDMETHODCALLTYPE *WriteAt)(ILockBytes *This, ULARGE_INTEGER offset, const void *buffer, ULONG size,
                                        ULONG *written);
    HRESULT(STDMETHODCALLTYPE *Flush)(ILockBytes *This);
    HRESULT(STDMETHODCALLTYPE *SetSize)(ILockBytes *This, ULARGE_INTEGER size);
    HRESULT(STDMETHODCALLTYPE *LockRegion)(ILockBytes *This, ULARGE_INTEGER offset, ULARGE_INTEGER size,
                                           DWORD type);
    HRESULT(STDMETHODCALLTYPE *UnlockRegion)(ILockBytes *This, ULARGE_INTEGER offset, ULARGE_INTEGER size,
                                             DWORD type);
    HRESULT(STDMETHODCALLTYPE *Stat)(ILockBytes *This, STATSTG *stat, DWORD flags);
} ILockBytesVtbl;
struct ILockBytes {
    const ILockBytesVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ persistence */

typedef struct IPersistStream IPersistStream, *LPPERSISTSTREAM;
typedef struct IPersistStreamVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPersistStream *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPersistStream *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPersistStream *This);
    HRESULT(STDMETHODCALLTYPE *GetClassID)(IPersistStream *This, CLSID *clsid);
    HRESULT(STDMETHODCALLTYPE *IsDirty)(IPersistStream *This);
    HRESULT(STDMETHODCALLTYPE *Load)(IPersistStream *This, IStream *stream);
    HRESULT(STDMETHODCALLTYPE *Save)(IPersistStream *This, IStream *stream, BOOL clear_dirty);
    HRESULT(STDMETHODCALLTYPE *GetSizeMax)(IPersistStream *This, ULARGE_INTEGER *size);
} IPersistStreamVtbl;
struct IPersistStream {
    const IPersistStreamVtbl *lpVtbl;
};

typedef struct IPersistStorage IPersistStorage, *LPPERSISTSTORAGE;
typedef struct IPersistStorageVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPersistStorage *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPersistStorage *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPersistStorage *This);
    HRESULT(STDMETHODCALLTYPE *GetClassID)(IPersistStorage *This, CLSID *clsid);
    HRESULT(STDMETHODCALLTYPE *IsDirty)(IPersistStorage *This);
    HRESULT(STDMETHODCALLTYPE *InitNew)(IPersistStorage *This, IStorage *storage);
    HRESULT(STDMETHODCALLTYPE *Load)(IPersistStorage *This, IStorage *storage);
    HRESULT(STDMETHODCALLTYPE *Save)(IPersistStorage *This, IStorage *storage, BOOL same_as_load);
    HRESULT(STDMETHODCALLTYPE *SaveCompleted)(IPersistStorage *This, IStorage *storage);
    HRESULT(STDMETHODCALLTYPE *HandsOffStorage)(IPersistStorage *This);
} IPersistStorageVtbl;
struct IPersistStorage {
    const IPersistStorageVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ monikers and the running object table */

typedef struct IMoniker IMoniker, *LPMONIKER;
typedef struct IBindCtx IBindCtx, *LPBC, *LPBINDCTX;
typedef struct IEnumMoniker IEnumMoniker, *LPENUMMONIKER;
typedef struct IRunningObjectTable IRunningObjectTable, *LPRUNNINGOBJECTTABLE;

typedef struct IMonikerVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IMoniker *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IMoniker *This);
    ULONG(STDMETHODCALLTYPE *Release)(IMoniker *This);
    HRESULT(STDMETHODCALLTYPE *GetClassID)(IMoniker *This, CLSID *clsid);
    HRESULT(STDMETHODCALLTYPE *IsDirty)(IMoniker *This);
    HRESULT(STDMETHODCALLTYPE *Load)(IMoniker *This, IStream *stream);
    HRESULT(STDMETHODCALLTYPE *Save)(IMoniker *This, IStream *stream, BOOL clear_dirty);
    HRESULT(STDMETHODCALLTYPE *GetSizeMax)(IMoniker *This, ULARGE_INTEGER *size);
    HRESULT(STDMETHODCALLTYPE *BindToObject)(IMoniker *This, IBindCtx *context, IMoniker *left, REFIID riid,
                                             void **object);
    HRESULT(STDMETHODCALLTYPE *BindToStorage)(IMoniker *This, IBindCtx *context, IMoniker *left, REFIID riid,
                                              void **object);
    HRESULT(STDMETHODCALLTYPE *Reduce)(IMoniker *This, IBindCtx *context, DWORD how_far, IMoniker **left,
                                       IMoniker **reduced);
    HRESULT(STDMETHODCALLTYPE *ComposeWith)(IMoniker *This, IMoniker *right, BOOL only_if_not_generic,
                                            IMoniker **composite);
    HRESULT(STDMETHODCALLTYPE *Enum)(IMoniker *This, BOOL forward, IEnumMoniker **enumerator);
    HRESULT(STDMETHODCALLTYPE *IsEqual)(IMoniker *This, IMoniker *other);
    HRESULT(STDMETHODCALLTYPE *Hash)(IMoniker *This, DWORD *hash);
    HRESULT(STDMETHODCALLTYPE *IsRunning)(IMoniker *This, IBindCtx *context, IMoniker *left,
                                          IMoniker *newly_running);
    HRESULT(STDMETHODCALLTYPE *GetTimeOfLastChange)(IMoniker *This, IBindCtx *context, IMoniker *left,
                                                    FILETIME *time);
    HRESULT(STDMETHODCALLTYPE *Inverse)(IMoniker *This, IMoniker **inverse);
    HRESULT(STDMETHODCALLTYPE *CommonPrefixWith)(IMoniker *This, IMoniker *other, IMoniker **prefix);
    HRESULT(STDMETHODCALLTYPE *RelativePathTo)(IMoniker *This, IMoniker *other, IMoniker **relative);
    HRESULT(STDMETHODCALLTYPE *GetDisplayName)(IMoniker *This, IBindCtx *context, IMoniker *left,
                                               LPOLESTR *name);
    HRESULT(STDMETHODCALLTYPE *ParseDisplayName)(IMoniker *This, IBindCtx *context, IMoniker *left,
                                                 LPOLESTR name, ULONG *eaten, IMoniker **out);
    HRESULT(STDMETHODCALLTYPE *IsSystemMoniker)(IMoniker *This, DWORD *kind);
} IMonikerVtbl;
struct IMoniker {
    const IMonikerVtbl *lpVtbl;
};

typedef struct IEnumMonikerVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumMoniker *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumMoniker *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumMoniker *This);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumMoniker *This, ULONG count, IMoniker **items, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumMoniker *This, ULONG count);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumMoniker *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumMoniker *This, IEnumMoniker **clone);
} IEnumMonikerVtbl;
struct IEnumMoniker {
    const IEnumMonikerVtbl *lpVtbl;
};

#define ROTFLAGS_REGISTRATIONKEEPSALIVE 0x1
#define ROTFLAGS_ALLOWANYCLIENT 0x2

typedef struct IRunningObjectTableVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IRunningObjectTable *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IRunningObjectTable *This);
    ULONG(STDMETHODCALLTYPE *Release)(IRunningObjectTable *This);
    HRESULT(STDMETHODCALLTYPE *Register)(IRunningObjectTable *This, DWORD flags, IUnknown *object, IMoniker *name,
                                         DWORD *cookie);
    HRESULT(STDMETHODCALLTYPE *Revoke)(IRunningObjectTable *This, DWORD cookie);
    HRESULT(STDMETHODCALLTYPE *IsRunning)(IRunningObjectTable *This, IMoniker *name);
    HRESULT(STDMETHODCALLTYPE *GetObject)(IRunningObjectTable *This, IMoniker *name, IUnknown **object);
    HRESULT(STDMETHODCALLTYPE *NoteChangeTime)(IRunningObjectTable *This, DWORD cookie, FILETIME *time);
    HRESULT(STDMETHODCALLTYPE *GetTimeOfLastChange)(IRunningObjectTable *This, IMoniker *name, FILETIME *time);
    HRESULT(STDMETHODCALLTYPE *EnumRunning)(IRunningObjectTable *This, IEnumMoniker **enumerator);
} IRunningObjectTableVtbl;
struct IRunningObjectTable {
    const IRunningObjectTableVtbl *lpVtbl;
};

typedef struct tagBIND_OPTS {
    DWORD cbStruct;
    DWORD grfFlags;
    DWORD grfMode;
    DWORD dwTickCountDeadline;
} BIND_OPTS, *LPBIND_OPTS;

typedef struct tagBIND_OPTS2 {
    DWORD cbStruct;
    DWORD grfFlags;
    DWORD grfMode;
    DWORD dwTickCountDeadline;
    DWORD dwTrackFlags;
    DWORD dwClassContext;
    LCID locale;
    COSERVERINFO *pServerInfo;
} BIND_OPTS2, *LPBIND_OPTS2;

typedef struct tagBIND_OPTS3 {
    DWORD cbStruct;
    DWORD grfFlags;
    DWORD grfMode;
    DWORD dwTickCountDeadline;
    DWORD dwTrackFlags;
    DWORD dwClassContext;
    LCID locale;
    COSERVERINFO *pServerInfo;
    HWND hwnd;
} BIND_OPTS3, *LPBIND_OPTS3;

typedef struct IBindCtxVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IBindCtx *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IBindCtx *This);
    ULONG(STDMETHODCALLTYPE *Release)(IBindCtx *This);
    HRESULT(STDMETHODCALLTYPE *RegisterObjectBound)(IBindCtx *This, IUnknown *object);
    HRESULT(STDMETHODCALLTYPE *RevokeObjectBound)(IBindCtx *This, IUnknown *object);
    HRESULT(STDMETHODCALLTYPE *ReleaseBoundObjects)(IBindCtx *This);
    HRESULT(STDMETHODCALLTYPE *SetBindOptions)(IBindCtx *This, BIND_OPTS *options);
    HRESULT(STDMETHODCALLTYPE *GetBindOptions)(IBindCtx *This, BIND_OPTS *options);
    HRESULT(STDMETHODCALLTYPE *GetRunningObjectTable)(IBindCtx *This, IRunningObjectTable **table);
    HRESULT(STDMETHODCALLTYPE *RegisterObjectParam)(IBindCtx *This, LPOLESTR key, IUnknown *object);
    HRESULT(STDMETHODCALLTYPE *GetObjectParam)(IBindCtx *This, LPOLESTR key, IUnknown **object);
    HRESULT(STDMETHODCALLTYPE *EnumObjectParam)(IBindCtx *This, IEnumString **enumerator);
    HRESULT(STDMETHODCALLTYPE *RevokeObjectParam)(IBindCtx *This, LPOLESTR key);
} IBindCtxVtbl;
struct IBindCtx {
    const IBindCtxVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ message filters and call security */

typedef struct tagINTERFACEINFO {
    IUnknown *pUnk;
    IID iid;
    WORD wMethod;
} INTERFACEINFO, *LPINTERFACEINFO;

typedef struct IMessageFilterVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IMessageFilter *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IMessageFilter *This);
    ULONG(STDMETHODCALLTYPE *Release)(IMessageFilter *This);
    DWORD(STDMETHODCALLTYPE *HandleInComingCall)(IMessageFilter *This, DWORD call_type, HTASK caller, DWORD tick,
                                                  INTERFACEINFO *info);
    DWORD(STDMETHODCALLTYPE *RetryRejectedCall)(IMessageFilter *This, HTASK callee, DWORD tick, DWORD reject_type);
    DWORD(STDMETHODCALLTYPE *MessagePending)(IMessageFilter *This, HTASK callee, DWORD tick, DWORD pending_type);
} IMessageFilterVtbl;
struct IMessageFilter {
    const IMessageFilterVtbl *lpVtbl;
};

typedef struct tagSOLE_AUTHENTICATION_SERVICE {
    DWORD dwAuthnSvc;
    DWORD dwAuthzSvc;
    OLECHAR *pPrincipalName;
    HRESULT hr;
} SOLE_AUTHENTICATION_SERVICE;

typedef struct IClientSecurity IClientSecurity;
typedef struct IClientSecurityVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IClientSecurity *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IClientSecurity *This);
    ULONG(STDMETHODCALLTYPE *Release)(IClientSecurity *This);
    HRESULT(STDMETHODCALLTYPE *QueryBlanket)(IClientSecurity *This, IUnknown *proxy, DWORD *authn_service,
                                             DWORD *authz_service, OLECHAR **server_principal, DWORD *authn_level,
                                             DWORD *imp_level, void **auth_info, DWORD *capabilities);
    HRESULT(STDMETHODCALLTYPE *SetBlanket)(IClientSecurity *This, IUnknown *proxy, DWORD authn_service,
                                           DWORD authz_service, OLECHAR *server_principal, DWORD authn_level,
                                           DWORD imp_level, void *auth_info, DWORD capabilities);
    HRESULT(STDMETHODCALLTYPE *CopyProxy)(IClientSecurity *This, IUnknown *proxy, IUnknown **copy);
} IClientSecurityVtbl;
struct IClientSecurity {
    const IClientSecurityVtbl *lpVtbl;
};

MIME_DEFINE_GUID(IID_ILockBytes, 0x0000000a, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_IStorage, 0x0000000b, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_IBindCtx, 0x0000000e, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_IMoniker, 0x0000000f, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_IRunningObjectTable, 0x00000010, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_IMessageFilter, 0x00000016, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_IEnumMoniker, 0x00000102, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_IPersistStream, 0x00000109, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_IPersistStorage, 0x0000010a, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_IPersist, 0x0000010c, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_IClientSecurity, 0x0000013d, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);

/* ------------------------------------------------------------------ ole32 functions */

WINOLEAPI HRESULT WINAPI CreateStreamOnHGlobal(HGLOBAL, BOOL, IStream **);
WINOLEAPI HRESULT WINAPI GetHGlobalFromStream(IStream *, HGLOBAL *);
WINOLEAPI HRESULT WINAPI CreateILockBytesOnHGlobal(HGLOBAL memory, BOOL delete_on_release, ILockBytes **result);
WINOLEAPI HRESULT WINAPI GetHGlobalFromILockBytes(ILockBytes *bytes, HGLOBAL *memory);
WINOLEAPI HRESULT WINAPI StgIsStorageFile(LPCOLESTR name);
WINOLEAPI HRESULT WINAPI StgCreateDocfile(LPCOLESTR name, DWORD mode, DWORD reserved, IStorage **storage);
WINOLEAPI HRESULT WINAPI StgOpenStorage(LPCOLESTR name, IStorage *priority, DWORD mode, SNB exclude,
                                        DWORD reserved, IStorage **storage);
WINOLEAPI HRESULT WINAPI StgCreateDocfileOnILockBytes(ILockBytes *bytes, DWORD mode, DWORD reserved,
                                                      IStorage **storage);
WINOLEAPI HRESULT WINAPI StgOpenStorageOnILockBytes(ILockBytes *bytes, IStorage *priority, DWORD mode,
                                                    SNB exclude, DWORD reserved, IStorage **storage);
WINOLEAPI HRESULT WINAPI ReadClassStg(IStorage *storage, CLSID *clsid);
WINOLEAPI HRESULT WINAPI WriteClassStg(IStorage *storage, REFCLSID clsid);
WINOLEAPI HRESULT WINAPI WriteClassStm(IStream *stream, REFCLSID clsid);
WINOLEAPI HRESULT WINAPI ReadClassStm(IStream *stream, CLSID *clsid);
WINOLEAPI HRESULT WINAPI CoMarshalHresult(IStream *stream, HRESULT value);
WINOLEAPI HRESULT WINAPI CoUnmarshalHresult(IStream *stream, HRESULT *value);
WINOLEAPI HRESULT WINAPI GetClassFile(LPCOLESTR name, CLSID *clsid);
WINOLEAPI HRESULT WINAPI StgIsStorageILockBytes(ILockBytes *bytes);
WINOLEAPI HRESULT WINAPI StgSetTimes(LPCOLESTR name, const FILETIME *created, const FILETIME *accessed,
                                     const FILETIME *modified);
WINOLEAPI HRESULT WINAPI StgCreateStorageEx(LPCOLESTR name, DWORD mode, DWORD format, DWORD attributes, void *options,
                                            void *reserved, REFIID iid, void **result);
WINOLEAPI HRESULT WINAPI StgOpenStorageEx(LPCOLESTR name, DWORD mode, DWORD format, DWORD attributes, void *options,
                                          void *reserved, REFIID iid, void **result);
WINOLEAPI HRESULT WINAPI ReadFmtUserTypeStg(IStorage *storage, CLIPFORMAT *format, LPOLESTR *user_type);
WINOLEAPI HRESULT WINAPI WriteFmtUserTypeStg(IStorage *storage, CLIPFORMAT format, LPOLESTR user_type);
WINOLEAPI HRESULT WINAPI SetConvertStg(IStorage *storage, BOOL convert);
WINOLEAPI HRESULT WINAPI CreateBindCtx(DWORD reserved, IBindCtx **context);
WINOLEAPI HRESULT WINAPI GetRunningObjectTable(DWORD reserved, IRunningObjectTable **table);
WINOLEAPI HRESULT WINAPI CreateFileMoniker(LPCOLESTR path, IMoniker **moniker);
WINOLEAPI HRESULT WINAPI CreateItemMoniker(LPCOLESTR delimiter, LPCOLESTR item, IMoniker **moniker);
WINOLEAPI HRESULT WINAPI CreateGenericComposite(IMoniker *first, IMoniker *rest, IMoniker **composite);
WINOLEAPI HRESULT WINAPI CoInitializeSecurity(PSECURITY_DESCRIPTOR descriptor, LONG service_count,
                                              SOLE_AUTHENTICATION_SERVICE *services, void *reserved1,
                                              DWORD authn_level, DWORD imp_level, void *auth_list,
                                              DWORD capabilities, void *reserved3);
WINOLEAPI HRESULT WINAPI CoSetProxyBlanket(IUnknown *proxy, DWORD authn_service, DWORD authz_service,
                                           OLECHAR *server_principal, DWORD authn_level, DWORD imp_level,
                                           void *auth_info, DWORD capabilities);

/* ------------------------------------------------------------------ marshaling */

#ifndef MSHLFLAGS_NORMAL
#define MSHLFLAGS_NORMAL 0
#define MSHLFLAGS_TABLESTRONG 1
#define MSHLFLAGS_TABLEWEAK 2
#define MSHLFLAGS_NOPING 4
#define MSHCTX_LOCAL 0
#define MSHCTX_NOSHAREDMEM 1
#define MSHCTX_DIFFERENTMACHINE 2
#define MSHCTX_INPROC 3
#endif

WINOLEAPI HRESULT WINAPI CoMarshalInterface(IStream *stream, REFIID iid, IUnknown *object, DWORD destination,
                                            void *context, DWORD flags);
WINOLEAPI HRESULT WINAPI CoUnmarshalInterface(IStream *stream, REFIID iid, void **object);
WINOLEAPI HRESULT WINAPI CoReleaseMarshalData(IStream *stream);
WINOLEAPI HRESULT WINAPI CoGetMarshalSizeMax(ULONG *size, REFIID iid, IUnknown *object, DWORD destination,
                                             void *context, DWORD flags);
WINOLEAPI HRESULT WINAPI CoRegisterPSClsid(REFIID iid, REFCLSID clsid);
WINOLEAPI HRESULT WINAPI CoGetPSClsid(REFIID iid, CLSID *clsid);

#endif
