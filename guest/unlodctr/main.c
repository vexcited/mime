/*
 * unlodctr.exe: removes the performance counters that lodctr registered from a manifest.
 *
 *   unlodctr /M:manifest.xml [/Q]
 *
 * the providers of the manifest lose their key below _V2Providers; the numbers they took from the counter names are not
 * given back. a provider that is not there is nothing to remove, which installers rely on to start from a clean state.
 */
#include "../lodctr/perfman.h"

static BOOL g_quiet;

static int fail(const WCHAR *what, DWORD error)
{
    if (!g_quiet) {
        TU_STR text = {};
        tu_error_text(error, &text);
        tu_eprint(L"Error: %s Error code: %lu (%s)\n", what, error, tu_str_text(&text));
        tu_str_free(&text);
    }
    return (int)error;
}

int tool_main(void)
{
    const WCHAR *manifest_path = nullptr;
    for (int i = 1; i < tu_argc; i++) {
        const WCHAR *value = pm_switch_value(tu_argv[i], L'm');
        if (value) {
            manifest_path = value;
        } else if ((tu_argv[i][0] == L'/' || tu_argv[i][0] == L'-') && tu_lower(tu_argv[i][1]) == L'q' && !tu_argv[i][2]) {
            g_quiet = TRUE;
        } else if ((tu_argv[i][0] == L'/' || tu_argv[i][0] == L'-') && tu_argv[i][1] == L'?') {
            tu_print(L"\nUnloads performance counters of a manifest.\n\nunlodctr /M:<manifest> [/Q]\n\n");
            return 0;
        } else {
            tu_eprint(L"Error: Unable to use \"%s\"; performance counters are unloaded with a manifest and /M only.\n", tu_argv[i]);
            return ERROR_NOT_SUPPORTED;
        }
    }
    if (!manifest_path || !*manifest_path) {
        tu_eprint(L"Error: Missing the manifest, use /M:<manifest>.\n");
        return ERROR_INVALID_PARAMETER;
    }
    WCHAR full[MAX_PATH];
    DWORD chars = GetFullPathNameW(manifest_path, TU_ARRAY(full), full, nullptr);
    const WCHAR *path = chars && chars < TU_ARRAY(full) ? full : manifest_path;
    PM_MANIFEST manifest;
    DWORD error = 0;
    if (!pm_parse(path, &manifest, &error))
        return fail(error == ERROR_INVALID_DATA ? L"The manifest is no counters manifest." : L"Unable to read the manifest.", error);
    HKEY providers = nullptr;
    LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, PM_PERFLIB L"\\_V2Providers", 0, KEY_ALL_ACCESS, &providers);
    if (status == ERROR_FILE_NOT_FOUND) {
        /* no provider was ever registered. */
        pm_free(&manifest);
        if (!g_quiet)
            tu_print(L"Info: Successfully uninstalled performance counters in %s\n", path);
        return 0;
    }
    if (status) {
        pm_free(&manifest);
        return fail(L"Unable to open the list of providers.", (DWORD)status);
    }
    for (DWORD p = 0; !status && p < manifest.Count; p++) {
        WCHAR *name = pm_key_name(manifest.Providers[p].Guid);
        status = RegDeleteTreeW(providers, name);
        tu_free(name);
        if (status == ERROR_FILE_NOT_FOUND)
            status = ERROR_SUCCESS;
    }
    RegCloseKey(providers);
    pm_free(&manifest);
    if (status)
        return fail(L"Unable to remove the provider.", (DWORD)status);
    if (!g_quiet)
        tu_print(L"Info: Successfully uninstalled performance counters in %s\n", path);
    return 0;
}
