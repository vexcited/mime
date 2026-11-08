/* the shlwapi exports windows implements in shlwapi itself, which call shcore here: an export is code in shlwapi,
 * as programs that resolve exports by walking the module list expect, and shcore is loaded with shlwapi. */
    .text
    .globl  ShlwapiThunk_SHCreateMemStream
    .p2align 4
ShlwapiThunk_SHCreateMemStream:
    jmp     *__imp_SHCreateMemStream(%rip)
    .globl  ShlwapiThunk_SHCreateThread
    .p2align 4
ShlwapiThunk_SHCreateThread:
    jmp     *__imp_SHCreateThread(%rip)
    .globl  ShlwapiThunk_IUnknown_AtomicRelease
    .p2align 4
ShlwapiThunk_IUnknown_AtomicRelease:
    jmp     *__imp_IUnknown_AtomicRelease(%rip)
    .globl  ShlwapiThunk_IUnknown_SetSite
    .p2align 4
ShlwapiThunk_IUnknown_SetSite:
    jmp     *__imp_IUnknown_SetSite(%rip)
    .globl  ShlwapiThunk_IUnknown_QueryService
    .p2align 4
ShlwapiThunk_IUnknown_QueryService:
    jmp     *__imp_IUnknown_QueryService(%rip)
    .globl  ShlwapiThunk_IStream_Read
    .p2align 4
ShlwapiThunk_IStream_Read:
    jmp     *__imp_IStream_Read(%rip)
    .globl  ShlwapiThunk_IUnknown_Set
    .p2align 4
ShlwapiThunk_IUnknown_Set:
    jmp     *__imp_IUnknown_Set(%rip)
    .globl  ShlwapiThunk_IStream_Write
    .p2align 4
ShlwapiThunk_IStream_Write:
    jmp     *__imp_IStream_Write(%rip)
    .globl  ShlwapiThunk_IStream_Reset
    .p2align 4
ShlwapiThunk_IStream_Reset:
    jmp     *__imp_IStream_Reset(%rip)
    .globl  ShlwapiThunk_IStream_Size
    .p2align 4
ShlwapiThunk_IStream_Size:
    jmp     *__imp_IStream_Size(%rip)
    .globl  ShlwapiThunk_SHAnsiToUnicode
    .p2align 4
ShlwapiThunk_SHAnsiToUnicode:
    jmp     *__imp_SHAnsiToUnicode(%rip)
    .globl  ShlwapiThunk_SHUnicodeToAnsi
    .p2align 4
ShlwapiThunk_SHUnicodeToAnsi:
    jmp     *__imp_SHUnicodeToAnsi(%rip)
    .globl  ShlwapiThunk_IUnknown_GetSite
    .p2align 4
ShlwapiThunk_IUnknown_GetSite:
    jmp     *__imp_IUnknown_GetSite(%rip)
    .globl  ShlwapiThunk_SHRegGetIntW
    .p2align 4
ShlwapiThunk_SHRegGetIntW:
    jmp     *__imp_SHRegGetIntW(%rip)
    .globl  ShlwapiThunk_SHAnsiToAnsi
    .p2align 4
ShlwapiThunk_SHAnsiToAnsi:
    jmp     *__imp_SHAnsiToAnsi(%rip)
    .globl  ShlwapiThunk_SHUnicodeToUnicode
    .p2align 4
ShlwapiThunk_SHUnicodeToUnicode:
    jmp     *__imp_SHUnicodeToUnicode(%rip)
    .globl  ShlwapiThunk_IsOS
    .p2align 4
ShlwapiThunk_IsOS:
    jmp     *__imp_IsOS(%rip)
    .globl  ShlwapiThunk_IStream_Copy
    .p2align 4
ShlwapiThunk_IStream_Copy:
    jmp     *__imp_IStream_Copy(%rip)
    .globl  ShlwapiThunk_IStream_ReadStr
    .p2align 4
ShlwapiThunk_IStream_ReadStr:
    jmp     *__imp_IStream_ReadStr(%rip)
    .globl  ShlwapiThunk_IStream_WriteStr
    .p2align 4
ShlwapiThunk_IStream_WriteStr:
    jmp     *__imp_IStream_WriteStr(%rip)
    .globl  ShlwapiThunk_SHCreateThreadWithHandle
    .p2align 4
ShlwapiThunk_SHCreateThreadWithHandle:
    jmp     *__imp_SHCreateThreadWithHandle(%rip)
    .globl  ShlwapiThunk_SHCopyKeyA
    .p2align 4
ShlwapiThunk_SHCopyKeyA:
    jmp     *__imp_SHCopyKeyA(%rip)
    .globl  ShlwapiThunk_SHCopyKeyW
    .p2align 4
ShlwapiThunk_SHCopyKeyW:
    jmp     *__imp_SHCopyKeyW(%rip)
    .globl  ShlwapiThunk_SHCreateStreamOnFileA
    .p2align 4
ShlwapiThunk_SHCreateStreamOnFileA:
    jmp     *__imp_SHCreateStreamOnFileA(%rip)
    .globl  ShlwapiThunk_SHCreateStreamOnFileEx
    .p2align 4
ShlwapiThunk_SHCreateStreamOnFileEx:
    jmp     *__imp_SHCreateStreamOnFileEx(%rip)
    .globl  ShlwapiThunk_SHCreateStreamOnFileW
    .p2align 4
ShlwapiThunk_SHCreateStreamOnFileW:
    jmp     *__imp_SHCreateStreamOnFileW(%rip)
    .globl  ShlwapiThunk_SHCreateThreadRef
    .p2align 4
ShlwapiThunk_SHCreateThreadRef:
    jmp     *__imp_SHCreateThreadRef(%rip)
    .globl  ShlwapiThunk_SHDeleteEmptyKeyA
    .p2align 4
ShlwapiThunk_SHDeleteEmptyKeyA:
    jmp     *__imp_SHDeleteEmptyKeyA(%rip)
    .globl  ShlwapiThunk_SHDeleteEmptyKeyW
    .p2align 4
ShlwapiThunk_SHDeleteEmptyKeyW:
    jmp     *__imp_SHDeleteEmptyKeyW(%rip)
    .globl  ShlwapiThunk_SHDeleteKeyA
    .p2align 4
ShlwapiThunk_SHDeleteKeyA:
    jmp     *__imp_SHDeleteKeyA(%rip)
    .globl  ShlwapiThunk_SHDeleteKeyW
    .p2align 4
ShlwapiThunk_SHDeleteKeyW:
    jmp     *__imp_SHDeleteKeyW(%rip)
    .globl  ShlwapiThunk_SHDeleteValueA
    .p2align 4
ShlwapiThunk_SHDeleteValueA:
    jmp     *__imp_SHDeleteValueA(%rip)
    .globl  ShlwapiThunk_SHDeleteValueW
    .p2align 4
ShlwapiThunk_SHDeleteValueW:
    jmp     *__imp_SHDeleteValueW(%rip)
    .globl  ShlwapiThunk_SHEnumKeyExA
    .p2align 4
ShlwapiThunk_SHEnumKeyExA:
    jmp     *__imp_SHEnumKeyExA(%rip)
    .globl  ShlwapiThunk_SHEnumKeyExW
    .p2align 4
ShlwapiThunk_SHEnumKeyExW:
    jmp     *__imp_SHEnumKeyExW(%rip)
    .globl  ShlwapiThunk_SHEnumValueA
    .p2align 4
ShlwapiThunk_SHEnumValueA:
    jmp     *__imp_SHEnumValueA(%rip)
    .globl  ShlwapiThunk_SHEnumValueW
    .p2align 4
ShlwapiThunk_SHEnumValueW:
    jmp     *__imp_SHEnumValueW(%rip)
    .globl  ShlwapiThunk_SHGetThreadRef
    .p2align 4
ShlwapiThunk_SHGetThreadRef:
    jmp     *__imp_SHGetThreadRef(%rip)
    .globl  ShlwapiThunk_SHGetValueA
    .p2align 4
ShlwapiThunk_SHGetValueA:
    jmp     *__imp_SHGetValueA(%rip)
    .globl  ShlwapiThunk_SHGetValueW
    .p2align 4
ShlwapiThunk_SHGetValueW:
    jmp     *__imp_SHGetValueW(%rip)
    .globl  ShlwapiThunk_SHOpenRegStream2A
    .p2align 4
ShlwapiThunk_SHOpenRegStream2A:
    jmp     *__imp_SHOpenRegStream2A(%rip)
    .globl  ShlwapiThunk_SHOpenRegStream2W
    .p2align 4
ShlwapiThunk_SHOpenRegStream2W:
    jmp     *__imp_SHOpenRegStream2W(%rip)
    .globl  ShlwapiThunk_SHOpenRegStreamA
    .p2align 4
ShlwapiThunk_SHOpenRegStreamA:
    jmp     *__imp_SHOpenRegStreamA(%rip)
    .globl  ShlwapiThunk_SHOpenRegStreamW
    .p2align 4
ShlwapiThunk_SHOpenRegStreamW:
    jmp     *__imp_SHOpenRegStreamW(%rip)
    .globl  ShlwapiThunk_SHQueryInfoKeyA
    .p2align 4
ShlwapiThunk_SHQueryInfoKeyA:
    jmp     *__imp_SHQueryInfoKeyA(%rip)
    .globl  ShlwapiThunk_SHQueryInfoKeyW
    .p2align 4
ShlwapiThunk_SHQueryInfoKeyW:
    jmp     *__imp_SHQueryInfoKeyW(%rip)
    .globl  ShlwapiThunk_SHQueryValueExA
    .p2align 4
ShlwapiThunk_SHQueryValueExA:
    jmp     *__imp_SHQueryValueExA(%rip)
    .globl  ShlwapiThunk_SHQueryValueExW
    .p2align 4
ShlwapiThunk_SHQueryValueExW:
    jmp     *__imp_SHQueryValueExW(%rip)
    .globl  ShlwapiThunk_SHRegDuplicateHKey
    .p2align 4
ShlwapiThunk_SHRegDuplicateHKey:
    jmp     *__imp_SHRegDuplicateHKey(%rip)
    .globl  ShlwapiThunk_SHRegGetPathA
    .p2align 4
ShlwapiThunk_SHRegGetPathA:
    jmp     *__imp_SHRegGetPathA(%rip)
    .globl  ShlwapiThunk_SHRegGetPathW
    .p2align 4
ShlwapiThunk_SHRegGetPathW:
    jmp     *__imp_SHRegGetPathW(%rip)
    .globl  ShlwapiThunk_SHRegGetValueA
    .p2align 4
ShlwapiThunk_SHRegGetValueA:
    jmp     *__imp_SHRegGetValueA(%rip)
    .globl  ShlwapiThunk_SHRegGetValueW
    .p2align 4
ShlwapiThunk_SHRegGetValueW:
    jmp     *__imp_SHRegGetValueW(%rip)
    .globl  ShlwapiThunk_SHRegSetPathA
    .p2align 4
ShlwapiThunk_SHRegSetPathA:
    jmp     *__imp_SHRegSetPathA(%rip)
    .globl  ShlwapiThunk_SHRegSetPathW
    .p2align 4
ShlwapiThunk_SHRegSetPathW:
    jmp     *__imp_SHRegSetPathW(%rip)
    .globl  ShlwapiThunk_SHReleaseThreadRef
    .p2align 4
ShlwapiThunk_SHReleaseThreadRef:
    jmp     *__imp_SHReleaseThreadRef(%rip)
    .globl  ShlwapiThunk_SHSetThreadRef
    .p2align 4
ShlwapiThunk_SHSetThreadRef:
    jmp     *__imp_SHSetThreadRef(%rip)
    .globl  ShlwapiThunk_SHSetValueA
    .p2align 4
ShlwapiThunk_SHSetValueA:
    jmp     *__imp_SHSetValueA(%rip)
    .globl  ShlwapiThunk_SHSetValueW
    .p2align 4
ShlwapiThunk_SHSetValueW:
    jmp     *__imp_SHSetValueW(%rip)
    .globl  ShlwapiThunk_SHStrDupA
    .p2align 4
ShlwapiThunk_SHStrDupA:
    jmp     *__imp_SHStrDupA(%rip)
    .globl  ShlwapiThunk_SHStrDupW
    .p2align 4
ShlwapiThunk_SHStrDupW:
    jmp     *__imp_SHStrDupW(%rip)
