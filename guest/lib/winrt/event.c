#include "winrt.h"

static volatile LONG64 g_tokens;

HRESULT WrtEventAdd(WRT_EVENT *event, IWrtEventHandler *handler, EventRegistrationToken *token)
{
    if (!token)
        return E_POINTER;
    if (!handler)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&event->Lock);
    if (event->Count == event->Capacity) {
        UINT32 capacity = event->Capacity ? event->Capacity * 2 : 4;
        void *entries = event->Entries
                            ? HeapReAlloc(GetProcessHeap(), 0, event->Entries, capacity * sizeof(*event->Entries))
                            : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*event->Entries));
        if (!entries) {
            ReleaseSRWLockExclusive(&event->Lock);
            return E_OUTOFMEMORY;
        }
        event->Entries = entries;
        event->Capacity = capacity;
    }
    handler->lpVtbl->AddRef(handler);
    INT64 value = InterlockedIncrement64(&g_tokens);
    event->Entries[event->Count].Token = value;
    event->Entries[event->Count].Handler = handler;
    event->Count++;
    ReleaseSRWLockExclusive(&event->Lock);
    token->value = value;
    return S_OK;
}

HRESULT WrtEventRemove(WRT_EVENT *event, EventRegistrationToken token)
{
    IWrtEventHandler *handler = nullptr;
    AcquireSRWLockExclusive(&event->Lock);
    for (UINT32 i = 0; i < event->Count; i++) {
        if (event->Entries[i].Token == token.value) {
            handler = event->Entries[i].Handler;
            memmove(&event->Entries[i], &event->Entries[i + 1], (event->Count - i - 1) * sizeof(*event->Entries));
            event->Count--;
            break;
        }
    }
    ReleaseSRWLockExclusive(&event->Lock);
    if (handler)
        handler->lpVtbl->Release(handler);
    return S_OK;
}

/* handlers added or removed by a handler take effect from the next raise. */
void WrtEventInvoke(WRT_EVENT *event, IInspectable *sender, IInspectable *args)
{
    AcquireSRWLockShared(&event->Lock);
    UINT32 count = event->Count;
    IWrtEventHandler **handlers = count ? HeapAlloc(GetProcessHeap(), 0, count * sizeof(*handlers)) : nullptr;
    for (UINT32 i = 0; handlers && i < count; i++) {
        handlers[i] = event->Entries[i].Handler;
        handlers[i]->lpVtbl->AddRef(handlers[i]);
    }
    ReleaseSRWLockShared(&event->Lock);
    if (!handlers)
        return;
    for (UINT32 i = 0; i < count; i++) {
        handlers[i]->lpVtbl->Invoke(handlers[i], sender, args);
        handlers[i]->lpVtbl->Release(handlers[i]);
    }
    HeapFree(GetProcessHeap(), 0, handlers);
}

void WrtEventClear(WRT_EVENT *event)
{
    for (UINT32 i = 0; i < event->Count; i++)
        event->Entries[i].Handler->lpVtbl->Release(event->Entries[i].Handler);
    if (event->Entries)
        HeapFree(GetProcessHeap(), 0, event->Entries);
    event->Entries = nullptr;
    event->Count = event->Capacity = 0;
}

UINT32 WrtEventCount(WRT_EVENT *event)
{
    AcquireSRWLockShared(&event->Lock);
    UINT32 count = event->Count;
    ReleaseSRWLockShared(&event->Lock);
    return count;
}
