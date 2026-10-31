/*
 * lodctr.exe: registers the performance counters of a provider from its manifest.
 *
 *   lodctr /M:manifest.xml [/Q]
 *
 * the manifest names a provider with counter sets and their counters. they are written below the _V2Providers key of
 * Perflib, with the numbers that the counter sets and their counters take from the list of counter names, as windows
 * writes them. the text based definitions of the older kind (an .ini file) and the other options are not supported.
 */
#include "perfman.h"

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

static LONG set_dword(HKEY key, const WCHAR *name, DWORD value)
{
    return RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
}

static LONG set_text(HKEY key, const WCHAR *name, DWORD type, const WCHAR *text)
{
    return RegSetValueExW(key, name, 0, type, (const BYTE *)text, (DWORD)((wcslen(text) + 1) * sizeof(WCHAR)));
}

/* the 56 bytes that describe a counter to the readers of the registry. */
static void counter_block(BYTE *out, const PM_COUNTER *c)
{
    DWORD fields[14] = {};
    fields[0] = c->Id;
    fields[1] = c->Type;
    memcpy(&fields[2], &c->DefaultScale, sizeof(c->DefaultScale));
    fields[4] = c->DetailLevel;
    fields[6] = 0xFFFFFFFF;
    fields[8] = 0xFFFFFFFF;
    memcpy(out, fields, sizeof(fields));
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
            tu_print(L"\nLoads performance counters from a manifest.\n\nlodctr /M:<manifest> [/Q]\n\n");
            return 0;
        } else {
            tu_eprint(L"Error: Unable to use \"%s\"; performance counters are loaded from a manifest with /M only.\n", tu_argv[i]);
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

    HKEY perflib = nullptr, providers = nullptr;
    LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, PM_PERFLIB, 0, KEY_ALL_ACCESS, &perflib);
    if (status) {
        pm_free(&manifest);
        return fail(L"Unable to open the Perflib key.", (DWORD)status);
    }
    DWORD last = 0, size = sizeof(last), type;
    if (RegQueryValueExW(perflib, L"Last Counter", nullptr, &type, (BYTE *)&last, &size) || type != REG_DWORD)
        last = 0;
    if (last < 1846)
        last = 1846;
    last &= ~1u;
    DWORD strings = 0;
    for (DWORD p = 0; p < manifest.Count; p++)
        for (DWORD s = 0; s < manifest.Providers[p].SetCount; s++)
            strings += manifest.Providers[p].Sets[s].Count + 1;
    if (last > 9999999 || 9999998 - last <= 2 * strings) {
        RegCloseKey(perflib);
        pm_free(&manifest);
        return fail(L"There are too many counters for the Perflib key.", ERROR_ARITHMETIC_OVERFLOW);
    }
    BOOL any_new = FALSE;
    status = RegCreateKeyExW(perflib, L"_V2Providers", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &providers, nullptr);
    DWORD next = last + 2;
    for (DWORD p = 0; !status && p < manifest.Count; p++) {
        PM_PROVIDER *provider = &manifest.Providers[p];
        WCHAR *name = pm_key_name(provider->Guid);
        HKEY key = nullptr;
        DWORD disposition = 0;
        status = RegCreateKeyExW(providers, name, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, &disposition);
        tu_free(name);
        if (status)
            break;
        /* a provider that is there already is left as it is. */
        if (disposition != REG_CREATED_NEW_KEY) {
            RegCloseKey(key);
            continue;
        }
        any_new = TRUE;
        status = set_dword(key, L"ProviderType", provider->Type);
        if (!status)
            status = set_text(key, L"ProviderName", REG_SZ, provider->Name);
        if (!status)
            status = set_text(key, L"ApplicationIdentity", REG_EXPAND_SZ, provider->Application);
        for (DWORD s = 0; !status && s < provider->SetCount; s++) {
            PM_SET *set = &provider->Sets[s];
            if (!set->Guid) {
                status = ERROR_INVALID_DATA;
                break;
            }
            WCHAR *set_name = pm_key_name(set->Guid);
            HKEY set_key = nullptr;
            status = RegCreateKeyExW(key, set_name, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &set_key, &disposition);
            tu_free(set_name);
            if (status)
                break;
            if (disposition != REG_CREATED_NEW_KEY) {
                status = ERROR_ALREADY_EXISTS;
            } else {
                status = set_dword(set_key, L"NameResource", 0xFFFFFFFF);
                if (!status)
                    status = set_dword(set_key, L"ExplainResource", 0xFFFFFFFF);
                if (!status)
                    status = set_text(set_key, L"NeutralName", REG_SZ, set->Name);
                if (!status)
                    status = set_dword(set_key, L"InstanceType", set->InstanceType);
                if (!status)
                    status = set_dword(set_key, L"First Counter", next);
                if (!status)
                    status = set_dword(set_key, L"Last Counter", next + 2 * set->Count);
                BYTE *block = tu_alloc((SIZE_T)(set->Count ? set->Count : 1) * 56);
                for (DWORD c = 0; c < set->Count; c++)
                    counter_block(block + (SIZE_T)c * 56, &set->Counters[c]);
                if (!status)
                    status = RegSetValueExW(set_key, L"CounterBlock", 0, REG_BINARY, block, set->Count * 56);
                tu_free(block);
                if (!status)
                    status = set_dword(set_key, L"CounterCount", set->Count);
                next += 2 * set->Count + 2;
            }
            RegCloseKey(set_key);
        }
        RegCloseKey(key);
        if (status) {
            WCHAR *again = pm_key_name(provider->Guid);
            RegDeleteTreeW(providers, again);
            tu_free(again);
        }
    }
    if (!status && any_new) {
        /* the counter names the new sets used are taken from the numbers that follow the old last one. */
        status = set_dword(perflib, L"Last Counter", last + 2 * strings);
        if (!status)
            status = set_dword(perflib, L"Last Help", last + 2 * strings + 1);
    }
    if (providers)
        RegCloseKey(providers);
    RegCloseKey(perflib);
    pm_free(&manifest);
    if (status)
        return fail(L"Unable to install the provider.", (DWORD)status);
    if (!g_quiet)
        tu_print(L"Info: Successfully installed performance counters in %s\n", path);
    return 0;
}
