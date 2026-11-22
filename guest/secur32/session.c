#include "schannel.h"

/*
 * schannel's client session cache, CSessionCacheTable::LookupCacheByName. an entry belongs to the credential that made
 * it, through the random tag CCredentialGroup draws, and to the exact target name. the newest entry wins. a tls 1.3
 * ticket serves once, a tls 1.2 session until it ages out of ClientCacheTime, ten hours.
 */
#define CLIENT_CACHE_MS (10ull * 60 * 60 * 1000)
/* MaximumCacheSize. */
#define CACHE_MAX 20000

static RTL_CRITICAL_SECTION g_lock = RTL_CRITICAL_SECTION_INIT;
static TLS_SESSION *g_sessions;
static ULONG g_count;

static PWSTR copy_target(PCWSTR target)
{
    if (!target)
        target = L"";
    SIZE_T bytes = (wcslen(target) + 1) * sizeof(WCHAR);
    PWSTR copy = SecHeapAlloc(bytes);
    if (copy)
        memcpy(copy, target, bytes);
    return copy;
}

TLS_SESSION *TlsSessionNew(UINT64 tag, PCWSTR target)
{
    TLS_SESSION *s = SecHeapAlloc(sizeof(*s));
    if (!s)
        return nullptr;
    memset(s, 0, sizeof(*s));
    s->Tag = tag;
    s->Target = copy_target(target);
    if (!s->Target) {
        SecHeapFree(s);
        return nullptr;
    }
    return s;
}

void TlsSessionFree(TLS_SESSION *s)
{
    if (!s)
        return;
    SecHeapFree(s->Target);
    SecHeapFree(s->Ticket);
    SchFreeCertificates(&s->Certs);
    mime_wipe(s, sizeof(*s));
    SecHeapFree(s);
}

static TLS_SESSION *copy_session(const TLS_SESSION *from)
{
    TLS_SESSION *s = SecHeapAlloc(sizeof(*s));
    if (!s)
        return nullptr;
    *s = *from;
    s->Next = nullptr;
    s->Target = copy_target(from->Target);
    s->Ticket = nullptr;
    memset(&s->Certs, 0, sizeof(s->Certs));
    if (from->TicketSize) {
        s->Ticket = SecHeapAlloc(from->TicketSize);
        if (s->Ticket)
            memcpy(s->Ticket, from->Ticket, from->TicketSize);
    }
    SchCopyCertificates(&s->Certs, &from->Certs);
    if (!s->Target || (from->TicketSize && !s->Ticket) || (from->Certs.Leaf && !s->Certs.Leaf)) {
        TlsSessionFree(s);
        return nullptr;
    }
    return s;
}

static bool expired(const TLS_SESSION *s, ULONGLONG now)
{
    ULONGLONG age = now - s->Created;
    if (age >= CLIENT_CACHE_MS)
        return true;
    return s->Version == 0x0304 && age >= (ULONGLONG)s->Lifetime * 1000;
}

/* the caller holds g_lock. */
static void unlink_at(TLS_SESSION **link)
{
    TLS_SESSION *s = *link;
    *link = s->Next;
    g_count--;
    TlsSessionFree(s);
}

TLS_SESSION *TlsSessionFind(UINT64 tag, PCWSTR target, ULONG protocols, bool (*usable)(const TLS_SESSION *, void *),
                            void *context)
{
    if (!target)
        target = L"";
    ULONGLONG now = GetTickCount64();
    TLS_SESSION *found = nullptr;
    RtlEnterCriticalSection(&g_lock);
    for (TLS_SESSION **link = &g_sessions; *link;) {
        TLS_SESSION *s = *link;
        if (expired(s, now)) {
            unlink_at(link);
            continue;
        }
        ULONG bit = s->Version == 0x0304 ? SP_PROT_TLS1_3_CLIENT : SP_PROT_TLS1_2_CLIENT;
        if (s->Tag == tag && (protocols & bit) && !wcscmp(s->Target, target) && usable(s, context)) {
            found = copy_session(s);
            if (found && s->Version == 0x0304)
                unlink_at(link);
            break;
        }
        link = &s->Next;
    }
    RtlLeaveCriticalSection(&g_lock);
    return found;
}

/* a tls 1.2 session the server renewed the ticket of keeps its entry, CSessionCacheClientItem::SetSessionTicket. */
static bool same_session(const TLS_SESSION *a, const TLS_SESSION *b)
{
    return a->Version == 0x0303 && b->Version == 0x0303 && a->Tag == b->Tag && !wcscmp(a->Target, b->Target) &&
           mime_ct_equal(a->Secret, b->Secret, sizeof(a->Secret));
}

void TlsSessionAdd(TLS_SESSION *s)
{
    s->Created = GetTickCount64();
    RtlEnterCriticalSection(&g_lock);
    for (TLS_SESSION **link = &g_sessions; *link; link = &(*link)->Next) {
        if (same_session(*link, s)) {
            s->Created = (*link)->Created;
            unlink_at(link);
            break;
        }
    }
    s->Next = g_sessions;
    g_sessions = s;
    g_count++;
    if (g_count > CACHE_MAX) {
        TLS_SESSION **link = &g_sessions;
        while ((*link)->Next)
            link = &(*link)->Next;
        unlink_at(link);
    }
    RtlLeaveCriticalSection(&g_lock);
}

/* SCHANNEL_SESSION_TOKEN with SSL_SESSION_DISABLE_RECONNECTS: the session of a context is not offered again. */
void TlsSessionForget(UINT64 tag, PCWSTR target, const BYTE *origin)
{
    if (!target)
        target = L"";
    RtlEnterCriticalSection(&g_lock);
    for (TLS_SESSION **link = &g_sessions; *link;) {
        TLS_SESSION *s = *link;
        if (s->Tag == tag && !wcscmp(s->Target, target) && mime_ct_equal(s->Origin, origin, sizeof(s->Origin)))
            unlink_at(link);
        else
            link = &s->Next;
    }
    RtlLeaveCriticalSection(&g_lock);
}

/* CSessionCacheManager::PurgeCacheForCredentialGroup, when the credential goes. */
void TlsSessionPurge(UINT64 tag)
{
    RtlEnterCriticalSection(&g_lock);
    for (TLS_SESSION **link = &g_sessions; *link;) {
        if ((*link)->Tag == tag)
            unlink_at(link);
        else
            link = &(*link)->Next;
    }
    RtlLeaveCriticalSection(&g_lock);
}
