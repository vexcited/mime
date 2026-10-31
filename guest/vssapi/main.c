/*
 * vssapi.dll, the client side of volume shadow copies. this system has no shadow copy service and no event system, so what
 * needs them says so the way it does on a machine without them: a writer takes its arguments and checks them like the
 * system does, and subscribing to the events of a backup fails with the error of a missing writer infrastructure. what asks
 * the service has the error of a service that is not there. the classes are the ones of the sdk: the writer is the pair of
 * a table of virtual calls and a pointer to the object that does the work, and a program derives its own writer from it.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>
#include <mime/win32/objbase.h>

#define VSSAPI_EXPORT DECLSPEC_EXPORT
#define NAME(mangled) __asm__(mangled)

#define VSS_E_BAD_STATE ((HRESULT)0x80042301)
#define VSS_E_WRITER_INFRASTRUCTURE ((HRESULT)0x80042318)
#define VSS_E_WRITER_ALREADY_SUBSCRIBED ((HRESULT)0x8004231A)
#define SM_BACKUP_AND_RESTORE_EVENTS 6

WINADVAPI BOOL WINAPI CheckTokenMembership(HANDLE token, PSID sid, PBOOL member);
WINADVAPI BOOL WINAPI CreateWellKnownSid(int type, PSID domain, PSID sid, DWORD *size);
#define WIN_BUILTIN_ADMINISTRATORS_SID 26

static const GUID CLSID_VssEvent = {0xFAF53CC4, 0xBD73, 0x4E36, {0x83, 0xF1, 0x2B, 0x23, 0xF4, 0x6E, 0x51, 0x3E}};
static const GUID IID_IClassFactory_ = {0x00000001, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_IUnknown_ = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

static BOOL is_administrator(void)
{
    BYTE sid[68];
    DWORD size = sizeof(sid);
    BOOL member = FALSE;
    if (!CreateWellKnownSid(WIN_BUILTIN_ADMINISTRATORS_SID, nullptr, sid, &size))
        return FALSE;
    return CheckTokenMembership(nullptr, sid, &member) && member;
}

static WCHAR *copy_text(const WCHAR *text)
{
    SIZE_T length = wcslen(text) + 1;
    WCHAR *copy = HeapAlloc(GetProcessHeap(), 0, length * sizeof(WCHAR));
    if (copy)
        memcpy(copy, text, length * sizeof(WCHAR));
    return copy;
}

static HRESULT event_system_present(void)
{
    void **factory = nullptr;
    HRESULT hr = CoGetClassObject(&CLSID_VssEvent, 0x15, nullptr, &IID_IClassFactory_, (void **)&factory);
    if (factory)
        ((ULONG(WINAPI *)(void *))(*(void ***)factory)[2])(factory);
    return hr;
}

/* ------------------------------------------------------------------ the object behind a writer */

typedef struct WRITER WRITER;

/* the table the writer class calls through, in the order the system has it. */
typedef struct WRITER_VTBL {
    HRESULT(WINAPI *QueryInterface)(WRITER *, const GUID *, void **);
    ULONG(WINAPI *AddRef)(WRITER *);
    ULONG(WINAPI *Release)(WRITER *);
    HRESULT(WINAPI *Initialize)(WRITER *, const GUID *, const WCHAR *, const WCHAR *, DWORD, DWORD, int, int, int, DWORD,
                                int, BOOLEAN);
    HRESULT(WINAPI *Subscribe)(WRITER *, DWORD, DWORD);
    HRESULT(WINAPI *Unsubscribe)(WRITER *);
    void(WINAPI *Uninitialize)(WRITER *);
    const WCHAR **(WINAPI *GetCurrentVolumeArray)(WRITER *);
    UINT(WINAPI *GetCurrentVolumeCount)(WRITER *);
    HRESULT(WINAPI *GetSnapshotDeviceName)(WRITER *, const WCHAR *, const WCHAR **);
    GUID *(WINAPI *GetCurrentSnapshotSetId)(WRITER *, GUID *);
    LONG(WINAPI *GetContext)(WRITER *);
    int(WINAPI *GetCurrentLevel)(WRITER *);
    BOOLEAN(WINAPI *IsPathAffected)(WRITER *, const WCHAR *);
    BOOLEAN(WINAPI *IsBootableSystemStateBackedUp)(WRITER *);
    BOOLEAN(WINAPI *AreComponentsSelected)(WRITER *);
    int(WINAPI *GetBackupType)(WRITER *);
    int(WINAPI *GetRestoreType)(WRITER *);
    HRESULT(WINAPI *SetWriterFailure)(WRITER *, HRESULT);
    BOOLEAN(WINAPI *IsPartialFileSupportEnabled)(WRITER *);
    HRESULT(WINAPI *InstallAlternateWriter)(WRITER *, const GUID *, const GUID *);
    void *(WINAPI *GetIdentityInformation)(WRITER *);
    HRESULT(WINAPI *SetWriterFailureEx)(WRITER *, HRESULT, HRESULT, const WCHAR *);
    HRESULT(WINAPI *GetSessionId)(WRITER *, GUID *);
    BOOLEAN(WINAPI *IsWriterShuttingDown)(WRITER *);
} WRITER_VTBL;

struct WRITER {
    const WRITER_VTBL *vtbl;
    LONG references;
    BOOLEAN initialized;
    BOOLEAN subscribed;
    GUID id;
    GUID instance_id;
    WCHAR *name;
    WCHAR *instance;
    int usage, source, level;
    DWORD timeout;
};

static HRESULT WINAPI writer_query_interface(WRITER *writer, const GUID *iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (memcmp(iid, &IID_IUnknown_, sizeof(GUID)) != 0)
        return E_NOINTERFACE;
    *object = writer;
    writer->vtbl->AddRef(writer);
    return S_OK;
}

static ULONG WINAPI writer_add_ref(WRITER *writer)
{
    return InterlockedIncrement(&writer->references);
}

static void writer_release_state(WRITER *writer)
{
    HeapFree(GetProcessHeap(), 0, writer->name);
    HeapFree(GetProcessHeap(), 0, writer->instance);
    writer->name = writer->instance = nullptr;
    writer->initialized = FALSE;
    writer->subscribed = FALSE;
}

static ULONG WINAPI writer_release(WRITER *writer)
{
    LONG left = InterlockedDecrement(&writer->references);
    if (!left) {
        writer_release_state(writer);
        HeapFree(GetProcessHeap(), 0, writer);
    }
    return left;
}

static HRESULT WINAPI writer_initialize(WRITER *writer, const GUID *id, const WCHAR *name, const WCHAR *instance,
                                        DWORD reserved1, DWORD reserved2, int usage, int source, int level, DWORD timeout,
                                        int alternate, BOOLEAN throttling_only)
{
    if (writer->initialized)
        return VSS_E_BAD_STATE;
    if (alternate != 1 || throttling_only)
        return E_INVALIDARG;
    if (usage < 1 || usage > 4)
        return E_INVALIDARG;
    if (source < 1 || source > 3)
        return E_INVALIDARG;
    if (!is_administrator())
        return E_ACCESSDENIED;
    if (!name || level < 1 || level > 4)
        return E_INVALIDARG;
    writer->id = *id;
    if (instance && !(writer->instance = copy_text(instance)))
        return E_OUTOFMEMORY;
    if (!(writer->name = copy_text(name))) {
        writer_release_state(writer);
        return E_OUTOFMEMORY;
    }
    writer->usage = usage;
    writer->source = source;
    writer->level = level;
    writer->timeout = timeout;
    HRESULT hr = CoCreateGuid(&writer->instance_id);
    if (FAILED(hr)) {
        writer_release_state(writer);
        return hr;
    }
    writer->initialized = TRUE;
    return S_OK;
}

/* the events of a backup come through the event system, which is not here. */
static HRESULT WINAPI writer_subscribe(WRITER *writer, DWORD unused, DWORD flags)
{
    if (FAILED(event_system_present()))
        return VSS_E_WRITER_INFRASTRUCTURE;
    if (!writer->initialized)
        return VSS_E_BAD_STATE;
    if (flags != SM_BACKUP_AND_RESTORE_EVENTS)
        return E_INVALIDARG;
    if (writer->subscribed)
        return VSS_E_WRITER_ALREADY_SUBSCRIBED;
    return VSS_E_WRITER_INFRASTRUCTURE;
}

static HRESULT WINAPI writer_unsubscribe(WRITER *writer)
{
    if (!writer->initialized)
        return VSS_E_BAD_STATE;
    writer->subscribed = FALSE;
    return S_OK;
}

static void WINAPI writer_uninitialize(WRITER *writer)
{
    writer_release_state(writer);
}

/* no snapshot is in progress, so there are no volumes and no event to ask about. */
static const WCHAR **WINAPI writer_volume_array(WRITER *writer)
{
    return nullptr;
}

static UINT WINAPI writer_volume_count(WRITER *writer)
{
    return 0;
}

static HRESULT WINAPI writer_device_name(WRITER *writer, const WCHAR *volume, const WCHAR **device)
{
    if (!volume || !device)
        return E_INVALIDARG;
    *device = nullptr;
    return VSS_E_BAD_STATE;
}

static GUID *WINAPI writer_snapshot_set(WRITER *writer, GUID *result)
{
    memset(result, 0, sizeof(*result));
    return result;
}

static LONG WINAPI writer_context(WRITER *writer)
{
    return 0;
}

static int WINAPI writer_level(WRITER *writer)
{
    return writer->level;
}

static BOOLEAN WINAPI writer_false(WRITER *writer)
{
    return FALSE;
}

static BOOLEAN WINAPI writer_path_affected(WRITER *writer, const WCHAR *path)
{
    return FALSE;
}

static int WINAPI writer_zero(WRITER *writer)
{
    return 0;
}

static HRESULT WINAPI writer_set_failure(WRITER *writer, HRESULT failure)
{
    return writer->vtbl->SetWriterFailureEx(writer, failure, 0, nullptr);
}

static HRESULT WINAPI writer_install_alternate(WRITER *writer, const GUID *a, const GUID *b)
{
    return E_NOTIMPL;
}

static void *WINAPI writer_identity(WRITER *writer)
{
    return nullptr;
}

/* a failure is set from inside an event of a backup. */
static HRESULT WINAPI writer_set_failure_ex(WRITER *writer, HRESULT failure, HRESULT application, const WCHAR *text)
{
    return VSS_E_BAD_STATE;
}

static HRESULT WINAPI writer_session(WRITER *writer, GUID *session)
{
    if (!session)
        return E_INVALIDARG;
    memset(session, 0, sizeof(*session));
    return VSS_E_BAD_STATE;
}

static const WRITER_VTBL g_writer_vtbl = {
    writer_query_interface, writer_add_ref, writer_release, writer_initialize, writer_subscribe, writer_unsubscribe,
    writer_uninitialize, writer_volume_array, writer_volume_count, writer_device_name, writer_snapshot_set, writer_context,
    writer_level, writer_path_affected, writer_false, writer_false, writer_zero, writer_zero, writer_set_failure,
    writer_false, writer_install_alternate, writer_identity, writer_set_failure_ex, writer_session, writer_false,
};

static HRESULT make_writer(WRITER **result)
{
    WRITER *writer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*writer));
    if (!writer)
        return E_OUTOFMEMORY;
    writer->vtbl = &g_writer_vtbl;
    writer->references = 1;
    *result = writer;
    return S_OK;
}

VSSAPI_EXPORT HRESULT WINAPI CreateWriter(void *owner, WRITER **writer)
{
    return make_writer(writer);
}

VSSAPI_EXPORT HRESULT WINAPI CreateWriterEx(void *owner, WRITER **writer)
{
    return make_writer(writer);
}

/* ------------------------------------------------------------------ CVssWriter */

typedef struct CVSSWRITER {
    void **vtbl;
    WRITER *writer;
} CVSSWRITER;

static void __cdecl pure_call(void)
{
    __builtin_trap();
}

BOOLEAN WINAPI writer_event_handled(void)
{
    return TRUE;
}

static void *WINAPI writer_deleting_destructor(CVSSWRITER *self, unsigned flags);
static void **const g_writer_class[16];

CVSSWRITER *vss_writer_construct(CVSSWRITER *self) NAME("??0CVssWriter@@QEAA@XZ");
CVSSWRITER *vss_writer_construct(CVSSWRITER *self)
{
    self->vtbl = (void **)g_writer_class;
    self->writer = nullptr;
    return self;
}

void vss_writer_destruct(CVSSWRITER *self) NAME("??1CVssWriter@@UEAA@XZ");
void vss_writer_destruct(CVSSWRITER *self)
{
    self->vtbl = (void **)g_writer_class;
    if (self->writer) {
        self->writer->vtbl->Uninitialize(self->writer);
        self->writer->vtbl->Release(self->writer);
    }
}

static void *WINAPI writer_deleting_destructor(CVSSWRITER *self, unsigned flags)
{
    vss_writer_destruct(self);
    if (flags & 1)
        HeapFree(GetProcessHeap(), 0, self);
    return self;
}

static void **const g_writer_class[16] = {
    (void *)writer_deleting_destructor, (void *)writer_event_handled, (void *)writer_event_handled, (void *)pure_call,
    (void *)pure_call,                  (void *)pure_call,            (void *)pure_call,            (void *)writer_event_handled,
    (void *)writer_event_handled,       (void *)writer_event_handled, (void *)writer_event_handled, (void *)writer_event_handled,
    (void *)writer_event_handled,       (void *)writer_event_handled, (void *)writer_event_handled, (void *)writer_event_handled,
};

HRESULT vss_writer_initialize(CVSSWRITER *self, const GUID *id, const WCHAR *name, int usage, int source, int level,
                              DWORD timeout, int alternate, BOOLEAN throttling_only, const WCHAR *instance)
    NAME("?Initialize@CVssWriter@@QEAAJU_GUID@@PEBGW4VSS_USAGE_TYPE@@W4VSS_SOURCE_TYPE@@W4_VSS_APPLICATION_LEVEL@@KW4VSS_ALTERNATE_WRITER_STATE@@_N1@Z");
HRESULT vss_writer_initialize(CVSSWRITER *self, const GUID *id, const WCHAR *name, int usage, int source, int level,
                              DWORD timeout, int alternate, BOOLEAN throttling_only, const WCHAR *instance)
{
    if (self->writer)
        return E_FAIL;
    HRESULT hr = CreateWriter(self, &self->writer);
    if (FAILED(hr))
        return hr;
    hr = self->writer->vtbl->Initialize(self->writer, id, name, instance, 0, 0, usage, source, level, timeout, alternate,
                                        throttling_only);
    if (FAILED(hr)) {
        self->writer->vtbl->Release(self->writer);
        self->writer = nullptr;
    }
    return hr;
}

HRESULT vss_writer_subscribe(CVSSWRITER *self, DWORD flags) NAME("?Subscribe@CVssWriter@@QEAAJK@Z");
HRESULT vss_writer_subscribe(CVSSWRITER *self, DWORD flags)
{
    if (!self->writer)
        return E_FAIL;
    return self->writer->vtbl->Subscribe(self->writer, 0, flags);
}

HRESULT vss_writer_unsubscribe(CVSSWRITER *self) NAME("?Unsubscribe@CVssWriter@@QEAAJXZ");
HRESULT vss_writer_unsubscribe(CVSSWRITER *self)
{
    if (!self->writer)
        return E_FAIL;
    self->writer->vtbl->Unsubscribe(self->writer);
    return S_OK;
}

HRESULT vss_writer_install_alternate(CVSSWRITER *self, const GUID *a, const GUID *b)
    NAME("?InstallAlternateWriter@CVssWriter@@QEAAJU_GUID@@0@Z");
HRESULT vss_writer_install_alternate(CVSSWRITER *self, const GUID *a, const GUID *b)
{
    if (!self->writer)
        return E_FAIL;
    GUID first = *a, second = *b;
    return self->writer->vtbl->InstallAlternateWriter(self->writer, &first, &second);
}

BOOLEAN vss_writer_components_selected(const CVSSWRITER *self) NAME("?AreComponentsSelected@CVssWriter@@IEBA_NXZ");
BOOLEAN vss_writer_components_selected(const CVSSWRITER *self)
{
    return self->writer ? self->writer->vtbl->AreComponentsSelected(self->writer) : FALSE;
}

int vss_writer_backup_type(const CVSSWRITER *self) NAME("?GetBackupType@CVssWriter@@IEBA?AW4_VSS_BACKUP_TYPE@@XZ");
int vss_writer_backup_type(const CVSSWRITER *self)
{
    return self->writer ? self->writer->vtbl->GetBackupType(self->writer) : 0;
}

LONG vss_writer_context(const CVSSWRITER *self) NAME("?GetContext@CVssWriter@@IEBAJXZ");
LONG vss_writer_context(const CVSSWRITER *self)
{
    return self->writer ? self->writer->vtbl->GetContext(self->writer) : 0;
}

int vss_writer_level(const CVSSWRITER *self) NAME("?GetCurrentLevel@CVssWriter@@IEBA?AW4_VSS_APPLICATION_LEVEL@@XZ");
int vss_writer_level(const CVSSWRITER *self)
{
    return self->writer ? self->writer->vtbl->GetCurrentLevel(self->writer) : -1;
}

GUID *vss_writer_snapshot_set(const CVSSWRITER *self, GUID *result) NAME("?GetCurrentSnapshotSetId@CVssWriter@@IEBA?AU_GUID@@XZ");
GUID *vss_writer_snapshot_set(const CVSSWRITER *self, GUID *result)
{
    if (self->writer)
        return self->writer->vtbl->GetCurrentSnapshotSetId(self->writer, result);
    memset(result, 0, sizeof(*result));
    return result;
}

const WCHAR **vss_writer_volume_array(const CVSSWRITER *self) NAME("?GetCurrentVolumeArray@CVssWriter@@IEBAPEAPEBGXZ");
const WCHAR **vss_writer_volume_array(const CVSSWRITER *self)
{
    return self->writer ? self->writer->vtbl->GetCurrentVolumeArray(self->writer) : nullptr;
}

UINT vss_writer_volume_count(const CVSSWRITER *self) NAME("?GetCurrentVolumeCount@CVssWriter@@IEBAIXZ");
UINT vss_writer_volume_count(const CVSSWRITER *self)
{
    return self->writer ? self->writer->vtbl->GetCurrentVolumeCount(self->writer) : 0;
}

int vss_writer_restore_type(const CVSSWRITER *self) NAME("?GetRestoreType@CVssWriter@@IEBA?AW4_VSS_RESTORE_TYPE@@XZ");
int vss_writer_restore_type(const CVSSWRITER *self)
{
    return self->writer ? self->writer->vtbl->GetRestoreType(self->writer) : 0;
}

HRESULT vss_writer_device_name(const CVSSWRITER *self, const WCHAR *volume, const WCHAR **device)
    NAME("?GetSnapshotDeviceName@CVssWriter@@IEBAJPEBGPEAPEBG@Z");
HRESULT vss_writer_device_name(const CVSSWRITER *self, const WCHAR *volume, const WCHAR **device)
{
    return self->writer ? self->writer->vtbl->GetSnapshotDeviceName(self->writer, volume, device) : E_FAIL;
}

BOOLEAN vss_writer_bootable(const CVSSWRITER *self) NAME("?IsBootableSystemStateBackedUp@CVssWriter@@IEBA_NXZ");
BOOLEAN vss_writer_bootable(const CVSSWRITER *self)
{
    return self->writer ? self->writer->vtbl->IsBootableSystemStateBackedUp(self->writer) : FALSE;
}

BOOLEAN vss_writer_partial_files(const CVSSWRITER *self) NAME("?IsPartialFileSupportEnabled@CVssWriter@@IEBA_NXZ");
BOOLEAN vss_writer_partial_files(const CVSSWRITER *self)
{
    return self->writer ? self->writer->vtbl->IsPartialFileSupportEnabled(self->writer) : FALSE;
}

BOOLEAN vss_writer_path_affected(const CVSSWRITER *self, const WCHAR *path) NAME("?IsPathAffected@CVssWriter@@IEBA_NPEBG@Z");
BOOLEAN vss_writer_path_affected(const CVSSWRITER *self, const WCHAR *path)
{
    return self->writer ? self->writer->vtbl->IsPathAffected(self->writer, path) : FALSE;
}

HRESULT vss_writer_set_failure(CVSSWRITER *self, HRESULT failure) NAME("?SetWriterFailure@CVssWriter@@IEAAJJ@Z");
HRESULT vss_writer_set_failure(CVSSWRITER *self, HRESULT failure)
{
    return self->writer ? self->writer->vtbl->SetWriterFailure(self->writer, failure) : E_FAIL;
}

/* ------------------------------------------------------------------ CVssJetWriter */

/* the object behind a database writer holds the writer of the sdk class. */
typedef struct JET_WRITER_IMPL {
    void *reserved;
    WRITER *writer;
} JET_WRITER_IMPL;

typedef struct CVSSJETWRITER {
    void **vtbl;
    JET_WRITER_IMPL *impl;
} CVSSJETWRITER;

static WRITER *jet_writer(const CVSSJETWRITER *self)
{
    return self->impl ? self->impl->writer : nullptr;
}

BOOLEAN WINAPI jet_event_handled(void)
{
    return TRUE;
}

void WINAPI jet_event_ignored(void)
{
}

static void *WINAPI jet_deleting_destructor(CVSSJETWRITER *self, unsigned flags);
static void **const g_jet_class[19];

CVSSJETWRITER *vss_jet_construct(CVSSJETWRITER *self) NAME("??0CVssJetWriter@@QEAA@XZ");
CVSSJETWRITER *vss_jet_construct(CVSSJETWRITER *self)
{
    self->vtbl = (void **)g_jet_class;
    self->impl = nullptr;
    return self;
}

void vss_jet_destruct(CVSSJETWRITER *self) NAME("??1CVssJetWriter@@UEAA@XZ");
void vss_jet_destruct(CVSSJETWRITER *self)
{
    self->vtbl = (void **)g_jet_class;
}

static void *WINAPI jet_deleting_destructor(CVSSJETWRITER *self, unsigned flags)
{
    vss_jet_destruct(self);
    if (flags & 1)
        HeapFree(GetProcessHeap(), 0, self);
    return self;
}

static void **const g_jet_class[19] = {
    (void *)jet_deleting_destructor, (void *)jet_event_handled, (void *)jet_event_handled, (void *)jet_event_handled,
    (void *)jet_event_handled,       (void *)jet_event_handled, (void *)jet_event_handled, (void *)jet_event_handled,
    (void *)jet_event_handled,       (void *)jet_event_handled, (void *)jet_event_handled, (void *)jet_event_ignored,
    (void *)jet_event_ignored,       (void *)jet_event_handled, (void *)jet_event_handled, (void *)jet_event_handled,
    (void *)jet_event_handled,       (void *)jet_event_handled, (void *)jet_event_handled,
};

/* the database writer makes the writer of the sdk class and subscribes it, so it fails where subscribing does. */
HRESULT vss_jet_initialize(CVSSJETWRITER *self, const GUID *id, const WCHAR *name, BOOLEAN system_service,
                           BOOLEAN bootable_state, const WCHAR *file_filter, const WCHAR *component_filter, DWORD timeout)
    NAME("?Initialize@CVssJetWriter@@QEAAJU_GUID@@PEBG_N211K@Z");
HRESULT vss_jet_initialize(CVSSJETWRITER *self, const GUID *id, const WCHAR *name, BOOLEAN system_service,
                           BOOLEAN bootable_state, const WCHAR *file_filter, const WCHAR *component_filter, DWORD timeout)
{
    if (self->impl)
        return E_FAIL;
    JET_WRITER_IMPL *impl = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*impl));
    if (!impl)
        return E_OUTOFMEMORY;
    HRESULT hr = make_writer(&impl->writer);
    if (SUCCEEDED(hr)) {
        int usage = bootable_state ? 1 : system_service ? 2 : 3;
        hr = impl->writer->vtbl->Initialize(impl->writer, id, name, nullptr, 0, 0, usage, 1, system_service ? 1 : 2, timeout, 1,
                                            FALSE);
        if (SUCCEEDED(hr))
            hr = impl->writer->vtbl->Subscribe(impl->writer, 0, SM_BACKUP_AND_RESTORE_EVENTS);
        if (FAILED(hr))
            impl->writer->vtbl->Release(impl->writer);
    }
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, impl);
        return hr;
    }
    self->impl = impl;
    return S_OK;
}

void vss_jet_uninitialize(CVSSJETWRITER *self) NAME("?Uninitialize@CVssJetWriter@@QEAAXXZ");
void vss_jet_uninitialize(CVSSJETWRITER *self)
{
    JET_WRITER_IMPL *impl = self->impl;
    if (!impl)
        return;
    self->impl = nullptr;
    impl->writer->vtbl->Unsubscribe(impl->writer);
    impl->writer->vtbl->Uninitialize(impl->writer);
    impl->writer->vtbl->Release(impl->writer);
    HeapFree(GetProcessHeap(), 0, impl);
}

BOOLEAN vss_jet_components_selected(const CVSSJETWRITER *self) NAME("?AreComponentsSelected@CVssJetWriter@@IEBA_NXZ");
BOOLEAN vss_jet_components_selected(const CVSSJETWRITER *self)
{
    WRITER *writer = jet_writer(self);
    return writer ? writer->vtbl->AreComponentsSelected(writer) : FALSE;
}

int vss_jet_backup_type(const CVSSJETWRITER *self) NAME("?GetBackupType@CVssJetWriter@@IEBA?AW4_VSS_BACKUP_TYPE@@XZ");
int vss_jet_backup_type(const CVSSJETWRITER *self)
{
    WRITER *writer = jet_writer(self);
    return writer ? writer->vtbl->GetBackupType(writer) : 0;
}

LONG vss_jet_context(const CVSSJETWRITER *self) NAME("?GetContext@CVssJetWriter@@IEBAJXZ");
LONG vss_jet_context(const CVSSJETWRITER *self)
{
    WRITER *writer = jet_writer(self);
    return writer ? writer->vtbl->GetContext(writer) : 0;
}

int vss_jet_level(const CVSSJETWRITER *self) NAME("?GetCurrentLevel@CVssJetWriter@@IEBA?AW4_VSS_APPLICATION_LEVEL@@XZ");
int vss_jet_level(const CVSSJETWRITER *self)
{
    WRITER *writer = jet_writer(self);
    return writer ? writer->vtbl->GetCurrentLevel(writer) : -1;
}

GUID *vss_jet_snapshot_set(const CVSSJETWRITER *self, GUID *result) NAME("?GetCurrentSnapshotSetId@CVssJetWriter@@IEBA?AU_GUID@@XZ");
GUID *vss_jet_snapshot_set(const CVSSJETWRITER *self, GUID *result)
{
    WRITER *writer = jet_writer(self);
    if (writer)
        return writer->vtbl->GetCurrentSnapshotSetId(writer, result);
    memset(result, 0, sizeof(*result));
    return result;
}

const WCHAR **vss_jet_volume_array(const CVSSJETWRITER *self) NAME("?GetCurrentVolumeArray@CVssJetWriter@@IEBAPEAPEBGXZ");
const WCHAR **vss_jet_volume_array(const CVSSJETWRITER *self)
{
    WRITER *writer = jet_writer(self);
    return writer ? writer->vtbl->GetCurrentVolumeArray(writer) : nullptr;
}

UINT vss_jet_volume_count(const CVSSJETWRITER *self) NAME("?GetCurrentVolumeCount@CVssJetWriter@@IEBAIXZ");
UINT vss_jet_volume_count(const CVSSJETWRITER *self)
{
    WRITER *writer = jet_writer(self);
    return writer ? writer->vtbl->GetCurrentVolumeCount(writer) : 0;
}

int vss_jet_restore_type(const CVSSJETWRITER *self) NAME("?GetRestoreType@CVssJetWriter@@IEBA?AW4_VSS_RESTORE_TYPE@@XZ");
int vss_jet_restore_type(const CVSSJETWRITER *self)
{
    WRITER *writer = jet_writer(self);
    return writer ? writer->vtbl->GetRestoreType(writer) : 0;
}

HRESULT vss_jet_device_name(const CVSSJETWRITER *self, const WCHAR *volume, const WCHAR **device)
    NAME("?GetSnapshotDeviceName@CVssJetWriter@@IEBAJPEBGPEAPEBG@Z");
HRESULT vss_jet_device_name(const CVSSJETWRITER *self, const WCHAR *volume, const WCHAR **device)
{
    WRITER *writer = jet_writer(self);
    return writer ? writer->vtbl->GetSnapshotDeviceName(writer, volume, device) : E_FAIL;
}

BOOLEAN vss_jet_bootable(const CVSSJETWRITER *self) NAME("?IsBootableSystemStateBackedUp@CVssJetWriter@@IEBA_NXZ");
BOOLEAN vss_jet_bootable(const CVSSJETWRITER *self)
{
    WRITER *writer = jet_writer(self);
    return writer ? writer->vtbl->IsBootableSystemStateBackedUp(writer) : FALSE;
}

BOOLEAN vss_jet_partial_files(const CVSSJETWRITER *self) NAME("?IsPartialFileSupportEnabled@CVssJetWriter@@IEBA_NXZ");
BOOLEAN vss_jet_partial_files(const CVSSJETWRITER *self)
{
    WRITER *writer = jet_writer(self);
    return writer ? writer->vtbl->IsPartialFileSupportEnabled(writer) : FALSE;
}

BOOLEAN vss_jet_path_affected(const CVSSJETWRITER *self, const WCHAR *path) NAME("?IsPathAffected@CVssJetWriter@@IEBA_NPEBG@Z");
BOOLEAN vss_jet_path_affected(const CVSSJETWRITER *self, const WCHAR *path)
{
    WRITER *writer = jet_writer(self);
    return writer ? writer->vtbl->IsPathAffected(writer, path) : FALSE;
}

HRESULT vss_jet_set_failure(CVSSJETWRITER *self, HRESULT failure) NAME("?SetWriterFailure@CVssJetWriter@@IEAAJJ@Z");
HRESULT vss_jet_set_failure(CVSSJETWRITER *self, HRESULT failure)
{
    WRITER *writer = jet_writer(self);
    return writer ? writer->vtbl->SetWriterFailure(writer, failure) : E_FAIL;
}

/* ------------------------------------------------------------------ calls that go to the service */

#define E_SERVICE_NOT_FOUND ((HRESULT)0x80070424)

static BOOLEAN service_present(void)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return FALSE;
    SC_HANDLE service = OpenServiceW(manager, L"VSS", SERVICE_QUERY_STATUS);
    CloseServiceHandle(manager);
    if (!service)
        return FALSE;
    CloseServiceHandle(service);
    return TRUE;
}

/* the answer of the system for a service that was never installed, which is what the call finds here. */
static HRESULT service_missing(void)
{
    if (service_present())
        return E_NOTIMPL;
    DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_SERVICE_NOT_FOUND;
}

VSSAPI_EXPORT HRESULT WINAPI IsVolumeSnapshottedInternal(const WCHAR *volume, BOOL *present, LONG *compatibility)
{
    if (present)
        *present = FALSE;
    if (compatibility)
        *compatibility = 0;
    if (!is_administrator())
        return E_ACCESSDENIED;
    if (!volume || !present || !compatibility)
        return E_INVALIDARG;
    return service_missing();
}

/* the memory of a snapshot description, the strings that follow its three counted fields. */
VSSAPI_EXPORT void WINAPI VssFreeSnapshotPropertiesInternal(void **properties)
{
    if (!properties)
        return;
    for (int i = 5; i <= 10; i++)
        CoTaskMemFree(properties[i]);
}

/* the answer needs a backup session with the service, and a client system has none to give. */
VSSAPI_EXPORT HRESULT WINAPI ShouldBlockRevertInternal(const WCHAR *volume, BOOLEAN *block)
{
    if (!volume || !block)
        return E_INVALIDARG;
    if (!is_administrator())
        return E_ACCESSDENIED;
    return E_NOTIMPL;
}

VSSAPI_EXPORT HRESULT WINAPI CreateVssBackupComponentsInternal(void **components)
{
    if (!components)
        return E_INVALIDARG;
    *components = nullptr;
    if (!is_administrator())
        return E_ACCESSDENIED;
    return service_missing();
}

VSSAPI_EXPORT HRESULT WINAPI CreateVssExamineWriterMetadataInternal(WCHAR *xml, void **metadata)
{
    if (metadata)
        *metadata = nullptr;
    return E_NOTIMPL;
}

VSSAPI_EXPORT HRESULT WINAPI CreateVssExpressWriterInternal(void **writer)
{
    if (writer)
        *writer = nullptr;
    return E_NOTIMPL;
}

VSSAPI_EXPORT HRESULT WINAPI CreateVssSnapshotSetDescription(const GUID *set, LONG context, void **description)
{
    if (description)
        *description = nullptr;
    return E_NOTIMPL;
}

VSSAPI_EXPORT HRESULT WINAPI GetProviderMgmtInterfaceInternal(const GUID *provider, LONG context, const GUID *iid, void **object)
{
    if (object)
        *object = nullptr;
    if (!object || !iid)
        return E_INVALIDARG;
    if (!is_administrator())
        return E_ACCESSDENIED;
    return service_missing();
}

VSSAPI_EXPORT HRESULT WINAPI GetProviderMgmtInterface(const GUID *provider, LONG context, const GUID *iid, void **object)
{
    return GetProviderMgmtInterfaceInternal(provider, context, iid, object);
}

VSSAPI_EXPORT HRESULT WINAPI LoadVssSnapshotSetDescription(const WCHAR *xml, void **description)
{
    if (description)
        *description = nullptr;
    return E_NOTIMPL;
}

VSSAPI_EXPORT HRESULT WINAPI DllGetClassObject(const GUID *clsid, const GUID *iid, void **object)
{
    if (object)
        *object = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

VSSAPI_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_OK;
}
