#include "mscms_private.h"

static void *heap_alloc(void *ctx, size_t size) { return HeapAlloc(GetProcessHeap(), 0, size ? size : 1); }
static void heap_free(void *ctx, void *p) { HeapFree(GetProcessHeap(), 0, p); }
const MimeAllocator mscms_alloc = {heap_alloc, heap_free, nullptr};

Profile *ProfileOf(HANDLE handle)
{
    if (!handle)
        return nullptr;
    Profile *p = (Profile *)((ULONG_PTR)handle ^ HANDLE_KEY);
    return p && p->magic == PROFILE_MAGIC ? p : nullptr;
}

HANDLE ProfileHandle(Profile *p) { return (HANDLE)((ULONG_PTR)p ^ HANDLE_KEY); }

Transform *TransformOf(HANDLE handle)
{
    if (!handle)
        return nullptr;
    Transform *t = (Transform *)((ULONG_PTR)handle ^ HANDLE_KEY);
    return t && t->magic == TRANSFORM_MAGIC ? t : nullptr;
}

static Profile *profile_new(DWORD access)
{
    Profile *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    p->magic = PROFILE_MAGIC;
    p->edit.alloc = mscms_alloc;
    p->access = access;
    InitializeCriticalSection(&p->lock);
    return p;
}

static void profile_free(Profile *p)
{
    p->magic = 0;
    DeleteCriticalSection(&p->lock);
    mime_free(&mscms_alloc, p->edit.data);
    HeapFree(GetProcessHeap(), 0, p->path);
    HeapFree(GetProcessHeap(), 0, p);
}

HANDLE ProfileFromBytes(const BYTE *data, DWORD size, DWORD access)
{
    Profile *p = profile_new(access);
    if (!p)
        return nullptr;
    p->edit.data = mime_alloc(&mscms_alloc, size);
    if (!p->edit.data) {
        profile_free(p);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    memcpy(p->edit.data, data, size);
    p->edit.size = size;
    return ProfileHandle(p);
}

WCHAR *WideFromAnsi(const char *s)
{
    if (!s)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    WCHAR *w = HeapAlloc(GetProcessHeap(), 0, (n ? n : 1) * sizeof(WCHAR));
    if (w)
        MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
    return w;
}

BOOL AnsiOut(const WCHAR *s, char *buffer, DWORD *size)
{
    int n = WideCharToMultiByte(CP_ACP, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (!buffer || *size < (DWORD)n) {
        *size = n;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    WideCharToMultiByte(CP_ACP, 0, s, -1, buffer, n, nullptr, nullptr);
    *size = n;
    return TRUE;
}

static HANDLE open_profile(PROFILE *description, DWORD access, DWORD share, DWORD creation, BOOL wide)
{
    if (!description || (access != PROFILE_READ && access != PROFILE_READWRITE) || !description->pProfileData) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (description->dwType == PROFILE_MEMBUFFER) {
        if (!description->cbDataSize) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return nullptr;
        }
        if (!icc_valid(description->pProfileData, description->cbDataSize)) {
            SetLastError(ERROR_INVALID_PROFILE);
            return nullptr;
        }
        return ProfileFromBytes(description->pProfileData, description->cbDataSize, access);
    }
    if (description->dwType != PROFILE_FILENAME) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    WCHAR *name = wide ? (WCHAR *)description->pProfileData : WideFromAnsi(description->pProfileData);
    WCHAR path[MAX_PATH];
    BOOL named = name && ColorPath(name, path, MAX_PATH);
    if (!wide)
        HeapFree(GetProcessHeap(), 0, name);
    if (!named) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    DWORD rights = access == PROFILE_READWRITE ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ;
    HANDLE file = CreateFileW(path, rights, share, nullptr, creation ? creation : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    DWORD size = GetFileSize(file, nullptr), read = 0;
    Profile *p = nullptr;
    if (size == INVALID_FILE_SIZE) {
    } else if (size == 0) {
        /* a new file: a profile with a header only, to be filled and written back. */
        if (access != PROFILE_READWRITE) {
            SetLastError(ERROR_INVALID_PROFILE);
        } else if ((p = profile_new(access)) && !icc_edit_blank(&p->edit)) {
            profile_free(p);
            p = nullptr;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        }
    } else {
        BYTE *data = mime_alloc(&mscms_alloc, size);
        if (!data) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        } else if (!ReadFile(file, data, size, &read, nullptr) || read != size) {
            SetLastError(ERROR_READ_FAULT);
            mime_free(&mscms_alloc, data);
        } else if (!icc_valid(data, size)) {
            SetLastError(ERROR_INVALID_PROFILE);
            mime_free(&mscms_alloc, data);
        } else if (!(p = profile_new(access))) {
            mime_free(&mscms_alloc, data);
        } else {
            p->edit.data = data;
            p->edit.size = size;
        }
    }
    CloseHandle(file);
    if (!p)
        return nullptr;
    if (access == PROFILE_READWRITE) {
        size_t n = wcslen(path) + 1;
        p->path = HeapAlloc(GetProcessHeap(), 0, n * sizeof(WCHAR));
        if (p->path)
            memcpy(p->path, path, n * sizeof(WCHAR));
        /* a blank profile is a change the file has not seen yet. */
        p->dirty = size == 0;
    }
    return ProfileHandle(p);
}

HANDLE WINAPI OpenColorProfileA(PROFILE *description, DWORD access, DWORD share, DWORD creation)
{
    return open_profile(description, access, share, creation, FALSE);
}

HANDLE WINAPI OpenColorProfileW(PROFILE *description, DWORD access, DWORD share, DWORD creation)
{
    return open_profile(description, access, share, creation, TRUE);
}

/* WcsOpenColorProfile takes the device model profile and leaves the appearance and gamut map models to the engine. */
HANDLE WINAPI WcsOpenColorProfileW(PROFILE *cdmp, PROFILE *camp, PROFILE *gmmp, DWORD access, DWORD share,
                                   DWORD creation, DWORD flags)
{
    HANDLE h = open_profile(cdmp, access, share, creation, TRUE);
    Profile *p = ProfileOf(h);
    if (p)
        p->wcs = TRUE;
    return h;
}

HANDLE WINAPI WcsOpenColorProfileA(PROFILE *cdmp, PROFILE *camp, PROFILE *gmmp, DWORD access, DWORD share,
                                   DWORD creation, DWORD flags)
{
    HANDLE h = open_profile(cdmp, access, share, creation, FALSE);
    Profile *p = ProfileOf(h);
    if (p)
        p->wcs = TRUE;
    return h;
}

BOOL WINAPI CloseColorProfile(HANDLE handle)
{
    Profile *p = ProfileOf(handle);
    if (!p) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = TRUE;
    if (p->dirty && p->path) {
        HANDLE file = CreateFileW(p->path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written = 0, size = icc_be32(p->edit.data);
        if (size > p->edit.size)
            size = p->edit.size;
        ok = file != INVALID_HANDLE_VALUE && WriteFile(file, p->edit.data, size, &written, nullptr) && written == size;
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
    }
    profile_free(p);
    return ok;
}

/* the profile of a handle locked, with the checks windows makes: a handle, then a profile that is whole. */
static Profile *enter(HANDLE handle, BOOL whole, BOOL write)
{
    Profile *p = ProfileOf(handle);
    if (!p) {
        SetLastError(TransformOf(handle) ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (write && p->access != PROFILE_READWRITE) {
        SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }
    EnterCriticalSection(&p->lock);
    if (whole && !icc_valid(p->edit.data, p->edit.size)) {
        LeaveCriticalSection(&p->lock);
        SetLastError(ERROR_INVALID_PROFILE);
        return nullptr;
    }
    return p;
}

static BOOL leave(Profile *p, BOOL result)
{
    LeaveCriticalSection(&p->lock);
    return result;
}

/* a profile is valid when it is whole and has the tags its class needs to convert colors. */
BOOL WINAPI IsColorProfileValid(HANDLE handle, BOOL *valid)
{
    Profile *p = ProfileOf(handle);
    if (!p || !valid) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    EnterCriticalSection(&p->lock);
    const BYTE *d = p->edit.data;
    BOOL whole = icc_valid(d, p->edit.size), ok = whole;
    if (whole) {
        uint32_t cls = icc_be32(d + 12), space = icc_be32(d + 16);
        BOOL lut = icc_tag_index(d, ICC_SIG('A', '2', 'B', '0')) >= 0;
        BOOL matrix = icc_tag_index(d, ICC_SIG('r', 'X', 'Y', 'Z')) >= 0 && icc_tag_index(d, ICC_SIG('r', 'T', 'R', 'C')) >= 0;
        BOOL gray = icc_tag_index(d, ICC_SIG('k', 'T', 'R', 'C')) >= 0;
        switch (cls) {
        case ICC_SIG('s', 'c', 'n', 'r'):
        case ICC_SIG('m', 'n', 't', 'r'):
        case ICC_SIG('p', 'r', 't', 'r'):
        case ICC_SIG('s', 'p', 'a', 'c'):
            ok = lut || (space == ICC_SIG('R', 'G', 'B', ' ') && matrix) || (space == ICC_SIG('G', 'R', 'A', 'Y') && gray);
            break;
        case ICC_SIG('l', 'i', 'n', 'k'):
        case ICC_SIG('a', 'b', 's', 't'):
            ok = lut;
            break;
        case ICC_SIG('n', 'm', 'c', 'l'):
            ok = icc_tag_index(d, ICC_SIG('n', 'c', 'l', '2')) >= 0;
            break;
        default:
            ok = FALSE;
        }
    }
    *valid = ok;
    /* icm32's CMIsProfileValid gives its verdict as its result, which mscms returns, a broken header is only told in *valid. */
    return leave(p, !whole || ok);
}

/* the header goes out with every dword in the order of the machine, the reserved bytes too, as windows does. */
BOOL WINAPI GetColorProfileHeader(HANDLE handle, PROFILEHEADER *header)
{
    Profile *p = enter(handle, FALSE, FALSE);
    if (!p)
        return FALSE;
    if (!header) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return leave(p, FALSE);
    }
    if (p->edit.size < ICC_HEADER_SIZE) {
        SetLastError(ERROR_INVALID_PROFILE);
        return leave(p, FALSE);
    }
    DWORD *out = (DWORD *)header;
    for (int i = 0; i < 32; i++)
        out[i] = icc_be32(p->edit.data + 4 * i);
    return leave(p, TRUE);
}

BOOL WINAPI SetColorProfileHeader(HANDLE handle, PROFILEHEADER *header)
{
    Profile *p = enter(handle, FALSE, TRUE);
    if (!p)
        return FALSE;
    if (!header) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return leave(p, FALSE);
    }
    const DWORD *in = (const DWORD *)header;
    for (int i = 0; i < 32; i++)
        icc_put32(p->edit.data + 4 * i, in[i]);
    p->dirty = TRUE;
    return leave(p, TRUE);
}

BOOL WINAPI GetCountColorProfileElements(HANDLE handle, DWORD *count)
{
    if (!count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Profile *p = enter(handle, TRUE, FALSE);
    if (!p)
        return FALSE;
    *count = icc_tag_count(p->edit.data);
    return leave(p, TRUE);
}

/* the index counts from 1. */
BOOL WINAPI GetColorProfileElementTag(HANDLE handle, DWORD index, DWORD *tag)
{
    if (!tag || !index) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Profile *p = enter(handle, TRUE, FALSE);
    if (!p)
        return FALSE;
    if (index > icc_tag_count(p->edit.data)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return leave(p, FALSE);
    }
    *tag = icc_be32(p->edit.data + 132 + (index - 1) * 12);
    return leave(p, TRUE);
}

BOOL WINAPI IsColorProfileTagPresent(HANDLE handle, DWORD tag, BOOL *present)
{
    if (!present) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Profile *p = enter(handle, TRUE, FALSE);
    if (!p)
        return FALSE;
    *present = icc_tag_index(p->edit.data, tag) >= 0;
    return leave(p, TRUE);
}

/*
 * without a buffer the size of the tag comes back with ERROR_INSUFFICIENT_BUFFER and FALSE. with one, at most *size
 * bytes from offset are copied and *size says how many. *reference tells whether the data is shared with another tag.
 */
BOOL WINAPI GetColorProfileElement(HANDLE handle, DWORD tag, DWORD offset, DWORD *size, void *buffer, BOOL *reference)
{
    if (!size || !reference) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Profile *p = enter(handle, TRUE, FALSE);
    if (!p)
        return FALSE;
    const BYTE *d = p->edit.data;
    int i = icc_tag_index(d, tag);
    if (i < 0) {
        SetLastError(ERROR_TAG_NOT_FOUND);
        return leave(p, FALSE);
    }
    DWORD total = p->edit.size, at = icc_be32(d + 136 + i * 12), length = icc_be32(d + 140 + i * 12);
    if (at >= total || length < 8) {
        SetLastError(ERROR_INVALID_PROFILE);
        return leave(p, FALSE);
    }
    if (length > total - at)
        length = total - at;
    *reference = icc_tag_shared(d, (uint32_t)i);
    if (!buffer) {
        *size = length;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return leave(p, FALSE);
    }
    if (offset >= length) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return leave(p, FALSE);
    }
    DWORD n = *size;
    if (n > length - offset)
        n = length - offset;
    memcpy(buffer, d + at + offset, n);
    *size = n;
    return leave(p, TRUE);
}

/* writes into a tag that exists, from offset, as much as fits: a shared tag gets data of its own first. */
BOOL WINAPI SetColorProfileElement(HANDLE handle, DWORD tag, DWORD offset, DWORD *size, void *buffer)
{
    if (!size || !buffer) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Profile *p = enter(handle, TRUE, TRUE);
    if (!p)
        return FALSE;
    if (icc_tag_index(p->edit.data, tag) < 0) {
        SetLastError(ERROR_TAG_NOT_FOUND);
        return leave(p, FALSE);
    }
    uint32_t length;
    BYTE *t = icc_edit_tag(&p->edit, tag, &length);
    if (!t) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return leave(p, FALSE);
    }
    if (offset >= length) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return leave(p, FALSE);
    }
    DWORD n = *size;
    if (n > length - offset)
        n = length - offset;
    memcpy(t + offset, buffer, n);
    *size = n;
    p->dirty = TRUE;
    return leave(p, TRUE);
}

/* a size of 0 removes the tag, a tag that is not there is added with that many zeros. */
BOOL WINAPI SetColorProfileElementSize(HANDLE handle, DWORD tag, DWORD size)
{
    Profile *p = enter(handle, TRUE, TRUE);
    if (!p)
        return FALSE;
    BOOL present = icc_tag_index(p->edit.data, tag) >= 0;
    if (!size && !present) {
        SetLastError(ERROR_TAG_NOT_FOUND);
        return leave(p, FALSE);
    }
    if (size && size < 8) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return leave(p, FALSE);
    }
    BOOL ok = size ? icc_edit_resize(&p->edit, tag, size) : icc_edit_remove(&p->edit, tag);
    if (!ok)
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    p->dirty |= ok;
    return leave(p, ok);
}

BOOL WINAPI SetColorProfileElementReference(HANDLE handle, DWORD tag, DWORD reference)
{
    Profile *p = enter(handle, TRUE, TRUE);
    if (!p)
        return FALSE;
    if (icc_tag_index(p->edit.data, reference) < 0) {
        SetLastError(ERROR_TAG_NOT_FOUND);
        return leave(p, FALSE);
    }
    if (icc_tag_index(p->edit.data, tag) >= 0) {
        SetLastError(ERROR_DUPLICATE_TAG);
        return leave(p, FALSE);
    }
    BOOL ok = icc_edit_reference(&p->edit, tag, reference);
    if (!ok)
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    p->dirty |= ok;
    return leave(p, ok);
}

/* the bytes of the profile, as long as its header says. */
BOOL WINAPI GetColorProfileFromHandle(HANDLE handle, BYTE *buffer, DWORD *size)
{
    if (!size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Profile *p = enter(handle, FALSE, FALSE);
    if (!p)
        return FALSE;
    DWORD n = p->edit.size >= 4 ? icc_be32(p->edit.data) : 0;
    if (n > p->edit.size)
        n = p->edit.size;
    BOOL ok = buffer && *size >= n;
    if (ok)
        memcpy(buffer, p->edit.data, n);
    else
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
    *size = n;
    return leave(p, ok);
}

/*
 * an icc profile from a wcs one. a wcs handle here wraps an icc profile already, so the result is a copy of it that
 * can be written to.
 */
HANDLE WINAPI WcsCreateIccProfile(HANDLE handle, DWORD options)
{
    Profile *p = ProfileOf(handle);
    if (!p || !p->wcs) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    EnterCriticalSection(&p->lock);
    HANDLE h = icc_valid(p->edit.data, p->edit.size) ? ProfileFromBytes(p->edit.data, icc_be32(p->edit.data), PROFILE_READWRITE)
                                                     : nullptr;
    if (!h && GetLastError() != ERROR_NOT_ENOUGH_MEMORY)
        SetLastError(ERROR_INVALID_PROFILE);
    LeaveCriticalSection(&p->lock);
    return h;
}

/* ------------------------------------------------------------------ profiles from logical color spaces */

#define LCS_CALIBRATED_RGB 0
#define LCS_sRGB 0x73524742
#define LCS_WINDOWS_COLOR_SPACE 0x57696E20

/*
 * an srgb profile for the standard spaces, else one from the endpoints and gammas: endpoints are 2.30 fixed point
 * xyz, gammas 16.16 fixed point. the buffer comes from GlobalAlloc.
 */
static BOOL profile_from_lcs(LONG type, const CIEXYZTRIPLE *endpoints, const DWORD gamma[3], BYTE **out)
{
    BYTE *data = nullptr;
    uint32_t size = 0;
    const CIEXYZ *e = &endpoints->ciexyzRed;
    BOOL calibrated = type == LCS_CALIBRATED_RGB && (e[0].ciexyzY || e[1].ciexyzY || e[2].ciexyzY);
    if (!calibrated) {
        data = icc_build_srgb(&mscms_alloc, &size);
    } else {
        double xyz[3][3];
        for (int i = 0; i < 3; i++) {
            xyz[i][0] = e[i].ciexyzX / 1073741824.0;
            xyz[i][1] = e[i].ciexyzY / 1073741824.0;
            xyz[i][2] = e[i].ciexyzZ / 1073741824.0;
        }
        double g = gamma[1] ? gamma[1] / 65536.0 : 0;
        IccRgbSpec spec = {.xyz = (const double (*)[3])xyz,
                           .curve = g ? ICC_CURVE_GAMMA : ICC_CURVE_SRGB,
                           .gamma = g,
                           .version = 2,
                           .description = "Calibrated RGB"};
        data = icc_build_rgb(&mscms_alloc, &spec, &size);
    }
    if (!data) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    *out = GlobalAlloc(GMEM_FIXED, size);
    if (*out)
        memcpy(*out, data, size);
    else
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    mime_free(&mscms_alloc, data);
    return *out != nullptr;
}

BOOL WINAPI CreateProfileFromLogColorSpaceW(LOGCOLORSPACEW *lcs, BYTE **profile)
{
    if (!profile || !lcs || lcs->lcsFilename[0]) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD gamma[3] = {lcs->lcsGammaRed, lcs->lcsGammaGreen, lcs->lcsGammaBlue};
    return profile_from_lcs(lcs->lcsCSType, &lcs->lcsEndpoints, gamma, profile);
}

BOOL WINAPI CreateProfileFromLogColorSpaceA(LOGCOLORSPACEA *lcs, BYTE **profile)
{
    if (!profile || !lcs || lcs->lcsFilename[0]) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD gamma[3] = {lcs->lcsGammaRed, lcs->lcsGammaGreen, lcs->lcsGammaBlue};
    return profile_from_lcs(lcs->lcsCSType, &lcs->lcsEndpoints, gamma, profile);
}

/* ------------------------------------------------------------------ named color profiles */

typedef struct NAMED_PROFILE_INFO {
    DWORD dwFlags, dwCount, dwCountDevCoordinates;
    char szPrefix[32], szSuffix[32];
} NAMED_PROFILE_INFO;

/* the ncl2 tag: flags, count, device coordinates, prefix and suffix, then entries of a 32 byte root name, the pcs
 * color and the device color. */
static const BYTE *named_colors(Profile *p, DWORD *count, DWORD *coords, DWORD *entry)
{
    uint32_t len;
    const BYTE *t = icc_tag(p->edit.data, p->edit.size, ICC_SIG('n', 'c', 'l', '2'), &len);
    if (!t || len < 84 || icc_be32(t) != ICC_SIG('n', 'c', 'l', '2'))
        return nullptr;
    *count = icc_be32(t + 12);
    *coords = icc_be32(t + 16);
    *entry = 32 + 6 + *coords * 2;
    if (*coords > 15 || *count > (len - 84) / *entry)
        return nullptr;
    return t;
}

BOOL WINAPI GetNamedProfileInfo(HANDLE handle, NAMED_PROFILE_INFO *info)
{
    if (!info) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Profile *p = enter(handle, TRUE, FALSE);
    if (!p)
        return FALSE;
    DWORD count, coords, entry;
    const BYTE *t = named_colors(p, &count, &coords, &entry);
    if (!t) {
        SetLastError(ERROR_INVALID_PROFILE);
        return leave(p, FALSE);
    }
    info->dwFlags = icc_be32(t + 8);
    info->dwCount = count;
    info->dwCountDevCoordinates = coords;
    memcpy(info->szPrefix, t + 20, 32);
    memcpy(info->szSuffix, t + 52, 32);
    info->szPrefix[31] = info->szSuffix[31] = 0;
    return leave(p, TRUE);
}

typedef char COLOR_NAME[32];

/* indices count from 1. */
BOOL WINAPI ConvertColorNameToIndex(HANDLE handle, COLOR_NAME *names, DWORD *indices, DWORD count)
{
    if (!names || !indices) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Profile *p = enter(handle, TRUE, FALSE);
    if (!p)
        return FALSE;
    DWORD total, coords, entry;
    const BYTE *t = named_colors(p, &total, &coords, &entry);
    if (!t) {
        SetLastError(ERROR_INVALID_PROFILE);
        return leave(p, FALSE);
    }
    for (DWORD i = 0; i < count; i++) {
        DWORD found = 0;
        for (DWORD k = 0; k < total && !found; k++)
            if (!strncmp((const char *)t + 84 + k * entry, names[i], 32))
                found = k + 1;
        if (!found) {
            SetLastError(ERROR_INVALID_COLORINDEX);
            return leave(p, FALSE);
        }
        indices[i] = found;
    }
    return leave(p, TRUE);
}

BOOL WINAPI ConvertIndexToColorName(HANDLE handle, DWORD *indices, COLOR_NAME *names, DWORD count)
{
    if (!names || !indices) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Profile *p = enter(handle, TRUE, FALSE);
    if (!p)
        return FALSE;
    DWORD total, coords, entry;
    const BYTE *t = named_colors(p, &total, &coords, &entry);
    if (!t) {
        SetLastError(ERROR_INVALID_PROFILE);
        return leave(p, FALSE);
    }
    for (DWORD i = 0; i < count; i++) {
        if (!indices[i] || indices[i] > total) {
            SetLastError(ERROR_INVALID_COLORINDEX);
            return leave(p, FALSE);
        }
        memcpy(names[i], t + 84 + (indices[i] - 1) * entry, 32);
        names[i][31] = 0;
    }
    return leave(p, TRUE);
}
