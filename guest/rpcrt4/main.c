/* rpcrt4 entry point. */
#include "rpcrt4_private.h"

int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    switch (reason) {
    case DLL_THREAD_DETACH:
        RpcpThreadDetach();
        break;
    case DLL_PROCESS_DETACH:
        if (!reserved)
            RpcpEpPurge(GetCurrentProcessId());
        break;
    }
    return TRUE;
}


/* always reports that no call is being served. */
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcOpenClientProcess(void *binding, ULONG access, void **process)
{
    if (process)
        *process = nullptr;
    return RPC_S_NO_CALL_ACTIVE;
}

/* exceptions that mean the process is in trouble, which a server must not swallow. */
static const ULONG g_fatal[] = {0xC0000005, 0xC0000194, 0xC00000AA, 0x80000002, 0xC0000096, 0xC000001D, 0x80000003,
                                0xC00000FD, 0xC0000235, 0xC0000006, 0xC0000420, 0xC0000409, 0x80000001, 0xC00002C9};

/* handles every exception except the fatal ones. */
RPCRTAPI int RPC_ENTRY I_RpcExceptionFilter(ULONG code)
{
    for (unsigned i = 0; i < sizeof(g_fatal) / sizeof(g_fatal[0]); i++)
        if (g_fatal[i] == code)
            return EXCEPTION_CONTINUE_SEARCH;
    return EXCEPTION_EXECUTE_HANDLER;
}

RPCRTAPI int RPC_ENTRY RpcExceptionFilter(ULONG code)
{
    return I_RpcExceptionFilter(code);
}

/* raised noncontinuable, an access violation becomes ERROR_NOACCESS. */
RPCRTAPI DECLSPEC_NORETURN void RPC_ENTRY RpcRaiseException(RPC_STATUS status)
{
    RaiseException(status == (RPC_STATUS)0xC0000005 ? 998 : (DWORD)status, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    for (;;)
        ;
}

/* rpc or win32 status to NTSTATUS. */
static const struct {
    ULONG Status;
    ULONG Nt;
} g_status_map[] = {
    {0, 0x00000000u}, {1700, 0xC0020001u}, {1701, 0xC0020002u}, {1702, 0xC0020003u},
    {1703, 0xC0020004u}, {1704, 0xC0020005u}, {1705, 0xC0020006u}, {1706, 0xC0020007u},
    {1707, 0xC0020008u}, {1708, 0xC0020009u}, {1709, 0xC002000Au}, {1710, 0xC002000Bu},
    {1711, 0xC002000Cu}, {1712, 0xC002000Du}, {1713, 0xC002000Eu}, {1714, 0xC002000Fu},
    {1715, 0xC0020010u}, {1716, 0xC0020011u}, {1717, 0xC0020012u}, {1718, 0xC0020013u},
    {1806, 0xC002004Au}, {1719, 0xC0020014u}, {1720, 0xC0020015u}, {1721, 0xC0020016u},
    {1722, 0xC0020017u}, {1723, 0xC0020018u}, {1724, 0xC0020019u}, {1725, 0xC002001Au},
    {1726, 0xC002001Bu}, {1818, 0xC0020050u}, {1727, 0xC002001Cu}, {1728, 0xC002001Du},
    {1730, 0xC002001Fu}, {1130, 0xC0000205u}, {1732, 0xC0020021u}, {1733, 0xC0020022u},
    {1734, 0xC0020023u}, {1735, 0xC0020024u}, {1736, 0xC0020025u}, {1737, 0xC0020026u},
    {1739, 0xC0020028u}, {1740, 0xC0020029u}, {1741, 0xC002002Au}, {1742, 0xC002002Bu},
    {1743, 0xC002002Cu}, {1744, 0xC002002Du}, {1745, 0xC002002Eu}, {1746, 0xC002002Fu},
    {1747, 0xC0020030u}, {1748, 0xC0020031u}, {1749, 0xC0020032u}, {1750, 0xC0020033u},
    {1751, 0xC0020034u}, {1752, 0xC0020035u}, {1753, 0xC0020036u}, {1754, 0xC0020037u},
    {1755, 0xC0020038u}, {1756, 0xC0020039u}, {1757, 0xC002003Au}, {1758, 0xC002003Bu},
    {1759, 0xC002003Cu}, {1760, 0xC002003Du}, {1761, 0xC002003Eu}, {1762, 0xC002003Fu},
    {1763, 0xC0020040u}, {1764, 0xC0020041u}, {1765, 0xC0020042u}, {1766, 0xC0020043u},
    {1767, 0xC0020044u}, {1768, 0xC0020045u}, {1769, 0xC0020046u}, {1770, 0xC0020047u},
    {1771, 0xC0020048u}, {1772, 0xC0030001u}, {1773, 0xC0030002u}, {1774, 0xC0030003u},
    {1775, 0xC0030004u}, {6, 0xC0030005u}, {1777, 0xC0030006u}, {1778, 0xC0030007u},
    {1779, 0xC0030008u}, {1780, 0xC0030009u}, {1781, 0xC003000Au}, {1782, 0xC003000Bu},
    {1783, 0xC003000Cu}, {87, 0xC000000Du}, {14, 0xC0000017u}, {164, 0xC0000017u},
    {122, 0xC0000023u}, {1338, 0xC0000079u}, {5, 0xC0000022u}, {998, 0xC0000005u},
    {1791, 0xC0020049u}, {1898, 0xC002004Bu}, {1899, 0xC002004Cu}, {1900, 0xC002004Du},
    {1914, 0xC0020062u}, {1915, 0xC0020063u}, {1916, 0xC003005Fu}, {1918, 0xC0030061u},
    {1917, 0xC0030060u}, {1, 0x00000001u}, {1907, 0xC0000224u}, {1330, 0xC0000071u},
    {1331, 0xC0000072u}, {1328, 0xC000006Fu}, {1909, 0xC0000234u}, {1323, 0xC000006Au},
};

/* a negative status is already an NTSTATUS, any other without an entry is STATUS_UNSUCCESSFUL. */
RPCRTAPI LONG RPC_ENTRY I_RpcMapWin32Status(RPC_STATUS status)
{
    for (unsigned i = 0; i < sizeof(g_status_map) / sizeof(g_status_map[0]); i++)
        if (g_status_map[i].Status == (ULONG)status)
            return (LONG)g_status_map[i].Nt;
    return status < 0 ? status : (LONG)0xC0000001;
}

/* the ndr engine allocates through ole32's task allocator, loaded on first use. */
static void *(WINAPI *g_task_alloc)(SIZE_T);
static void(WINAPI *g_task_free)(void *);

static BOOL load_task_allocator(void)
{
    if (!g_task_alloc || !g_task_free) {
        HMODULE ole32 = LoadLibraryW(L"ole32.dll");
        if (!ole32)
            return FALSE;
        g_task_free = (void(WINAPI *)(void *))GetProcAddress(ole32, "CoTaskMemFree");
        g_task_alloc = (void *(WINAPI *)(SIZE_T))GetProcAddress(ole32, "CoTaskMemAlloc");
    }
    return g_task_alloc && g_task_free;
}

RPCRTAPI void *RPC_ENTRY NdrOleAllocate(SIZE_T size)
{
    return load_task_allocator() ? g_task_alloc(size) : nullptr;
}

RPCRTAPI void RPC_ENTRY NdrOleFree(void *memory)
{
    if (load_task_allocator())
        g_task_free(memory);
}

#define RPC_ASYNC_SIGNATURE 0x43594E41u

RPCRTAPI RPC_STATUS RPC_ENTRY RpcAsyncInitializeHandle(PRPC_ASYNC_STATE state, unsigned int size)
{
    if (!state || size != sizeof(*state))
        return ERROR_INVALID_PARAMETER;
    memset(state, 0, sizeof(*state));
    state->Size = size;
    state->Signature = RPC_ASYNC_SIGNATURE;
    return RPC_S_OK;
}
