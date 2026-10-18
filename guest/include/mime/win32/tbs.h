/* the tpm base services, tbs.h. */
#ifndef MIME_WIN32_TBS_H
#define MIME_WIN32_TBS_H

#include <mime/win32/winbase.h>

#ifndef TBSAPI
#define TBSAPI DECLSPEC_IMPORT
#endif

typedef UINT32 TBS_RESULT;
typedef UINT32 *PUINT32;
typedef PVOID TBS_HCONTEXT, *PTBS_HCONTEXT;
typedef UINT32 TBS_COMMAND_LOCALITY;
typedef UINT32 TBS_COMMAND_PRIORITY;

#define TBS_SUCCESS 0u
#define TBS_E_INTERNAL_ERROR 0x80284001u
#define TBS_E_BAD_PARAMETER 0x80284002u
#define TBS_E_INVALID_OUTPUT_POINTER 0x80284003u
#define TBS_E_INVALID_CONTEXT 0x80284004u
#define TBS_E_INSUFFICIENT_BUFFER 0x80284005u
#define TBS_E_INVALID_CONTEXT_PARAM 0x80284007u
#define TBS_E_BUFFER_TOO_LARGE 0x8028400Eu
#define TBS_E_TPM_NOT_FOUND 0x8028400Fu
#define TBS_E_NO_EVENT_LOG 0x80284011u

#define TBS_CONTEXT_VERSION_ONE 1
#define TBS_CONTEXT_VERSION_TWO 2

typedef struct tdTBS_CONTEXT_PARAMS {
    UINT32 version;
} TBS_CONTEXT_PARAMS, *PTBS_CONTEXT_PARAMS;
typedef const TBS_CONTEXT_PARAMS *PCTBS_CONTEXT_PARAMS;

typedef struct tdTBS_CONTEXT_PARAMS2 {
    UINT32 version;
    UINT32 asUINT32;
} TBS_CONTEXT_PARAMS2;

typedef struct _TPM_DEVICE_INFO {
    UINT32 structVersion;
    UINT32 tpmVersion;
    UINT32 tpmInterfaceType;
    UINT32 tpmImpRevision;
} TPM_DEVICE_INFO, *PTPM_DEVICE_INFO;

TBSAPI TBS_RESULT WINAPI Tbsi_GetDeviceInfo(UINT32 size, PVOID info);
TBSAPI TBS_RESULT WINAPI Tbsi_Context_Create(PCTBS_CONTEXT_PARAMS params, PTBS_HCONTEXT context);
TBSAPI TBS_RESULT WINAPI Tbsip_Context_Close(TBS_HCONTEXT context);
TBSAPI TBS_RESULT WINAPI Tbsip_Submit_Command(TBS_HCONTEXT context, TBS_COMMAND_LOCALITY locality,
                                              TBS_COMMAND_PRIORITY priority, const BYTE *command, UINT32 size,
                                              PBYTE result, PUINT32 result_size);
TBSAPI TBS_RESULT WINAPI Tbsi_Get_TCG_Log(TBS_HCONTEXT context, PBYTE log, PUINT32 size);

#endif
