/*
 * builds results twice, first to measure and then to fill the caller's buffer. structures grow from the start;
 * strings grow from the end.
 */
#include "winspool_private.h"

static SIZE_T align_up(SIZE_T n, SIZE_T a)
{
    return (n + a - 1) & ~(a - 1);
}

void SpoolPackBegin(SpoolPack *p, BYTE *buffer, SIZE_T capacity, BOOL measure, BOOL ansi)
{
    p->base = buffer;
    p->capacity = capacity;
    p->head = 0;
    p->tail = 0;
    p->ansi = ansi;
    p->measure = measure;
}

void *SpoolPackStruct(SpoolPack *p, SIZE_T size)
{
    SIZE_T at = align_up(p->head, sizeof(void *));
    p->head = at + align_up(size, sizeof(void *));
    return p->measure ? nullptr : p->base + at;
}

LPVOID SpoolPackBlob(SpoolPack *p, const void *data, SIZE_T size)
{
    if (!data)
        return nullptr;
    p->tail = align_up(p->tail + size, sizeof(void *));
    if (p->measure)
        return nullptr;
    BYTE *at = p->base + p->capacity - p->tail;
    memcpy(at, data, size);
    return at;
}

LPVOID SpoolPackString(SpoolPack *p, LPCWSTR text)
{
    if (!text)
        return nullptr;
    if (!p->ansi)
        return SpoolPackBlob(p, text, (wcslen(text) + 1) * sizeof(WCHAR));
    int n = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        n = 1;
    p->tail = align_up(p->tail + (SIZE_T)n, 2);
    if (p->measure)
        return nullptr;
    BYTE *at = p->base + p->capacity - p->tail;
    at[0] = 0;
    WideCharToMultiByte(CP_ACP, 0, text, -1, (LPSTR)at, n, nullptr, nullptr);
    return at;
}

SIZE_T SpoolPackNeeded(const SpoolPack *p)
{
    return align_up(p->head, sizeof(void *)) + p->tail;
}
