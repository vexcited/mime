#include "ntdll_private.h"

NTDLL_EXPORT void NTAPI RtlInitializeBitMap(PRTL_BITMAP bitmap, PULONG buffer, ULONG size)
{
    bitmap->SizeOfBitMap = size;
    bitmap->Buffer = buffer;
}

static void modify_bits(PRTL_BITMAP bitmap, ULONG start, ULONG count, BOOL set)
{
    if (!count)
        return;
    PULONG word = bitmap->Buffer + start / 32;
    ULONG offset = start % 32;
    while (count) {
        ULONG width = count < 32 - offset ? count : 32 - offset;
        ULONG mask = (0xFFFFFFFFu >> (32 - width)) << offset;
        if (set)
            *word |= mask;
        else
            *word &= ~mask;
        word++;
        offset = 0;
        count -= width;
    }
}

NTDLL_EXPORT void NTAPI RtlSetBits(PRTL_BITMAP bitmap, ULONG start, ULONG count)
{
    modify_bits(bitmap, start, count, TRUE);
}

NTDLL_EXPORT void NTAPI RtlClearBits(PRTL_BITMAP bitmap, ULONG start, ULONG count)
{
    modify_bits(bitmap, start, count, FALSE);
}

static ULONG clear_run(PRTL_BITMAP bitmap, ULONG start, ULONG end, ULONG count)
{
    ULONG run = 0;
    for (ULONG bit = start; bit < end; bit++) {
        if (bitmap->Buffer[bit / 32] & (1u << (bit % 32))) {
            run = 0;
        } else if (++run == count) {
            return bit - count + 1;
        }
    }
    return 0xFFFFFFFFu;
}

static ULONG find_clear(PRTL_BITMAP bitmap, ULONG count, ULONG hint)
{
    ULONG size = bitmap->SizeOfBitMap;
    if (hint >= size)
        hint = 0;
    if (!count)
        return hint & ~7u;
    if (count > size)
        return 0xFFFFFFFFu;
    ULONG found = clear_run(bitmap, hint, size, count);
    if (found == 0xFFFFFFFFu && hint) {
        ULONG end = count > size - hint ? size : hint + count;
        found = clear_run(bitmap, 0, end, count);
    }
    return found;
}

NTDLL_EXPORT ULONG NTAPI RtlFindClearBits(PRTL_BITMAP bitmap, ULONG count, ULONG hint)
{
    return find_clear(bitmap, count, hint);
}

NTDLL_EXPORT ULONG NTAPI RtlFindClearBitsAndSet(PRTL_BITMAP bitmap, ULONG count, ULONG hint)
{
    ULONG found = find_clear(bitmap, count, hint);
    if (found != 0xFFFFFFFFu)
        RtlSetBits(bitmap, found, count);
    return found;
}

NTDLL_EXPORT void NTAPI RtlSetBit(PRTL_BITMAP bitmap, ULONG bit)
{
    bitmap->Buffer[bit / 32] |= 1u << (bit % 32);
}

NTDLL_EXPORT void NTAPI RtlClearBit(PRTL_BITMAP bitmap, ULONG bit)
{
    bitmap->Buffer[bit / 32] &= ~(1u << (bit % 32));
}

NTDLL_EXPORT BOOLEAN NTAPI RtlTestBit(PRTL_BITMAP bitmap, ULONG bit)
{
    return bit < bitmap->SizeOfBitMap && (bitmap->Buffer[bit / 32] & (1u << (bit % 32))) != 0;
}
