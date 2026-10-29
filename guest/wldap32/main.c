/*
 * wldap32.dll: the LDAP client api. there is no network transport for LDAP here, so a handle can be made and given
 * options but never connects: every bind and search finds the server down, the way it is when a directory cannot be
 * reached. the calls that read results see no result, and the ones that free memory free what the library made, which
 * is the handle, the controls it builds and the strings of its errors.
 */
#include <mime/win32/winbase.h>
#include <rtl.h>

typedef ULONG_PTR ARG;

#define LDAP_SUCCESS 0x00
#define LDAP_SERVER_DOWN 0x51
#define LDAP_LOCAL_ERROR 0x52
#define LDAP_ENCODING_ERROR 0x53
#define LDAP_PARAM_ERROR 0x59
#define LDAP_NO_MEMORY 0x5a
#define LDAP_CONNECT_ERROR 0x5b
#define LDAP_NOT_SUPPORTED 0x5c
#define LDAP_NO_RESULTS_RETURNED 0x5e
#define LDAP_OPT_PROTOCOL_VERSION 0x11
#define LDAP_OPT_HOST_NAME 0x30
#define LDAP_OPT_ERROR_NUMBER 0x31

typedef struct ldap {
    struct {
        ULONG_PTR sb_sd;
        UCHAR Reserved1[41];
        ULONG_PTR sb_naddr;
        UCHAR Reserved2[24];
    } ld_sb;
    PCHAR ld_host;
    ULONG ld_version;
    UCHAR ld_lberoptions;
    ULONG ld_deref;
    ULONG ld_timelimit;
    ULONG ld_sizelimit;
    ULONG ld_errno;
    PCHAR ld_matched;
    PCHAR ld_error;
    ULONG ld_msgid;
    UCHAR Reserved3[25];
    ULONG ld_cldaptries;
    ULONG ld_cldaptimeout;
    ULONG ld_refhopcount;
    ULONG ld_options;
} LDAP;

/* what the library keeps for a handle beyond what the structure shows. */
#define OPTION_COUNT 0xA0

typedef struct HANDLE_DATA {
    LDAP Public;
    struct HANDLE_DATA *Next;
    ULONG_PTR Options[OPTION_COUNT];
} HANDLE_DATA;

typedef struct {
    ULONG bv_len;
    PCHAR bv_val;
} BERVAL;

typedef struct {
    PCHAR ldctl_oid;
    BERVAL ldctl_value;
    BOOLEAN ldctl_iscritical;
} LDAPCONTROL;

static SRWLOCK g_lock = SRWLOCK_INIT;
static HANDLE_DATA *g_handles;
static DWORD g_error_slot = TLS_OUT_OF_INDEXES;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        g_error_slot = TlsAlloc();
    else if (reason == DLL_PROCESS_DETACH && g_error_slot != TLS_OUT_OF_INDEXES)
        TlsFree(g_error_slot);
    return TRUE;
}

static ULONG set_last_error(ULONG error)
{
    if (g_error_slot != TLS_OUT_OF_INDEXES)
        TlsSetValue(g_error_slot, (PVOID)(ULONG_PTR)(error + 1));
    return error;
}

/* the last error is 0x52, a local error, for a thread that never had one. */
ULONG LdapGetLastError(void)
{
    ULONG_PTR value = g_error_slot == TLS_OUT_OF_INDEXES ? 0 : (ULONG_PTR)TlsGetValue(g_error_slot);
    return value ? (ULONG)(value - 1) : LDAP_LOCAL_ERROR;
}

/* ------------------------------------------------------------------ error codes */

ULONG LdapMapErrorToWin32(ULONG error)
{
    switch (error) {
    case 0:
        return 0;
    case 1:
        return 110;
    case 2:
        return 124;
    case 3:
        return 1460;
    case 4:
    case 9:
    case 10:
    case 94:
    case 95:
        return 234;
    case 7:
    case 8:
    case 0x30:
    case 0x32:
    case 0x43:
    case 0x45:
        return 5;
    case 11:
        return 1816;
    case 12:
    case 0x35:
    case 0x42:
    case 0x47:
    case 0x5c:
        return 1003;
    case 16:
    case 0x12:
    case 0x13:
    case 0x22:
    case 0x40:
    case 0x41:
    case 0x57:
    case 0x59:
        return 87;
    case 0x14:
    case 0x44:
        return 183;
    case 0x15:
        return 123;
    case 0x20:
        return 2;
    case 0x31:
        return 1326;
    case 0x33:
        return 170;
    case 0x34:
        return 55;
    case 0x3c:
        return 8261;
    case 0x3d:
        return 8262;
    case 0x46:
        return 122;
    case 0x51:
        return 58;
    case 0x53:
    case 0x54:
        return 59;
    case 0x55:
        return 1053;
    case 0x56:
        return 1323;
    case 0x58:
        return 1223;
    case 0x5a:
        return 8;
    case 0x5b:
        return 1225;
    case 0x5d:
        return 1168;
    }
    return 8341;
}

static const char *const g_messages[] = {
    "Success", "Operations Error", "Protocol Error", "Timelimit Exceeded", "Sizelimit Exceeded", "Compare False",
    "Compare True", "Authentication Method Not Supported", "Strong Authentication Required", "Referral (v2)", "Referral",
    "Administrative Limit Exceeded", "Unavailable Critical Extension", "Confidentiality Required",
    "SASL Bind In Progress",
};

static const struct {
    ULONG Code;
    const char *Text;
} g_more_messages[] = {
    {0x10, "No Such Attribute"}, {0x11, "Undefined Attribute Type"}, {0x12, "Inappropriate Matching"},
    {0x13, "Constraint Violation"}, {0x14, "Attribute Or Value Exists"}, {0x15, "Invalid Syntax"},
    {0x20, "No Such Object"}, {0x21, "Alias Problem"}, {0x22, "Invalid DN Syntax"}, {0x23, "Is Leaf"},
    {0x24, "Alias Dereference Problem"}, {0x30, "Inappropriate Authentication"}, {0x31, "Invalid Credentials"},
    {0x32, "Insufficient Rights"}, {0x33, "Busy"}, {0x34, "Unavailable"}, {0x35, "Unwilling To Perform"},
    {0x36, "Loop Detected"}, {0x3c, "Sort Control Missing"}, {0x3d, "Index range error"}, {0x40, "Naming Violation"},
    {0x41, "Object Class Violation"}, {0x42, "Not allowed on Non-leaf"}, {0x43, "Not allowed on RDN"},
    {0x44, "Already Exists"}, {0x45, "No Object Class Mods"}, {0x46, "Results Too Large"},
    {0x47, "Affects Multiple DSAs"}, {0x4c, "Virtual List View Error"}, {0x50, "Other"}, {0x51, "Server Down"},
    {0x52, "Local Error"}, {0x53, "Encoding Error"}, {0x54, "Decoding Error"}, {0x55, "Timeout"},
    {0x56, "Auth Unknown"}, {0x57, "Filter Error"}, {0x58, "User Cancelled"}, {0x59, "Parameter Error"},
    {0x5a, "No Memory"}, {0x5b, "Can't connect to the LDAP server"},
    {0x5c, "Operation not supported by this version of the LDAP protocol"}, {0x5d, "Control not found"},
    {0x5e, "No result message returned"}, {0x5f, "More results to return"}, {0x60, "Client Loop"},
    {0x61, "Referral Limit Exceeded"},
};

static const char *error_text(ULONG error)
{
    if (error < RTL_NUMBER_OF(g_messages))
        return g_messages[error];
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_more_messages); i++)
        if (g_more_messages[i].Code == error)
            return g_more_messages[i].Text;
    return "Unknown Error";
}

PCHAR ldap_err2stringA(ULONG error)
{
    return (PCHAR)error_text(error);
}

PCHAR ldap_err2string(ULONG error)
{
    return (PCHAR)error_text(error);
}

PWCHAR ldap_err2stringW(ULONG error)
{
    static WCHAR text[96][64];
    ULONG slot = error < 96 ? error : 95;
    if (!text[slot][0]) {
        const char *narrow = error_text(error);
        for (int i = 0; narrow[i] && i < 63; i++)
            text[slot][i] = (WCHAR)(BYTE)narrow[i];
    }
    return text[slot];
}

void ldap_perror(LDAP *ld, const PCHAR message)
{
}

/* ------------------------------------------------------------------ handles */

static HANDLE_DATA *handle_of(LDAP *ld)
{
    HANDLE_DATA *found = nullptr;
    AcquireSRWLockShared(&g_lock);
    for (HANDLE_DATA *h = g_handles; h; h = h->Next)
        if (&h->Public == ld) {
            found = h;
            break;
        }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

static LDAP *make_handle(const char *host, ULONG port)
{
    HANDLE_DATA *h = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*h));
    if (!h) {
        set_last_error(LDAP_NO_MEMORY);
        return nullptr;
    }
    SIZE_T length = host ? lstrlenA(host) : 0;
    h->Public.ld_host = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, length + 1);
    if (!h->Public.ld_host) {
        HeapFree(GetProcessHeap(), 0, h);
        set_last_error(LDAP_NO_MEMORY);
        return nullptr;
    }
    if (length)
        memcpy(h->Public.ld_host, host, length);
    h->Public.ld_version = 2;
    h->Options[0x11] = 2;
    h->Options[0x08] = 1;
    AcquireSRWLockExclusive(&g_lock);
    h->Next = g_handles;
    g_handles = h;
    ReleaseSRWLockExclusive(&g_lock);
    return &h->Public;
}

/* the host of a wide name is kept in the ansi code page, as the structure holds it. */
static LDAP *make_handle_wide(const WCHAR *host, ULONG port)
{
    char narrow[512] = {};
    if (host)
        WideCharToMultiByte(CP_ACP, 0, host, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
    return make_handle(narrow, port);
}

LDAP *ldap_sslinitA(PSTR host, ULONG port, int secure)
{
    return make_handle(host, port);
}

LDAP *ldap_sslinit(PSTR host, ULONG port, int secure)
{
    return make_handle(host, port);
}

LDAP *ldap_sslinitW(PWSTR host, ULONG port, int secure)
{
    return make_handle_wide(host, port);
}

LDAP *ldap_initA(PSTR host, ULONG port)
{
    return make_handle(host, port);
}

LDAP *ldap_init(PSTR host, ULONG port)
{
    return make_handle(host, port);
}

LDAP *ldap_initW(PWSTR host, ULONG port)
{
    return make_handle_wide(host, port);
}

/* opening connects at once, and nothing can be connected to. */
LDAP *ldap_openA(PSTR host, ULONG port)
{
    set_last_error(LDAP_SERVER_DOWN);
    return nullptr;
}

LDAP *ldap_open(PSTR host, ULONG port)
{
    set_last_error(LDAP_SERVER_DOWN);
    return nullptr;
}

LDAP *ldap_openW(PWSTR host, ULONG port)
{
    set_last_error(LDAP_SERVER_DOWN);
    return nullptr;
}

LDAP *cldap_openA(PSTR host, ULONG port)
{
    set_last_error(LDAP_SERVER_DOWN);
    return nullptr;
}

LDAP *cldap_open(PSTR host, ULONG port)
{
    set_last_error(LDAP_SERVER_DOWN);
    return nullptr;
}

LDAP *cldap_openW(PWSTR host, ULONG port)
{
    set_last_error(LDAP_SERVER_DOWN);
    return nullptr;
}

ULONG ldap_unbind(LDAP *ld)
{
    HANDLE_DATA *h = handle_of(ld);
    if (!h)
        return set_last_error(LDAP_PARAM_ERROR);
    AcquireSRWLockExclusive(&g_lock);
    for (HANDLE_DATA **at = &g_handles; *at; at = &(*at)->Next)
        if (*at == h) {
            *at = h->Next;
            break;
        }
    ReleaseSRWLockExclusive(&g_lock);
    HeapFree(GetProcessHeap(), 0, h->Public.ld_host);
    HeapFree(GetProcessHeap(), 0, h);
    return LDAP_SUCCESS;
}

ULONG ldap_unbind_s(LDAP *ld)
{
    return ldap_unbind(ld);
}

/* the failure of everything that needs the server, for the handle and the error code it leaves in it. */
static ULONG server_down(LDAP *ld)
{
    HANDLE_DATA *h = handle_of(ld);
    if (!h)
        return set_last_error(LDAP_PARAM_ERROR);
    h->Public.ld_errno = LDAP_SERVER_DOWN;
    return set_last_error(LDAP_SERVER_DOWN);
}

/* the calls that start an operation give back the number of a message, or -1 when it did not start. */
static ULONG start_failed(LDAP *ld)
{
    server_down(ld);
    return (ULONG)-1;
}

ULONG ldap_connect(LDAP *ld, PVOID timeout)
{
    return server_down(ld);
}

ULONG ldap_abandon(LDAP *ld, ULONG message)
{
    return handle_of(ld) ? LDAP_SUCCESS : set_last_error(LDAP_PARAM_ERROR);
}

/* ------------------------------------------------------------------ options */

static BOOL option_known(int option)
{
    return option >= 0 && option < OPTION_COUNT;
}

/* the options that are given and returned as the value itself and not through a pointer. */
static BOOL option_by_value(int option)
{
    switch (option) {
    case 0x08:
    case 0x0a:
    case 0x40:
    case 0x91:
    case 0x95:
    case 0x96:
    case 0x98:
    case 0x9a:
        return TRUE;
    }
    return FALSE;
}

ULONG ldap_set_optionW(LDAP *ld, int option, const void *value)
{
    HANDLE_DATA *h = handle_of(ld);
    if (!h || !option_known(option) || option == LDAP_OPT_HOST_NAME || option == LDAP_OPT_ERROR_NUMBER)
        return LDAP_PARAM_ERROR;
    if (option_by_value(option))
        h->Options[option] = (ULONG_PTR)value;
    else if (value)
        h->Options[option] = *(const ULONG *)value;
    else
        return LDAP_PARAM_ERROR;
    if (option == LDAP_OPT_PROTOCOL_VERSION)
        h->Public.ld_version = (ULONG)h->Options[option];
    return LDAP_SUCCESS;
}

ULONG ldap_set_optionA(LDAP *ld, int option, const void *value)
{
    return ldap_set_optionW(ld, option, value);
}

ULONG ldap_set_option(LDAP *ld, int option, const void *value)
{
    return ldap_set_optionW(ld, option, value);
}

ULONG ldap_get_optionW(LDAP *ld, int option, void *value)
{
    HANDLE_DATA *h = handle_of(ld);
    if (!value || !option_known(option) || (!h && option != 0))
        return LDAP_PARAM_ERROR;
    if (!h)
        return LDAP_PARAM_ERROR;
    if (option == LDAP_OPT_HOST_NAME)
        *(PCHAR *)value = h->Public.ld_host;
    else if (option == LDAP_OPT_ERROR_NUMBER)
        *(ULONG *)value = h->Public.ld_errno;
    else
        *(ULONG *)value = (ULONG)h->Options[option];
    return LDAP_SUCCESS;
}

ULONG ldap_get_optionA(LDAP *ld, int option, void *value)
{
    return ldap_get_optionW(ld, option, value);
}

ULONG ldap_get_option(LDAP *ld, int option, void *value)
{
    return ldap_get_optionW(ld, option, value);
}

/* ------------------------------------------------------------------ binds */

#define SYNC_2(name) ULONG name(LDAP *ld, ARG a) { return server_down(ld); }
#define SYNC_3(name) ULONG name(LDAP *ld, ARG a, ARG b) { return server_down(ld); }
#define SYNC_4(name) ULONG name(LDAP *ld, ARG a, ARG b, ARG c) { return server_down(ld); }
#define SYNC_5(name) ULONG name(LDAP *ld, ARG a, ARG b, ARG c, ARG d) { return server_down(ld); }
#define SYNC_6(name) ULONG name(LDAP *ld, ARG a, ARG b, ARG c, ARG d, ARG e) { return server_down(ld); }
#define SYNC_7(name) ULONG name(LDAP *ld, ARG a, ARG b, ARG c, ARG d, ARG e, ARG f) { return server_down(ld); }
#define SYNC_8(name) ULONG name(LDAP *ld, ARG a, ARG b, ARG c, ARG d, ARG e, ARG f, ARG g) { return server_down(ld); }
#define SYNC_11(name) ULONG name(LDAP *ld, ARG a, ARG b, ARG c, ARG d, ARG e, ARG f, ARG g, ARG h, ARG i, ARG j) { return server_down(ld); }
#define ASYNC_2(name) ULONG name(LDAP *ld, ARG a) { return start_failed(ld); }
#define ASYNC_3(name) ULONG name(LDAP *ld, ARG a, ARG b) { return start_failed(ld); }
#define ASYNC_4(name) ULONG name(LDAP *ld, ARG a, ARG b, ARG c) { return start_failed(ld); }
#define ASYNC_6(name) ULONG name(LDAP *ld, ARG a, ARG b, ARG c, ARG d, ARG e) { return start_failed(ld); }
#define ASYNC_7(name) ULONG name(LDAP *ld, ARG a, ARG b, ARG c, ARG d, ARG e, ARG f) { return start_failed(ld); }
#define ASYNC_8(name) ULONG name(LDAP *ld, ARG a, ARG b, ARG c, ARG d, ARG e, ARG f, ARG g) { return start_failed(ld); }
#define ASYNC_11(name) ULONG name(LDAP *ld, ARG a, ARG b, ARG c, ARG d, ARG e, ARG f, ARG g, ARG h, ARG i, ARG j) { return start_failed(ld); }

/* the binds take a distinguished name, credentials and for ldap_bind the method of authentication. */
ASYNC_4(ldap_bindA)
ASYNC_4(ldap_bindW)
ASYNC_4(ldap_bind)
SYNC_4(ldap_bind_sA)
SYNC_4(ldap_bind_sW)
SYNC_4(ldap_bind_s)
ASYNC_3(ldap_simple_bindA)
ASYNC_3(ldap_simple_bindW)
ASYNC_3(ldap_simple_bind)
SYNC_3(ldap_simple_bind_sA)
SYNC_3(ldap_simple_bind_sW)
SYNC_3(ldap_simple_bind_s)
/* the sasl binds take a mechanism, credentials, controls and the number or the data of the server. */
SYNC_7(ldap_sasl_bindA)
SYNC_7(ldap_sasl_bindW)
SYNC_7(ldap_sasl_bind_sA)
SYNC_7(ldap_sasl_bind_sW)

/* ------------------------------------------------------------------ searches and changes */

ASYNC_6(ldap_searchA)
ASYNC_6(ldap_searchW)
ASYNC_6(ldap_search)
SYNC_7(ldap_search_sA)
SYNC_7(ldap_search_sW)
SYNC_7(ldap_search_s)
SYNC_8(ldap_search_stA)
SYNC_8(ldap_search_stW)
SYNC_8(ldap_search_st)
SYNC_11(ldap_search_extA)
SYNC_11(ldap_search_extW)
SYNC_11(ldap_search_ext)
SYNC_11(ldap_search_ext_sA)
SYNC_11(ldap_search_ext_sW)
SYNC_11(ldap_search_ext_s)

ASYNC_3(ldap_addA)
ASYNC_3(ldap_addW)
ASYNC_3(ldap_add)
SYNC_3(ldap_add_sA)
SYNC_3(ldap_add_sW)
SYNC_3(ldap_add_s)
SYNC_6(ldap_add_extA)
SYNC_6(ldap_add_extW)
SYNC_6(ldap_add_ext)
SYNC_5(ldap_add_ext_sA)
SYNC_5(ldap_add_ext_sW)
SYNC_5(ldap_add_ext_s)
ASYNC_3(ldap_modifyA)
ASYNC_3(ldap_modifyW)
ASYNC_3(ldap_modify)
SYNC_3(ldap_modify_sA)
SYNC_3(ldap_modify_sW)
SYNC_3(ldap_modify_s)
ASYNC_2(ldap_deleteA)
ASYNC_2(ldap_deleteW)
ASYNC_2(ldap_delete)
SYNC_2(ldap_delete_sA)
SYNC_2(ldap_delete_sW)
SYNC_2(ldap_delete_s)
ASYNC_4(ldap_compareA)
ASYNC_4(ldap_compareW)
ASYNC_4(ldap_compare)
SYNC_4(ldap_compare_sA)
SYNC_4(ldap_compare_sW)
SYNC_4(ldap_compare_s)
ASYNC_3(ldap_modrdnA)
ASYNC_3(ldap_modrdnW)
ASYNC_3(ldap_modrdn)
SYNC_3(ldap_modrdn_sA)
SYNC_3(ldap_modrdn_sW)
SYNC_3(ldap_modrdn_s)

ULONG ldap_result(LDAP *ld, ULONG message, ULONG all, PVOID timeout, PVOID *result)
{
    if (result)
        *result = nullptr;
    server_down(ld);
    return (ULONG)-1;
}

ULONG ldap_msgfree(PVOID message)
{
    return LDAP_SUCCESS;
}

ULONG ldap_result2error(LDAP *ld, PVOID message, ULONG free_it)
{
    return handle_of(ld) ? LDAP_NO_RESULTS_RETURNED : LDAP_PARAM_ERROR;
}

/* ------------------------------------------------------------------ results: there are none */

PVOID ldap_first_entry(LDAP *ld, PVOID message)
{
    return nullptr;
}

PVOID ldap_next_entry(LDAP *ld, PVOID entry)
{
    return nullptr;
}

ULONG ldap_count_entries(LDAP *ld, PVOID message)
{
    return 0;
}

PVOID ldap_first_attributeA(LDAP *ld, PVOID entry, PVOID *element)
{
    return nullptr;
}

PVOID ldap_first_attributeW(LDAP *ld, PVOID entry, PVOID *element)
{
    return nullptr;
}

PVOID ldap_first_attribute(LDAP *ld, PVOID entry, PVOID *element)
{
    return nullptr;
}

PVOID ldap_next_attributeA(LDAP *ld, PVOID entry, PVOID element)
{
    return nullptr;
}

PVOID ldap_next_attributeW(LDAP *ld, PVOID entry, PVOID element)
{
    return nullptr;
}

PVOID ldap_next_attribute(LDAP *ld, PVOID entry, PVOID element)
{
    return nullptr;
}

PVOID ldap_get_dnA(LDAP *ld, PVOID entry)
{
    return nullptr;
}

PVOID ldap_get_dnW(LDAP *ld, PVOID entry)
{
    return nullptr;
}

PVOID ldap_get_dn(LDAP *ld, PVOID entry)
{
    return nullptr;
}

PVOID ldap_get_valuesA(LDAP *ld, PVOID entry, PVOID attribute)
{
    return nullptr;
}

PVOID ldap_get_valuesW(LDAP *ld, PVOID entry, PVOID attribute)
{
    return nullptr;
}

PVOID ldap_get_values(LDAP *ld, PVOID entry, PVOID attribute)
{
    return nullptr;
}

PVOID ldap_get_values_lenA(LDAP *ld, PVOID entry, PVOID attribute)
{
    return nullptr;
}

PVOID ldap_get_values_lenW(LDAP *ld, PVOID entry, PVOID attribute)
{
    return nullptr;
}

PVOID ldap_get_values_len(LDAP *ld, PVOID entry, PVOID attribute)
{
    return nullptr;
}

ULONG ldap_count_valuesA(PVOID values)
{
    return 0;
}

ULONG ldap_count_valuesW(PVOID values)
{
    return 0;
}

ULONG ldap_count_values(PVOID values)
{
    return 0;
}

ULONG ldap_count_values_len(PVOID values)
{
    return 0;
}

ULONG ldap_value_freeA(PVOID values)
{
    return LDAP_SUCCESS;
}

ULONG ldap_value_freeW(PVOID values)
{
    return LDAP_SUCCESS;
}

ULONG ldap_value_free(PVOID values)
{
    return LDAP_SUCCESS;
}

ULONG ldap_value_free_len(PVOID values)
{
    return LDAP_SUCCESS;
}

/* the memory that the library hands out is the controls and the errors, which are freed as they are made. */
void ldap_memfreeA(PCHAR memory)
{
}

void ldap_memfreeW(PWCHAR memory)
{
}

void ldap_memfree(PCHAR memory)
{
}

void ber_free(PVOID element, int free_buffer)
{
}

void ber_bvfree(BERVAL *value)
{
    if (value) {
        if (value->bv_val)
            HeapFree(GetProcessHeap(), 0, value->bv_val);
        HeapFree(GetProcessHeap(), 0, value);
    }
}

/* ------------------------------------------------------------------ the sort control */

#define SORT_OID "1.2.840.113556.1.4.473"

typedef struct {
    PVOID AttributeType;
    PVOID MatchingRule;
    BOOLEAN ReverseOrder;
} SORT_KEY;

static int ber_length(BYTE *out, ULONG length)
{
    if (length < 0x80) {
        out[0] = (BYTE)length;
        return 1;
    }
    int bytes = length > 0xFFFFFF ? 4 : length > 0xFFFF ? 3 : length > 0xFF ? 2 : 1;
    out[0] = (BYTE)(0x80 | bytes);
    for (int i = 0; i < bytes; i++)
        out[1 + i] = (BYTE)(length >> (8 * (bytes - 1 - i)));
    return 1 + bytes;
}

/* the keys are narrow strings in the ansi form and wide strings in the other, and both are sent as ansi. */
static ULONG create_sort_control(LDAP *ld, SORT_KEY **keys, BOOLEAN critical, LDAPCONTROL **control, BOOL wide)
{
    if (!handle_of(ld) || !keys || !control)
        return LDAP_PARAM_ERROR;
    BYTE *body = HeapAlloc(GetProcessHeap(), 0, 4096);
    LDAPCONTROL *made = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*made));
    if (!body || !made) {
        HeapFree(GetProcessHeap(), 0, body);
        HeapFree(GetProcessHeap(), 0, made);
        return LDAP_NO_MEMORY;
    }
    /* the list is a sequence of sequences made backwards from the end of the buffer. */
    int count = 0;
    while (keys[count])
        count++;
    BYTE *items[64];
    int item_lengths[64];
    if (count > 64) {
        HeapFree(GetProcessHeap(), 0, body);
        HeapFree(GetProcessHeap(), 0, made);
        return LDAP_PARAM_ERROR;
    }
    int total = 0;
    for (int i = 0; i < count; i++) {
        char name[256], rule[256];
        name[0] = rule[0] = 0;
        if (wide) {
            if (keys[i]->AttributeType)
                WideCharToMultiByte(CP_ACP, 0, keys[i]->AttributeType, -1, name, sizeof(name), nullptr, nullptr);
            if (keys[i]->MatchingRule)
                WideCharToMultiByte(CP_ACP, 0, keys[i]->MatchingRule, -1, rule, sizeof(rule), nullptr, nullptr);
        } else {
            if (keys[i]->AttributeType)
                lstrcpynA(name, keys[i]->AttributeType, sizeof(name));
            if (keys[i]->MatchingRule)
                lstrcpynA(rule, keys[i]->MatchingRule, sizeof(rule));
        }
        int name_length = lstrlenA(name), rule_length = lstrlenA(rule);
        BYTE inner[640];
        int at = 0;
        inner[at++] = 0x04;
        at += ber_length(inner + at, name_length);
        memcpy(inner + at, name, name_length);
        at += name_length;
        if (rule_length) {
            inner[at++] = 0x80;
            at += ber_length(inner + at, rule_length);
            memcpy(inner + at, rule, rule_length);
            at += rule_length;
        }
        if (keys[i]->ReverseOrder) {
            inner[at++] = 0x81;
            inner[at++] = 1;
            inner[at++] = 0xFF;
        }
        items[i] = HeapAlloc(GetProcessHeap(), 0, at + 8);
        int n = 0;
        items[i][n++] = 0x30;
        n += ber_length(items[i] + n, at);
        memcpy(items[i] + n, inner, at);
        item_lengths[i] = n + at;
        total += item_lengths[i];
    }
    int at = 0;
    body[at++] = 0x30;
    at += ber_length(body + at, total);
    for (int i = 0; i < count; i++) {
        memcpy(body + at, items[i], item_lengths[i]);
        at += item_lengths[i];
        HeapFree(GetProcessHeap(), 0, items[i]);
    }
    made->ldctl_oid = HeapAlloc(GetProcessHeap(), 0, sizeof(SORT_OID));
    memcpy(made->ldctl_oid, SORT_OID, sizeof(SORT_OID));
    made->ldctl_value.bv_len = at;
    made->ldctl_value.bv_val = (PCHAR)body;
    made->ldctl_iscritical = critical;
    *control = made;
    return LDAP_SUCCESS;
}

ULONG ldap_create_sort_controlA(LDAP *ld, SORT_KEY **keys, BOOLEAN critical, LDAPCONTROL **control)
{
    return create_sort_control(ld, keys, critical, control, FALSE);
}

ULONG ldap_create_sort_controlW(LDAP *ld, SORT_KEY **keys, BOOLEAN critical, LDAPCONTROL **control)
{
    return create_sort_control(ld, keys, critical, control, TRUE);
}

ULONG ldap_create_sort_control(LDAP *ld, SORT_KEY **keys, BOOLEAN critical, LDAPCONTROL **control)
{
    return create_sort_control(ld, keys, critical, control, FALSE);
}

static ULONG free_control(LDAPCONTROL *control)
{
    if (control) {
        HeapFree(GetProcessHeap(), 0, control->ldctl_oid);
        HeapFree(GetProcessHeap(), 0, control->ldctl_value.bv_val);
        HeapFree(GetProcessHeap(), 0, control);
    }
    return LDAP_SUCCESS;
}

ULONG ldap_control_freeA(LDAPCONTROL *control)
{
    return free_control(control);
}

ULONG ldap_control_freeW(LDAPCONTROL *control)
{
    return free_control(control);
}

ULONG ldap_control_free(LDAPCONTROL *control)
{
    return free_control(control);
}

ULONG ldap_controls_freeA(LDAPCONTROL **controls)
{
    for (int i = 0; controls && controls[i]; i++)
        free_control(controls[i]);
    if (controls)
        HeapFree(GetProcessHeap(), 0, controls);
    return LDAP_SUCCESS;
}

ULONG ldap_controls_freeW(LDAPCONTROL **controls)
{
    return ldap_controls_freeA(controls);
}

ULONG ldap_controls_free(LDAPCONTROL **controls)
{
    return ldap_controls_freeA(controls);
}

/* ------------------------------------------------------------------ text */

ULONG LdapUTF8ToUnicode(LPCSTR utf8, int utf8_length, LPWSTR unicode, int unicode_length)
{
    return MultiByteToWideChar(CP_UTF8, 0, utf8, utf8_length, unicode, unicode_length);
}

ULONG LdapUnicodeToUTF8(LPCWSTR unicode, int unicode_length, LPSTR utf8, int utf8_length)
{
    return WideCharToMultiByte(CP_UTF8, 0, unicode, unicode_length, utf8, utf8_length, nullptr, nullptr);
}
