/* memory and checksums of the servicing library. */
#include "cbs.h"
#ifdef _WIN32
#include <mime/win32/winbase.h>
#include <rtl.h>
#else
#include <stdlib.h>
#include <string.h>
#endif

void *CbsAlloc(size_t size)
{
#ifdef _WIN32
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
#else
    return malloc(size ? size : 1);
#endif
}

void *CbsAllocZero(size_t size)
{
#ifdef _WIN32
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
#else
    return calloc(1, size ? size : 1);
#endif
}

void CbsFree(void *memory)
{
    if (!memory)
        return;
#ifdef _WIN32
    HeapFree(GetProcessHeap(), 0, memory);
#else
    free(memory);
#endif
}

void *CbsAllocLarge(size_t size)
{
#ifdef _WIN32
    return VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    return malloc(size);
#endif
}

void CbsFreeLarge(void *memory, size_t size)
{
    if (!memory)
        return;
#ifdef _WIN32
    (void)size;
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    (void)size;
    free(memory);
#endif
}

/* ------------------------------------------------------------------ crc-32 */

static uint32_t g_crc_table[8][256];
static bool g_crc_ready;

static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crc_table[0][i] = c;
    }
    for (uint32_t i = 0; i < 256; i++)
        for (int t = 1; t < 8; t++)
            g_crc_table[t][i] = (g_crc_table[t - 1][i] >> 8) ^ g_crc_table[0][g_crc_table[t - 1][i] & 0xFF];
    g_crc_ready = true;
}

uint32_t CbsCrc32(uint32_t crc, const void *data, size_t length)
{
    if (!g_crc_ready)
        crc_init();
    const uint8_t *p = data;
    crc = ~crc;
    while (length >= 8) {
        uint32_t a = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        a ^= crc;
        crc = g_crc_table[7][a & 0xFF] ^ g_crc_table[6][(a >> 8) & 0xFF] ^ g_crc_table[5][(a >> 16) & 0xFF] ^
              g_crc_table[4][a >> 24] ^ g_crc_table[3][p[4]] ^ g_crc_table[2][p[5]] ^ g_crc_table[1][p[6]] ^
              g_crc_table[0][p[7]];
        p += 8;
        length -= 8;
    }
    while (length--)
        crc = g_crc_table[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
