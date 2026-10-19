/*
 * IFileOperation, the shell's file operation object: items to copy, move, rename, delete or create are queued, and
 * PerformOperations does them in order, telling the progress sinks about each. there is no dialog, and no recycle bin.
 * the work is done by the same tree functions as SHFileOperation.
 */
#include "windows_storage_private.h"

#define MAX_SINKS 8

static void *HeapAllocBytes(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static void *HeapAllocZero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

static void HeapFreeBlock(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

static LPWSTR Duplicate(LPCWSTR text)
{
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    LPWSTR copy = HeapAllocBytes(bytes);
    if (copy)
        memcpy(copy, text, bytes);
    return copy;
}

/* ------------------------------------------------------------------ the trees, over SHFileOperation */

/* a double terminated list of one name. */
static BOOL one_name(WCHAR *out, SIZE_T chars, LPCWSTR path)
{
    SIZE_T n = wcslen(path);
    if (n + 2 > chars)
        return FALSE;
    memcpy(out, path, n * sizeof(WCHAR));
    out[n] = out[n + 1] = 0;
    return TRUE;
}

static DWORD delete_tree(LPCWSTR path)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return GetLastError();
    if (attributes & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesW(path, attributes & ~FILE_ATTRIBUTE_READONLY);
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return DeleteFileW(path) ? ERROR_SUCCESS : GetLastError();
    WCHAR from[MAX_PATH * 4];
    if (!one_name(from, RTL_NUMBER_OF(from), path))
        return ERROR_FILENAME_EXCED_RANGE;
    SHFILEOPSTRUCTW op = {nullptr, FO_DELETE, from, nullptr, FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI};
    return (DWORD)SHFileOperationW(&op);
}

static DWORD copy_tree(LPCWSTR source, LPCWSTR target, BOOL replace)
{
    DWORD attributes = GetFileAttributesW(source);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return GetLastError();
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return CopyFileW(source, target, !replace) ? ERROR_SUCCESS : GetLastError();
    WCHAR from[MAX_PATH * 4], to[MAX_PATH * 4];
    if (!one_name(from, RTL_NUMBER_OF(from), source) || !one_name(to, RTL_NUMBER_OF(to), target))
        return ERROR_FILENAME_EXCED_RANGE;
    SHFILEOPSTRUCTW op = {nullptr, FO_COPY, from, to, FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI};
    return (DWORD)SHFileOperationW(&op);
}

static DWORD move_tree(LPCWSTR source, LPCWSTR target, BOOL replace)
{
    if (MoveFileExW(source, target, replace ? MOVEFILE_REPLACE_EXISTING : 0))
        return ERROR_SUCCESS;
    DWORD error = GetLastError();
    if (error != ERROR_NOT_SAME_DEVICE)
        return error;
    error = copy_tree(source, target, replace);
    return error ? error : delete_tree(source);
}
#define LONG_PATH (MAX_PATH * 4)

#ifndef E_ABORT
#define E_ABORT ((HRESULT)0x80004004L)
#endif

typedef enum _OP_KIND {
    OP_RENAME,
    OP_MOVE,
    OP_COPY,
    OP_DELETE,
    OP_NEW,
} OP_KIND;

typedef struct _FO_OP {
    OP_KIND Kind;
    IShellItem *Item;   /* the source, or for a new item the folder that gets it */
    IShellItem *Folder; /* the destination folder of a move or a copy */
    LPWSTR Name;        /* the new name, NULL to keep the source's */
    LPWSTR Template;
    DWORD Attributes;
    IFileOperationProgressSink *Sink;
} FO_OP;

typedef struct _FILE_OPERATION {
    IFileOperation Interface;
    LONG Refs;
    DWORD Flags;
    HWND Owner;
    LPWSTR Message;
    IFileOperationProgressSink *Sinks[MAX_SINKS];
    DWORD Cookies[MAX_SINKS];
    DWORD NextCookie;
    FO_OP *Ops;
    ULONG Count, Capacity;
    BOOL Aborted;
    BOOL Running;
} FILE_OPERATION;

static FILE_OPERATION *from_interface(IFileOperation *This)
{
    return (FILE_OPERATION *)This;
}

static HRESULT STDMETHODCALLTYPE fo_query_interface(IFileOperation *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IFileOperation))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&from_interface(This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE fo_add_ref(IFileOperation *This)
{
    return (ULONG)InterlockedIncrement(&from_interface(This)->Refs);
}

static void free_op(FO_OP *op)
{
    if (op->Item)
        op->Item->lpVtbl->Release(op->Item);
    if (op->Folder)
        op->Folder->lpVtbl->Release(op->Folder);
    if (op->Sink)
        op->Sink->lpVtbl->Release(op->Sink);
    HeapFreeBlock(op->Name);
    HeapFreeBlock(op->Template);
}

static ULONG STDMETHODCALLTYPE fo_release(IFileOperation *This)
{
    FILE_OPERATION *f = from_interface(This);
    ULONG refs = (ULONG)InterlockedDecrement(&f->Refs);
    if (refs)
        return refs;
    for (ULONG i = 0; i < f->Count; i++)
        free_op(&f->Ops[i]);
    for (int i = 0; i < MAX_SINKS; i++)
        if (f->Sinks[i])
            f->Sinks[i]->lpVtbl->Release(f->Sinks[i]);
    HeapFreeBlock(f->Ops);
    HeapFreeBlock(f->Message);
    HeapFreeBlock(f);
    return 0;
}

/* sinks are told in the order they were advised. one of the call's own comes last. */
static HRESULT STDMETHODCALLTYPE fo_advise(IFileOperation *This, IFileOperationProgressSink *sink, DWORD *cookie)
{
    FILE_OPERATION *f = from_interface(This);
    if (!sink || !cookie)
        return E_INVALIDARG;
    for (int i = 0; i < MAX_SINKS; i++) {
        if (f->Sinks[i])
            continue;
        sink->lpVtbl->AddRef(sink);
        f->Sinks[i] = sink;
        f->Cookies[i] = *cookie = ++f->NextCookie;
        return S_OK;
    }
    return E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE fo_unadvise(IFileOperation *This, DWORD cookie)
{
    FILE_OPERATION *f = from_interface(This);
    for (int i = 0; i < MAX_SINKS; i++) {
        if (f->Sinks[i] && f->Cookies[i] == cookie) {
            f->Sinks[i]->lpVtbl->Release(f->Sinks[i]);
            f->Sinks[i] = nullptr;
            return S_OK;
        }
    }
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE fo_set_flags(IFileOperation *This, DWORD flags)
{
    from_interface(This)->Flags = flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fo_set_message(IFileOperation *This, LPCWSTR message)
{
    FILE_OPERATION *f = from_interface(This);
    LPWSTR copy = message ? Duplicate(message) : nullptr;
    if (message && !copy)
        return E_OUTOFMEMORY;
    HeapFreeBlock(f->Message);
    f->Message = copy;
    return S_OK;
}

/* there is no progress dialog to hand an object to. */
static HRESULT STDMETHODCALLTYPE fo_set_dialog(IFileOperation *This, IOperationsProgressDialog *dialog)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fo_set_properties(IFileOperation *This, IPropertyChangeArray *properties)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fo_set_owner(IFileOperation *This, HWND owner)
{
    from_interface(This)->Owner = owner;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fo_apply_properties(IFileOperation *This, IShellItem *item)
{
    return item ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE fo_apply_properties_items(IFileOperation *This, IUnknown *items)
{
    return items ? S_OK : E_INVALIDARG;
}

/* queues an operation, which keeps its own references. */
static HRESULT queue(FILE_OPERATION *f, OP_KIND kind, IShellItem *item, IShellItem *folder, LPCWSTR name,
                     LPCWSTR template_name, DWORD attributes, IFileOperationProgressSink *sink)
{
    if (f->Running)
        return E_UNEXPECTED;
    if (f->Count == f->Capacity) {
        ULONG capacity = f->Capacity ? f->Capacity * 2 : 4;
        FO_OP *ops = HeapAllocBytes(capacity * sizeof(*ops));
        if (!ops)
            return E_OUTOFMEMORY;
        if (f->Count)
            memcpy(ops, f->Ops, f->Count * sizeof(*ops));
        HeapFreeBlock(f->Ops);
        f->Ops = ops;
        f->Capacity = capacity;
    }
    FO_OP op = {kind, item, folder, nullptr, nullptr, attributes, sink};
    if (name && !(op.Name = Duplicate(name)))
        return E_OUTOFMEMORY;
    if (template_name && !(op.Template = Duplicate(template_name))) {
        HeapFreeBlock(op.Name);
        return E_OUTOFMEMORY;
    }
    if (item)
        item->lpVtbl->AddRef(item);
    if (folder)
        folder->lpVtbl->AddRef(folder);
    if (sink)
        sink->lpVtbl->AddRef(sink);
    f->Ops[f->Count++] = op;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fo_rename_item(IFileOperation *This, IShellItem *item, LPCWSTR name,
                                                IFileOperationProgressSink *sink)
{
    if (!item || !name || !name[0])
        return E_INVALIDARG;
    return queue(from_interface(This), OP_RENAME, item, nullptr, name, nullptr, 0, sink);
}

static HRESULT STDMETHODCALLTYPE fo_move_item(IFileOperation *This, IShellItem *item, IShellItem *folder,
                                              LPCWSTR name, IFileOperationProgressSink *sink)
{
    if (!item || !folder)
        return E_INVALIDARG;
    return queue(from_interface(This), OP_MOVE, item, folder, name, nullptr, 0, sink);
}

static HRESULT STDMETHODCALLTYPE fo_copy_item(IFileOperation *This, IShellItem *item, IShellItem *folder,
                                              LPCWSTR name, IFileOperationProgressSink *sink)
{
    if (!item || !folder)
        return E_INVALIDARG;
    return queue(from_interface(This), OP_COPY, item, folder, name, nullptr, 0, sink);
}

static HRESULT STDMETHODCALLTYPE fo_delete_item(IFileOperation *This, IShellItem *item,
                                                IFileOperationProgressSink *sink)
{
    if (!item)
        return E_INVALIDARG;
    return queue(from_interface(This), OP_DELETE, item, nullptr, nullptr, nullptr, 0, sink);
}

static HRESULT STDMETHODCALLTYPE fo_new_item(IFileOperation *This, IShellItem *folder, DWORD attributes, LPCWSTR name,
                                             LPCWSTR template_name, IFileOperationProgressSink *sink)
{
    if (!folder || !name || !name[0])
        return E_INVALIDARG;
    return queue(from_interface(This), OP_NEW, folder, nullptr, name, template_name, attributes, sink);
}

/* the items of an array or one item, each queued by the single item call. */
static HRESULT queue_each(FILE_OPERATION *f, IUnknown *items, OP_KIND kind, IShellItem *folder, LPCWSTR name)
{
    if (!items)
        return E_INVALIDARG;
    IShellItemArray *array;
    if (FAILED(items->lpVtbl->QueryInterface(items, &IID_IShellItemArray, (void **)&array))) {
        IShellItem *item;
        if (FAILED(items->lpVtbl->QueryInterface(items, &IID_IShellItem, (void **)&item)))
            return E_NOINTERFACE;
        HRESULT hr = queue(f, kind, item, folder, name, nullptr, 0, nullptr);
        item->lpVtbl->Release(item);
        return hr;
    }
    DWORD count = 0;
    HRESULT hr = array->lpVtbl->GetCount(array, &count);
    for (DWORD i = 0; SUCCEEDED(hr) && i < count; i++) {
        IShellItem *item;
        hr = array->lpVtbl->GetItemAt(array, i, &item);
        if (FAILED(hr))
            break;
        hr = queue(f, kind, item, folder, name, nullptr, 0, nullptr);
        item->lpVtbl->Release(item);
    }
    array->lpVtbl->Release(array);
    return hr;
}

static HRESULT STDMETHODCALLTYPE fo_rename_items(IFileOperation *This, IUnknown *items, LPCWSTR name)
{
    if (!name || !name[0])
        return E_INVALIDARG;
    return queue_each(from_interface(This), items, OP_RENAME, nullptr, name);
}

static HRESULT STDMETHODCALLTYPE fo_move_items(IFileOperation *This, IUnknown *items, IShellItem *folder)
{
    return folder ? queue_each(from_interface(This), items, OP_MOVE, folder, nullptr) : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE fo_copy_items(IFileOperation *This, IUnknown *items, IShellItem *folder)
{
    return folder ? queue_each(from_interface(This), items, OP_COPY, folder, nullptr) : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE fo_delete_items(IFileOperation *This, IUnknown *items)
{
    return queue_each(from_interface(This), items, OP_DELETE, nullptr, nullptr);
}

/* ------------------------------------------------------------------ performing */

static HRESULT path_of(IShellItem *item, WCHAR *path)
{
    LPWSTR text = nullptr;
    HRESULT hr = item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, &text);
    if (FAILED(hr))
        return hr;
    if (wcslen(text) >= LONG_PATH) {
        HeapFreeBlock(text);
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    }
    wcscpy(path, text);
    HeapFreeBlock(text);
    return S_OK;
}

static HRESULT error_hr(DWORD error)
{
    return error ? HRESULT_FROM_WIN32(error) : S_OK;
}

/* the last name of a path. */
static LPCWSTR base_name(LPCWSTR path)
{
    LPCWSTR slash = wcsrchr(path, L'\\');
    return slash ? slash + 1 : path;
}

/* a name that is free in the folder: "name (2).ext", "name (3).ext"... as a copy on the same name is renamed. */
static BOOL unique_path(WCHAR *path)
{
    WCHAR base[LONG_PATH];
    wcscpy(base, path);
    LPWSTR dot = wcsrchr(base, L'.');
    LPWSTR slash = wcsrchr(base, L'\\');
    WCHAR extension[MAX_PATH] = L"";
    if (dot && (!slash || dot > slash)) {
        wcscpy(extension, dot);
        *dot = 0;
    }
    for (int i = 2; i < 1000; i++) {
        if (wcslen(base) + wcslen(extension) + 8 >= LONG_PATH)
            return FALSE;
        _snwprintf(path, LONG_PATH, L"%ls (%d)%ls", base, i, extension);
        path[LONG_PATH - 1] = 0;
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
            return TRUE;
    }
    return FALSE;
}

/* the target of a move or copy: the folder's path and the name, which defaults to the source's. */
static HRESULT target_path(const FO_OP *op, WCHAR *source, WCHAR *target)
{
    WCHAR folder[LONG_PATH];
    HRESULT hr = path_of(op->Folder, folder);
    if (FAILED(hr))
        return hr;
    LPCWSTR name = op->Name && op->Name[0] ? op->Name : base_name(source);
    SIZE_T length = wcslen(folder);
    if (length + wcslen(name) + 2 >= LONG_PATH)
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    wcscpy(target, folder);
    if (length && target[length - 1] != L'\\')
        target[length++] = L'\\';
    wcscpy(target + length, name);
    return S_OK;
}

/* the item of a path that exists now, or NULL. */
static IShellItem *item_of(LPCWSTR path)
{
    IShellItem *item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(path, nullptr, &IID_IShellItem, (void **)&item)))
        return nullptr;
    return item;
}

/* the sinks to tell, those advised and the operation's own. */
typedef struct _TELL {
    IFileOperationProgressSink *List[MAX_SINKS + 1];
    int Count;
} TELL;

static void gather(FILE_OPERATION *f, const FO_OP *op, TELL *t)
{
    t->Count = 0;
    for (int i = 0; i < MAX_SINKS; i++) {
        if (f->Sinks[i]) {
            f->Sinks[i]->lpVtbl->AddRef(f->Sinks[i]);
            t->List[t->Count++] = f->Sinks[i];
        }
    }
    if (op && op->Sink) {
        op->Sink->lpVtbl->AddRef(op->Sink);
        t->List[t->Count++] = op->Sink;
    }
}

static void gather_release(TELL *t)
{
    for (int i = 0; i < t->Count; i++)
        t->List[i]->lpVtbl->Release(t->List[i]);
    t->Count = 0;
}

/* the first failure a sink answers to a Pre call, which stops the operation. E_NOTIMPL is not a refusal. */
static HRESULT pre_result(HRESULT hr)
{
    return hr == E_NOTIMPL ? S_OK : hr;
}

static HRESULT run_one(FILE_OPERATION *f, FO_OP *op)
{
    TELL tell;
    gather(f, op, &tell);
    HRESULT hr = S_OK, result = S_OK;
    WCHAR source[LONG_PATH], target[LONG_PATH];
    IShellItem *created = nullptr;
    BOOL replace = (f->Flags & FOF_NOCONFIRMATION) != 0;
    DWORD tsf = (f->Flags & FOF_RENAMEONCOLLISION) ? TSF_RENAME_EXIST : (replace ? TSF_OVERWRITE_EXIST : 0);
    if (f->Flags & FOF_NOCOPYSECURITYATTRIBS)
        tsf |= TSF_NO_SECURITY;

    switch (op->Kind) {
    case OP_RENAME:
    case OP_MOVE:
    case OP_COPY:
    case OP_DELETE:
        hr = path_of(op->Item, source);
        break;
    case OP_NEW:
        hr = path_of(op->Item, target);
        break;
    }
    for (int i = 0; SUCCEEDED(hr) && i < tell.Count; i++) {
        IFileOperationProgressSink *s = tell.List[i];
        switch (op->Kind) {
        case OP_RENAME:
            hr = pre_result(s->lpVtbl->PreRenameItem(s, tsf, op->Item, op->Name));
            break;
        case OP_MOVE:
            hr = pre_result(s->lpVtbl->PreMoveItem(s, tsf, op->Item, op->Folder, op->Name));
            break;
        case OP_COPY:
            hr = pre_result(s->lpVtbl->PreCopyItem(s, tsf, op->Item, op->Folder, op->Name));
            break;
        case OP_DELETE:
            hr = pre_result(s->lpVtbl->PreDeleteItem(s, tsf, op->Item));
            break;
        case OP_NEW:
            hr = pre_result(s->lpVtbl->PreNewItem(s, tsf, op->Item, op->Name));
            break;
        }
    }
    if (FAILED(hr)) {
        /* a refusal of a sink is the user cancelling. */
        gather_release(&tell);
        f->Aborted = TRUE;
        return hr;
    }

    DWORD error = ERROR_SUCCESS;
    LPCWSTR new_name = op->Name;
    switch (op->Kind) {
    case OP_RENAME: {
        WCHAR folder[LONG_PATH];
        wcscpy(folder, source);
        LPWSTR slash = wcsrchr(folder, L'\\');
        if (slash)
            slash[1] = 0;
        if (wcslen(folder) + wcslen(op->Name) >= LONG_PATH) {
            error = ERROR_FILENAME_EXCED_RANGE;
            break;
        }
        wcscpy(target, folder);
        wcscat(target, op->Name);
        if (_wcsicmp(source, target) && GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES)
            error = ERROR_ALREADY_EXISTS;
        else
            error = MoveFileW(source, target) ? ERROR_SUCCESS : GetLastError();
        break;
    }
    case OP_MOVE:
    case OP_COPY:
        hr = target_path(op, source, target);
        if (FAILED(hr)) {
            error = HRESULT_CODE(hr);
            break;
        }
        if (GetFileAttributesW(source) == INVALID_FILE_ATTRIBUTES) {
            error = GetLastError();
            break;
        }
        if (!_wcsicmp(source, target)) {
            /* onto itself: a copy makes a new name when it may, a move has nothing to do. */
            if (op->Kind == OP_MOVE)
                break;
            if (!(f->Flags & FOF_RENAMEONCOLLISION)) {
                error = ERROR_ALREADY_EXISTS;
                break;
            }
        }
        if (GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES) {
            if (f->Flags & FOF_RENAMEONCOLLISION) {
                if (!unique_path(target))
                    error = ERROR_ALREADY_EXISTS;
            } else if (!replace) {
                error = ERROR_ALREADY_EXISTS;
            }
        }
        if (!error)
            error = op->Kind == OP_MOVE ? move_tree(source, target, replace) : copy_tree(source, target, replace);
        break;
    case OP_DELETE:
        error = delete_tree(source);
        break;
    case OP_NEW: {
        SIZE_T length = wcslen(target);
        if (length + wcslen(new_name) + 2 >= LONG_PATH) {
            error = ERROR_FILENAME_EXCED_RANGE;
            break;
        }
        if (length && target[length - 1] != L'\\')
            target[length++] = L'\\';
        wcscpy(target + length, new_name);
        if (GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES) {
            error = ERROR_ALREADY_EXISTS;
        } else if (op->Attributes & FILE_ATTRIBUTE_DIRECTORY) {
            error = CreateDirectoryW(target, nullptr) ? ERROR_SUCCESS : GetLastError();
        } else {
            DWORD attributes = op->Attributes & ~FILE_ATTRIBUTE_DIRECTORY;
            HANDLE file = CreateFileW(target, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      attributes ? attributes : FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
                error = GetLastError();
            else
                CloseHandle(file);
        }
        break;
    }
    }
    result = error_hr(error);
    if (!error && op->Kind != OP_DELETE)
        created = item_of(target);

    for (int i = 0; i < tell.Count; i++) {
        IFileOperationProgressSink *s = tell.List[i];
        switch (op->Kind) {
        case OP_RENAME:
            s->lpVtbl->PostRenameItem(s, tsf, op->Item, op->Name, result, created);
            break;
        case OP_MOVE:
            s->lpVtbl->PostMoveItem(s, tsf, op->Item, op->Folder, op->Name, result, created);
            break;
        case OP_COPY:
            s->lpVtbl->PostCopyItem(s, tsf, op->Item, op->Folder, op->Name, result, created);
            break;
        case OP_DELETE:
            s->lpVtbl->PostDeleteItem(s, tsf, op->Item, result, nullptr);
            break;
        case OP_NEW:
            s->lpVtbl->PostNewItem(s, tsf, op->Item, op->Name, op->Template, op->Attributes, result, created);
            break;
        }
    }
    if (created)
        created->lpVtbl->Release(created);
    gather_release(&tell);
    return result;
}

/*
 * the operations run in the order they were queued, between StartOperations and FinishOperations. an operation that
 * failed is told to the sinks and is not an error of PerformOperations, which only fails when it cannot run at all;
 * with FOFX_EARLYFAILURE the first failure ends the rest. FinishOperations gets that first failure.
 */
static HRESULT STDMETHODCALLTYPE fo_perform(IFileOperation *This)
{
    FILE_OPERATION *f = from_interface(This);
    if (f->Running)
        return E_UNEXPECTED;
    f->Running = TRUE;
    f->Aborted = FALSE;
    FO_OP *ops = f->Ops;
    ULONG count = f->Count;
    f->Ops = nullptr;
    f->Count = f->Capacity = 0;
    This->lpVtbl->AddRef(This);
    TELL tell;
    gather(f, nullptr, &tell);
    for (int i = 0; i < tell.Count; i++)
        tell.List[i]->lpVtbl->StartOperations(tell.List[i]);
    HRESULT first = S_OK;
    for (ULONG i = 0; i < count; i++) {
        HRESULT hr = first != S_OK && (f->Flags & FOFX_EARLYFAILURE) ? E_ABORT : run_one(f, &ops[i]);
        if (FAILED(hr) && first == S_OK)
            first = hr;
        if (hr == E_ABORT)
            f->Aborted = TRUE;
        for (int k = 0; k < tell.Count; k++)
            tell.List[k]->lpVtbl->UpdateProgress(tell.List[k], count, i + 1);
    }
    for (int i = 0; i < tell.Count; i++)
        tell.List[i]->lpVtbl->FinishOperations(tell.List[i], first);
    gather_release(&tell);
    for (ULONG i = 0; i < count; i++)
        free_op(&ops[i]);
    HeapFreeBlock(ops);
    f->Running = FALSE;
    This->lpVtbl->Release(This);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fo_get_aborted(IFileOperation *This, BOOL *aborted)
{
    if (!aborted)
        return E_INVALIDARG;
    *aborted = from_interface(This)->Aborted;
    return S_OK;
}

static const IFileOperationVtbl g_operation_vtbl = {
    fo_query_interface, fo_add_ref,    fo_release,         fo_advise,      fo_unadvise,
    fo_set_flags,       fo_set_message, fo_set_dialog,      fo_set_properties, fo_set_owner,
    fo_apply_properties, fo_apply_properties_items, fo_rename_item, fo_rename_items, fo_move_item,
    fo_move_items,      fo_copy_item,  fo_copy_items,      fo_delete_item, fo_delete_items,
    fo_new_item,        fo_perform,    fo_get_aborted,
};

/* the object the class factory makes, a queue with the flags of a call with no flags at all. */
HRESULT FileOperationCreate(IUnknown *outer, REFIID riid, void **object)
{
    if (outer)
        return CLASS_E_NOAGGREGATION;
    FILE_OPERATION *f = HeapAllocZero(sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    f->Interface.lpVtbl = &g_operation_vtbl;
    f->Refs = 1;
    HRESULT hr = fo_query_interface(&f->Interface, riid, object);
    fo_release(&f->Interface);
    return hr;
}
