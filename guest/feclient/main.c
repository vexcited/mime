/*
 * feclient.dll, the client of the encrypting file system and of enterprise data protection. Mime's volumes have no
 * encryption and no protection policy: a file is never encrypted, decrypting it does nothing and everything else is
 * not supported. Installers load the library before they start, the WiX bootstrapper among them, and stop without it.
 */
#include <mime/nt.h>
#include <mime/win32/winbase.h>

#define FECLIENT DECLSPEC_EXPORT

/* FileEncryptionStatus results. */
#define FILE_SYSTEM_NOT_SUPPORT 6

#define EFS_UNSUPPORTED(name, parameters)                                                                              \
    FECLIENT DWORD WINAPI name parameters                                                                              \
    {                                                                                                                  \
        return ERROR_NOT_SUPPORTED;                                                                                    \
    }

#define EDP_UNSUPPORTED(name, parameters)                                                                              \
    FECLIENT HRESULT WINAPI name parameters                                                                            \
    {                                                                                                                  \
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);                                                                \
    }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

/* The table of the client that advapi32 asks for, there is none. */
FECLIENT DWORD WINAPI FeClientInitialize(DWORD version, void **table)
{
    if (table)
        *table = nullptr;
    return ERROR_NOT_SUPPORTED;
}

FECLIENT DWORD WINAPI FeClClearCaches(void)
{
    return ERROR_SUCCESS;
}

/* Nothing was handed out, so there is nothing to free. */
FECLIENT void WINAPI EdpFree(void *memory)
{
}

FECLIENT DWORD WINAPI EfsClientDecryptFile(LPCWSTR name, DWORD reserved)
{
    if (!name || reserved)
        return ERROR_INVALID_PARAMETER;
    return GetFileAttributesW(name) == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
}

FECLIENT DWORD WINAPI EfsClientFileEncryptionStatus(LPCWSTR name, LPDWORD status)
{
    if (!name || !status)
        return ERROR_INVALID_PARAMETER;
    if (GetFileAttributesW(name) == INVALID_FILE_ATTRIBUTES)
        return GetLastError();
    *status = FILE_SYSTEM_NOT_SUPPORT;
    return ERROR_SUCCESS;
}

EDP_UNSUPPORTED(DpQueryUserProtectorDescriptor, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3))
EDP_UNSUPPORTED(DpQueryUserProtectorDescriptorInfo, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3))
EDP_UNSUPPORTED(EdpAllowFileAccessForProcess, (ULONG_PTR a0, ULONG_PTR a1))
EDP_UNSUPPORTED(EdpContainerizeFile, (ULONG_PTR a0, ULONG_PTR a1))
EDP_UNSUPPORTED(EdpCredentialCreate, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3))
EDP_UNSUPPORTED(EdpCredentialDelete, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4))
EDP_UNSUPPORTED(EdpCredentialExists, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3))
EDP_UNSUPPORTED(EdpCredentialQuery, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3))
EDP_UNSUPPORTED(EdpDecontainerizeFile, (ULONG_PTR a0, ULONG_PTR a1))
EDP_UNSUPPORTED(EdpDplPolicyEnabledForUser, (ULONG_PTR a0, ULONG_PTR a1))
EDP_UNSUPPORTED(EdpDplStartCredServiceIfDplEnabledForUser, (ULONG_PTR a0))
EDP_UNSUPPORTED(EdpDplUpgradePinInfo, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3))
EDP_UNSUPPORTED(EdpDplUpgradeVerifyUser, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3))
EDP_UNSUPPORTED(EdpDplUserCredentialsSet, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4))
EDP_UNSUPPORTED(EdpDplUserUnlockComplete, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2))
EDP_UNSUPPORTED(EdpDplUserUnlockStart, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4))
EDP_UNSUPPORTED(EdpGetContainerIdentity, (ULONG_PTR a0, ULONG_PTR a1))
EDP_UNSUPPORTED(EdpGetCredServiceState, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3))
EDP_UNSUPPORTED(EdpIsConsumerDataProtectionEnforced, (ULONG_PTR a0, ULONG_PTR a1))
EDP_UNSUPPORTED(EdpIsConsumerDataProtectionSupported, (ULONG_PTR a0))
EDP_UNSUPPORTED(EdpPurgeAppLearningEvents, (ULONG_PTR a0, ULONG_PTR a1))
EDP_UNSUPPORTED(EdpQueryCredServiceInfo, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4))
EDP_UNSUPPORTED(EdpQueryDplEnforcedPolicyOwnerIds, (ULONG_PTR a0, ULONG_PTR a1))
EDP_UNSUPPORTED(EdpQueryRevokedPolicyOwnerIds, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2))
EDP_UNSUPPORTED(EdpRmsClearKeys, (ULONG_PTR a0, ULONG_PTR a1))
EDP_UNSUPPORTED(EdpSetCredServiceInfo, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4))
EDP_UNSUPPORTED(EdpUnprotectFile, (ULONG_PTR a0, ULONG_PTR a1))
EDP_UNSUPPORTED(EdpWriteLogSiteLearningEvents, (ULONG_PTR a0, ULONG_PTR a1))
EFS_UNSUPPORTED(EfsClientAddUsers, (ULONG_PTR a0, ULONG_PTR a1))
EFS_UNSUPPORTED(EfsClientCloseFileRaw, (ULONG_PTR a0))
EFS_UNSUPPORTED(EfsClientCopyFileRaw, (ULONG_PTR a0, ULONG_PTR a1))
EFS_UNSUPPORTED(EfsClientDuplicateEncryptionInfo, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4))
EFS_UNSUPPORTED(EfsClientEncryptFile, (ULONG_PTR a0))
EFS_UNSUPPORTED(EfsClientEncryptFileEx, (ULONG_PTR a0, ULONG_PTR a1))
EFS_UNSUPPORTED(EfsClientFreeHashList, (ULONG_PTR a0))
EFS_UNSUPPORTED(EfsClientFreeKeyInfo, (ULONG_PTR a0))
EFS_UNSUPPORTED(EfsClientFreeProtectorList, (ULONG_PTR a0))
EFS_UNSUPPORTED(EfsClientGetEncryptedFileVersion, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2))
EFS_UNSUPPORTED(EfsClientGetKeyInfo, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2))
EFS_UNSUPPORTED(EfsClientOpenFileRaw, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2))
EFS_UNSUPPORTED(EfsClientQueryProtectors, (ULONG_PTR a0, ULONG_PTR a1))
EFS_UNSUPPORTED(EfsClientQueryRecoveryAgents, (ULONG_PTR a0, ULONG_PTR a1))
EFS_UNSUPPORTED(EfsClientQueryUsers, (ULONG_PTR a0, ULONG_PTR a1))
EFS_UNSUPPORTED(EfsClientReadFileRaw, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2))
EFS_UNSUPPORTED(EfsClientRemoveUsers, (ULONG_PTR a0, ULONG_PTR a1))
EFS_UNSUPPORTED(EfsClientWriteFileRaw, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2))
EFS_UNSUPPORTED(EfsClientWriteFileWithHeaderRaw, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4, ULONG_PTR a5, ULONG_PTR a6, ULONG_PTR a7))
EFS_UNSUPPORTED(EfsReprotectFile, (ULONG_PTR a0, ULONG_PTR a1))
EFS_UNSUPPORTED(EfsValidateTokenForConsumer, (ULONG_PTR a0, ULONG_PTR a1))
EFS_UNSUPPORTED(EfsValidateUserForConsumer, (ULONG_PTR a0, ULONG_PTR a1))
EFS_UNSUPPORTED(FeClQueryInfo, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3))
EDP_UNSUPPORTED(GetLockSessionUnwrappedKey, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4, ULONG_PTR a5, ULONG_PTR a6))
EDP_UNSUPPORTED(GetLockSessionWrappedKey, (ULONG_PTR a0, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4, ULONG_PTR a5, ULONG_PTR a6))
EFS_UNSUPPORTED(OefsCheckSupport, (ULONG_PTR a0, ULONG_PTR a1))
