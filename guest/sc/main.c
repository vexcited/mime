/*
 * sc.exe: talks to the service control manager from a command line.
 *
 *   sc [\\server] command [service] [options...]
 *
 * options come as a keyword that ends with an equal sign and its value in the next word: `start= auto`. a word that
 * holds both, `start=auto`, is cut in two on a second pass, which only runs after a first pass found the command line
 * wrong. the exit code is the win32 error of what failed, 1639 for a command line that makes no sense, 0 for success.
 */
#include "tool.h"
#include <mime/win32/winsvc.h>

#define ERROR_INVALID_COMMAND_LINE_ 1639

#define SC_MANAGER_CONNECT_ 0x0001
#define SC_MANAGER_CREATE_SERVICE_ 0x0002
#define SC_MANAGER_ENUMERATE_SERVICE_ 0x0004
#define SC_MANAGER_LOCK_ 0x0008
#define SC_MANAGER_QUERY_LOCK_STATUS_ 0x0010

#define SERVICE_QUERY_CONFIG_ 0x0001
#define SERVICE_CHANGE_CONFIG_ 0x0002
#define SERVICE_QUERY_STATUS_ 0x0004
#define SERVICE_ENUMERATE_DEPENDENTS_ 0x0008
#define SERVICE_START_ 0x0010
#define SERVICE_STOP_ 0x0020
#define SERVICE_PAUSE_CONTINUE_ 0x0040
#define SERVICE_INTERROGATE_ 0x0080
#define SERVICE_USER_DEFINED_CONTROL_ 0x0100
#define DELETE_ 0x00010000
#define READ_CONTROL_ 0x00020000
#define WRITE_DAC_ 0x00040000
#define ACCESS_SYSTEM_SECURITY_ 0x01000000
#define GENERIC_WRITE_ 0x40000000

#define SERVICE_CONFIG_MANAGED_ACCOUNT_ 11
#define SERVICE_CONFIG_LAUNCH_PROTECTED_ 12

#define SE_SECURITY_PRIVILEGE_ 8
#define SE_SHUTDOWN_PRIVILEGE_ 19

#define SECURITY_DESCRIPTOR_REVISION_1_ 1
#define SE_DACL_PRESENT_ 4
#define SE_SACL_PRESENT_ 0x10

typedef struct STATUS_REASON {
    DWORD Reason;
    LPWSTR Comment;
    SERVICE_STATUS_PROCESS Status;
} STATUS_REASON;

WINBASEAPI SC_HANDLE WINAPI OpenSCManagerW(LPCWSTR machine, LPCWSTR database, DWORD access);
WINBASEAPI NTSTATUS NTAPI RtlAdjustPrivilege(ULONG privilege, BOOLEAN enable, BOOLEAN client, PBOOLEAN previous);
WINBASEAPI NTSTATUS NTAPI RtlCreateServiceSid(PUNICODE_STRING name, PSID sid, PULONG size);
WINBASEAPI BOOL WINAPI ConvertSidToStringSidW(PSID sid, LPWSTR *text);
WINBASEAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                            PSECURITY_DESCRIPTOR *sd, PULONG size);
WINBASEAPI BOOL WINAPI ConvertSecurityDescriptorToStringSecurityDescriptorW(PSECURITY_DESCRIPTOR sd, DWORD revision,
                                                                            DWORD information, LPWSTR *text,
                                                                            PULONG size);
WINBASEAPI BOOL WINAPI GetSecurityDescriptorControl(PSECURITY_DESCRIPTOR sd, PWORD control, PDWORD revision);

/* true after a first pass found the command line wrong: the second pass explains. */
static BOOL g_retry;

/* ------------------------------------------------------------------ messages */

static void say(const WCHAR *format, ...)
{
    TU_STR text = {};
    va_list ap;
    va_start(ap, format);
    tu_str_vformat(&text, format, ap);
    va_end(ap);
    tu_write(TU_STDOUT, text.data, text.length);
    tu_str_free(&text);
}

/* the failure of an api call, in the shape scripts look for; the result is the error. */
static DWORD api_failed(const WCHAR *name, DWORD error)
{
    WCHAR text[512];
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, text,
                             TU_ARRAY(text), nullptr);
    while (n && (text[n - 1] == L'\r' || text[n - 1] == L'\n'))
        n--;
    text[n] = 0;
    if (!n || wcschr(text, L'%'))
        say(L"[SC] %s FAILED with error %lu.\n", name, error);
    else
        say(L"[SC] %s FAILED %lu:\n\n%s\n\n", name, error, text);
    return error;
}

static DWORD last_failed(const WCHAR *name)
{
    return api_failed(name, GetLastError());
}

static void success(const WCHAR *name)
{
    say(L"[SC] %s SUCCESS\n", name);
}

static DWORD more_data(const WCHAR *name, DWORD bytes, DWORD resume, BOOL with_resume)
{
    if (with_resume)
        say(L"\n[SC] %s: more data, need %lu bytes start resume at index %lu\n", name, bytes, resume);
    else
        say(L"\n[SC] %s: more data, need %lu bytes\n", name, bytes);
    return ERROR_MORE_DATA;
}

static const WCHAR kUsage[] =
    L"DESCRIPTION:\n"
    L"        SC is a command line program used for communicating with the\n"
    L"        Service Control Manager and services.\n"
    L"USAGE:\n"
    L"        sc <server> [command] [service name] <option1> <option2>...\n"
    L"\n"
    L"\n"
    L"        The option <server> has the form \"\\\\ServerName\"\n"
    L"        Further help on commands can be obtained by typing: \"sc [command]\"\n"
    L"        Commands:\n"
    L"          query-----------Queries the status for a service, or\n"
    L"                          enumerates the status for types of services.\n"
    L"          queryex---------Queries the extended status for a service, or\n"
    L"                          enumerates the status for types of services.\n"
    L"          start-----------Starts a service.\n"
    L"          pause-----------Sends a PAUSE control request to a service.\n"
    L"          interrogate-----Sends an INTERROGATE control request to a service.\n"
    L"          continue--------Sends a CONTINUE control request to a service.\n"
    L"          stop------------Sends a STOP request to a service.\n"
    L"          config----------Changes the configuration of a service (persistent).\n"
    L"          description-----Changes the description of a service.\n"
    L"          failure---------Changes the actions taken by a service upon failure.\n"
    L"          failureflag-----Changes the failure actions flag of a service.\n"
    L"          sidtype---------Changes the service SID type of a service.\n"
    L"          privs-----------Changes the required privileges of a service.\n"
    L"          qc--------------Queries the configuration information for a service.\n"
    L"          qdescription----Queries the description for a service.\n"
    L"          qfailure--------Queries the actions taken by a service upon failure.\n"
    L"          qfailureflag----Queries the failure actions flag of a service.\n"
    L"          qsidtype--------Queries the service SID type of a service.\n"
    L"          qprivs----------Queries the required privileges of a service.\n"
    L"          qtriggerinfo----Queries the trigger parameters of a service.\n"
    L"          qprotection-----Queries the process protection level of a service.\n"
    L"          delete----------Deletes a service (from the registry).\n"
    L"          create----------Creates a service. (adds it to the registry).\n"
    L"          control---------Sends a control to a service.\n"
    L"          sdshow----------Displays a service's security descriptor.\n"
    L"          sdset-----------Sets a service's security descriptor.\n"
    L"          showsid---------Displays the service SID string corresponding to an arbitrary name.\n"
    L"          GetDisplayName--Gets the DisplayName for a service.\n"
    L"          GetKeyName------Gets the ServiceKeyName for a service.\n"
    L"          EnumDepend------Enumerates Service Dependencies.\n"
    L"\n"
    L"        The following commands don't require a service name:\n"
    L"        sc <server> <command> <option>\n"
    L"          boot------------(ok | bad) Indicates whether the last boot should\n"
    L"                          be saved as the last-known-good boot configuration\n"
    L"          QueryLock-------Queries the LockStatus for the SCManager Database\n"
    L"EXAMPLE:\n"
    L"        sc start MyService\n"
    L"\n"
    L"QUERY and QUERYEX OPTIONS:\n"
    L"        If the query command is followed by a service name, the status\n"
    L"        for that service is returned.  Further options do not apply in\n"
    L"        this case.  If the query command is followed by nothing or one of\n"
    L"        the options listed below, the services are enumerated.\n"
    L"    type=    Type of services to enumerate (driver, service, userservice, all)\n"
    L"             (default = service)\n"
    L"    state=   State of services to enumerate (inactive, all)\n"
    L"             (default = active)\n"
    L"    bufsize= The size (in bytes) of the enumeration buffer\n"
    L"             (default = 4096)\n"
    L"    ri=      The resume index number at which to begin the enumeration\n"
    L"             (default = 0)\n"
    L"    group=   Service group to enumerate\n"
    L"             (default = all groups)\n"
    L"\n"
    L"SYNTAX EXAMPLES\n"
    L"sc query                - Enumerates status for active services & drivers\n"
    L"sc query eventlog       - Displays status for the eventlog service\n"
    L"sc queryex eventlog     - Displays extended status for the eventlog service\n"
    L"sc query type= driver   - Enumerates only active drivers\n"
    L"sc query type= service  - Enumerates only Win32 services\n"
    L"sc query state= all     - Enumerates all services & drivers\n"
    L"sc query bufsize= 50    - Enumerates with a 50 byte buffer\n"
    L"sc query ri= 14         - Enumerates with resume index = 14\n"
    L"sc queryex group= \"\"    - Enumerates active services not in a group\n"
    L"sc query type= interact - Enumerates all interactive services\n"
    L"sc query type= driver group= NDIS     - Enumerates all NDIS drivers\n"
    L"\n";

static const WCHAR kConfigUsage[] =
    L"DESCRIPTION:\n"
    L"        Modifies a service entry in the registry and Service Database.\n"
    L"USAGE:\n"
    L"        sc <server> config [service name] <option1> <option2>...\n"
    L"\n"
    L"OPTIONS:\n"
    L"NOTE: The option name includes the equal sign.\n"
    L"      A space is required between the equal sign and the value.\n"
    L"      To remove the dependency, use a single / as dependency value.\n"
    L" type= <own|share|interact|kernel|filesys|rec|adapt|userown|usershare>\n"
    L" start= <boot|system|auto|demand|disabled|delayed-auto>\n"
    L" error= <normal|severe|critical|ignore>\n"
    L" binPath= <BinaryPathName to the .exe file>\n"
    L" group= <LoadOrderGroup>\n"
    L" tag= <yes|no>\n"
    L" depend= <Dependencies(separated by / (forward slash))>\n"
    L" obj= <AccountName|ObjectName>\n"
    L" DisplayName= <display name>\n"
    L" password= <password>\n"
    L"\n";

static const WCHAR kCreateUsage[] =
    L"DESCRIPTION:\n"
    L"        Creates a service entry in the registry and Service Database.\n"
    L"USAGE:\n"
    L"        sc <server> create [service name] [binPath= ] <option1> <option2>...\n"
    L"\n"
    L"OPTIONS:\n"
    L"NOTE: The option name includes the equal sign.\n"
    L"      A space is required between the equal sign and the value.\n"
    L" type= <own|share|interact|kernel|filesys|rec|userown|usershare>\n"
    L"       (default = own)\n"
    L" start= <boot|system|auto|demand|disabled|delayed-auto>\n"
    L"       (default = demand)\n"
    L" error= <normal|severe|critical|ignore>\n"
    L"       (default = normal)\n"
    L" binPath= <BinaryPathName to the .exe file>\n"
    L" group= <LoadOrderGroup>\n"
    L" tag= <yes|no>\n"
    L" depend= <Dependencies(separated by / (forward slash))>\n"
    L" obj= <AccountName|ObjectName>\n"
    L"       (default = LocalSystem)\n"
    L" DisplayName= <display name>\n"
    L" password= <password>\n"
    L"\n";

static const WCHAR kFailureUsage[] =
    L"DESCRIPTION:\n"
    L"        Changes the actions upon failure\n"
    L"USAGE:\n"
    L"        sc <server> failure [service name] <option1> <option2>...\n"
    L"\n"
    L"OPTIONS:\n"
    L"        reset=   <Length of period of no failures (in seconds)\n"
    L"                  after which to reset the failure count to 0 (may be INFINITE)>\n"
    L"                  (Must be used in conjunction with actions= )\n"
    L"        reboot=  <Message broadcast before rebooting on failure>\n"
    L"        command= <Command line to be run on failure>\n"
    L"        actions= <Failure actions and their delay time (in milliseconds),\n"
    L"                  separated by / (forward slash) -- e.g., run/5000/reboot/800\n"
    L"                  Valid actions are <run|restart|reboot> >\n"
    L"                  (Must be used in conjunction with the reset= option)\n"
    L"\n";

static const WCHAR kStopUsage[] =
    L"DESCRIPTION:\n"
    L"        Sends a STOP control request to a service.\n"
    L"USAGE:\n"
    L"        sc <server> stop [service name] <reason> <comment>\n"
    L"        <reason> = Optional reason code number for service stop\n"
    L"                   formed with the following elements in the format:\n"
    L"\n"
    L"                        Flag:Major reason:Minor reason\n"
    L"\n"
    L"                        [E.g., 1:2:8 means Hardware: Disk (Unplanned)]\n"
    L"\n"
    L"                   Flag: 1 Unplanned, 2 Custom, 4 Planned\n"
    L"                   Major reason: 1 Other, 2 Hardware, 3 Operating System, 4 Software,\n"
    L"                                 5 Application, 64-255 Custom\n"
    L"                   Minor reason: 1 Other, 2 Maintenance, 3 Installation, 4 Upgrade,\n"
    L"                                 5 Reconfiguration, 6 Hung, 7 Unstable, 8 Disk,\n"
    L"                                 9 Network Card, 10 Environment, 11 Hardware Driver,\n"
    L"                                 12 Other Driver, 13 Service Pack, 14 Software Update,\n"
    L"                                 15 Security Fix, 16 Security, 17 Network Connectivity,\n"
    L"                                 18 WMI, 19 Service Pack Uninstall,\n"
    L"                                 20 Software Update Uninstall, 22 Security Fix Uninstall,\n"
    L"                                 23 MMC, 256-65535 Custom\n"
    L"\n"
    L"       <comment> = Optional comment for the reason above (127 characters maximum)\n"
    L"\n";

static const WCHAR kControlUsage[] =
    L"DESCRIPTION:\n"
    L"        Sends a CONTROL code to a service.\n"
    L"USAGE:\n"
    L"        sc <server> control [service name] <value>\n"
    L"            <value> = user-defined control code\n"
    L"            <value> = <paramchange|\n"
    L"                       netbindadd|netbindremove|\n"
    L"                       netbindenable|netbinddisable>\n"
    L"\n"
    L"See also sc stop, sc pause, etc.\n"
    L"\n";

/* the one line commands: a usage that says what the command takes. */
static const WCHAR kStartUsage[] = L"DESCRIPTION:\n        Starts a service running.\nUSAGE:\n        sc <server> start [service name] <arg1> <arg2> ...\n\n";
static const WCHAR kPauseUsage[] = L"DESCRIPTION:\n        Sends a PAUSE control request to a service.\nUSAGE:\n        sc <server> pause [service name]\n\n";
static const WCHAR kInterrogateUsage[] = L"DESCRIPTION:\n        Sends an INTERROGATE control request to a service.\nUSAGE:\n        sc <server> interrogate [service name]\n\n";
static const WCHAR kContinueUsage[] = L"DESCRIPTION:\n        Sends a CONTINUE control request to a service.\nUSAGE:\n        sc <server> continue [service name]\n\n";
static const WCHAR kDescriptionUsage[] = L"DESCRIPTION:\n        Sets the description string for a service.\nUSAGE:\n        sc <server> description [service name] [description]\n\n";
static const WCHAR kQcUsage[] = L"DESCRIPTION:\n        Queries the configuration information for a service.\nUSAGE:\n        sc <server> qc [service name] <bufferSize>\n\n";
static const WCHAR kQdescriptionUsage[] = L"DESCRIPTION:\n        Retrieves the description string of a service.\nUSAGE:\n        sc <server> qdescription [service name] <bufferSize>\n\n";
static const WCHAR kQfailureUsage[] = L"DESCRIPTION:\n        Retrieves the actions performed on service failure.\nUSAGE:\n        sc <server> qfailure [service name] <bufferSize>\n\n";
static const WCHAR kQfailureflagUsage[] = L"DESCRIPTION:\n        Retrieves the failure actions flag setting of a service.\nUSAGE:\n        sc <server> qfailureflag [service name]\n\n";
static const WCHAR kFailureflagUsage[] = L"DESCRIPTION:\n        Changes the failure actions flag setting of a service.\nUSAGE:\n        sc <server> failureflag [service name] [flag]\n\n";
static const WCHAR kQsidtypeUsage[] = L"DESCRIPTION:\n        Queries the service security identifier (SID) type setting of a service.\nUSAGE:\n        sc <server> qsidtype [service name]\n\n";
static const WCHAR kSidtypeUsage[] = L"DESCRIPTION:\n        Changes the service security identifier (SID) type setting of a service.\nUSAGE:\n        sc <server> sidtype [service name] [type]\n\nOPTIONS:\n        type = <none|unrestricted|restricted>\n\n";
static const WCHAR kQprivsUsage[] = L"DESCRIPTION:\n        Queries the required privileges setting for a service.\nUSAGE:\n        sc <server> qprivs [service name] <bufferSize>\n\n";
static const WCHAR kPrivsUsage[] = L"DESCRIPTION:\n        Changes the required privileges setting of a service.\nUSAGE:\n        sc <server> privs [service name] [privileges]\n\nOPTIONS:\n        privileges = <Privileges(separated by / (forward slash))>\n                     [E.g., SeBackupPrivilege/SeRestorePrivilege]\n\n\n";
static const WCHAR kQtriggerUsage[] = L"DESCRIPTION:\n        Retrieves the trigger information of a service.\nUSAGE:\n        sc <server> qtriggerinfo [service name] <bufferSize>\n\n";
static const WCHAR kQprotectionUsage[] = L"DESCRIPTION:\n        Queries whether the service is marked to start in\n        a protected process.  Note that this setting cannot\n        be changed.\n\nUSAGE:\n        sc <server> qprotection [service name]\n\n";
static const WCHAR kDeleteUsage[] = L"DESCRIPTION:\n        Deletes a service entry from the registry.\n        If the service is running, or another process has an\n        open handle to the service, the service is simply marked\n        for deletion.\nUSAGE:\n        sc <server> delete [service name]\n\n";
static const WCHAR kBootUsage[] = L"DESCRIPTION:\n        Indicates whether the last boot should be saved as the\n        last-known-good boot configuration on the local machine.\n        If specified, the server name is ignored.\nUSAGE:\n        sc <server> boot <bad | ok>\n\n";
static const WCHAR kGetDisplayUsage[] = L"DESCRIPTION:\n        Gets the display name associated with a particular service\nUSAGE:\n        sc <server> GetDisplayName <service key name> <bufsize>\n\n";
static const WCHAR kGetKeyUsage[] = L"DESCRIPTION:\n        Gets the key name associated with a particular service,\n        using the display name as input\nUSAGE:\n        sc <server> GetKeyName <service display name> <bufsize>\n\n";
static const WCHAR kEnumDependUsage[] = L"DESCRIPTION:\n        Enumerates the Services that are dependent on this one\nUSAGE:\n        sc <server> EnumDepend <service name> <bufsize>\n\n";
static const WCHAR kSdshowUsage[] = L"DESCRIPTION:\n        Displays a service's security descriptor in SDDL format\nUSAGE:\n        sc <server> sdshow <service name> <showrights>\n\n";
static const WCHAR kSdsetUsage[] = L"DESCRIPTION:\n        Sets a service's security descriptor\nUSAGE:\n        sc <server> sdset <service name> <SD in SDDL format>\n\n";
static const WCHAR kShowsidUsage[] = L"DESCRIPTION:\n        Displays the service SID string corresponding to an arbitrary name. The name can be that of an existing or non-existent service.\nUSAGE:\n        sc showsid [name]\n\n";

static const WCHAR kSdRights[] =
    L"\nSDDL right       Right value\n"
    L"----------       -----------\n"
    L"   GA        -   GENERIC_ALL \n"
    L"   GR        -   GENERIC_READ \n"
    L"   GW        -   GENERIC_WRITE \n"
    L"   GX        -   GENERIC_EXECUTE \n"
    L"   RC        -   READ_CONTROL \n"
    L"   SD        -   DELETE \n"
    L"   WD        -   WRITE_DAC\n"
    L"   WO        -   WRITE_OWNER\n"
    L"   RP        -   SERVICE_START\n"
    L"   WP        -   SERVICE_STOP\n"
    L"   CC        -   SERVICE_QUERY_CONFIG\n"
    L"   DC        -   SERVICE_CHANGE_CONFIG\n"
    L"   LC        -   SERVICE_QUERY_STATUS\n"
    L"   SW        -   SERVICE_ENUMERATE_DEPENDENTS\n"
    L"   LO        -   SERVICE_INTERROGATE\n"
    L"   DT        -   SERVICE_PAUSE_CONTINUE\n"
    L"   CR        -   SERVICE_USER_DEFINED_CONTROL\n";

static const WCHAR kSdRightsManager[] =
    L"\nSDDL right       Right value\n"
    L"----------       -----------\n"
    L"   GA        -   GENERIC_ALL \n"
    L"   KA        -   GENERIC_ALL\n"
    L"   GR        -   GENERIC_READ \n"
    L"   GW        -   GENERIC_WRITE \n"
    L"   GX        -   GENERIC_EXECUTE \n"
    L"   RC        -   READ_CONTROL \n"
    L"   SD        -   DELETE \n"
    L"   WD        -   WRITE_DAC\n"
    L"   WO        -   WRITE_OWNER\n"
    L"   RP        -   SC_MANAGER_QUERY_LOCK_STATUS\n"
    L"   WP        -   SC_MANAGER_MODIFY_BOOT_CONFIG\n"
    L"   CC        -   SC_MANAGER_CONNECT\n"
    L"   DC        -   SC_MANAGER_CREATE_SERVICE\n"
    L"   LC        -   SC_MANAGER_ENUMERATE_SERVICE\n"
    L"   SW        -   SC_MANAGER_LOCK\n";

/* the usage of a command is shown on the second pass only. */
static DWORD usage_of(const WCHAR *text)
{
    if (g_retry)
        tu_puts(text);
    else
        g_retry = TRUE;
    return ERROR_INVALID_COMMAND_LINE_;
}

static DWORD usage(void)
{
    if (g_retry) {
        tu_puts(kUsage);
        tu_puts(L"\n");
    } else {
        g_retry = TRUE;
    }
    return ERROR_INVALID_COMMAND_LINE_;
}

/* a usage that is shown at once, whichever the pass. */
static DWORD usage_now(const WCHAR *text)
{
    tu_puts(text);
    return ERROR_INVALID_COMMAND_LINE_;
}

/* ------------------------------------------------------------------ status display */

static const WCHAR *type_name(DWORD type)
{
    switch (type) {
    case 0x01: return L"KERNEL_DRIVER ";
    case 0x02: return L"FILE_SYSTEM_DRIVER ";
    case 0x04: return L"ADAPTER ";
    case 0x0B: return L"DRIVER ";
    case 0x10: return L"WIN32_OWN_PROCESS ";
    case 0x20: return L"WIN32_SHARE_PROCESS ";
    case 0x30: return L"WIN32 ";
    case 0x50: return L"USER_OWN_PROCESS TEMPLATE ";
    case 0x60: return L"USER_SHARE_PROCESS TEMPLATE ";
    case 0xD0: return L"USER_OWN_PROCESS INSTANCE ";
    case 0xE0: return L"USER_SHARE_PROCESS INSTANCE ";
    case 0x210: return L"WIN32_PACKAGED_PROCESS ";
    default: return L" ERROR ";
    }
}

static const WCHAR *state_name(DWORD state)
{
    switch (state) {
    case 1: return L"STOPPED ";
    case 2: return L"START_PENDING ";
    case 3: return L"STOP_PENDING ";
    case 4: return L"RUNNING ";
    case 5: return L"CONTINUE_PENDING ";
    case 6: return L"PAUSE_PENDING ";
    case 7: return L"PAUSED ";
    default: return L" ERROR ";
    }
}

/* the block of one service; basic leaves out the process id and flags, display may be null. */
static void display_status(const WCHAR *name, const WCHAR *display, const SERVICE_STATUS_PROCESS *s, BOOL basic)
{
    DWORD type = s->dwServiceType;
    BOOL interactive = (type & 0x100) != 0;
    TU_STR out = {};
    tu_str_format(&out, L"\nSERVICE_NAME: %s", name);
    if (display)
        tu_str_format(&out, L"\nDISPLAY_NAME: %s\n", display);
    else
        tu_str_puts(&out, L" \n");
    tu_str_format(&out, L"        TYPE               : %x  %s %s\n", type, type_name(type & ~0x100u),
                  interactive ? L"(interactive)" : L"");
    tu_str_format(&out, L"        STATE              : %x  %s\n", s->dwCurrentState, state_name(s->dwCurrentState));
    if (s->dwCurrentState != 1) {
        DWORD c = s->dwControlsAccepted;
        tu_str_format(&out, L"                                (%s, %s, %s)\n", (c & 1) ? L"STOPPABLE" : L"NOT_STOPPABLE",
                      (c & 2) ? L"PAUSABLE" : L"NOT_PAUSABLE",
                      (c & 0x100) ? L"ACCEPTS_PRESHUTDOWN" : (c & 4) ? L"ACCEPTS_SHUTDOWN" : L"IGNORES_SHUTDOWN");
    }
    tu_str_format(&out, L"        WIN32_EXIT_CODE    : %lu  (0x%x)\n", s->dwWin32ExitCode, s->dwWin32ExitCode);
    tu_str_format(&out, L"        SERVICE_EXIT_CODE  : %lu  (0x%x)\n", s->dwServiceSpecificExitCode,
                  s->dwServiceSpecificExitCode);
    tu_str_format(&out, L"        CHECKPOINT         : 0x%x\n", s->dwCheckPoint);
    tu_str_format(&out, L"        WAIT_HINT          : 0x%x\n", s->dwWaitHint);
    if (!basic) {
        tu_str_format(&out, L"        PID                : %lu\n", s->dwProcessId);
        tu_str_format(&out, L"        FLAGS              : %s\n",
                      (s->dwServiceFlags & SERVICE_RUNS_IN_SYSTEM_PROCESS) ? L"RUNS_IN_SYSTEM_PROCESS" : L"");
    }
    tu_write(TU_STDOUT, out.data, out.length);
    tu_str_free(&out);
}

/* ------------------------------------------------------------------ helpers */

typedef struct SCSESSION {
    const WCHAR *machine;
    SC_HANDLE manager;
    SC_HANDLE service;
} SCSESSION;

/* the manager with the access, or the error already told. */
static DWORD open_manager(SCSESSION *s, DWORD access)
{
    s->manager = OpenSCManagerW(s->machine, nullptr, access);
    if (!s->manager)
        return last_failed(L"OpenSCManager");
    return 0;
}

static void close_session(SCSESSION *s)
{
    if (s->service)
        CloseServiceHandle(s->service);
    if (s->manager)
        CloseServiceHandle(s->manager);
    s->service = s->manager = nullptr;
}

static DWORD open_service(SCSESSION *s, const WCHAR *name, DWORD access, const WCHAR *what)
{
    s->service = OpenServiceW(s->manager, name, access);
    if (!s->service)
        return last_failed(what ? what : L"OpenService");
    return 0;
}

static BOOL equals(const WCHAR *a, const WCHAR *b)
{
    return !_wcsicmp(a, b);
}

/* the number of a text, zero when it is not one: sc reads numbers with the c runtime's rules. */
static DWORD to_number(const WCHAR *text)
{
    LONG sign = 1;
    while (*text == L' ' || *text == L'\t')
        text++;
    if (*text == L'-') {
        sign = -1;
        text++;
    } else if (*text == L'+') {
        text++;
    }
    DWORD v = 0;
    while (tu_is_digit(*text))
        v = v * 10 + (*text++ - L'0');
    return (DWORD)((LONG)v * sign);
}

/* the whole of a configuration query that sc reads in two calls. */
static void *query_config(SC_HANDLE service, DWORD level, DWORD *error)
{
    DWORD size = 512;
    for (int tries = 0; tries < 4; tries++) {
        BYTE *buffer = tu_alloc(size);
        DWORD needed = 0;
        BOOL ok = level ? QueryServiceConfig2W(service, level, buffer, size, &needed)
                        : QueryServiceConfigW(service, (LPQUERY_SERVICE_CONFIGW)buffer, size, &needed);
        if (ok) {
            *error = 0;
            return buffer;
        }
        *error = GetLastError();
        tu_free(buffer);
        if (*error != ERROR_INSUFFICIENT_BUFFER || needed <= size)
            return nullptr;
        size = needed;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ query and queryex */

static DWORD cmd_query(SCSESSION *s, WCHAR **args, int count, BOOL basic)
{
    DWORD resume = 0, buffer_size = 4096, type_mask = 0, state = SERVICE_ACTIVE;
    const WCHAR *group = nullptr, *name = nullptr;
    for (int i = 0; i < count; i++) {
        const WCHAR *a = args[i];
        BOOL has_value = i + 1 < count;
        if (!wcscmp(a, L"ri=")) {
            if (has_value)
                resume = to_number(args[++i]);
        } else if (!wcscmp(a, L"type=")) {
            if (!has_value)
                continue;
            const WCHAR *v = args[++i];
            if (!wcscmp(v, L"driver"))
                type_mask |= 0x0B;
            else if (!wcscmp(v, L"service"))
                type_mask |= 0x30;
            else if (!wcscmp(v, L"all"))
                type_mask |= 0x3FF;
            else if (!wcscmp(v, L"interact"))
                type_mask |= 0x130;
            else if (!wcscmp(v, L"kernel"))
                type_mask |= 1;
            else if (!wcscmp(v, L"filesys"))
                type_mask |= 2;
            else if (!wcscmp(v, L"adapter"))
                type_mask |= 4;
            else if (!wcscmp(v, L"own"))
                type_mask |= 0x10;
            else if (!wcscmp(v, L"share"))
                type_mask |= 0x20;
            else if (!wcscmp(v, L"userservice"))
                type_mask |= 0x40;
            else {
                say(L"\nERROR: Invalid %s field\n\n", L"type=");
                return 87;
            }
        } else if (!wcscmp(a, L"state=")) {
            if (!has_value)
                continue;
            const WCHAR *v = args[++i];
            if (!wcscmp(v, L"inactive"))
                state = SERVICE_INACTIVE;
            else if (!wcscmp(v, L"all"))
                state = SERVICE_STATE_ALL;
            else {
                say(L"\nERROR: Invalid %s field\n\n", L"state=");
                return 87;
            }
        } else if (!wcscmp(a, L"group=")) {
            if (has_value)
                group = args[++i];
        } else if (!wcscmp(a, L"bufsize=")) {
            if (has_value)
                buffer_size = to_number(args[++i]);
        } else {
            if (i != 0) {
                if (g_retry)
                    tu_puts(L"\nERROR:  Invalid Option\n\n");
                return usage();
            }
            if (wcschr(a, L'=')) {
                if (g_retry)
                    tu_puts(L"\nERROR:  Invalid Option\n\n");
                return usage();
            }
            name = a;
        }
    }

    BYTE *buffer = buffer_size ? tu_alloc(buffer_size) : nullptr;
    DWORD result = 0;
    if (name) {
        if (group) {
            say(L"\nERROR:  Cannot specify a service name when enumerating a group\n\n");
            tu_free(buffer);
            return 87;
        }
        if ((result = open_manager(s, SC_MANAGER_CONNECT_)))
            goto done;
        if ((result = open_service(s, name, SERVICE_QUERY_STATUS_, L"EnumQueryServicesStatus:OpenService")))
            goto done;
        SERVICE_STATUS_PROCESS status;
        memset(&status, 0, sizeof(status));
        BOOL ok;
        if (basic) {
            ok = QueryServiceStatus(s->service, (LPSERVICE_STATUS)&status);
        } else {
            DWORD needed;
            ok = QueryServiceStatusEx(s->service, SC_STATUS_PROCESS_INFO, (LPBYTE)&status, sizeof(status), &needed);
        }
        if (!ok && GetLastError()) {
            result = last_failed(basic ? L"QueryServiceStatus" : L"QueryServiceStatusEx");
            goto done;
        }
        display_status(name, nullptr, &status, basic);
        goto done;
    }

    if ((result = open_manager(s, SC_MANAGER_ENUMERATE_SERVICE_)))
        goto done;
    if (!type_mask)
        type_mask = 0x30;
    for (;;) {
        DWORD needed = 0, returned = 0;
        BOOL ok = EnumServicesStatusExW(s->manager, SC_ENUM_PROCESS_INFO, type_mask, state, buffer, buffer_size,
                                        &needed, &returned, &resume, group);
        DWORD error = ok ? 0 : GetLastError();
        if (error && error != ERROR_MORE_DATA) {
            result = api_failed(basic ? L"EnumServicesStatus" : L"EnumServicesStatusEx", error);
            break;
        }
        ENUM_SERVICE_STATUS_PROCESSW *list = (ENUM_SERVICE_STATUS_PROCESSW *)buffer;
        for (DWORD i = 0; i < returned; i++)
            display_status(list[i].lpServiceName, list[i].lpDisplayName, &list[i].ServiceStatusProcess, basic);
        if (error != ERROR_MORE_DATA)
            break;
        if (!returned) {
            result = more_data(L"EnumServicesStatus", needed, resume, TRUE);
            break;
        }
    }
done:
    tu_free(buffer);
    return result;
}

/* ------------------------------------------------------------------ start and the controls */

static DWORD cmd_start(SCSESSION *s, WCHAR **args, int count)
{
    DWORD result;
    if ((result = open_manager(s, SC_MANAGER_CONNECT_)))
        return result;
    const WCHAR *name = args[0];
    if ((result = open_service(s, name, SERVICE_QUERY_STATUS_ | SERVICE_START_, L"StartService: OpenService")))
        return result;
    if (!StartServiceW(s->service, (DWORD)(count - 1), count > 1 ? (LPCWSTR *)&args[1] : nullptr))
        return last_failed(L"StartService");
    SERVICE_STATUS_PROCESS status;
    DWORD needed;
    memset(&status, 0, sizeof(status));
    if (!QueryServiceStatusEx(s->service, SC_STATUS_PROCESS_INFO, (LPBYTE)&status, sizeof(status), &needed))
        return last_failed(L"QueryServiceStatusEx");
    display_status(name, nullptr, &status, FALSE);
    return 0;
}

/* flag:major:minor to the reason code of the service control manager. */
static DWORD stop_reason(const WCHAR *text)
{
    if (!text)
        return 0;
    const WCHAR *first = wcschr(text, L':');
    if (!first)
        return 0;
    const WCHAR *second = wcschr(first + 1, L':');
    if (!second)
        return 0;
    DWORD flag = to_number(text), major = to_number(first + 1), minor = to_number(second + 1);
    DWORD high = (flag & 0xF) == flag ? flag << 28 : 0x80000000;
    DWORD middle = (major & 0xFFFF) == major ? major << 16 : 0x70000;
    return high + minor + middle;
}

static DWORD cmd_control(SCSESSION *s, const WCHAR *name, DWORD control, const WCHAR *reason, const WCHAR *comment)
{
    DWORD access;
    if (control == SERVICE_CONTROL_STOP)
        access = SERVICE_STOP_;
    else if (control == SERVICE_CONTROL_PAUSE || control == SERVICE_CONTROL_CONTINUE || (control >= 6 && control <= 10))
        access = SERVICE_PAUSE_CONTINUE_;
    else if (control == SERVICE_CONTROL_INTERROGATE)
        access = SERVICE_INTERROGATE_;
    else
        access = SERVICE_USER_DEFINED_CONTROL_;
    DWORD result;
    if ((result = open_manager(s, SC_MANAGER_CONNECT_)))
        return result;
    if ((result = open_service(s, name, access, nullptr)))
        return result;
    if (reason) {
        STATUS_REASON params;
        memset(&params, 0, sizeof(params));
        params.Reason = stop_reason(reason);
        params.Comment = (LPWSTR)comment;
        if (!ControlServiceExW(s->service, control, 1, &params)) {
            DWORD error = GetLastError();
            if (error)
                return api_failed(L"ControlServiceEx", error);
        }
        display_status(name, nullptr, &params.Status, FALSE);
        return 0;
    }
    SERVICE_STATUS status;
    memset(&status, 0, sizeof(status));
    if (!ControlService(s->service, control, &status)) {
        DWORD error = GetLastError();
        if (error)
            return api_failed(L"ControlService", error);
    }
    display_status(name, nullptr, (SERVICE_STATUS_PROCESS *)&status, TRUE);
    return 0;
}

/* ------------------------------------------------------------------ create and config */

/* the words that carry a value: the keyword and its value are two words. */
typedef struct SETTINGS {
    DWORD Type;
    DWORD Start;
    DWORD ErrorControl;
    BOOL Delayed;
    const WCHAR *BinaryPath, *Group, *Display, *StartName, *Password;
    WCHAR *Dependencies;
    BOOL WantTag;
} SETTINGS;

static BOOL parse_type(const WCHAR *v, BOOL for_config, DWORD *value)
{
    if (equals(v, L"own"))
        *value = 0x10;
    else if (equals(v, L"share"))
        *value = 0x20;
    else if (equals(v, L"userown"))
        *value = 0x50;
    else if (equals(v, L"usershare"))
        *value = 0x60;
    else if (equals(v, L"interact"))
        *value = 0x100;
    else if (equals(v, L"kernel"))
        *value = 1;
    else if (equals(v, L"filesys"))
        *value = 2;
    else if (equals(v, L"rec"))
        *value = 8;
    else if (for_config && equals(v, L"adapt"))
        *value = 4;
    else
        return FALSE;
    return TRUE;
}

static BOOL parse_start(const WCHAR *v, SETTINGS *o)
{
    if (equals(v, L"boot"))
        o->Start = 0;
    else if (equals(v, L"system"))
        o->Start = 1;
    else if (equals(v, L"auto"))
        o->Start = 2;
    else if (equals(v, L"demand"))
        o->Start = 3;
    else if (equals(v, L"disabled"))
        o->Start = 4;
    else if (equals(v, L"delayed-auto")) {
        o->Start = 2;
        o->Delayed = TRUE;
    } else
        return FALSE;
    return TRUE;
}

static BOOL parse_error(const WCHAR *v, DWORD *value)
{
    if (equals(v, L"normal"))
        *value = 1;
    else if (equals(v, L"severe"))
        *value = 2;
    else if (equals(v, L"critical"))
        *value = 3;
    else if (equals(v, L"ignore"))
        *value = 0;
    else
        return FALSE;
    return TRUE;
}

/* a double null terminated list from names separated by slashes. */
static WCHAR *dependency_list(const WCHAR *text)
{
    SIZE_T n = wcslen(text);
    WCHAR *list = tu_alloc((n + 2) * sizeof(WCHAR));
    memcpy(list, text, n * sizeof(WCHAR));
    for (SIZE_T i = 0; i < n; i++)
        if (list[i] == L'/')
            list[i] = 0;
    return list;
}

/* the options of create and config; returns the usage error or 0. */
static DWORD parse_settings(WCHAR **args, int count, SETTINGS *o, BOOL for_config)
{
    const WCHAR *usage_text = for_config ? kConfigUsage : kCreateUsage;
    for (int i = 0; i < count; i += 2) {
        const WCHAR *k = args[i];
        BOOL has_value = i + 1 < count;
        const WCHAR *v = has_value ? args[i + 1] : nullptr;
        if (has_value && equals(k, L"type=")) {
            DWORD t;
            if (!parse_type(v, for_config, &t)) {
                if (g_retry) {
                    say(L"\nERROR: Invalid %s field\n\n", L"type=");
                    tu_puts(usage_text);
                    return ERROR_INVALID_COMMAND_LINE_;
                }
                return usage_of(usage_text);
            }
            o->Type = (o->Type == (DWORD)-1 ? 0 : o->Type) | t;
        } else if (has_value && equals(k, L"start=")) {
            if (!parse_start(v, o)) {
                if (g_retry) {
                    say(L"\nERROR: Invalid %s field\n\n", L"start=");
                    tu_puts(usage_text);
                    return ERROR_INVALID_COMMAND_LINE_;
                }
                return usage_of(usage_text);
            }
        } else if (has_value && equals(k, L"error=")) {
            if (!parse_error(v, &o->ErrorControl)) {
                if (g_retry) {
                    say(L"\nERROR: Invalid %s field\n\n", L"error=");
                    tu_puts(usage_text);
                    return ERROR_INVALID_COMMAND_LINE_;
                }
                return usage_of(usage_text);
            }
        } else if (has_value && equals(k, L"binPath=")) {
            o->BinaryPath = v;
        } else if (has_value && equals(k, L"group=")) {
            o->Group = v;
        } else if (has_value && equals(k, L"tag=")) {
            if (equals(v, L"YES"))
                o->WantTag = TRUE;
        } else if (has_value && equals(k, L"DisplayName=")) {
            o->Display = v;
        } else if (has_value && equals(k, L"depend=")) {
            tu_free(o->Dependencies);
            o->Dependencies = dependency_list(v);
        } else if (has_value && equals(k, L"obj=")) {
            o->StartName = v;
        } else if (has_value && equals(k, L"password=")) {
            o->Password = v;
        } else {
            return usage_of(usage_text);
        }
    }
    return 0;
}

static DWORD cmd_create(SCSESSION *s, const WCHAR *name, WCHAR **args, int count)
{
    SETTINGS o = {(DWORD)-1, 3, 1};
    DWORD result = parse_settings(args, count, &o, FALSE);
    if (result)
        goto done;
    if ((result = open_manager(s, SC_MANAGER_CREATE_SERVICE_)))
        goto done;
    if (o.Type == (DWORD)-1)
        o.Type = 0x10;
    DWORD tag = 0;
    s->service = CreateServiceW(s->manager, name, o.Display, 0xF01FF, o.Type, o.Start, o.ErrorControl, o.BinaryPath,
                                o.Group, o.WantTag ? &tag : nullptr, o.Dependencies, o.StartName, o.Password);
    if (!s->service) {
        result = last_failed(L"CreateService");
        goto done;
    }
    if (o.Delayed) {
        SERVICE_DELAYED_AUTO_START_INFO info = {TRUE};
        if (!ChangeServiceConfig2W(s->service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &info) && GetLastError()) {
            result = last_failed(L"ChangeServiceConfig2 (delayed autostart flag)");
            if (!DeleteService(s->service))
                result = last_failed(L"DeleteService");
            goto done;
        }
    }
    success(L"CreateService");
    if (o.Type & 0x100)
        say(L"\nWARNING:  The service %s is configured as interactive whose support is being deprecated. The service may not function properly.\n\n",
            name);
done:
    tu_free(o.Dependencies);
    return result;
}

static DWORD cmd_config(SCSESSION *s, const WCHAR *name, WCHAR **args, int count)
{
    SETTINGS o = {(DWORD)-1, (DWORD)-1, (DWORD)-1};
    DWORD result = parse_settings(args, count, &o, TRUE);
    if (result)
        goto done;
    if ((result = open_manager(s, SC_MANAGER_CONNECT_)))
        goto done;
    BOOL set_start = o.Start != (DWORD)-1;
    if ((result = open_service(s, name, (set_start ? 1 : 0) + SERVICE_CHANGE_CONFIG_, nullptr)))
        goto done;
    /* the delayed flag only fits an automatic start, so it is changed once the start type is. */
    DWORD old_delayed = 0;
    if (set_start && o.Start == SERVICE_AUTO_START) {
        DWORD needed = 0;
        if (!QueryServiceConfig2W(s->service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, (LPBYTE)&old_delayed, 4, &needed)) {
            DWORD error = GetLastError();
            if (error != ERROR_INVALID_LEVEL || o.Delayed) {
                result = api_failed(L"QueryServiceConfig2 (delayed autostart flag)", error);
                goto done;
            }
        }
    }
    DWORD tag = 0;
    if (ChangeServiceConfigW(s->service, o.Type, o.Start, o.ErrorControl, o.BinaryPath, o.Group,
                             o.WantTag ? &tag : nullptr, o.Dependencies, o.StartName, o.Password, o.Display) ||
        !GetLastError()) {
        if (set_start && o.Start == SERVICE_AUTO_START && (DWORD)o.Delayed != old_delayed) {
            SERVICE_DELAYED_AUTO_START_INFO info = {o.Delayed};
            if (!ChangeServiceConfig2W(s->service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &info) && GetLastError()) {
                result = last_failed(L"ChangeServiceConfig2 (delayed autostart flag)");
                goto done;
            }
        }
        success(L"ChangeServiceConfig");
        if (o.WantTag)
            say(L"[SC] Tag = %lu\n", tag);
        if (o.Type != (DWORD)-1 && (o.Type & 0x100))
            say(L"\nWARNING:  The service %s is configured as interactive whose support is being deprecated. The service may not function properly.\n\n",
                name);
    } else {
        result = last_failed(L"ChangeServiceConfig");
        if (set_start && o.Start != 4 && result == ERROR_DUPLICATE_SERVICE_NAME)
            tu_puts(L"This error may occur if another service is already using a Named \n"
                    L"Pipe or RPC trigger for the same endpoint or interface as the\n"
                    L"given service.\n");
    }
done:
    tu_free(o.Dependencies);
    return result;
}

/* ------------------------------------------------------------------ failure actions */

static DWORD cmd_failure(SCSESSION *s, const WCHAR *name, WCHAR **args, int count)
{
    DWORD reset = 0;
    BOOL have_reset = FALSE, have_actions = FALSE, reboot_action = FALSE, restart_action = FALSE;
    const WCHAR *reboot = nullptr, *command = nullptr;
    SC_ACTION *actions = nullptr;
    DWORD action_count = 0;
    DWORD result = 0;
    for (int i = 0; i < count; i += 2) {
        BOOL has_value = i + 1 < count;
        const WCHAR *k = args[i];
        if (has_value && equals(k, L"reset=")) {
            reset = equals(args[i + 1], L"infinite") ? (DWORD)-1 : to_number(args[i + 1]);
            have_reset = TRUE;
        } else if (has_value && equals(k, L"reboot=")) {
            reboot = args[i + 1];
        } else if (has_value && equals(k, L"command=")) {
            command = args[i + 1];
        } else if (has_value && equals(k, L"actions=")) {
            tu_free(actions);
            WCHAR *list = tu_dup(args[i + 1]);
            SIZE_T slashes = 0;
            for (WCHAR *p = list; *p; p++)
                if (*p == L'/')
                    slashes++;
            actions = tu_alloc((slashes / 2 + 2) * sizeof(SC_ACTION));
            action_count = 0;
            BOOL want_action = TRUE;
            WCHAR *word = list;
            for (WCHAR *p = list;; p++) {
                if (*p != L'/' && *p)
                    continue;
                BOOL end = !*p;
                *p = 0;
                if (want_action) {
                    DWORD type = 0;
                    if (equals(word, L"restart")) {
                        type = SC_ACTION_RESTART;
                        restart_action = TRUE;
                    } else if (equals(word, L"reboot")) {
                        type = SC_ACTION_REBOOT;
                        reboot_action = TRUE;
                    } else if (equals(word, L"run")) {
                        type = SC_ACTION_RUN_COMMAND;
                    }
                    actions[action_count].Type = (SC_ACTION_TYPE)type;
                    actions[action_count].Delay = 0;
                } else {
                    actions[action_count++].Delay = to_number(word);
                }
                want_action = !want_action;
                word = p + 1;
                if (end)
                    break;
            }
            have_actions = TRUE;
            tu_free(list);
        } else {
            tu_free(actions);
            if (g_retry)
                tu_puts(L"\nERROR:  Invalid Option\n\n");
            return usage_of(kFailureUsage);
        }
    }
    if (have_reset != have_actions) {
        tu_free(actions);
        if (g_retry)
            tu_puts(L"\nERROR:  The reset and actions options must be set simultaneously\n\n");
        return usage_of(kFailureUsage);
    }
    if (reboot_action) {
        BOOLEAN previous;
        NTSTATUS status = RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE_, TRUE, FALSE, &previous);
        if (status < 0) {
            tu_free(actions);
            return api_failed(L"ChangeServiceFailure: RtlAdjustPrivilege", RtlNtStatusToDosError(status));
        }
    }
    if ((result = open_manager(s, SC_MANAGER_CONNECT_)))
        goto done;
    if ((result = open_service(s, name, SERVICE_CHANGE_CONFIG_ | (restart_action ? SERVICE_START_ : 0), nullptr)))
        goto done;
    SERVICE_FAILURE_ACTIONSW info = {};
    info.dwResetPeriod = reset;
    info.lpRebootMsg = (LPWSTR)reboot;
    info.lpCommand = (LPWSTR)command;
    info.cActions = action_count;
    info.lpsaActions = actions;
    if (ChangeServiceConfig2W(s->service, SERVICE_CONFIG_FAILURE_ACTIONS, &info) || !GetLastError())
        success(L"ChangeServiceConfig2");
    else
        result = last_failed(L"ChangeServiceConfig2");
done:
    tu_free(actions);
    return result;
}

/* ------------------------------------------------------------------ small changes */

static DWORD change_level(SCSESSION *s, const WCHAR *name, DWORD level, void *info)
{
    DWORD result;
    if ((result = open_manager(s, SC_MANAGER_CONNECT_)))
        return result;
    if ((result = open_service(s, name, SERVICE_CHANGE_CONFIG_, nullptr)))
        return result;
    if (ChangeServiceConfig2W(s->service, level, info) || !GetLastError()) {
        success(L"ChangeServiceConfig2");
        return 0;
    }
    return last_failed(L"ChangeServiceConfig2");
}

static DWORD cmd_privs(SCSESSION *s, const WCHAR *name, const WCHAR *list)
{
    DWORD result;
    if ((result = open_manager(s, SC_MANAGER_CONNECT_)))
        return result;
    if ((result = open_service(s, name, SERVICE_CHANGE_CONFIG_, nullptr)))
        return result;
    SIZE_T n = wcslen(list);
    WCHAR *multi = tu_alloc((n + 2) * sizeof(WCHAR));
    memcpy(multi, list, n * sizeof(WCHAR));
    for (SIZE_T i = 0; i < n; i++)
        if (multi[i] == L'/')
            multi[i] = 0;
    SERVICE_REQUIRED_PRIVILEGES_INFOW info = {multi};
    if (ChangeServiceConfig2W(s->service, SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO, &info) || !GetLastError())
        success(L"ChangeServiceConfig2");
    else
        result = last_failed(L"ChangeServiceConfig2");
    tu_free(multi);
    return result;
}

/* ------------------------------------------------------------------ queries of the configuration */

static const WCHAR *start_name(DWORD start, BOOL delayed)
{
    switch (start) {
    case 0: return L"BOOT_START";
    case 1: return L"SYSTEM_START";
    case 2: return delayed ? L"AUTO_START  (DELAYED)" : L"AUTO_START";
    case 3: return L"DEMAND_START";
    case 4: return L"DISABLED";
    default: return L"ERROR";
    }
}

static const WCHAR *error_name(DWORD e)
{
    switch (e) {
    case 0: return L"IGNORE";
    case 1: return L"NORMAL";
    case 2: return L"SEVERE";
    case 3: return L"CRITICAL";
    default: return L"ERROR";
    }
}

static DWORD cmd_qc(SCSESSION *s, const WCHAR *name)
{
    DWORD result;
    if ((result = open_manager(s, SC_MANAGER_CONNECT_)))
        return result;
    if ((result = open_service(s, name, SERVICE_QUERY_CONFIG_, nullptr)))
        return result;
    DWORD delayed = 0, needed;
    if (!QueryServiceConfig2W(s->service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, (LPBYTE)&delayed, 4, &needed)) {
        DWORD error = GetLastError();
        if (error != ERROR_INVALID_LEVEL)
            return api_failed(L"QueryServiceConfig2 (delayed autostart flag)", error);
    }
    DWORD error;
    QUERY_SERVICE_CONFIGW *c = query_config(s->service, 0, &error);
    if (!c) {
        result = api_failed(L"QueryServiceConfig", error);
        return result;
    }
    success(L"QueryServiceConfig");
    DWORD type = c->dwServiceType & ~0x100u;
    const WCHAR *tn = type_name(type);
    if (!wcscmp(tn, L" ERROR "))
        tn = L"ERROR";
    TU_STR out = {};
    tu_str_format(&out, L"\nSERVICE_NAME: %s\n", name);
    /* the names here have no trailing blank, the layout has its own. */
    TU_STR trimmed = {};
    tu_str_puts(&trimmed, tn);
    while (trimmed.length && trimmed.data[trimmed.length - 1] == L' ')
        trimmed.data[--trimmed.length] = 0;
    tu_str_format(&out, L"        TYPE               : %x  %s %s\n", c->dwServiceType, tu_str_text(&trimmed),
                  (c->dwServiceType & 0x100) ? L"(interactive)" : L"");
    tu_str_free(&trimmed);
    tu_str_format(&out, L"        START_TYPE         : %x   %s\n", c->dwStartType, start_name(c->dwStartType, delayed));
    tu_str_format(&out, L"        ERROR_CONTROL      : %x   %s\n", c->dwErrorControl, error_name(c->dwErrorControl));
    tu_str_format(&out, L"        BINARY_PATH_NAME   : %s\n", c->lpBinaryPathName ? c->lpBinaryPathName : L"");
    tu_str_format(&out, L"        LOAD_ORDER_GROUP   : %s\n", c->lpLoadOrderGroup ? c->lpLoadOrderGroup : L"");
    tu_str_format(&out, L"        TAG                : %lu\n", c->dwTagId);
    tu_str_format(&out, L"        DISPLAY_NAME       : %s\n", c->lpDisplayName ? c->lpDisplayName : L"");
    const WCHAR *dep = c->lpDependencies;
    tu_str_format(&out, L"        DEPENDENCIES       : %s\n", dep ? dep : L"");
    if (dep && *dep) {
        dep += wcslen(dep) + 1;
        while (*dep) {
            tu_str_format(&out, L"                           : %s\n", dep);
            dep += wcslen(dep) + 1;
        }
    }
    tu_str_format(&out, L"        SERVICE_START_NAME : %s\n", c->lpServiceStartName ? c->lpServiceStartName : L"");
    tu_write(TU_STDOUT, out.data, out.length);
    tu_str_free(&out);
    tu_free(c);
    return 0;
}

static DWORD cmd_qinfo(SCSESSION *s, const WCHAR *name, DWORD level)
{
    DWORD result;
    if ((result = open_manager(s, SC_MANAGER_CONNECT_)))
        return result;
    if ((result = open_service(s, name, SERVICE_QUERY_CONFIG_, nullptr)))
        return result;
    DWORD error;
    BYTE *info = query_config(s->service, level, &error);
    if (!info)
        return api_failed(L"QueryServiceConfig2", error);
    success(L"QueryServiceConfig2");
    TU_STR out = {};
    switch (level) {
    case SERVICE_CONFIG_DESCRIPTION: {
        SERVICE_DESCRIPTIONW *d = (SERVICE_DESCRIPTIONW *)info;
        tu_str_format(&out, L"\nSERVICE_NAME: %s\nDESCRIPTION:  %s\n", name, d->lpDescription ? d->lpDescription : L"");
        break;
    }
    case SERVICE_CONFIG_FAILURE_ACTIONS: {
        SERVICE_FAILURE_ACTIONSW *f = (SERVICE_FAILURE_ACTIONSW *)info;
        tu_str_format(&out, L"\nSERVICE_NAME: %s\n        RESET_PERIOD (in seconds)    : ", name);
        if (f->dwResetPeriod == (DWORD)-1)
            tu_str_puts(&out, L"INFINITE");
        else
            tu_str_format(&out, L"%lu", f->dwResetPeriod);
        tu_str_format(&out, L"\n        REBOOT_MESSAGE               : %s\n", f->lpRebootMsg ? f->lpRebootMsg : L"");
        tu_str_format(&out, L"        COMMAND_LINE                 : %s\n", f->lpCommand ? f->lpCommand : L"");
        BOOL first = TRUE;
        for (DWORD i = 0; i < f->cActions; i++) {
            SC_ACTION *a = &f->lpsaActions[i];
            const WCHAR *what;
            if (a->Type == SC_ACTION_RESTART)
                what = L"RESTART";
            else if (a->Type == SC_ACTION_REBOOT)
                what = L"REBOOT";
            else if (a->Type == SC_ACTION_RUN_COMMAND)
                what = L"RUN PROCESS";
            else
                continue;
            tu_str_format(&out, first ? L"        FAILURE_ACTIONS              : %s -- Delay = %lu milliseconds.\n"
                                      : L"                                       %s -- Delay = %lu milliseconds.\n",
                          what, a->Delay);
            first = FALSE;
        }
        tu_str_putc(&out, L'\n');
        break;
    }
    case SERVICE_CONFIG_FAILURE_ACTIONS_FLAG:
        tu_str_format(&out, L"\nSERVICE_NAME: %s\nFAILURE_ACTIONS_ON_NONCRASH_FAILURES:  %s\n", name,
                      *(DWORD *)info ? L"TRUE" : L"FALSE");
        break;
    case SERVICE_CONFIG_SERVICE_SID_INFO: {
        DWORD t = *(DWORD *)info;
        tu_str_format(&out, L"\nSERVICE_NAME: %s\nSERVICE_SID_TYPE:  %s\n", name,
                      t == 1 ? L"UNRESTRICTED" : t == 3 ? L"RESTRICTED" : L"NONE");
        break;
    }
    case SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO: {
        SERVICE_REQUIRED_PRIVILEGES_INFOW *p = (SERVICE_REQUIRED_PRIVILEGES_INFOW *)info;
        const WCHAR *list = p->pmszRequiredPrivileges;
        tu_str_format(&out, L"\nSERVICE_NAME: %s\n        PRIVILEGES       : %s\n", name, list ? list : L"");
        if (list && *list) {
            list += wcslen(list) + 1;
            while (*list) {
                tu_str_format(&out, L"                         : %s\n", list);
                list += wcslen(list) + 1;
            }
        }
        break;
    }
    case SERVICE_CONFIG_TRIGGER_INFO:
        tu_str_format(&out, L"\n    The service %s has not registered for any start or stop triggers.\n\n", name);
        break;
    case SERVICE_CONFIG_LAUNCH_PROTECTED_: {
        DWORD t = *(DWORD *)info;
        tu_str_format(&out, L"SERVICE %s PROTECTION LEVEL: %s.\n", name,
                      t == 0 ? L"NONE" : t == 1 ? L"WINDOWS" : t == 2 ? L"WINDOWS LIGHT" : t == 3 ? L"ANTIMALWARE LIGHT"
                                                                                                  : L"INVALID VALUE");
        break;
    }
    default:
        break;
    }
    tu_write(TU_STDOUT, out.data, out.length);
    tu_str_free(&out);
    tu_free(info);
    return 0;
}

/* ------------------------------------------------------------------ names, dependents, security */

static DWORD cmd_names(SCSESSION *s, const WCHAR *input, DWORD chars, BOOL display)
{
    DWORD result;
    if ((result = open_manager(s, SC_MANAGER_CONNECT_)))
        return result;
    WCHAR *buffer = chars ? tu_alloc(chars * sizeof(WCHAR)) : nullptr;
    DWORD size = chars;
    const WCHAR *what = display ? L"GetServiceDisplayName" : L"GetServiceKeyName";
    BOOL ok = display ? GetServiceDisplayNameW(s->manager, input, buffer, &size)
                      : GetServiceKeyNameW(s->manager, input, buffer, &size);
    if (ok) {
        success(what);
        say(L"Name = %s\n", buffer);
    } else {
        result = last_failed(what);
        if (result == ERROR_INSUFFICIENT_BUFFER)
            more_data(what, size + 1, 0, FALSE);
    }
    tu_free(buffer);
    return result;
}

static DWORD cmd_enum_depend(SCSESSION *s, const WCHAR *name, DWORD bytes)
{
    DWORD result;
    if ((result = open_manager(s, SC_MANAGER_CONNECT_)))
        return result;
    if ((result = open_service(s, name, SERVICE_ENUMERATE_DEPENDENTS_, nullptr)))
        return result;
    BYTE *buffer = bytes ? tu_alloc(bytes) : nullptr;
    DWORD needed = 0, returned = 0;
    BOOL ok = EnumDependentServicesW(s->service, SERVICE_STATE_ALL, (LPENUM_SERVICE_STATUSW)buffer, bytes, &needed,
                                     &returned);
    DWORD error = ok ? 0 : GetLastError();
    if (!error || error == ERROR_MORE_DATA) {
        say(L"[SC] EnumDependentServices: entriesread = %lu\n", returned);
        ENUM_SERVICE_STATUSW *list = (ENUM_SERVICE_STATUSW *)buffer;
        for (DWORD i = 0; i < returned; i++)
            display_status(list[i].lpServiceName, list[i].lpDisplayName, (SERVICE_STATUS_PROCESS *)&list[i].ServiceStatus,
                           TRUE);
        if (error == ERROR_MORE_DATA)
            more_data(L"EnumDependentServices", needed, 0, FALSE);
        result = error;
    } else {
        result = api_failed(L"EnumDependentServices", error);
    }
    tu_free(buffer);
    return result;
}

static DWORD cmd_sdshow(SCSESSION *s, const WCHAR *name, BOOL show_rights)
{
    BOOL manager = equals(name, L"SCMANAGER");
    BOOLEAN previous = FALSE;
    NTSTATUS privilege = RtlAdjustPrivilege(SE_SECURITY_PRIVILEGE_, TRUE, FALSE, &previous);
    DWORD access = READ_CONTROL_ | (privilege >= 0 ? ACCESS_SYSTEM_SECURITY_ : 0);
    DWORD error = 0;
    HANDLE object = nullptr;
    if (!manager && !(s->manager = OpenSCManagerW(s->machine, nullptr, SC_MANAGER_CONNECT_)))
        return last_failed(L"OpenSCManager");
    if (manager)
        object = OpenSCManagerW(s->machine, nullptr, access);
    else
        object = OpenServiceW(s->manager, name, access);
    if (!object)
        error = GetLastError();
    if (privilege >= 0)
        RtlAdjustPrivilege(SE_SECURITY_PRIVILEGE_, previous, FALSE, &previous);
    if (error == ERROR_ACCESS_DENIED && (access & ACCESS_SYSTEM_SECURITY_)) {
        access = READ_CONTROL_;
        error = 0;
        object = manager ? OpenSCManagerW(s->machine, nullptr, access) : OpenServiceW(s->manager, name, access);
        if (!object)
            error = GetLastError();
    }
    if (error)
        return api_failed(manager ? L"OpenSCManager" : L"OpenService", error);
    s->service = object;
    DWORD information = 4 | ((access & ACCESS_SYSTEM_SECURITY_) ? 8 : 0);
    DWORD size = 1024, needed = 0;
    BYTE *sd = tu_alloc(size);
    if (!QueryServiceObjectSecurity(object, information, sd, size, &needed)) {
        error = GetLastError();
        if (error == ERROR_INSUFFICIENT_BUFFER) {
            tu_free(sd);
            sd = tu_alloc(needed);
            size = needed;
            error = QueryServiceObjectSecurity(object, information, sd, size, &needed) ? 0 : GetLastError();
        }
        if (error) {
            tu_free(sd);
            return api_failed(L"QueryServiceObjectSecurity", error);
        }
    }
    WCHAR *text = nullptr;
    if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(sd, SECURITY_DESCRIPTOR_REVISION_1_, information, &text,
                                                              nullptr)) {
        error = GetLastError();
        tu_free(sd);
        return api_failed(L"ConvertSecurityDescriptorToStringSecurityDescriptor", error);
    }
    say(L"\n%s\n", text);
    LocalFree(text);
    if (show_rights)
        tu_puts(manager ? kSdRightsManager : kSdRights);
    tu_free(sd);
    return 0;
}

static DWORD cmd_sdset(SCSESSION *s, const WCHAR *name, const WCHAR *sddl)
{
    BOOL manager = equals(name, L"SCMANAGER");
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SECURITY_DESCRIPTOR_REVISION_1_, &sd, nullptr))
        return last_failed(L"ConvertStringSecurityDescriptorToSecurityDescriptor");
    WORD control = 0;
    DWORD revision = 0;
    if (!GetSecurityDescriptorControl(sd, &control, &revision)) {
        DWORD error = GetLastError();
        LocalFree(sd);
        return api_failed(L"GetSecurityDescriptorControl", error);
    }
    DWORD access = 0, information = 0;
    if (control & SE_DACL_PRESENT_) {
        access |= WRITE_DAC_;
        information |= 4;
    }
    BOOLEAN previous = FALSE;
    NTSTATUS privilege = -1;
    if (control & SE_SACL_PRESENT_) {
        information |= 8;
        access |= ACCESS_SYSTEM_SECURITY_;
        privilege = RtlAdjustPrivilege(SE_SECURITY_PRIVILEGE_, TRUE, FALSE, &previous);
    }
    DWORD result = 0;
    if (!manager && !(s->manager = OpenSCManagerW(s->machine, nullptr, GENERIC_WRITE_))) {
        result = last_failed(L"OpenSCManager");
        LocalFree(sd);
        return result;
    }
    HANDLE object = manager ? OpenSCManagerW(s->machine, nullptr, access) : OpenServiceW(s->manager, name, access);
    if (!object) {
        result = last_failed(manager ? L"OpenSCManager" : L"OpenService");
        LocalFree(sd);
        return result;
    }
    s->service = object;
    if (SetServiceObjectSecurity(object, information, sd))
        success(L"SetServiceObjectSecurity");
    else
        result = last_failed(L"SetServiceObjectSecurity");
    if (privilege >= 0)
        RtlAdjustPrivilege(SE_SECURITY_PRIVILEGE_, previous, FALSE, &previous);
    LocalFree(sd);
    return result;
}

static DWORD cmd_showsid(SCSESSION *s, const WCHAR *name)
{
    DWORD result;
    if ((result = open_manager(s, SC_MANAGER_CONNECT_)))
        return result;
    UNICODE_STRING uname;
    RtlInitUnicodeString(&uname, name);
    BYTE sid[68];
    ULONG size = sizeof(sid);
    NTSTATUS status = RtlCreateServiceSid(&uname, sid, &size);
    if (status < 0)
        return api_failed(L"RtlCreateServiceSid", RtlNtStatusToDosError(status));
    WCHAR *text = nullptr;
    if (!ConvertSidToStringSidW(sid, &text))
        return last_failed(L"ConvertSidToStringSid");
    SC_HANDLE service = OpenServiceW(s->manager, name, SERVICE_QUERY_CONFIG_);
    BOOL active = FALSE;
    if (service) {
        DWORD error;
        DWORD *type = query_config(service, SERVICE_CONFIG_SERVICE_SID_INFO, &error);
        active = type && *type != 0;
        tu_free(type);
        CloseServiceHandle(service);
    }
    say(active ? L"\nNAME: %s\nSERVICE SID: %s\nSTATUS: Active\n" : L"\nNAME: %s \nSERVICE SID: %s\nSTATUS: Inactive\n",
        name, text);
    LocalFree(text);
    return 0;
}

static DWORD cmd_querylock(SCSESSION *s)
{
    DWORD result;
    if ((result = open_manager(s, SC_MANAGER_QUERY_LOCK_STATUS_)))
        return result;
    DWORD needed = 0;
    BYTE buffer[512];
    if (!QueryServiceLockStatusW(s->manager, (LPQUERY_SERVICE_LOCK_STATUSW)buffer, sizeof(buffer), &needed))
        return last_failed(L"QueryServiceLockStatus");
    QUERY_SERVICE_LOCK_STATUSW *l = (QUERY_SERVICE_LOCK_STATUSW *)buffer;
    success(L"QueryServiceLockStatus");
    say(L"\n%s\n", l->fIsLocked ? L"        IsLocked      : TRUE" : L"        IsLocked      : FALSE");
    if (l->fIsLocked)
        say(L"        LockOwner     : %s\n        LockDuration  : %lu (seconds since acquired)\n\n",
            l->lpLockOwner ? l->lpLockOwner : L"", l->dwLockDuration);
    return 0;
}

/* ------------------------------------------------------------------ the command */

static BOOL is(const WCHAR *a, const WCHAR *b)
{
    return equals(a, b);
}

static DWORD run(int argc, WCHAR **argv)
{
    SCSESSION s = {};
    DWORD result = 0;
    if (argc < 2)
        return usage();
    int first = 1;
    if (!wcsncmp(argv[1], L"\\\\", 2)) {
        if (argc == 2)
            return usage();
        first = 2;
        s.machine = argv[1];
    }
    const WCHAR *cmd = argv[first];
    WCHAR **args = argv + first + 1;
    int count = argc - first - 1;
    /* commands that need the service name first. */
#define NEED(n, text)                                                                                                  \
    if (count < (n)) {                                                                                                 \
        tu_puts(text);                                                                                                 \
        return ERROR_INVALID_COMMAND_LINE_;                                                                            \
    }

    if (is(cmd, L"query")) {
        result = cmd_query(&s, args, count, TRUE);
    } else if (is(cmd, L"queryex")) {
        result = cmd_query(&s, args, count, FALSE);
    } else if (is(cmd, L"start")) {
        NEED(1, kStartUsage);
        result = cmd_start(&s, args, count);
    } else if (is(cmd, L"pause")) {
        NEED(1, kPauseUsage);
        result = cmd_control(&s, args[0], SERVICE_CONTROL_PAUSE, nullptr, nullptr);
    } else if (is(cmd, L"interrogate")) {
        NEED(1, kInterrogateUsage);
        result = cmd_control(&s, args[0], SERVICE_CONTROL_INTERROGATE, nullptr, nullptr);
    } else if (is(cmd, L"continue")) {
        NEED(1, kContinueUsage);
        result = cmd_control(&s, args[0], SERVICE_CONTROL_CONTINUE, nullptr, nullptr);
    } else if (is(cmd, L"stop")) {
        if (count < 1 || count > 3)
            return usage_now(kStopUsage);
        result = cmd_control(&s, args[0], SERVICE_CONTROL_STOP, count >= 2 ? args[1] : nullptr,
                             count == 3 ? args[2] : nullptr);
    } else if (is(cmd, L"control")) {
        NEED(2, kControlUsage);
        DWORD code = to_number(args[1]);
        if (!code) {
            if (is(args[1], L"paramchange"))
                code = 6;
            else if (is(args[1], L"netbindadd"))
                code = 7;
            else if (is(args[1], L"netbindremove"))
                code = 8;
            else if (is(args[1], L"netbindenable"))
                code = 9;
            else if (is(args[1], L"netbinddisable"))
                code = 10;
        }
        result = cmd_control(&s, args[0], code, nullptr, nullptr);
    } else if (is(cmd, L"config")) {
        if (count < 2)
            return usage_of(kConfigUsage);
        result = cmd_config(&s, args[0], args + 1, count - 1);
    } else if (is(cmd, L"description")) {
        NEED(2, kDescriptionUsage);
        SERVICE_DESCRIPTIONW info = {args[1]};
        result = change_level(&s, args[0], SERVICE_CONFIG_DESCRIPTION, &info);
    } else if (is(cmd, L"failure")) {
        if (count < 2)
            return usage_of(kFailureUsage);
        result = cmd_failure(&s, args[0], args + 1, count - 1);
    } else if (is(cmd, L"privs")) {
        NEED(2, kPrivsUsage);
        result = cmd_privs(&s, args[0], args[1]);
    } else if (is(cmd, L"failureflag")) {
        NEED(2, kFailureflagUsage);
        SERVICE_FAILURE_ACTIONS_FLAG info = {(BOOL)to_number(args[1])};
        result = change_level(&s, args[0], SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &info);
    } else if (is(cmd, L"sidtype")) {
        DWORD type = 0;
        BOOL valid = count >= 2;
        if (valid && is(args[1], L"unrestricted"))
            type = 1;
        else if (valid && is(args[1], L"restricted"))
            type = 3;
        else if (valid && is(args[1], L"none"))
            type = 0;
        else
            valid = FALSE;
        if (!valid)
            return usage_now(kSidtypeUsage);
        SERVICE_SID_INFO info = {type};
        result = change_level(&s, args[0], SERVICE_CONFIG_SERVICE_SID_INFO, &info);
    } else if (is(cmd, L"qc")) {
        NEED(1, kQcUsage);
        result = cmd_qc(&s, args[0]);
    } else if (is(cmd, L"qdescription")) {
        NEED(1, kQdescriptionUsage);
        result = cmd_qinfo(&s, args[0], SERVICE_CONFIG_DESCRIPTION);
    } else if (is(cmd, L"qfailure")) {
        NEED(1, kQfailureUsage);
        result = cmd_qinfo(&s, args[0], SERVICE_CONFIG_FAILURE_ACTIONS);
    } else if (is(cmd, L"qfailureflag")) {
        NEED(1, kQfailureflagUsage);
        result = cmd_qinfo(&s, args[0], SERVICE_CONFIG_FAILURE_ACTIONS_FLAG);
    } else if (is(cmd, L"qsidtype")) {
        NEED(1, kQsidtypeUsage);
        result = cmd_qinfo(&s, args[0], SERVICE_CONFIG_SERVICE_SID_INFO);
    } else if (is(cmd, L"qprivs")) {
        NEED(1, kQprivsUsage);
        result = cmd_qinfo(&s, args[0], SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO);
    } else if (is(cmd, L"qtriggerinfo")) {
        NEED(1, kQtriggerUsage);
        result = cmd_qinfo(&s, args[0], SERVICE_CONFIG_TRIGGER_INFO);
    } else if (is(cmd, L"qprotection")) {
        NEED(1, kQprotectionUsage);
        result = cmd_qinfo(&s, args[0], SERVICE_CONFIG_LAUNCH_PROTECTED_);
    } else if (is(cmd, L"querylock")) {
        result = cmd_querylock(&s);
    } else if (is(cmd, L"delete")) {
        NEED(1, kDeleteUsage);
        if (!(result = open_manager(&s, SC_MANAGER_CONNECT_)) && !(result = open_service(&s, args[0], DELETE_, nullptr))) {
            if (DeleteService(s.service))
                success(L"DeleteService");
            else
                result = last_failed(L"DeleteService");
        }
    } else if (is(cmd, L"create")) {
        if (count < 2)
            return usage_of(kCreateUsage);
        result = cmd_create(&s, args[0], args + 1, count - 1);
    } else if (is(cmd, L"boot")) {
        BOOL ok = count >= 1 && is(args[0], L"ok");
        if (count < 1 || (!ok && !is(args[0], L"bad")))
            return usage_now(kBootUsage);
        if (!NotifyBootConfigStatus(ok))
            result = last_failed(L"NotifyBootConfigStatus");
    } else if (is(cmd, L"GetDisplayName")) {
        NEED(1, kGetDisplayUsage);
        result = cmd_names(&s, args[0], count > 1 ? to_number(args[1]) : 500, TRUE);
    } else if (is(cmd, L"GetKeyName")) {
        NEED(1, kGetKeyUsage);
        result = cmd_names(&s, args[0], count > 1 ? to_number(args[1]) : 500, FALSE);
    } else if (is(cmd, L"EnumDepend")) {
        NEED(1, kEnumDependUsage);
        result = cmd_enum_depend(&s, args[0], count > 1 ? to_number(args[1]) : 500);
    } else if (is(cmd, L"sdshow")) {
        NEED(1, kSdshowUsage);
        result = cmd_sdshow(&s, args[0], count > 1 && is(args[1], L"showrights"));
    } else if (is(cmd, L"sdset")) {
        NEED(2, kSdsetUsage);
        result = cmd_sdset(&s, args[0], args[1]);
    } else if (is(cmd, L"showsid")) {
        if (argc != 3 || first != 1)
            return usage_now(kShowsidUsage);
        result = cmd_showsid(&s, args[0]);
    } else {
        if (!g_retry)
            return usage();
        tu_puts(L"\nERROR:  Unrecognized command\n\n");
        return usage();
    }
    close_session(&s);
    return result;
}

/* a first pass leaves arguments that hold both a keyword and a value as they are, the second cuts them. */
static WCHAR **split_arguments(int *argc, WCHAR **argv)
{
    int extra = 0;
    for (int i = 2; i < *argc; i++) {
        WCHAR *eq = wcschr(argv[i], L'=');
        if (eq && eq[1])
            extra++;
    }
    if (!extra)
        return argv;
    WCHAR **out = tu_alloc((*argc + extra + 1) * sizeof(WCHAR *));
    int n = 0;
    out[n++] = argv[0];
    out[n++] = argv[1];
    for (int i = 2; i < *argc; i++) {
        WCHAR *eq = wcschr(argv[i], L'=');
        if (eq && eq[1]) {
            out[n++] = tu_dup_n(argv[i], (SIZE_T)(eq - argv[i]) + 1);
            out[n++] = tu_dup(eq + 1);
        } else {
            out[n++] = argv[i];
        }
    }
    *argc = n;
    return out;
}

int tool_main(void)
{
    DWORD result = run(tu_argc, tu_argv);
    if (g_retry) {
        int argc = tu_argc;
        WCHAR **argv = split_arguments(&argc, tu_argv);
        result = run(argc, argv);
    }
    return (int)result;
}
