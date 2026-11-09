/* the shlwapi exports windows implements in shlwapi itself, which call shcore here: an export is code in shlwapi,
 * as programs that resolve exports by walking the module list expect, and shcore is loaded with shlwapi. */
    .text
    .globl  ShlwapiThunk_SHCreateMemStream
    .p2align 2
ShlwapiThunk_SHCreateMemStream:
    adrp    x16, __imp_SHCreateMemStream
    ldr     x16, [x16, :lo12:__imp_SHCreateMemStream]
    br      x16
    .globl  ShlwapiThunk_SHCreateThread
    .p2align 2
ShlwapiThunk_SHCreateThread:
    adrp    x16, __imp_SHCreateThread
    ldr     x16, [x16, :lo12:__imp_SHCreateThread]
    br      x16
    .globl  ShlwapiThunk_IUnknown_AtomicRelease
    .p2align 2
ShlwapiThunk_IUnknown_AtomicRelease:
    adrp    x16, __imp_IUnknown_AtomicRelease
    ldr     x16, [x16, :lo12:__imp_IUnknown_AtomicRelease]
    br      x16
    .globl  ShlwapiThunk_IUnknown_SetSite
    .p2align 2
ShlwapiThunk_IUnknown_SetSite:
    adrp    x16, __imp_IUnknown_SetSite
    ldr     x16, [x16, :lo12:__imp_IUnknown_SetSite]
    br      x16
    .globl  ShlwapiThunk_IUnknown_QueryService
    .p2align 2
ShlwapiThunk_IUnknown_QueryService:
    adrp    x16, __imp_IUnknown_QueryService
    ldr     x16, [x16, :lo12:__imp_IUnknown_QueryService]
    br      x16
    .globl  ShlwapiThunk_IStream_Read
    .p2align 2
ShlwapiThunk_IStream_Read:
    adrp    x16, __imp_IStream_Read
    ldr     x16, [x16, :lo12:__imp_IStream_Read]
    br      x16
    .globl  ShlwapiThunk_IUnknown_Set
    .p2align 2
ShlwapiThunk_IUnknown_Set:
    adrp    x16, __imp_IUnknown_Set
    ldr     x16, [x16, :lo12:__imp_IUnknown_Set]
    br      x16
    .globl  ShlwapiThunk_IStream_Write
    .p2align 2
ShlwapiThunk_IStream_Write:
    adrp    x16, __imp_IStream_Write
    ldr     x16, [x16, :lo12:__imp_IStream_Write]
    br      x16
    .globl  ShlwapiThunk_IStream_Reset
    .p2align 2
ShlwapiThunk_IStream_Reset:
    adrp    x16, __imp_IStream_Reset
    ldr     x16, [x16, :lo12:__imp_IStream_Reset]
    br      x16
    .globl  ShlwapiThunk_IStream_Size
    .p2align 2
ShlwapiThunk_IStream_Size:
    adrp    x16, __imp_IStream_Size
    ldr     x16, [x16, :lo12:__imp_IStream_Size]
    br      x16
    .globl  ShlwapiThunk_SHAnsiToUnicode
    .p2align 2
ShlwapiThunk_SHAnsiToUnicode:
    adrp    x16, __imp_SHAnsiToUnicode
    ldr     x16, [x16, :lo12:__imp_SHAnsiToUnicode]
    br      x16
    .globl  ShlwapiThunk_SHUnicodeToAnsi
    .p2align 2
ShlwapiThunk_SHUnicodeToAnsi:
    adrp    x16, __imp_SHUnicodeToAnsi
    ldr     x16, [x16, :lo12:__imp_SHUnicodeToAnsi]
    br      x16
    .globl  ShlwapiThunk_IUnknown_GetSite
    .p2align 2
ShlwapiThunk_IUnknown_GetSite:
    adrp    x16, __imp_IUnknown_GetSite
    ldr     x16, [x16, :lo12:__imp_IUnknown_GetSite]
    br      x16
    .globl  ShlwapiThunk_SHRegGetIntW
    .p2align 2
ShlwapiThunk_SHRegGetIntW:
    adrp    x16, __imp_SHRegGetIntW
    ldr     x16, [x16, :lo12:__imp_SHRegGetIntW]
    br      x16
    .globl  ShlwapiThunk_SHAnsiToAnsi
    .p2align 2
ShlwapiThunk_SHAnsiToAnsi:
    adrp    x16, __imp_SHAnsiToAnsi
    ldr     x16, [x16, :lo12:__imp_SHAnsiToAnsi]
    br      x16
    .globl  ShlwapiThunk_SHUnicodeToUnicode
    .p2align 2
ShlwapiThunk_SHUnicodeToUnicode:
    adrp    x16, __imp_SHUnicodeToUnicode
    ldr     x16, [x16, :lo12:__imp_SHUnicodeToUnicode]
    br      x16
    .globl  ShlwapiThunk_IsOS
    .p2align 2
ShlwapiThunk_IsOS:
    adrp    x16, __imp_IsOS
    ldr     x16, [x16, :lo12:__imp_IsOS]
    br      x16
    .globl  ShlwapiThunk_IStream_Copy
    .p2align 2
ShlwapiThunk_IStream_Copy:
    adrp    x16, __imp_IStream_Copy
    ldr     x16, [x16, :lo12:__imp_IStream_Copy]
    br      x16
    .globl  ShlwapiThunk_IStream_ReadStr
    .p2align 2
ShlwapiThunk_IStream_ReadStr:
    adrp    x16, __imp_IStream_ReadStr
    ldr     x16, [x16, :lo12:__imp_IStream_ReadStr]
    br      x16
    .globl  ShlwapiThunk_IStream_WriteStr
    .p2align 2
ShlwapiThunk_IStream_WriteStr:
    adrp    x16, __imp_IStream_WriteStr
    ldr     x16, [x16, :lo12:__imp_IStream_WriteStr]
    br      x16
    .globl  ShlwapiThunk_SHCreateThreadWithHandle
    .p2align 2
ShlwapiThunk_SHCreateThreadWithHandle:
    adrp    x16, __imp_SHCreateThreadWithHandle
    ldr     x16, [x16, :lo12:__imp_SHCreateThreadWithHandle]
    br      x16
    .globl  ShlwapiThunk_SHCopyKeyA
    .p2align 2
ShlwapiThunk_SHCopyKeyA:
    adrp    x16, __imp_SHCopyKeyA
    ldr     x16, [x16, :lo12:__imp_SHCopyKeyA]
    br      x16
    .globl  ShlwapiThunk_SHCopyKeyW
    .p2align 2
ShlwapiThunk_SHCopyKeyW:
    adrp    x16, __imp_SHCopyKeyW
    ldr     x16, [x16, :lo12:__imp_SHCopyKeyW]
    br      x16
    .globl  ShlwapiThunk_SHCreateStreamOnFileA
    .p2align 2
ShlwapiThunk_SHCreateStreamOnFileA:
    adrp    x16, __imp_SHCreateStreamOnFileA
    ldr     x16, [x16, :lo12:__imp_SHCreateStreamOnFileA]
    br      x16
    .globl  ShlwapiThunk_SHCreateStreamOnFileEx
    .p2align 2
ShlwapiThunk_SHCreateStreamOnFileEx:
    adrp    x16, __imp_SHCreateStreamOnFileEx
    ldr     x16, [x16, :lo12:__imp_SHCreateStreamOnFileEx]
    br      x16
    .globl  ShlwapiThunk_SHCreateStreamOnFileW
    .p2align 2
ShlwapiThunk_SHCreateStreamOnFileW:
    adrp    x16, __imp_SHCreateStreamOnFileW
    ldr     x16, [x16, :lo12:__imp_SHCreateStreamOnFileW]
    br      x16
    .globl  ShlwapiThunk_SHCreateThreadRef
    .p2align 2
ShlwapiThunk_SHCreateThreadRef:
    adrp    x16, __imp_SHCreateThreadRef
    ldr     x16, [x16, :lo12:__imp_SHCreateThreadRef]
    br      x16
    .globl  ShlwapiThunk_SHDeleteEmptyKeyA
    .p2align 2
ShlwapiThunk_SHDeleteEmptyKeyA:
    adrp    x16, __imp_SHDeleteEmptyKeyA
    ldr     x16, [x16, :lo12:__imp_SHDeleteEmptyKeyA]
    br      x16
    .globl  ShlwapiThunk_SHDeleteEmptyKeyW
    .p2align 2
ShlwapiThunk_SHDeleteEmptyKeyW:
    adrp    x16, __imp_SHDeleteEmptyKeyW
    ldr     x16, [x16, :lo12:__imp_SHDeleteEmptyKeyW]
    br      x16
    .globl  ShlwapiThunk_SHDeleteKeyA
    .p2align 2
ShlwapiThunk_SHDeleteKeyA:
    adrp    x16, __imp_SHDeleteKeyA
    ldr     x16, [x16, :lo12:__imp_SHDeleteKeyA]
    br      x16
    .globl  ShlwapiThunk_SHDeleteKeyW
    .p2align 2
ShlwapiThunk_SHDeleteKeyW:
    adrp    x16, __imp_SHDeleteKeyW
    ldr     x16, [x16, :lo12:__imp_SHDeleteKeyW]
    br      x16
    .globl  ShlwapiThunk_SHDeleteValueA
    .p2align 2
ShlwapiThunk_SHDeleteValueA:
    adrp    x16, __imp_SHDeleteValueA
    ldr     x16, [x16, :lo12:__imp_SHDeleteValueA]
    br      x16
    .globl  ShlwapiThunk_SHDeleteValueW
    .p2align 2
ShlwapiThunk_SHDeleteValueW:
    adrp    x16, __imp_SHDeleteValueW
    ldr     x16, [x16, :lo12:__imp_SHDeleteValueW]
    br      x16
    .globl  ShlwapiThunk_SHEnumKeyExA
    .p2align 2
ShlwapiThunk_SHEnumKeyExA:
    adrp    x16, __imp_SHEnumKeyExA
    ldr     x16, [x16, :lo12:__imp_SHEnumKeyExA]
    br      x16
    .globl  ShlwapiThunk_SHEnumKeyExW
    .p2align 2
ShlwapiThunk_SHEnumKeyExW:
    adrp    x16, __imp_SHEnumKeyExW
    ldr     x16, [x16, :lo12:__imp_SHEnumKeyExW]
    br      x16
    .globl  ShlwapiThunk_SHEnumValueA
    .p2align 2
ShlwapiThunk_SHEnumValueA:
    adrp    x16, __imp_SHEnumValueA
    ldr     x16, [x16, :lo12:__imp_SHEnumValueA]
    br      x16
    .globl  ShlwapiThunk_SHEnumValueW
    .p2align 2
ShlwapiThunk_SHEnumValueW:
    adrp    x16, __imp_SHEnumValueW
    ldr     x16, [x16, :lo12:__imp_SHEnumValueW]
    br      x16
    .globl  ShlwapiThunk_SHGetThreadRef
    .p2align 2
ShlwapiThunk_SHGetThreadRef:
    adrp    x16, __imp_SHGetThreadRef
    ldr     x16, [x16, :lo12:__imp_SHGetThreadRef]
    br      x16
    .globl  ShlwapiThunk_SHGetValueA
    .p2align 2
ShlwapiThunk_SHGetValueA:
    adrp    x16, __imp_SHGetValueA
    ldr     x16, [x16, :lo12:__imp_SHGetValueA]
    br      x16
    .globl  ShlwapiThunk_SHGetValueW
    .p2align 2
ShlwapiThunk_SHGetValueW:
    adrp    x16, __imp_SHGetValueW
    ldr     x16, [x16, :lo12:__imp_SHGetValueW]
    br      x16
    .globl  ShlwapiThunk_SHOpenRegStream2A
    .p2align 2
ShlwapiThunk_SHOpenRegStream2A:
    adrp    x16, __imp_SHOpenRegStream2A
    ldr     x16, [x16, :lo12:__imp_SHOpenRegStream2A]
    br      x16
    .globl  ShlwapiThunk_SHOpenRegStream2W
    .p2align 2
ShlwapiThunk_SHOpenRegStream2W:
    adrp    x16, __imp_SHOpenRegStream2W
    ldr     x16, [x16, :lo12:__imp_SHOpenRegStream2W]
    br      x16
    .globl  ShlwapiThunk_SHOpenRegStreamA
    .p2align 2
ShlwapiThunk_SHOpenRegStreamA:
    adrp    x16, __imp_SHOpenRegStreamA
    ldr     x16, [x16, :lo12:__imp_SHOpenRegStreamA]
    br      x16
    .globl  ShlwapiThunk_SHOpenRegStreamW
    .p2align 2
ShlwapiThunk_SHOpenRegStreamW:
    adrp    x16, __imp_SHOpenRegStreamW
    ldr     x16, [x16, :lo12:__imp_SHOpenRegStreamW]
    br      x16
    .globl  ShlwapiThunk_SHQueryInfoKeyA
    .p2align 2
ShlwapiThunk_SHQueryInfoKeyA:
    adrp    x16, __imp_SHQueryInfoKeyA
    ldr     x16, [x16, :lo12:__imp_SHQueryInfoKeyA]
    br      x16
    .globl  ShlwapiThunk_SHQueryInfoKeyW
    .p2align 2
ShlwapiThunk_SHQueryInfoKeyW:
    adrp    x16, __imp_SHQueryInfoKeyW
    ldr     x16, [x16, :lo12:__imp_SHQueryInfoKeyW]
    br      x16
    .globl  ShlwapiThunk_SHQueryValueExA
    .p2align 2
ShlwapiThunk_SHQueryValueExA:
    adrp    x16, __imp_SHQueryValueExA
    ldr     x16, [x16, :lo12:__imp_SHQueryValueExA]
    br      x16
    .globl  ShlwapiThunk_SHQueryValueExW
    .p2align 2
ShlwapiThunk_SHQueryValueExW:
    adrp    x16, __imp_SHQueryValueExW
    ldr     x16, [x16, :lo12:__imp_SHQueryValueExW]
    br      x16
    .globl  ShlwapiThunk_SHRegDuplicateHKey
    .p2align 2
ShlwapiThunk_SHRegDuplicateHKey:
    adrp    x16, __imp_SHRegDuplicateHKey
    ldr     x16, [x16, :lo12:__imp_SHRegDuplicateHKey]
    br      x16
    .globl  ShlwapiThunk_SHRegGetPathA
    .p2align 2
ShlwapiThunk_SHRegGetPathA:
    adrp    x16, __imp_SHRegGetPathA
    ldr     x16, [x16, :lo12:__imp_SHRegGetPathA]
    br      x16
    .globl  ShlwapiThunk_SHRegGetPathW
    .p2align 2
ShlwapiThunk_SHRegGetPathW:
    adrp    x16, __imp_SHRegGetPathW
    ldr     x16, [x16, :lo12:__imp_SHRegGetPathW]
    br      x16
    .globl  ShlwapiThunk_SHRegGetValueA
    .p2align 2
ShlwapiThunk_SHRegGetValueA:
    adrp    x16, __imp_SHRegGetValueA
    ldr     x16, [x16, :lo12:__imp_SHRegGetValueA]
    br      x16
    .globl  ShlwapiThunk_SHRegGetValueW
    .p2align 2
ShlwapiThunk_SHRegGetValueW:
    adrp    x16, __imp_SHRegGetValueW
    ldr     x16, [x16, :lo12:__imp_SHRegGetValueW]
    br      x16
    .globl  ShlwapiThunk_SHRegSetPathA
    .p2align 2
ShlwapiThunk_SHRegSetPathA:
    adrp    x16, __imp_SHRegSetPathA
    ldr     x16, [x16, :lo12:__imp_SHRegSetPathA]
    br      x16
    .globl  ShlwapiThunk_SHRegSetPathW
    .p2align 2
ShlwapiThunk_SHRegSetPathW:
    adrp    x16, __imp_SHRegSetPathW
    ldr     x16, [x16, :lo12:__imp_SHRegSetPathW]
    br      x16
    .globl  ShlwapiThunk_SHReleaseThreadRef
    .p2align 2
ShlwapiThunk_SHReleaseThreadRef:
    adrp    x16, __imp_SHReleaseThreadRef
    ldr     x16, [x16, :lo12:__imp_SHReleaseThreadRef]
    br      x16
    .globl  ShlwapiThunk_SHSetThreadRef
    .p2align 2
ShlwapiThunk_SHSetThreadRef:
    adrp    x16, __imp_SHSetThreadRef
    ldr     x16, [x16, :lo12:__imp_SHSetThreadRef]
    br      x16
    .globl  ShlwapiThunk_SHSetValueA
    .p2align 2
ShlwapiThunk_SHSetValueA:
    adrp    x16, __imp_SHSetValueA
    ldr     x16, [x16, :lo12:__imp_SHSetValueA]
    br      x16
    .globl  ShlwapiThunk_SHSetValueW
    .p2align 2
ShlwapiThunk_SHSetValueW:
    adrp    x16, __imp_SHSetValueW
    ldr     x16, [x16, :lo12:__imp_SHSetValueW]
    br      x16
    .globl  ShlwapiThunk_SHStrDupA
    .p2align 2
ShlwapiThunk_SHStrDupA:
    adrp    x16, __imp_SHStrDupA
    ldr     x16, [x16, :lo12:__imp_SHStrDupA]
    br      x16
    .globl  ShlwapiThunk_SHStrDupW
    .p2align 2
ShlwapiThunk_SHStrDupW:
    adrp    x16, __imp_SHStrDupW
    ldr     x16, [x16, :lo12:__imp_SHStrDupW]
    br      x16
