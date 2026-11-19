#include "rpcss.h"

#define OBJREF_SIGNATURE 0x574F454D /* "MEOW" */
#define OBJREF_CUSTOM 4
#define SERIALIZATION_HEADERS 16

static const GUID CLSID_InstantiationInfo = {0x000001ab, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};

typedef struct READER {
    const BYTE *Data;
    ULONG Size, At;
} READER;

static BOOL take(READER *r, void *out, ULONG n)
{
    if (r->At > r->Size || n > r->Size - r->At)
        return FALSE;
    if (out)
        memcpy(out, r->Data + r->At, n);
    r->At += n;
    return TRUE;
}

static BOOL u32(READER *r, ULONG *out)
{
    r->At = (r->At + 3) & ~3u;
    return take(r, out, 4);
}

/* where the activation properties blob starts in an objref, and its size. */
static BOOL blob_of(const BYTE *objref, ULONG size, READER *blob)
{
    READER r = {objref, size, 0};
    ULONG signature, flags, extension, length;
    GUID iid, clsid;
    if (!take(&r, &signature, 4) || !take(&r, &flags, 4) || !take(&r, &iid, 16) || !take(&r, &clsid, 16) ||
        !take(&r, &extension, 4) || !take(&r, &length, 4))
        return FALSE;
    if (signature != OBJREF_SIGNATURE || flags != OBJREF_CUSTOM || extension)
        return FALSE;
    blob->Data = objref + r.At;
    blob->Size = size - r.At;
    blob->At = 0;
    return TRUE;
}

/*
 * the activation properties combase sends the activator are an OBJREF_CUSTOM of CLSID_ActivationPropertiesIn whose data
 * is an ActivationPropertiesBlob of [MS-DCOM] 2.2.22: two sizes, then the CustomHeader and each property, all NDR type
 * serialized with the eight byte common header and the eight byte private header of [MS-RPCE] 2.2.6.
 * the class and the context of an activation come from the InstantiationInfoData property: the classId, classCtx and
 * actvflags that open it, after the serialization headers.
 */
BOOL RsActivationRequest(const BYTE *objref, ULONG size, GUID *clsid, ULONG *clsctx, ULONG *flags)
{
    READER blob;
    if (!blob_of(objref, size, &blob))
        return FALSE;
    ULONG total, reserved;
    if (!take(&blob, &total, 4) || !take(&blob, &reserved, 4))
        return FALSE;
    ULONG header_start = blob.At;
    /* the CustomHeader: totalSize, headerSize, dwReserved, destCtx, cIfs, classInfoClsid, then the referents of
     * pclsid, pSizes and pdwReserved, then the deferred arrays. */
    READER h = {blob.Data + header_start + SERIALIZATION_HEADERS, blob.Size - header_start - SERIALIZATION_HEADERS, 0};
    if (blob.Size < header_start + SERIALIZATION_HEADERS)
        return FALSE;
    ULONG total_size, header_size, dw_reserved, dest, count, pclsid, psizes, pres;
    GUID info_clsid;
    if (!u32(&h, &total_size) || !u32(&h, &header_size) || !u32(&h, &dw_reserved) || !u32(&h, &dest) ||
        !u32(&h, &count) || !take(&h, &info_clsid, 16) || !u32(&h, &pclsid) || !u32(&h, &psizes) ||
        !u32(&h, &pres))
        return FALSE;
    if (!count || count > 64 || !pclsid || !psizes)
        return FALSE;
    ULONG max;
    GUID clsids[64];
    ULONG sizes[64];
    if (!u32(&h, &max) || max != count || !take(&h, clsids, count * 16))
        return FALSE;
    if (!u32(&h, &max) || max != count || !take(&h, sizes, count * 4))
        return FALSE;
    ULONG at = header_start + header_size;
    for (ULONG i = 0; i < count; i++) {
        if (at > blob.Size || sizes[i] > blob.Size - at)
            return FALSE;
        if (!memcmp(&clsids[i], &CLSID_InstantiationInfo, sizeof(GUID))) {
            READER p = {blob.Data + at + SERIALIZATION_HEADERS, sizes[i] - SERIALIZATION_HEADERS, 0};
            if (sizes[i] < SERIALIZATION_HEADERS)
                return FALSE;
            return take(&p, clsid, 16) && u32(&p, clsctx) && u32(&p, flags);
        }
        at += sizes[i];
    }
    return FALSE;
}

/* whether the classes of the machine or of the caller register a server outside the caller's process for a class. */
BOOL RsClassHasLocalServer(const GUID *clsid)
{
    WCHAR key[96];
    _snwprintf(key, RTL_NUMBER_OF(key), L"CLSID\\{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
               clsid->Data1, clsid->Data2, clsid->Data3, clsid->Data4[0], clsid->Data4[1], clsid->Data4[2],
               clsid->Data4[3], clsid->Data4[4], clsid->Data4[5], clsid->Data4[6], clsid->Data4[7]);
    static const WCHAR *const kinds[] = {L"LocalServer32", L"LocalServer"};
    BOOL found = FALSE;
    HKEY classes;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &classes))
        return FALSE;
    for (ULONG i = 0; i < RTL_NUMBER_OF(kinds) && !found; i++) {
        HKEY k;
        if (!RegOpenKeyExW(classes, kinds[i], 0, KEY_READ, &k)) {
            found = TRUE;
            RegCloseKey(k);
        }
    }
    /* a class of a service or a surrogate names its server through its AppID. */
    WCHAR appid[64];
    DWORD bytes = sizeof(appid);
    if (!found && !RegGetValueW(classes, nullptr, L"AppID", RRF_RT_REG_SZ, nullptr, appid, &bytes)) {
        WCHAR path[96];
        _snwprintf(path, RTL_NUMBER_OF(path), L"AppID\\%s", appid);
        HKEY a;
        if (!RegOpenKeyExW(HKEY_CLASSES_ROOT, path, 0, KEY_READ, &a)) {
            found = !RegGetValueW(a, nullptr, L"LocalService", RRF_RT_REG_SZ, nullptr, nullptr, nullptr) ||
                    !RegGetValueW(a, nullptr, L"DllSurrogate", RRF_RT_REG_SZ, nullptr, nullptr, nullptr);
            RegCloseKey(a);
        }
    }
    RegCloseKey(classes);
    return found;
}
