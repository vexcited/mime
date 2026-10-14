#include "kernelbase_private.h"

DECLSPEC_EXPORT PVOID WINAPI RtlPcToFileHeader(PVOID pc, PVOID *base)
{
    if (!base)
        return NULL;
    PLDR_DATA_TABLE_ENTRY entry;
    *base = NT_SUCCESS(LdrFindEntryForAddress(pc, &entry)) ? entry->DllBase : NULL;
    return *base;
}
