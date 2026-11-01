/*
 * the security helpers windows keeps in sechost.dll for rpc servers and clients: security descriptors that name a
 * transient object, capability checks of a caller, the descriptor that lets another token share an object, the
 * credential records of the credential manager and the isolation and trace functions it exports.
 */
#define MIME_BUILD_ADVAPI32 1
#include "advapi32_private.h"
#include <mime/ntapi.h>

#define STATUS_INVALID_PARAMETER_ ((NTSTATUS)0xC000000DL)
#define STATUS_INVALID_TRANSIENT_DATA ((NTSTATUS)0xC000090BL)
#define STATUS_RPC_SERVER_UNAVAILABLE_ ((NTSTATUS)0xC0020017L)
#define E_INVALIDARG_ ((LONG)0x80070057L)
#define E_ACCESSDENIED_ ((LONG)0x80070005L)
#define HRESULT_NOT_FOUND ((LONG)0x80070490L)
#define HRESULT_FROM_ERROR(e) ((LONG)(((e) & 0xFFFF) | 0x80070000))
#define TOKEN_CAPABILITIES_CLASS 30
#define TOKEN_IS_APP_CONTAINER_CLASS 29
#define TOKEN_APP_CONTAINER_SID_CLASS 31
#define TOKEN_INTEGRITY_CLASS 25

ADVAPI BOOL WINAPI AddAccessAllowedAceEx(PACL acl, DWORD revision, DWORD flags, DWORD mask, PSID sid);
ADVAPI BOOL WINAPI AddMandatoryAce(PACL acl, DWORD revision, DWORD flags, DWORD policy, PSID label);
ADVAPI BOOL WINAPI InitializeAcl(PACL acl, DWORD length, DWORD revision);
ADVAPI BOOL WINAPI EqualSid(PSID a, PSID b);
ADVAPI BOOL WINAPI OpenThreadToken(HANDLE thread, DWORD access, BOOL self, PHANDLE token);
ADVAPI BOOL WINAPI InitializeSid(PSID sid, PSID_IDENTIFIER_AUTHORITY authority, BYTE count);
ADVAPI PDWORD WINAPI GetSidSubAuthority(PSID sid, DWORD index);

typedef struct TOKEN_APPCONTAINER_INFORMATION {
    PSID TokenAppContainer;
} TOKEN_APPCONTAINER_INFORMATION;

typedef struct TOKEN_MANDATORY_LABEL {
    SID_AND_ATTRIBUTES Label;
} TOKEN_MANDATORY_LABEL;

#define SECURITY_DESCRIPTOR_MIN_LENGTH_ 64
ADVAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                        PSECURITY_DESCRIPTOR *result, PULONG size);
ADVAPI DWORD WINAPI GetSecurityDescriptorLength(PSECURITY_DESCRIPTOR sd);
NTSYSAPI NTSTATUS NTAPI RtlDeriveCapabilitySidsFromName(PCUNICODE_STRING name, PSID group, PSID capability);

/* ------------------------------------------------------------------ security descriptors of transient objects */

/* the kinds of object a name is looked up for, in the order of the enumeration windows uses. */
static PCWSTR transient_prefix(DWORD type)
{
    switch (type) {
    case 0: return L"%5C%5C.%5CMutex%5C";
    case 1: return L"%5C%5C.%5CEvent%5C";
    case 2: return L"%5C%5C.%5CSemaphore%5C";
    case 3: return L"%5C%5C.%5CWaitableTimer%5C";
    case 4: return L"%5C%5C.%5CJobObject%5C";
    case 5: return L"%5C%5C.%5CFileMapping%5C";
    case 6: return L"%5C%5C.%5CNamedPipe%5C";
    case 8: return L"%5C%5C.%5CAlpcPort%5C";
    case 9: return L"%5C%5C.%5CRpc%5C";
    case 10: return L"%5C%5C.%5CWnf%5C";
    case 11: return L"%5C%5C.%5CTemplate%5C";
    case 12: return L"%5C%5C.%5CPrivate%5C";
    }
    return nullptr;
}

static NTSTATUS transient_key_path(DWORD type, PCWSTR name, PWSTR *path)
{
    static const WCHAR root[] = L"\\REGISTRY\\MACHINE\\Software\\Microsoft\\SecurityManager\\TransientObjects\\";
    PCWSTR prefix = transient_prefix(type);
    if (!prefix || !name)
        return STATUS_INVALID_PARAMETER_;
    SIZE_T chars = wcslen(root) + wcslen(prefix);
    for (PCWSTR p = name; *p; p++)
        chars += *p == L'\\' ? 3 : 1;
    PWSTR out = AdvAlloc((chars + 1) * sizeof(WCHAR));
    if (!out)
        return STATUS_NO_MEMORY;
    wcscpy(out, root);
    wcscat(out, prefix);
    SIZE_T at = wcslen(out);
    for (PCWSTR p = name; *p; p++) {
        if (*p == L'\\') {
            out[at++] = L'%';
            out[at++] = L'5';
            out[at++] = L'C';
        } else {
            out[at++] = *p;
        }
    }
    out[at] = 0;
    *path = out;
    return STATUS_SUCCESS;
}

/* the text of a descriptor kept for each user, with %s standing for the sid of the caller. */
static NTSTATUS per_user_descriptor(PCWSTR format, PSECURITY_DESCRIPTOR *result)
{
    TOKEN_USER *user = AdvQueryTokenUser(ADV_CURRENT_THREAD_EFFECTIVE_TOKEN);
    LPWSTR sid_text = nullptr;
    if (!user || !ConvertSidToStringSidW(user->User.Sid, &sid_text)) {
        AdvFree(user);
        return STATUS_UNSUCCESSFUL;
    }
    SIZE_T sid_length = wcslen(sid_text), chars = wcslen(format) + 1;
    for (PCWSTR p = format; (p = wcsstr(p, L"%s")); p += 2)
        chars += sid_length;
    PWSTR text = AdvAlloc(chars * sizeof(WCHAR));
    NTSTATUS status = STATUS_NO_MEMORY;
    if (text) {
        SIZE_T at = 0;
        for (PCWSTR p = format; *p;) {
            if (p[0] == L'%' && p[1] == L's') {
                memcpy(text + at, sid_text, sid_length * sizeof(WCHAR));
                at += sid_length;
                p += 2;
            } else {
                text[at++] = *p++;
            }
        }
        text[at] = 0;
        PSECURITY_DESCRIPTOR sd;
        status = STATUS_UNSUCCESSFUL;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(text, 1, &sd, nullptr)) {
            DWORD length = GetSecurityDescriptorLength(sd);
            *result = AdvAlloc(length);
            if (*result) {
                memcpy(*result, sd, length);
                status = STATUS_SUCCESS;
            } else {
                status = STATUS_NO_MEMORY;
            }
            LocalFree(sd);
        }
        AdvFree(text);
    }
    LocalFree(sid_text);
    AdvFree(user);
    return status;
}

/*
 * the descriptor registered for a transient object, kept in a value of the key named after it. a binary value is the
 * descriptor, a string is the text of one for each user. the capability form of a string needs the capability
 * database of the package manager and is refused.
 */
ADVAPI NTSTATUS WINAPI QueryTransientObjectSecurityDescriptor(DWORD type, PCWSTR name, PSECURITY_DESCRIPTOR *result)
{
    if (!name || !result)
        return STATUS_INVALID_PARAMETER_;
    PWSTR path;
    NTSTATUS status = transient_key_path(type, name, &path);
    if (!NT_SUCCESS(status))
        return status;
    UNICODE_STRING key_name;
    OBJECT_ATTRIBUTES attributes;
    RtlInitUnicodeString(&key_name, path);
    InitializeObjectAttributes(&attributes, &key_name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE key = nullptr;
    BYTE *value = nullptr;
    status = NtOpenKey(&key, KEY_READ, &attributes);
    if (NT_SUCCESS(status)) {
        UNICODE_STRING value_name;
        ULONG length = 0;
        BYTE probe[16];
        RtlInitUnicodeString(&value_name, L"SecurityDescriptor");
        status = NtQueryValueKey(key, &value_name, 2, probe, sizeof(probe), &length);
        if (NT_SUCCESS(status)) {
            status = STATUS_INVALID_TRANSIENT_DATA;
        } else if (status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL) {
            value = AdvAlloc(length);
            status = value ? NtQueryValueKey(key, &value_name, 2, value, length, &length) : STATUS_NO_MEMORY;
        }
    }
    if (NT_SUCCESS(status) && value) {
        DWORD type_of_value = *(DWORD *)(value + 4), data_length = *(DWORD *)(value + 8);
        const BYTE *data = value + 12;
        if (type_of_value == REG_BINARY) {
            *result = AdvAlloc(data_length);
            if (*result)
                memcpy(*result, data, data_length);
            else
                status = STATUS_NO_MEMORY;
        } else if (type_of_value == REG_SZ && data_length) {
            status = *(const WCHAR *)data == L'{' ? STATUS_NOT_SUPPORTED : per_user_descriptor((PCWSTR)data, result);
        } else {
            status = STATUS_INVALID_TRANSIENT_DATA;
        }
    }
    AdvFree(value);
    if (key)
        NtClose(key);
    AdvFree(path);
    return status;
}

ADVAPI BOOLEAN WINAPI FreeTransientObjectSecurityDescriptor(PVOID descriptor)
{
    if (descriptor)
        return RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, descriptor);
    return FALSE;
}

/* ------------------------------------------------------------------ rpc security from transient descriptors */

typedef LONG(WINAPI *RPC_FUNCTION)();

static RPC_FUNCTION rpc_function(PCSTR name)
{
    HMODULE rpcrt4 = LoadLibraryW(L"rpcrt4.dll");
    return rpcrt4 ? (RPC_FUNCTION)(void *)GetProcAddress(rpcrt4, name) : nullptr;
}

/* an rpc status is a win32 error, shown as a failure result; a status of the descriptor lookup is flagged. */
static LONG hresult_of_rpc(LONG status)
{
    return status > 0 ? HRESULT_FROM_ERROR(status) : status;
}

static LONG name_ends_with(PCWSTR name, PCWSTR ending)
{
    SIZE_T length = wcslen(name), tail = wcslen(ending);
    if (tail > length)
        return HRESULT_NOT_FOUND;
    return _wcsicmp(name + length - tail, ending) ? HRESULT_NOT_FOUND : 0;
}

typedef struct RPC_SECURITY_QOS_V5_ {
    DWORD Version;
    DWORD Capabilities;
    DWORD IdentityTracking;
    DWORD ImpersonationType;
    DWORD AdditionalSecurityInfoType;
    PVOID AdditionalSecurityInfo;
    PVOID Sid;
    BOOL EffectiveOnly;
    PVOID ServerSecurityDescriptor;
} RPC_SECURITY_QOS_V5_;

#define RPC_C_AUTHN_LEVEL_PKT_PRIVACY 6
#define RPC_C_AUTHN_WINNT_ 10
#define RPC_C_IMP_LEVEL_IMPERSONATE_ 2

/* secures a binding with the descriptor registered for the named object, which must end in "\Server". */
ADVAPI LONG WINAPI LocalRpcBindingSetAuthInfoEx(PVOID binding, PCWSTR name)
{
    if (!binding || !name)
        return E_INVALIDARG_;
    LONG result = name_ends_with(name, L"\\Server");
    if (result < 0)
        return result;
    PSECURITY_DESCRIPTOR sd = nullptr;
    NTSTATUS status = QueryTransientObjectSecurityDescriptor(9, name, &sd);
    if (!NT_SUCCESS(status))
        return status | 0x10000000;
    RPC_SECURITY_QOS_V5_ qos = {5, 0, 0, RPC_C_IMP_LEVEL_IMPERSONATE_};
    qos.ServerSecurityDescriptor = sd;
    RPC_FUNCTION set = rpc_function("RpcBindingSetAuthInfoExW");
    result = set ? hresult_of_rpc(((LONG(WINAPI *)(PVOID, PWSTR, DWORD, DWORD, PVOID, DWORD, PVOID))set)(
                       binding, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_AUTHN_WINNT_, nullptr, 0, &qos))
                 : HRESULT_FROM_ERROR(ERROR_PROC_NOT_FOUND);
    FreeTransientObjectSecurityDescriptor(sd);
    return result;
}

/* makes a binding that is secured like the named object. rpcrt4 here cannot create a binding from a template. */
ADVAPI LONG WINAPI LocalRpcBindingCreateWithSecurity(PVOID template, PVOID options, PVOID interface_spec, PCWSTR name,
                                                     PVOID *binding)
{
    (void)template;
    (void)options;
    (void)interface_spec;
    if (!binding || !name)
        return E_INVALIDARG_;
    LONG result = name_ends_with(name, L"\\Server");
    if (result < 0)
        return result;
    PSECURITY_DESCRIPTOR sd = nullptr;
    NTSTATUS status = QueryTransientObjectSecurityDescriptor(9, name, &sd);
    if (!NT_SUCCESS(status))
        return status | 0x10000000;
    FreeTransientObjectSecurityDescriptor(sd);
    return rpc_function("RpcBindingCreateW") ? HRESULT_FROM_ERROR(ERROR_CALL_NOT_IMPLEMENTED)
                                             : HRESULT_FROM_ERROR(ERROR_PROC_NOT_FOUND);
}

/* registers an interface that local callers alone reach, with the descriptor named "...\Interface". */
ADVAPI LONG WINAPI SetLocalRpcServerInterfaceSecurity(PVOID interface_spec, PCWSTR name, PVOID callback)
{
    if (!interface_spec || !name)
        return E_INVALIDARG_;
    LONG result = name_ends_with(name, L"\\Interface");
    if (result < 0)
        return result;
    PSECURITY_DESCRIPTOR sd = nullptr;
    NTSTATUS status = QueryTransientObjectSecurityDescriptor(9, name, &sd);
    if (!NT_SUCCESS(status))
        return status | 0x10000000;
    RPC_FUNCTION reg = rpc_function("RpcServerRegisterIf3");
    result = reg ? hresult_of_rpc(((LONG(WINAPI *)(PVOID, PVOID, PVOID, DWORD, DWORD, DWORD, PVOID, PVOID))reg)(
                       interface_spec, nullptr, nullptr, 41, 1234, 0, callback, sd))
                 : HRESULT_FROM_ERROR(ERROR_PROC_NOT_FOUND);
    FreeTransientObjectSecurityDescriptor(sd);
    return result;
}

ADVAPI LONG WINAPI SetLocalRpcServerProtseqSecurity(void)
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    NTSTATUS status = QueryTransientObjectSecurityDescriptor(8, L"DefaultRpcAccess", &sd);
    if (!NT_SUCCESS(status))
        return status | 0x10000000;
    RPC_FUNCTION use = rpc_function("RpcServerUseProtseqW");
    LONG result = use ? hresult_of_rpc(((LONG(WINAPI *)(PWSTR, DWORD, PVOID))use)(L"ncalrpc", 10, sd))
                      : HRESULT_FROM_ERROR(ERROR_PROC_NOT_FOUND);
    FreeTransientObjectSecurityDescriptor(sd);
    return result;
}

/* ------------------------------------------------------------------ capability checks */

static BOOL token_has_sid(const TOKEN_GROUPS *groups, PSID sid)
{
    for (DWORD i = 0; groups && i < groups->GroupCount; i++)
        if (EqualSid(groups->Groups[i].Sid, sid))
            return TRUE;
    return FALSE;
}

static BOOL token_member_of(HANDLE token, PSID sid)
{
    TOKEN_USER *user = AdvQueryTokenUser(token);
    BOOL member = user && EqualSid(user->User.Sid, sid);
    AdvFree(user);
    if (member)
        return TRUE;
    TOKEN_GROUPS *groups = AdvQueryTokenGroups(token);
    for (DWORD i = 0; groups && i < groups->GroupCount && !member; i++)
        member = EqualSid(groups->Groups[i].Sid, sid) && (groups->Groups[i].Attributes & SE_GROUP_ENABLED);
    AdvFree(groups);
    return member;
}

/*
 * whether the caller token holds a capability. an app container holds it when the capability sid is in its list, any
 * other token when it is a member of the group of the capability, or runs as the local system.
 */
ADVAPI NTSTATUS WINAPI CapabilityCheck(HANDLE token, PCWSTR capability, BOOLEAN *granted)
{
    if (!capability || !granted)
        return STATUS_INVALID_PARAMETER_;
    SIZE_T length = wcslen(capability);
    if (length >= 0x7FFF)
        return STATUS_INVALID_PARAMETER_;
    UNICODE_STRING name = {(USHORT)(length * sizeof(WCHAR)), (USHORT)((length + 1) * sizeof(WCHAR)), (PWSTR)capability};
    BYTE group[SECURITY_MAX_SID_SIZE], sid[SECURITY_MAX_SID_SIZE];
    NTSTATUS status = RtlDeriveCapabilitySidsFromName(&name, group, sid);
    if (!NT_SUCCESS(status))
        return status;
    if (!token)
        token = ADV_CURRENT_THREAD_EFFECTIVE_TOKEN;
    DWORD *is_container = AdvQueryToken(token, TOKEN_IS_APP_CONTAINER_CLASS);
    BOOL container = is_container && *is_container;
    AdvFree(is_container);
    BOOL result;
    if (container) {
        TOKEN_GROUPS *capabilities = AdvQueryToken(token, TOKEN_CAPABILITIES_CLASS);
        result = token_has_sid(capabilities, (PSID)sid);
        AdvFree(capabilities);
    } else {
        BYTE system[SECURITY_MAX_SID_SIZE];
        AdvMakeSid(system, 5, 1, 18);
        result = token_member_of(token, (PSID)group) || token_member_of(token, (PSID)system);
    }
    *granted = result != FALSE;
    return STATUS_SUCCESS;
}

/* a single session sku relaxes no rule of the check on a machine that is not one. */
ADVAPI NTSTATUS WINAPI CapabilityCheckForSingleSessionSku(HANDLE token, PCWSTR capability, BOOLEAN *granted)
{
    return CapabilityCheck(token, capability, granted);
}

/* checks the capability of the client of the rpc call in progress. the result is an hresult. */
ADVAPI LONG WINAPI RpcClientCapabilityCheck(PCWSTR capability)
{
    if (!capability)
        return E_INVALIDARG_;
    RPC_FUNCTION impersonate = rpc_function("RpcImpersonateClient");
    RPC_FUNCTION revert = rpc_function("RpcRevertToSelfEx");
    if (!impersonate || !revert)
        return HRESULT_FROM_ERROR(ERROR_PROC_NOT_FOUND);
    LONG status = ((LONG(WINAPI *)(PVOID))impersonate)(nullptr);
    if (status)
        return hresult_of_rpc(status);
    HANDLE token = nullptr;
    LONG result = 0;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) {
        DWORD error = GetLastError();
        result = error ? HRESULT_FROM_ERROR(error) : 0;
    }
    status = ((LONG(WINAPI *)(PVOID))revert)(nullptr);
    if (status)
        result = hresult_of_rpc(status);
    if (result >= 0) {
        BOOLEAN granted = FALSE;
        NTSTATUS check = CapabilityCheck(token, capability, &granted);
        if (!NT_SUCCESS(check))
            result = HRESULT_FROM_ERROR(RtlNtStatusToDosError(check));
        else if (!granted)
            result = E_ACCESSDENIED_;
    }
    if (token)
        CloseHandle(token);
    return result;
}

/* ------------------------------------------------------------------ descriptors for sharing an object between tokens */

static NTSTATUS token_users(HANDLE token, PSID *user, PSID *package)
{
    *user = nullptr;
    *package = nullptr;
    TOKEN_USER *info = AdvQueryTokenUser(token);
    if (!info)
        return STATUS_UNSUCCESSFUL;
    DWORD length = AdvSidLength(info->User.Sid);
    *user = AdvAlloc(length);
    if (*user)
        memcpy(*user, info->User.Sid, length);
    AdvFree(info);
    if (!*user)
        return STATUS_NO_MEMORY;
    DWORD *container = AdvQueryToken(token, TOKEN_IS_APP_CONTAINER_CLASS);
    BOOL is_container = container && *container;
    AdvFree(container);
    if (is_container) {
        TOKEN_APPCONTAINER_INFORMATION *package_info = AdvQueryToken(token, TOKEN_APP_CONTAINER_SID_CLASS);
        if (package_info && package_info->TokenAppContainer) {
            length = AdvSidLength(package_info->TokenAppContainer);
            *package = AdvAlloc(length);
            if (*package)
                memcpy(*package, package_info->TokenAppContainer, length);
        }
        AdvFree(package_info);
    }
    return STATUS_SUCCESS;
}

static BOOL integrity_level(HANDLE token, DWORD *level)
{
    TOKEN_MANDATORY_LABEL *label = AdvQueryToken(token, TOKEN_INTEGRITY_CLASS);
    if (!label)
        return FALSE;
    const SID *sid = label->Label.Sid;
    *level = sid->SubAuthority[sid->SubAuthorityCount - 1];
    AdvFree(label);
    return TRUE;
}

static NTSTATUS sharing_descriptor(PSID user, PSID package, PSID owner, PSID owner_package, ACCESS_MASK mask, PACL sacl,
                                   PSECURITY_DESCRIPTOR *result)
{
    BYTE system[SECURITY_MAX_SID_SIZE];
    AdvMakeSid(system, 5, 1, 18);
    DWORD size = 8 + 24 + AdvSidLength(owner) + AdvSidLength((SID *)system) + AdvSidLength(user) +
                 (owner_package ? AdvSidLength(owner_package) : 0) + (package ? AdvSidLength(package) : 0) + 4 * 16;
    PACL dacl = AdvAllocZero(size);
    if (!dacl)
        return STATUS_NO_MEMORY;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    if (InitializeAcl(dacl, size, ACL_REVISION) && AddAccessAllowedAceEx(dacl, ACL_REVISION, 0, GENERIC_ALL, owner) &&
        (!owner_package || AddAccessAllowedAceEx(dacl, ACL_REVISION, 0, GENERIC_ALL, owner_package)) &&
        (EqualSid(owner, system) || AddAccessAllowedAceEx(dacl, ACL_REVISION, 0, GENERIC_ALL, system)) &&
        (EqualSid(user, system) || EqualSid(user, owner) || AddAccessAllowedAceEx(dacl, ACL_REVISION, 0, mask, user)) &&
        (!package || AddAccessAllowedAceEx(dacl, ACL_REVISION, 0, mask, package))) {
        BYTE absolute[SECURITY_DESCRIPTOR_MIN_LENGTH_];
        if (InitializeSecurityDescriptor(absolute, SECURITY_DESCRIPTOR_REVISION) &&
            SetSecurityDescriptorDacl(absolute, TRUE, dacl, FALSE) &&
            (!sacl || SetSecurityDescriptorSacl(absolute, TRUE, sacl, FALSE)) &&
            SetSecurityDescriptorOwner(absolute, owner, FALSE)) {
            DWORD length = 0;
            MakeSelfRelativeSD(absolute, nullptr, &length);
            PSECURITY_DESCRIPTOR relative = length ? AdvAlloc(length) : nullptr;
            if (relative && MakeSelfRelativeSD(absolute, relative, &length)) {
                *result = relative;
                status = STATUS_SUCCESS;
            } else {
                AdvFree(relative);
                status = relative ? STATUS_UNSUCCESSFUL : STATUS_NO_MEMORY;
            }
        }
    }
    AdvFree(dacl);
    return status;
}

/*
 * the descriptor that lets the owner token and the target token use an object: the owner and the system have all
 * access, the target the mask. nothing is returned when both tokens are the same user and no label is asked for, and
 * with a label the object is labeled with the lower of the two integrity levels.
 */
ADVAPI NTSTATUS WINAPI BuildSecurityDescriptorForSharingAccessEx(HANDLE target, HANDLE owner_token, ACCESS_MASK mask,
                                                                 DWORD label_policy, PSECURITY_DESCRIPTOR *result)
{
    PSID user = nullptr, package = nullptr, owner = nullptr, owner_package = nullptr;
    PACL sacl = nullptr;
    PSID label = nullptr;
    HANDLE opened = nullptr;
    NTSTATUS status = STATUS_INVALID_PARAMETER_;
    if (!target || !mask || !result)
        goto done;
    *result = nullptr;
    status = token_users(target, &user, &package);
    if (!NT_SUCCESS(status))
        goto done;
    status = token_users(owner_token ? owner_token : ADV_CURRENT_PROCESS_TOKEN, &owner, &owner_package);
    if (!NT_SUCCESS(status))
        goto done;
    BOOL same = EqualSid(user, owner);
    if (same)
        same = package && owner_package && EqualSid(package, owner_package);
    BOOL equal_levels = TRUE;
    if (label_policy) {
        DWORD owner_level, target_level;
        if (!integrity_level(owner_token ? owner_token : ADV_CURRENT_PROCESS_TOKEN, &owner_level) ||
            !integrity_level(target, &target_level)) {
            status = STATUS_UNSUCCESSFUL;
            goto done;
        }
        equal_levels = owner_level == target_level;
        BYTE authority[6] = {0, 0, 0, 0, 0, 16};
        label = AdvAlloc(12);
        if (!label || !InitializeSid(label, (PSID_IDENTIFIER_AUTHORITY)authority, 1)) {
            status = STATUS_NO_MEMORY;
            goto done;
        }
        *GetSidSubAuthority(label, 0) = owner_level < target_level ? owner_level : target_level;
        DWORD sacl_size = 16 + AdvSidLength(label);
        sacl = AdvAllocZero(sacl_size);
        if (!sacl || !InitializeAcl(sacl, sacl_size, ACL_REVISION) ||
            !AddMandatoryAce(sacl, ACL_REVISION, 0, label_policy, label)) {
            status = sacl ? STATUS_UNSUCCESSFUL : STATUS_NO_MEMORY;
            goto done;
        }
    }
    if (same && equal_levels)
        status = STATUS_SUCCESS;
    else
        status = sharing_descriptor(user, package, owner, owner_package, mask, sacl, result);
done:
    AdvFree(user);
    AdvFree(package);
    AdvFree(owner);
    AdvFree(owner_package);
    AdvFree(label);
    AdvFree(sacl);
    if (opened)
        CloseHandle(opened);
    if (!NT_SUCCESS(status))
        SetLastError(RtlNtStatusToDosError(status));
    return status;
}

ADVAPI NTSTATUS WINAPI BuildSecurityDescriptorForSharingAccess(HANDLE target, HANDLE owner_token, ACCESS_MASK mask,
                                                               PSECURITY_DESCRIPTOR *result)
{
    return BuildSecurityDescriptorForSharingAccessEx(target, owner_token, mask, 0, result);
}
