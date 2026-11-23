#include "qmgr.h"

#define SETTINGS_KEY L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\BITS"
#define POLICY_KEY L"Software\\Policies\\Microsoft\\Windows\\BITS"
#define MIN_BUFFER (256 * 1024)
#define MAX_BUFFER (10 * 1024 * 1024)

QM_CONFIG g_config;

/* the per priority default cost flags, PolicyMonitor's defaults for Foreground, High, Normal and Low. */
static const DWORD g_cost_defaults[4] = {0x800000FF, 0x8000006F, 0x80000065, 0x80000021};
static const WCHAR *const g_cost_names[4] = {L"ForegroundTransferPolicy", L"HighTransferPolicy", L"NormalTransferPolicy",
                                             L"LowTransferPolicy"};
static HKEY g_policy;

static BOOL read_dword(HKEY root, const WCHAR *path, const WCHAR *name, DWORD *value)
{
    DWORD size = sizeof(*value);
    return !RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, value, &size);
}

static DWORD setting(const WCHAR *name, DWORD fallback)
{
    DWORD value;
    return read_dword(HKEY_LOCAL_MACHINE, SETTINGS_KEY, name, &value) ? value : fallback;
}

DWORD ConfigGetDword(const WCHAR *name, DWORD fallback)
{
    return setting(name, fallback);
}

void ConfigSetDword(const WCHAR *name, DWORD value)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, SETTINGS_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr))
        return;
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
}

static DWORD policy(const WCHAR *name, DWORD fallback, BOOL *present)
{
    DWORD value;
    BOOL found = read_dword(HKEY_LOCAL_MACHINE, POLICY_KEY, name, &value);
    if (present)
        *present = found;
    return found ? value : fallback;
}

/* PolicyMonitor::LoadJobLimitsAndOtherParameters and the transfer policies, read again on a group policy change. */
void ConfigLoadPolicy(void)
{
    g_config.limits = TRUE;
    g_config.max_download_time = policy(L"MaxDownloadTime", 7776000, nullptr);
    g_config.max_jobs_user = policy(L"MaxJobsPerUser", 60, nullptr);
    g_config.max_jobs_machine = policy(L"MaxJobsPerMachine", 300, nullptr);
    if (g_config.max_jobs_user > g_config.max_jobs_machine) {
        g_config.max_jobs_user = 60;
        g_config.max_jobs_machine = 300;
    }
    g_config.max_files = policy(L"MaxFilesPerJob", 200, nullptr);
    g_config.max_ranges = policy(L"MaxRangesPerFile", 500, nullptr);
    for (int i = 0; i < 4; i++)
        g_config.cost_default[i] = policy(g_cost_names[i], g_cost_defaults[i], nullptr);
    /* JobInactivityTimeout is in seconds in the settings and in days as a policy, which wins. */
    g_config.inactivity = (ULONGLONG)setting(L"JobInactivityTimeout", 7776000) * 10000000ull;
    BOOL present;
    DWORD days = policy(L"JobInactivityTimeout", 0, &present);
    if (present && days)
        g_config.inactivity = (ULONGLONG)days * 86400ull * 10000000ull;
    g_config.bandwidth = policy(L"EnableBITSMaxBandwidth", 0, nullptr) != 0;
    g_config.rate_on = policy(L"MaxTransferRateOnSchedule", 0, nullptr);
    g_config.rate_off = policy(L"MaxTransferRateOffSchedule", 0, nullptr);
    DWORD from = policy(L"MaxBandwidthValidFrom", 8, nullptr), to = policy(L"MaxBandwidthValidTo", 18, nullptr);
    g_config.window_from = policy(L"MaxBandwidthValidFromSeconds", from * 3600, nullptr);
    g_config.window_to = policy(L"MaxBandwidthValidToSeconds", to * 3600, nullptr);
}

void ConfigLoad(void)
{
    g_config.retry_delay = setting(L"JobMinimumRetryDelay", 600);
    g_config.no_progress = setting(L"JobNoProgressTimeout", 1209600);
    g_config.quantum = setting(L"TimeQuantaLength", 300);
    if (!g_config.quantum)
        g_config.quantum = 300;
    DWORD buffer = setting(L"TransferBufferSize", 3 * 1024 * 1024);
    g_config.buffer_size = buffer < MIN_BUFFER ? MIN_BUFFER : buffer > MAX_BUFFER ? MAX_BUFFER : buffer;
    g_config.idle_stop_ms = setting(L"IdleStopDelayMilliseconds", 120000);
    g_config.low_priority_tcp = setting(L"EnableLowPriorityTcp", 0) != 0;
    DWORD setup = 0;
    read_dword(HKEY_LOCAL_MACHINE, L"System\\Setup", L"SystemSetupInProgress", &setup);
    g_config.setup_in_progress = setup != 0;
    ConfigLoadPolicy();
}

/* the key the scheduler watches for group policy changes, made when missing so there is one to watch. */
HKEY ConfigPolicyKey(void)
{
    if (!g_policy &&
        RegCreateKeyExW(HKEY_LOCAL_MACHINE, POLICY_KEY, 0, nullptr, 0, KEY_NOTIFY | KEY_READ, nullptr, &g_policy, nullptr))
        g_policy = nullptr;
    return g_policy;
}

/* the throttle of EnableBITSMaxBandwidth in bytes a second, 0 for none: the on schedule rate inside the window. */
DWORD ConfigRateLimit(void)
{
    if (!g_config.bandwidth)
        return 0;
    SYSTEMTIME now;
    GetLocalTime(&now);
    DWORD second = now.wHour * 3600u + now.wMinute * 60u + now.wSecond;
    BOOL inside = g_config.window_from <= g_config.window_to
                      ? second >= g_config.window_from && second < g_config.window_to
                      : second >= g_config.window_from || second < g_config.window_to;
    DWORD kbps = inside ? g_config.rate_on : g_config.rate_off;
    if (!kbps)
        return 0;
    return kbps * 1000 / 8;
}
