    .text
    .globl  InitErrors
    .p2align 4
InitErrors:
    movl    $0, %eax
    jmp     mscoree_thunk_common
    .globl  PostError
    .p2align 4
PostError:
    movl    $1, %eax
    jmp     mscoree_thunk_common
    .globl  InitSSAutoEnterThread
    .p2align 4
InitSSAutoEnterThread:
    movl    $2, %eax
    jmp     mscoree_thunk_common
    .globl  UpdateError
    .p2align 4
UpdateError:
    movl    $3, %eax
    jmp     mscoree_thunk_common
    .globl  CloseCtrs
    .p2align 4
CloseCtrs:
    movl    $4, %eax
    jmp     mscoree_thunk_common
    .globl  LoadStringRC
    .p2align 4
LoadStringRC:
    movl    $5, %eax
    jmp     mscoree_thunk_common
    .globl  ReOpenMetaDataWithMemory
    .p2align 4
ReOpenMetaDataWithMemory:
    movl    $6, %eax
    jmp     mscoree_thunk_common
    .globl  CollectCtrs
    .p2align 4
CollectCtrs:
    movl    $7, %eax
    jmp     mscoree_thunk_common
    .globl  CorDllMainWorker
    .p2align 4
CorDllMainWorker:
    movl    $8, %eax
    jmp     mscoree_thunk_common
    .globl  EEDllGetClassObjectFromClass
    .p2align 4
EEDllGetClassObjectFromClass:
    movl    $9, %eax
    jmp     mscoree_thunk_common
    .globl  GetPrivateContextsPerfCounters
    .p2align 4
GetPrivateContextsPerfCounters:
    movl    $10, %eax
    jmp     mscoree_thunk_common
    .globl  GetStartupFlags
    .p2align 4
GetStartupFlags:
    movl    $11, %eax
    jmp     mscoree_thunk_common
    .globl  GetTargetForVTableEntry
    .p2align 4
GetTargetForVTableEntry:
    movl    $12, %eax
    jmp     mscoree_thunk_common
    .globl  GetTokenForVTableEntry
    .p2align 4
GetTokenForVTableEntry:
    movl    $13, %eax
    jmp     mscoree_thunk_common
    .globl  LogHelp_LogAssert
    .p2align 4
LogHelp_LogAssert:
    movl    $14, %eax
    jmp     mscoree_thunk_common
    .globl  LogHelp_NoGuiOnAssert
    .p2align 4
LogHelp_NoGuiOnAssert:
    movl    $15, %eax
    jmp     mscoree_thunk_common
    .globl  LogHelp_TerminateOnAssert
    .p2align 4
LogHelp_TerminateOnAssert:
    movl    $16, %eax
    jmp     mscoree_thunk_common
    .globl  OpenCtrs
    .p2align 4
OpenCtrs:
    movl    $17, %eax
    jmp     mscoree_thunk_common
    .globl  SetTargetForVTableEntry
    .p2align 4
SetTargetForVTableEntry:
    movl    $18, %eax
    jmp     mscoree_thunk_common
    .globl  CallFunctionShim
    .p2align 4
CallFunctionShim:
    movl    $19, %eax
    jmp     mscoree_thunk_common
    .globl  ClrCreateManagedInstance
    .p2align 4
ClrCreateManagedInstance:
    movl    $20, %eax
    jmp     mscoree_thunk_common
    .globl  CoEEShutDownCOM
    .p2align 4
CoEEShutDownCOM:
    movl    $21, %eax
    jmp     mscoree_thunk_common
    .globl  CoInitializeCor
    .p2align 4
CoInitializeCor:
    movl    $22, %eax
    jmp     mscoree_thunk_common
    .globl  CoInitializeEE
    .p2align 4
CoInitializeEE:
    movl    $23, %eax
    jmp     mscoree_thunk_common
    .globl  CoUninitializeCor
    .p2align 4
CoUninitializeCor:
    movl    $24, %eax
    jmp     mscoree_thunk_common
    .globl  CoUninitializeEE
    .p2align 4
CoUninitializeEE:
    movl    $25, %eax
    jmp     mscoree_thunk_common
    .globl  CorBindToCurrentRuntime
    .p2align 4
CorBindToCurrentRuntime:
    movl    $26, %eax
    jmp     mscoree_thunk_common
    .globl  CorBindToRuntimeByCfg
    .p2align 4
CorBindToRuntimeByCfg:
    movl    $27, %eax
    jmp     mscoree_thunk_common
    .globl  CorBindToRuntimeByPath
    .p2align 4
CorBindToRuntimeByPath:
    movl    $28, %eax
    jmp     mscoree_thunk_common
    .globl  CorBindToRuntimeByPathEx
    .p2align 4
CorBindToRuntimeByPathEx:
    movl    $29, %eax
    jmp     mscoree_thunk_common
    .globl  CorBindToRuntimeEx
    .p2align 4
CorBindToRuntimeEx:
    movl    $30, %eax
    jmp     mscoree_thunk_common
    .globl  CorBindToRuntimeHost
    .p2align 4
CorBindToRuntimeHost:
    movl    $31, %eax
    jmp     mscoree_thunk_common
    .globl  CorExitProcess
    .p2align 4
CorExitProcess:
    movl    $32, %eax
    jmp     mscoree_thunk_common
    .globl  CorGetSvc
    .p2align 4
CorGetSvc:
    movl    $33, %eax
    jmp     mscoree_thunk_common
    .globl  CorIsLatestSvc
    .p2align 4
CorIsLatestSvc:
    movl    $34, %eax
    jmp     mscoree_thunk_common
    .globl  CorMarkThreadInThreadPool
    .p2align 4
CorMarkThreadInThreadPool:
    movl    $35, %eax
    jmp     mscoree_thunk_common
    .globl  CorTickleSvc
    .p2align 4
CorTickleSvc:
    movl    $36, %eax
    jmp     mscoree_thunk_common
    .globl  CreateDebuggingInterfaceFromVersion
    .p2align 4
CreateDebuggingInterfaceFromVersion:
    movl    $37, %eax
    jmp     mscoree_thunk_common
    .globl  CreateInterface
    .p2align 4
CreateInterface:
    movl    $38, %eax
    jmp     mscoree_thunk_common
    .globl  DllCanUnloadNow
    .p2align 4
DllCanUnloadNow:
    movl    $39, %eax
    jmp     mscoree_thunk_common
    .globl  DllGetClassObject
    .p2align 4
DllGetClassObject:
    movl    $40, %eax
    jmp     mscoree_thunk_common
    .globl  DllRegisterServer
    .p2align 4
DllRegisterServer:
    movl    $41, %eax
    jmp     mscoree_thunk_common
    .globl  DllUnregisterServer
    .p2align 4
DllUnregisterServer:
    movl    $42, %eax
    jmp     mscoree_thunk_common
    .globl  EEDllRegisterServer
    .p2align 4
EEDllRegisterServer:
    movl    $43, %eax
    jmp     mscoree_thunk_common
    .globl  EEDllUnregisterServer
    .p2align 4
EEDllUnregisterServer:
    movl    $44, %eax
    jmp     mscoree_thunk_common
    .globl  GetAssemblyMDImport
    .p2align 4
GetAssemblyMDImport:
    movl    $45, %eax
    jmp     mscoree_thunk_common
    .globl  GetCORRequiredVersion
    .p2align 4
GetCORRequiredVersion:
    movl    $46, %eax
    jmp     mscoree_thunk_common
    .globl  GetCORRootDirectory
    .p2align 4
GetCORRootDirectory:
    movl    $47, %eax
    jmp     mscoree_thunk_common
    .globl  GetCORSystemDirectory
    .p2align 4
GetCORSystemDirectory:
    movl    $48, %eax
    jmp     mscoree_thunk_common
    .globl  GetCompileInfo
    .p2align 4
GetCompileInfo:
    movl    $49, %eax
    jmp     mscoree_thunk_common
    .globl  GetFileVersion
    .p2align 4
GetFileVersion:
    movl    $50, %eax
    jmp     mscoree_thunk_common
    .globl  GetHashFromAssemblyFile
    .p2align 4
GetHashFromAssemblyFile:
    movl    $51, %eax
    jmp     mscoree_thunk_common
    .globl  GetHashFromAssemblyFileW
    .p2align 4
GetHashFromAssemblyFileW:
    movl    $52, %eax
    jmp     mscoree_thunk_common
    .globl  GetHashFromBlob
    .p2align 4
GetHashFromBlob:
    movl    $53, %eax
    jmp     mscoree_thunk_common
    .globl  GetHashFromFile
    .p2align 4
GetHashFromFile:
    movl    $54, %eax
    jmp     mscoree_thunk_common
    .globl  GetHashFromFileW
    .p2align 4
GetHashFromFileW:
    movl    $55, %eax
    jmp     mscoree_thunk_common
    .globl  GetHashFromHandle
    .p2align 4
GetHashFromHandle:
    movl    $56, %eax
    jmp     mscoree_thunk_common
    .globl  GetHostConfigurationFile
    .p2align 4
GetHostConfigurationFile:
    movl    $57, %eax
    jmp     mscoree_thunk_common
    .globl  GetMetaDataInternalInterface
    .p2align 4
GetMetaDataInternalInterface:
    movl    $58, %eax
    jmp     mscoree_thunk_common
    .globl  GetMetaDataInternalInterfaceFromPublic
    .p2align 4
GetMetaDataInternalInterfaceFromPublic:
    movl    $59, %eax
    jmp     mscoree_thunk_common
    .globl  GetMetaDataPublicInterfaceFromInternal
    .p2align 4
GetMetaDataPublicInterfaceFromInternal:
    movl    $60, %eax
    jmp     mscoree_thunk_common
    .globl  GetPermissionRequests
    .p2align 4
GetPermissionRequests:
    movl    $61, %eax
    jmp     mscoree_thunk_common
    .globl  GetRealProcAddress
    .p2align 4
GetRealProcAddress:
    movl    $62, %eax
    jmp     mscoree_thunk_common
    .globl  GetRequestedRuntimeVersion
    .p2align 4
GetRequestedRuntimeVersion:
    movl    $63, %eax
    jmp     mscoree_thunk_common
    .globl  GetRequestedRuntimeVersionForCLSID
    .p2align 4
GetRequestedRuntimeVersionForCLSID:
    movl    $64, %eax
    jmp     mscoree_thunk_common
    .globl  GetVersionFromProcess
    .p2align 4
GetVersionFromProcess:
    movl    $65, %eax
    jmp     mscoree_thunk_common
    .globl  GetXMLElement
    .p2align 4
GetXMLElement:
    movl    $66, %eax
    jmp     mscoree_thunk_common
    .globl  GetXMLElementAttribute
    .p2align 4
GetXMLElementAttribute:
    movl    $67, %eax
    jmp     mscoree_thunk_common
    .globl  GetXMLObject
    .p2align 4
GetXMLObject:
    movl    $68, %eax
    jmp     mscoree_thunk_common
    .globl  IEE
    .p2align 4
IEE:
    movl    $69, %eax
    jmp     mscoree_thunk_common
    .globl  LoadLibraryShim
    .p2align 4
LoadLibraryShim:
    movl    $70, %eax
    jmp     mscoree_thunk_common
    .globl  LoadLibraryWithPolicyShim
    .p2align 4
LoadLibraryWithPolicyShim:
    movl    $71, %eax
    jmp     mscoree_thunk_common
    .globl  LoadStringRCEx
    .p2align 4
LoadStringRCEx:
    movl    $72, %eax
    jmp     mscoree_thunk_common
    .globl  LockClrVersion
    .p2align 4
LockClrVersion:
    movl    $73, %eax
    jmp     mscoree_thunk_common
    .globl  MetaDataGetDispenser
    .p2align 4
MetaDataGetDispenser:
    movl    $74, %eax
    jmp     mscoree_thunk_common
    .globl  ND_CopyObjDst
    .p2align 4
ND_CopyObjDst:
    movl    $75, %eax
    jmp     mscoree_thunk_common
    .globl  ND_CopyObjSrc
    .p2align 4
ND_CopyObjSrc:
    movl    $76, %eax
    jmp     mscoree_thunk_common
    .globl  ND_RI2
    .p2align 4
ND_RI2:
    movl    $77, %eax
    jmp     mscoree_thunk_common
    .globl  ND_RI4
    .p2align 4
ND_RI4:
    movl    $78, %eax
    jmp     mscoree_thunk_common
    .globl  ND_RI8
    .p2align 4
ND_RI8:
    movl    $79, %eax
    jmp     mscoree_thunk_common
    .globl  ND_RU1
    .p2align 4
ND_RU1:
    movl    $80, %eax
    jmp     mscoree_thunk_common
    .globl  ND_WI2
    .p2align 4
ND_WI2:
    movl    $81, %eax
    jmp     mscoree_thunk_common
    .globl  ND_WI4
    .p2align 4
ND_WI4:
    movl    $82, %eax
    jmp     mscoree_thunk_common
    .globl  ND_WI8
    .p2align 4
ND_WI8:
    movl    $83, %eax
    jmp     mscoree_thunk_common
    .globl  ND_WU1
    .p2align 4
ND_WU1:
    movl    $84, %eax
    jmp     mscoree_thunk_common
    .globl  ReOpenMetaDataWithMemoryEx
    .p2align 4
ReOpenMetaDataWithMemoryEx:
    movl    $85, %eax
    jmp     mscoree_thunk_common
    .globl  RunDll32ShimW
    .p2align 4
RunDll32ShimW:
    movl    $86, %eax
    jmp     mscoree_thunk_common
    .globl  RuntimeOSHandle
    .p2align 4
RuntimeOSHandle:
    movl    $87, %eax
    jmp     mscoree_thunk_common
    .globl  RuntimeOpenImage
    .p2align 4
RuntimeOpenImage:
    movl    $88, %eax
    jmp     mscoree_thunk_common
    .globl  RuntimeReleaseHandle
    .p2align 4
RuntimeReleaseHandle:
    movl    $89, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameCompareAssemblies
    .p2align 4
StrongNameCompareAssemblies:
    movl    $90, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameErrorInfo
    .p2align 4
StrongNameErrorInfo:
    movl    $91, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameFreeBuffer
    .p2align 4
StrongNameFreeBuffer:
    movl    $92, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameGetBlob
    .p2align 4
StrongNameGetBlob:
    movl    $93, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameGetBlobFromImage
    .p2align 4
StrongNameGetBlobFromImage:
    movl    $94, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameGetPublicKey
    .p2align 4
StrongNameGetPublicKey:
    movl    $95, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameHashSize
    .p2align 4
StrongNameHashSize:
    movl    $96, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameKeyDelete
    .p2align 4
StrongNameKeyDelete:
    movl    $97, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameKeyGen
    .p2align 4
StrongNameKeyGen:
    movl    $98, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameKeyGenEx
    .p2align 4
StrongNameKeyGenEx:
    movl    $99, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameKeyInstall
    .p2align 4
StrongNameKeyInstall:
    movl    $100, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameSignatureGeneration
    .p2align 4
StrongNameSignatureGeneration:
    movl    $101, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameSignatureGenerationEx
    .p2align 4
StrongNameSignatureGenerationEx:
    movl    $102, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameSignatureSize
    .p2align 4
StrongNameSignatureSize:
    movl    $103, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameSignatureVerification
    .p2align 4
StrongNameSignatureVerification:
    movl    $104, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameSignatureVerificationEx
    .p2align 4
StrongNameSignatureVerificationEx:
    movl    $105, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameSignatureVerificationFromImage
    .p2align 4
StrongNameSignatureVerificationFromImage:
    movl    $106, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameTokenFromAssembly
    .p2align 4
StrongNameTokenFromAssembly:
    movl    $107, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameTokenFromAssemblyEx
    .p2align 4
StrongNameTokenFromAssemblyEx:
    movl    $108, %eax
    jmp     mscoree_thunk_common
    .globl  StrongNameTokenFromPublicKey
    .p2align 4
StrongNameTokenFromPublicKey:
    movl    $109, %eax
    jmp     mscoree_thunk_common
    .globl  TranslateSecurityAttributes
    .p2align 4
TranslateSecurityAttributes:
    movl    $110, %eax
    jmp     mscoree_thunk_common
    .globl  _CorDllMain
    .p2align 4
_CorDllMain:
    movl    $111, %eax
    jmp     mscoree_thunk_common
    .globl  _CorExeMain2
    .p2align 4
_CorExeMain2:
    movl    $112, %eax
    jmp     mscoree_thunk_common
