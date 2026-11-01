/*
 * objects of external classes, a search in a variant and the files of a program.
 */
#include "vb.h"

/* ------------------------------------------------------------------ New */

/*
 * the description of a class the compiler writes: flags, where 1 is a class of the project and 2 an object that is
 * running already, the class id, the interface asked for and a license key.
 */
typedef struct VbClassInfo {
    BYTE flags;
    BYTE reserved[3];
    const CLSID *clsid;
    const IID *iid;
    const WCHAR *license;
} VbClassInfo;

/* the factory that makes objects of a class that is licensed. */
typedef struct LicensedFactory LicensedFactory;
struct LicensedFactory {
    const struct {
        HRESULT(STDMETHODCALLTYPE *QueryInterface)(LicensedFactory *, REFIID, void **);
        ULONG(STDMETHODCALLTYPE *AddRef)(LicensedFactory *);
        ULONG(STDMETHODCALLTYPE *Release)(LicensedFactory *);
        HRESULT(STDMETHODCALLTYPE *CreateInstance)(LicensedFactory *, IUnknown *, REFIID, void **);
        HRESULT(STDMETHODCALLTYPE *LockServer)(LicensedFactory *, BOOL);
        HRESULT(STDMETHODCALLTYPE *GetLicInfo)(LicensedFactory *, void *);
        HRESULT(STDMETHODCALLTYPE *RequestLicKey)(LicensedFactory *, DWORD, BSTR *);
        HRESULT(STDMETHODCALLTYPE *CreateInstanceLic)(LicensedFactory *, IUnknown *, IUnknown *, REFIID, BSTR, void **);
    } *lpVtbl;
};

static const IID IID_LicensedFactory = {0xB196B28F, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}};

VB_STD LONG vb___vbaInStr(LONG locale_mode, BSTR find, BSTR haystack, LONG start);

static HRESULT create_object(const VbClassInfo *info, void **object)
{
    HRESULT hr = E_FAIL;
    *object = nullptr;
    if (info->flags & 2) {
        IUnknown *running = nullptr;
        hr = GetActiveObject(info->clsid, nullptr, &running);
        if (SUCCEEDED(hr)) {
            hr = running->lpVtbl->QueryInterface(running, info->iid, object);
            running->lpVtbl->Release(running);
            return hr;
        }
    }
    if (info->license) {
        LicensedFactory *factory = nullptr;
        hr = CoGetClassObject(info->clsid, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, nullptr, &IID_LicensedFactory, (void **)&factory);
        if (FAILED(hr))
            return hr;
        BSTR key = SysAllocString(info->license);
        hr = factory->lpVtbl->CreateInstanceLic(factory, nullptr, nullptr, info->iid, key, object);
        SysFreeString(key);
        factory->lpVtbl->Release(factory);
        return hr;
    }
    return CoCreateInstance(info->clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, info->iid, object);
}

/* a class that fails to start is error 429, one that lacks the interface 430. */
static void make(const VbClassInfo *info, void **slot)
{
    if (info->flags & 1)
        VbRaise(VBERR_CANNOT_CREATE_OBJECT);
    void *object;
    HRESULT hr = create_object(info, &object);
    if (FAILED(hr)) {
        if (hr == E_NOINTERFACE)
            VbRaise(430);
        VbRaise(VBERR_CANNOT_CREATE_OBJECT);
    }
    *slot = object;
}

VB_STD void *vb___vbaNew(const VbClassInfo *info)
{
    void *object;
    make(info, &object);
    return object;
}

VB_STD LONG vb___vbaNew2(const VbClassInfo *info, void **slot)
{
    void *object;
    make(info, &object);
    *slot = object;
    return 0;
}

/* ------------------------------------------------------------------ InStr */

static BSTR text_of(VARIANT *v)
{
    VARIANT t;
    VariantInit(&t);
    HRESULT hr = VariantChangeTypeEx(&t, v, VbLocale(), 0, VT_BSTR);
    if (hr == DISP_E_TYPEMISMATCH && V_VT(v) == VT_DISPATCH)
        VbCheck(VariantChangeTypeEx(&t, v, VbLocale(), 0, VT_BSTR));
    VbCheck(hr);
    return V_BSTR(&t);
}

VB_STD VARIANT *vb___vbaInStrVar(VARIANT *dest, LONG mode, VARIANT *find, VARIANT *text, LONG start)
{
    VariantInit(dest);
    if (V_VT(text) == VT_NULL || V_VT(find) == VT_NULL) {
        V_VT(dest) = VT_NULL;
        return dest;
    }
    BSTR haystack = text_of(text), needle = text_of(find);
    LONG position = vb___vbaInStr(mode, needle, haystack, start);
    SysFreeString(haystack);
    SysFreeString(needle);
    V_VT(dest) = VT_I4;
    V_I4(dest) = position;
    return dest;
}

/* ------------------------------------------------------------------ files */

typedef struct VbFile {
    HANDLE handle;
    BYTE mode;
    LONG record_length;
} VbFile;

#define MAX_FILES 512
static VbFile g_files[MAX_FILES + 1];

static VbFile *file_of(LONG number)
{
    if (number < 1 || number > MAX_FILES || !g_files[number].handle)
        VbRaise(VBERR_BAD_FILE_NUMBER);
    return &g_files[number];
}

/* Open: the mode in the low byte of the first word, the access and the sharing above it. */
VB_STD void vb___vbaFileOpen(SHORT mode_word, SHORT record_length, SHORT number, const WCHAR *name)
{
    BYTE mode = (BYTE)mode_word;
    ULONG access_bits = (USHORT)mode_word & 0xF00;
    ULONG share_bits = (USHORT)mode_word & 0xF000;
    if (number < 1 || number > MAX_FILES)
        VbRaise(VBERR_BAD_FILE_NUMBER);
    if (g_files[number].handle)
        VbRaise(VBERR_FILE_ALREADY_OPEN);
    if (record_length != -1 && record_length <= 0)
        VbRaise(VBERR_INVALID_CALL);
    ULONG access = GENERIC_READ | GENERIC_WRITE, creation = OPEN_ALWAYS;
    switch (mode) {
    case 1: access = GENERIC_READ; creation = OPEN_EXISTING; break;
    case 2: access = GENERIC_WRITE; creation = CREATE_ALWAYS; break;
    case 8: access = GENERIC_WRITE; creation = OPEN_ALWAYS; break;
    case 4:
    case 32: break;
    default: VbRaise(VBERR_INVALID_CALL);
    }
    if (access_bits == 0x100)
        access = GENERIC_READ;
    else if (access_bits == 0x200)
        access = GENERIC_WRITE;
    ULONG share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    if (share_bits == 0x1000)
        share = FILE_SHARE_READ;
    else if (share_bits == 0x2000)
        share = FILE_SHARE_WRITE;
    else if (share_bits == 0x3000)
        share = 0;
    HANDLE handle = CreateFileW(name, access, share, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        switch (GetLastError()) {
        case ERROR_FILE_NOT_FOUND: VbRaise(VBERR_FILE_NOT_FOUND);
        case ERROR_PATH_NOT_FOUND: VbRaise(VBERR_PATH_NOT_FOUND);
        case ERROR_ACCESS_DENIED: VbRaise(70);
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION: VbRaise(70);
        case ERROR_INVALID_NAME: VbRaise(VBERR_BAD_FILE_NAME);
        default: VbRaise(VBERR_IO);
        }
    }
    if (mode == 8)
        SetFilePointer(handle, 0, nullptr, FILE_END);
    g_files[number].handle = handle;
    g_files[number].mode = mode;
    g_files[number].record_length = record_length == -1 ? (mode == 4 ? 128 : 512) : record_length;
}

VB_STD void vb___vbaFileClose(SHORT number)
{
    if (number == 0) {
        for (int i = 1; i <= MAX_FILES; i++) {
            if (g_files[i].handle) {
                CloseHandle(g_files[i].handle);
                g_files[i].handle = nullptr;
            }
        }
        return;
    }
    VbFile *file = file_of(number);
    CloseHandle(file->handle);
    file->handle = nullptr;
}

/* one line of a file in the ansi code page, without its end of line. */
static BSTR read_line(VbFile *file)
{
    char *buffer = nullptr;
    DWORD length = 0, capacity = 0, got;
    BOOL any = FALSE;
    for (;;) {
        char c;
        if (!ReadFile(file->handle, &c, 1, &got, nullptr) || got == 0)
            break;
        any = TRUE;
        if (c == '\n')
            break;
        if (c == '\r') {
            DWORD position = SetFilePointer(file->handle, 0, nullptr, FILE_CURRENT);
            char next;
            if (ReadFile(file->handle, &next, 1, &got, nullptr) && got == 1 && next != '\n')
                SetFilePointer(file->handle, (LONG)position, nullptr, FILE_BEGIN);
            break;
        }
        if (length + 1 >= capacity) {
            capacity = capacity ? capacity * 2 : 128;
            char *larger = HeapReAlloc(GetProcessHeap(), 0, buffer ? buffer : HeapAlloc(GetProcessHeap(), 0, capacity), capacity);
            if (!larger)
                VbRaise(VBERR_OUT_OF_MEMORY);
            buffer = larger;
        }
        buffer[length++] = c;
    }
    if (!any) {
        HeapFree(GetProcessHeap(), 0, buffer);
        VbRaise(VBERR_END_OF_FILE);
    }
    int wide = length ? MultiByteToWideChar(CP_ACP, 0, buffer, (int)length, nullptr, 0) : 0;
    BSTR text = SysAllocStringLen(nullptr, (UINT)wide);
    if (wide)
        MultiByteToWideChar(CP_ACP, 0, buffer, (int)length, text, wide);
    HeapFree(GetProcessHeap(), 0, buffer);
    return text;
}

VB_STD BSTR vb___vbaLineInputStr(BSTR *slot, SHORT number)
{
    VbFile *file = file_of(number);
    if (file->mode != 1 && file->mode != 4 && file->mode != 32)
        VbRaise(54);
    BSTR text = read_line(file);
    SysFreeString(*slot);
    *slot = text;
    return text;
}

VB_STD BSTR vb___vbaLineInputVar(VARIANT *v, SHORT number)
{
    if (V_VT(v) == VT_BSTR)
        return vb___vbaLineInputStr(&V_BSTR(v), number);
    if (V_VT(v) & VT_BYREF) {
        if (V_VT(v) != (VT_BSTR | VT_BYREF))
            VbRaise(VBERR_TYPE_MISMATCH);
        return vb___vbaLineInputStr(v->pbstrVal, number);
    }
    VariantClear(v);
    V_VT(v) = VT_BSTR;
    V_BSTR(v) = nullptr;
    return vb___vbaLineInputStr(&V_BSTR(v), number);
}
