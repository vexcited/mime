/*
 * small helpers of shlwapi that shell code keeps in its ordinals: the growing arrays of the FDSA set, timers on the
 * system timer queue and the fitting of a picture in a rectangle.
 */
#include "shlwapi_private.h"

/* the array: the count of items, the items, how many fit, and how many more to make room for at a time. */
typedef struct {
    int count;
    void *items;
    int capacity;
    union {
        struct {
            signed char grow, item_size;
        };
        DWORD flags;
    };
} FDSA;

/* set when the items were allocated here and not given by the caller. */
#define FDSA_OWNED 0x10000

BOOL WINAPI FDSA_Initialize(int item_size, int grow, FDSA *array, void *buffer, int capacity);
BOOL WINAPI FDSA_Destroy(FDSA *array);
int WINAPI FDSA_InsertItem(FDSA *array, int index, const void *item);
BOOL WINAPI FDSA_DeleteItem(FDSA *array, int index);

BOOL WINAPI FDSA_Initialize(int item_size, int grow, FDSA *array, void *buffer, int capacity)
{
    int step = grow ? grow : 1;
    if (capacity % step)
        return FALSE;
    if (buffer)
        memset(buffer, 0, capacity * item_size);
    if (array) {
        array->count = 0;
        array->flags &= ~FDSA_OWNED;
        array->item_size = (signed char)item_size;
        array->grow = (signed char)step;
        array->capacity = capacity;
        array->items = buffer;
    }
    return TRUE;
}

BOOL WINAPI FDSA_Destroy(FDSA *array)
{
    if (!array || !(array->flags & FDSA_OWNED) || !array->items)
        return TRUE;
    return LocalFree(array->items) == nullptr;
}

int WINAPI FDSA_InsertItem(FDSA *array, int index, const void *item)
{
    if (index < 0)
        return -1;
    if (index > array->count)
        index = array->count;
    if (array->count + 1 > array->capacity) {
        UINT capacity = array->capacity + array->grow;
        if (capacity < (UINT)array->capacity || (ULONGLONG)capacity * (ULONG)array->item_size > 0xFFFFFFFF)
            return -1;
        void *items;
        if (array->flags & FDSA_OWNED) {
            items = LocalReAlloc(array->items, capacity * array->item_size, LMEM_MOVEABLE | LMEM_ZEROINIT);
        } else {
            items = LocalAlloc(LMEM_ZEROINIT, capacity * array->item_size);
            if (items)
                memcpy(items, array->items, array->count * array->item_size);
        }
        if (!items)
            return -1;
        array->flags |= FDSA_OWNED;
        array->capacity += array->grow;
        array->items = items;
    }
    BYTE *bytes = array->items;
    if (index < array->count)
        memmove(bytes + (index + 1) * array->item_size, bytes + index * array->item_size,
                (array->count - index) * array->item_size);
    array->count++;
    memcpy(bytes + index * array->item_size, item, array->item_size);
    return index;
}

BOOL WINAPI FDSA_DeleteItem(FDSA *array, int index)
{
    if (index < 0 || index >= array->count)
        return FALSE;
    BYTE *bytes = array->items;
    if (index < array->count - 1)
        memmove(bytes + index * array->item_size, bytes + (index + 1) * array->item_size,
                (array->count - index - 1) * array->item_size);
    array->count--;
    return TRUE;
}

/* ------------------------------------------------------------------ timers */

#define WT_EXECUTELONGFUNCTION_ 0x10

WINBASEAPI BOOL WINAPI CreateTimerQueueTimer(PHANDLE timer, HANDLE queue, WAITORTIMERCALLBACK callback, PVOID context,
                                             DWORD due, DWORD period, ULONG flags);
WINBASEAPI BOOL WINAPI DeleteTimerQueueTimer(HANDLE queue, HANDLE timer, HANDLE event);

HANDLE WINAPI SHSetTimerQueueTimer(HANDLE queue, WAITORTIMERCALLBACK callback, void *context, DWORD due, DWORD period,
                                   LPCSTR name, DWORD flags);
HANDLE WINAPI SHSetTimerQueueTimer(HANDLE queue, WAITORTIMERCALLBACK callback, void *context, DWORD due, DWORD period,
                                   LPCSTR name, DWORD flags)
{
    ULONG options = (flags & 1) | ((flags & 8) ? WT_EXECUTELONGFUNCTION_ : 0);
    HANDLE timer = nullptr;
    return CreateTimerQueueTimer(&timer, queue, callback, context, due, period, options) ? timer : nullptr;
}

BOOL WINAPI SHCancelTimerQueueTimer(HANDLE queue, HANDLE timer);
BOOL WINAPI SHCancelTimerQueueTimer(HANDLE queue, HANDLE timer)
{
    return DeleteTimerQueueTimer(queue, timer, INVALID_HANDLE_VALUE);
}

/* ------------------------------------------------------------------ pictures */

/* fits the size of the rectangle in the target, to the middle when asked, and leaves the rectangle holding the result. */
void WINAPI CalculateAspectRatio(const SIZE *target, BOOL center, RECT *rect);
void WINAPI CalculateAspectRatio(const SIZE *target, BOOL center, RECT *rect)
{
    int height = rect->top - rect->bottom;
    if (height < 0)
        height = -height;
    int width = rect->right - rect->left;
    if (width < 0)
        width = -width;
    rect->left = 0;
    rect->top = 0;
    rect->right = width;
    rect->bottom = height;
    int ratio_width = MulDiv(width, 1000, target->cx);
    int ratio_height = MulDiv(height, 1000, target->cy);
    if (ratio_width > ratio_height) {
        rect->right = target->cx;
        int fitted = MulDiv(height, 1000, ratio_width);
        if (!fitted)
            fitted = 1;
        int offset = center ? (target->cy - fitted) / 2 : 0;
        rect->top = offset;
        rect->bottom = offset + fitted;
    } else {
        rect->bottom = target->cy;
        int fitted = MulDiv(width, 1000, ratio_height);
        if (!fitted)
            fitted = 1;
        int offset = center ? (target->cx - fitted) / 2 : 0;
        rect->left = offset;
        rect->right = offset + fitted;
    }
}
