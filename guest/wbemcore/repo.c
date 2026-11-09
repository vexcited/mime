/*
 * the repository: namespaces with their classes and instances. what the engine ships with is built into the code
 * and never saved; everything a program adds is kept in one mof file that every process reads, and is read again when
 * another process changed it.
 */
#include "wbemcore.h"

static CRITICAL_SECTION g_lock;
static BOOL g_lock_ready;
static HANDLE g_write_mutex;
static WNs **g_ns;
static UINT g_ns_count, g_ns_capacity;
static BOOL g_ready, g_loading;
static FILETIME g_stamp;
static UINT g_depth, g_write_depth;
static BOOL g_dirty;

static const WCHAR *repository_path(void)
{
    static WCHAR path[MAX_PATH];
    if (!path[0]) {
        GetSystemDirectoryW(path, MAX_PATH);
        wcscat(path, L"\\wbem\\Repository\\repository.mof");
    }
    return path;
}

void wr_init(void)
{
    if (!g_lock_ready) {
        InitializeCriticalSection(&g_lock);
        g_lock_ready = TRUE;
    }
}

static void ns_free(WNs *ns)
{
    for (UINT i = 0; i < ns->class_count; i++)
        wo_free(ns->classes[i]);
    wb_free(ns->classes);
    for (UINT i = 0; i < ns->instance_count; i++)
        wo_free(ns->instances[i]);
    wb_free(ns->instances);
    wb_free(ns->path);
    wb_free(ns->sddl);
    wb_free(ns);
}

static void clear_all(void)
{
    for (UINT i = 0; i < g_ns_count; i++)
        ns_free(g_ns[i]);
    g_ns_count = 0;
}

static BOOL file_stamp(FILETIME *stamp)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(repository_path(), GetFileExInfoStandard, &data)) {
        memset(stamp, 0, sizeof(*stamp));
        return FALSE;
    }
    *stamp = data.ftLastWriteTime;
    return TRUE;
}

static void load_file(void)
{
    HANDLE file = CreateFileW(repository_path(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    LARGE_INTEGER size;
    BYTE *bytes = nullptr;
    DWORD got = 0;
    if (GetFileSizeEx(file, &size) && size.QuadPart < 0x10000000) {
        bytes = wb_alloc((SIZE_T)size.QuadPart + 4);
        if (!ReadFile(file, bytes, (DWORD)size.QuadPart, &got, nullptr))
            got = 0;
    }
    CloseHandle(file);
    if (bytes && got >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        WMofResult info;
        wmof_compile((const WCHAR *)(bytes + 2), (got - 2) / sizeof(WCHAR), nullptr, 0, 0, 0, &info, FALSE);
    }
    wb_free(bytes);
}

static void rebuild(void)
{
    g_ready = TRUE;
    g_loading = TRUE;
    clear_all();
    WMofResult info;
    wmof_compile(g_base_mof, wcslen(g_base_mof), L"root", 0, 0, 0, &info, TRUE);
    load_file();
    g_loading = FALSE;
    file_stamp(&g_stamp);
    g_dirty = FALSE;
    g_ready = TRUE;
}

void wr_lock(void)
{
    wr_init();
    EnterCriticalSection(&g_lock);
    g_depth++;
    if (g_depth > 1 && g_ready)
        return;
    if (!g_ready) {
        rebuild();
        return;
    }
    FILETIME now;
    file_stamp(&now);
    if (CompareFileTime(&now, &g_stamp) != 0)
        rebuild();
}

void wr_unlock(void)
{
    g_depth--;
    LeaveCriticalSection(&g_lock);
}

/* ------------------------------------------------------------------ writing */

static void write_begin(void)
{
    if (!g_loading && !g_write_depth) {
        if (!g_write_mutex)
            g_write_mutex = CreateMutexW(nullptr, FALSE, L"Global\\WbemRepository");
        if (g_write_mutex) {
            WaitForSingleObject(g_write_mutex, 30000);
            /* another process may have written since this one last looked. */
            FILETIME now;
            file_stamp(&now);
            if (CompareFileTime(&now, &g_stamp) != 0)
                rebuild();
        }
    }
    g_write_depth++;
}

static void add_pragma_namespace(WBuf *buf, const WCHAR *path)
{
    wb_buf_add(buf, L"#pragma namespace(\"\\\\\\\\.\\\\");
    for (const WCHAR *c = path; *c; c++) {
        if (*c == '\\')
            wb_buf_add(buf, L"\\\\");
        else
            wb_buf_addc(buf, *c);
    }
    wb_buf_add(buf, L"\")\n");
}

static void save_now(void)
{
    WBuf buf = {0};
    for (UINT n = 0; n < g_ns_count; n++) {
        WNs *ns = g_ns[n];
        BOOL header = FALSE;
        if (ns->sddl) {
            add_pragma_namespace(&buf, ns->path);
            header = TRUE;
            wb_buf_add(&buf, L"#pragma mimesd(\"");
            for (const WCHAR *c = ns->sddl; *c; c++) {
                if (*c == '\\' || *c == '"')
                    wb_buf_addc(&buf, '\\');
                wb_buf_addc(&buf, *c);
            }
            wb_buf_add(&buf, L"\")\n");
        }
        for (UINT i = 0; i < ns->class_count; i++) {
            if (ns->classes[i]->builtin)
                continue;
            if (!header) {
                add_pragma_namespace(&buf, ns->path);
                header = TRUE;
            }
            WCHAR *text = wo_text(ns->classes[i], 0);
            wb_buf_add(&buf, text);
            wb_free(text);
        }
        for (UINT i = 0; i < ns->instance_count; i++) {
            if (ns->instances[i]->builtin)
                continue;
            if (!header) {
                add_pragma_namespace(&buf, ns->path);
                header = TRUE;
            }
            WCHAR *text = wo_text(ns->instances[i], 0);
            wb_buf_add(&buf, text);
            wb_free(text);
        }
    }
    WCHAR dir[MAX_PATH];
    wcscpy(dir, repository_path());
    WCHAR *slash = wcsrchr(dir, '\\');
    if (slash) {
        *slash = 0;
        CreateDirectoryW(dir, nullptr);
    }
    WCHAR temp[MAX_PATH];
    wcscpy(temp, repository_path());
    wcscat(temp, L".tmp");
    HANDLE file = CreateFileW(temp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WORD bom = 0xFEFF;
        BOOL ok = WriteFile(file, &bom, sizeof(bom), &written, nullptr);
        ok = ok && WriteFile(file, buf.text ? buf.text : L"", (DWORD)(buf.length * sizeof(WCHAR)), &written, nullptr);
        CloseHandle(file);
        if (ok)
            MoveFileExW(temp, repository_path(), MOVEFILE_REPLACE_EXISTING);
        else
            DeleteFileW(temp);
    }
    wb_free(buf.text);
    file_stamp(&g_stamp);
}

static void write_end(void)
{
    g_write_depth--;
    if (g_write_depth)
        return;
    if (g_dirty && !g_loading)
        save_now();
    g_dirty = FALSE;
    if (!g_loading && g_write_mutex)
        ReleaseMutex(g_write_mutex);
}

void wr_save(void)
{
    g_dirty = TRUE;
}

void wr_write_begin(void)
{
    write_begin();
}

void wr_write_end(void)
{
    write_end();
}

/* ------------------------------------------------------------------ namespaces */

static WCHAR *normalize(const WCHAR *path)
{
    while (path[0] == '\\' && path[1] == '\\')
        path += 2;
    if (path[0] == '.' && (path[1] == '\\' || path[1] == '/'))
        path += 2;
    else if (path[0] == '.' && !path[1])
        path++;
    else if (path[0] == '\\' || path[0] == '/') {
        /* \\server\ns: the server is dropped. */
        const WCHAR *c = path;
        while (*c == '\\' || *c == '/')
            c++;
        path = c;
    }
    WCHAR *copy = wb_wcsdup(path);
    for (WCHAR *c = copy; *c; c++)
        if (*c == '/')
            *c = '\\';
    SIZE_T n = wcslen(copy);
    while (n && copy[n - 1] == '\\')
        copy[--n] = 0;
    return copy;
}

WNs *wr_find_ns(const WCHAR *path)
{
    WCHAR *clean = normalize(path);
    WNs *found = nullptr;
    for (UINT i = 0; i < g_ns_count && !found; i++)
        if (wb_eq(g_ns[i]->path, clean))
            found = g_ns[i];
    wb_free(clean);
    return found;
}

static BOOL is_system_class(const WObj *c)
{
    return c->name[0] == '_' && c->name[1] == '_';
}

WNs *wr_create_ns(const WCHAR *path)
{
    WCHAR *clean = normalize(path);
    WNs *ns = wr_find_ns(clean);
    if (!ns) {
        ns = wb_alloc(sizeof(WNs));
        ns->path = clean;
        clean = nullptr;
        WB_GROW(g_ns, g_ns_count, g_ns_capacity);
        g_ns[g_ns_count++] = ns;
        /* the system classes are in every namespace. */
        WNs *root = wr_find_ns(L"root");
        if (root && root != ns) {
            for (UINT i = 0; i < root->class_count; i++) {
                if (is_system_class(root->classes[i])) {
                    WObj *copy = wo_clone(root->classes[i]);
                    wb_free(copy->ns);
                    copy->ns = wb_wcsdup(ns->path);
                    WB_GROW(ns->classes, ns->class_count, ns->class_capacity);
                    ns->classes[ns->class_count++] = copy;
                }
            }
        }
    }
    wb_free(clean);
    return ns;
}

BOOL wr_delete_ns(const WCHAR *path)
{
    WCHAR *clean = normalize(path);
    SIZE_T n = wcslen(clean);
    BOOL removed = FALSE;
    for (UINT i = g_ns_count; i-- > 0;) {
        if (wb_eq(g_ns[i]->path, clean) || (!_wcsnicmp(g_ns[i]->path, clean, n) && g_ns[i]->path[n] == '\\')) {
            ns_free(g_ns[i]);
            memmove(&g_ns[i], &g_ns[i + 1], (g_ns_count - i - 1) * sizeof(WNs *));
            g_ns_count--;
            removed = TRUE;
        }
    }
    wb_free(clean);
    return removed;
}

/* ------------------------------------------------------------------ classes */

WObj *wr_find_class(WNs *ns, const WCHAR *name)
{
    for (UINT i = 0; i < ns->class_count; i++)
        if (wb_eq(ns->classes[i]->name, name))
            return ns->classes[i];
    return nullptr;
}

UINT wr_subclasses(WNs *ns, const WCHAR *name, BOOL deep, WObj ***out)
{
    WObj **list = nullptr;
    UINT n = 0, capacity = 0;
    for (UINT i = 0; i < ns->class_count; i++) {
        WObj *c = ns->classes[i];
        BOOL match;
        if (!name || !*name)
            match = deep || !c->super;
        else if (deep)
            match = wo_inherits(c, name) && !wb_eq(c->name, name);
        else
            match = c->super && wb_eq(c->super, name);
        if (match) {
            WB_GROW(list, n, capacity);
            list[n++] = c;
        }
    }
    *out = list;
    return n;
}

HRESULT wr_put_class(WNs *ns, const WObj *c, LONG flags)
{
    if (!c->is_class || !c->name || !*c->name)
        return WBEM_E_INVALID_CLASS;
    if (c->super && !wr_find_class(ns, c->super))
        return WBEM_E_INVALID_SUPERCLASS;
    write_begin();
    HRESULT hr = S_OK;
    WObj *existing = wr_find_class(ns, c->name);
    if (existing && (flags & 3) == WBEM_FLAG_CREATE_ONLY) {
        hr = WBEM_E_ALREADY_EXISTS;
    } else if (!existing && (flags & 3) == WBEM_FLAG_UPDATE_ONLY) {
        hr = WBEM_E_NOT_FOUND;
    } else {
        WObj *copy = wo_clone(c);
        wb_free(copy->ns);
        copy->ns = wb_wcsdup(ns->path);
        if (existing) {
            for (UINT i = 0; i < ns->class_count; i++) {
                if (ns->classes[i] == existing) {
                    wo_free(existing);
                    ns->classes[i] = copy;
                    break;
                }
            }
        } else {
            WB_GROW(ns->classes, ns->class_count, ns->class_capacity);
            ns->classes[ns->class_count++] = copy;
        }
        if (!copy->builtin)
            wr_save();
    }
    write_end();
    return hr;
}

HRESULT wr_delete_class(WNs *ns, const WCHAR *name)
{
    WObj *c = wr_find_class(ns, name);
    if (!c)
        return WBEM_E_NOT_FOUND;
    write_begin();
    HRESULT hr = S_OK;
    for (UINT i = 0; i < ns->class_count; i++) {
        if (ns->classes[i]->super && wb_eq(ns->classes[i]->super, name)) {
            hr = WBEM_E_CLASS_HAS_CHILDREN;
            break;
        }
    }
    if (SUCCEEDED(hr)) {
        for (UINT i = ns->instance_count; i-- > 0;) {
            if (wb_eq(ns->instances[i]->name, name)) {
                wo_free(ns->instances[i]);
                memmove(&ns->instances[i], &ns->instances[i + 1], (ns->instance_count - i - 1) * sizeof(WObj *));
                ns->instance_count--;
            }
        }
        for (UINT i = 0; i < ns->class_count; i++) {
            if (ns->classes[i] == c) {
                wo_free(c);
                memmove(&ns->classes[i], &ns->classes[i + 1], (ns->class_count - i - 1) * sizeof(WObj *));
                ns->class_count--;
                break;
            }
        }
        wr_save();
    }
    write_end();
    return hr;
}

/* ------------------------------------------------------------------ instances */

WObj *wr_find_instance(WNs *ns, const WCHAR *relpath)
{
    for (UINT i = 0; i < ns->instance_count; i++) {
        WCHAR *rel = wo_relpath(ns->instances[i]);
        BOOL same = rel && !_wcsicmp(rel, relpath);
        wb_free(rel);
        if (same)
            return ns->instances[i];
    }
    return nullptr;
}

/* the value of the name key of a namespace instance. */
static WCHAR *namespace_child(WNs *ns, const WObj *instance)
{
    WProp *name = wo_prop(instance, L"Name");
    if (!name || name->value.vt != VT_BSTR || !name->value.bstrVal)
        return nullptr;
    return wb_format(L"%s\\%s", ns->path, name->value.bstrVal);
}

HRESULT wr_put_instance(WNs *ns, const WObj *instance, LONG flags)
{
    if (instance->is_class || !instance->name)
        return WBEM_E_INVALID_OBJECT;
    WObj *class_object = wr_find_class(ns, instance->name);
    if (!class_object)
        return WBEM_E_INVALID_CLASS;
    write_begin();
    HRESULT hr = S_OK;
    WObj *copy = wo_clone(instance);
    wb_free(copy->ns);
    copy->ns = wb_wcsdup(ns->path);
    /* the instance takes the schema of its class, keeping the values it has. */
    WObj *fresh = wo_spawn(class_object);
    for (UINT i = 0; i < copy->prop_count; i++) {
        WProp *dest = wo_prop(fresh, copy->props[i].name);
        if (dest) {
            VariantClear(&dest->value);
            dest->value = copy->props[i].value;
            VariantInit(&copy->props[i].value);
        }
    }
    wb_free(fresh->ns);
    fresh->ns = wb_wcsdup(ns->path);
    fresh->builtin = instance->builtin;
    wo_free(copy);
    copy = fresh;
    WCHAR *rel = wo_relpath(copy);
    WObj *existing = rel ? wr_find_instance(ns, rel) : nullptr;
    if (!rel && wo_keys(copy, nullptr, 0)) {
        hr = WBEM_E_ILLEGAL_NULL;
    } else if (existing && (flags & 3) == WBEM_FLAG_CREATE_ONLY) {
        hr = WBEM_E_ALREADY_EXISTS;
    } else if (!existing && (flags & 3) == WBEM_FLAG_UPDATE_ONLY) {
        hr = WBEM_E_NOT_FOUND;
    }
    if (SUCCEEDED(hr)) {
        if (existing) {
            for (UINT i = 0; i < ns->instance_count; i++) {
                if (ns->instances[i] == existing) {
                    wo_free(existing);
                    ns->instances[i] = copy;
                    copy = nullptr;
                    break;
                }
            }
        } else {
            WB_GROW(ns->instances, ns->instance_count, ns->instance_capacity);
            ns->instances[ns->instance_count++] = copy;
            copy = nullptr;
        }
        /* an instance of a namespace class makes the namespace. */
        if (wo_inherits(class_object, L"__Namespace")) {
            WObj *stored = existing ? nullptr : ns->instances[ns->instance_count - 1];
            WCHAR *child = stored ? namespace_child(ns, stored) : nullptr;
            if (child) {
                BOOL was_builtin = stored->builtin;
                BOOL loading = g_loading;
                wr_create_ns(child);
                (void)was_builtin;
                (void)loading;
            }
            wb_free(child);
        }
        wr_save();
    }
    wo_free(copy);
    wb_free(rel);
    write_end();
    return hr;
}

HRESULT wr_delete_instance(WNs *ns, const WCHAR *relpath)
{
    WObj *inst = wr_find_instance(ns, relpath);
    if (!inst)
        return WBEM_E_NOT_FOUND;
    write_begin();
    WObj *class_object = wr_find_class(ns, inst->name);
    if (class_object && wo_inherits(class_object, L"__Namespace")) {
        WCHAR *child = namespace_child(ns, inst);
        if (child)
            wr_delete_ns(child);
        wb_free(child);
    }
    for (UINT i = 0; i < ns->instance_count; i++) {
        if (ns->instances[i] == inst) {
            wo_free(inst);
            memmove(&ns->instances[i], &ns->instances[i + 1], (ns->instance_count - i - 1) * sizeof(WObj *));
            ns->instance_count--;
            break;
        }
    }
    wr_save();
    write_end();
    return S_OK;
}
