/*
 * the command line: the program name and the arguments split the way the host does it, the options that start with // and
 * what they set, and the arguments that stay for the script.
 */
#include "wsh.h"

static const struct {
    PCWSTR Name;
    int Code;
    BOOL Value;
} g_options[] = {
    {L"B", 'B', FALSE}, {L"D", 'D', FALSE}, {L"E", 'E', TRUE},  {L"H", 'H', TRUE},       {L"I", 'I', FALSE},
    {L"Job", 'J', TRUE}, {L"S", 'S', FALSE}, {L"T", 'T', TRUE},  {L"X", 'X', FALSE},      {L"CP", 'c', TRUE},
    {L"logo", 'l', FALSE}, {L"nologo", 'n', FALSE}, {L"U", 'U', FALSE}, {L"?", '?', FALSE},
};

/* the arguments of a line: blanks separate them and quotes group, a quote is dropped. With nothing to fill it counts. */
int WshSplitCommandLine(PWSTR line, PWSTR *out)
{
    int count = 0;
    PWSTR at = line;
    for (;;) {
        while (*at == L' ' || *at == L'\t')
            at++;
        if (!*at)
            return count;
        BOOL quoted = FALSE;
        PWSTR start = at;
        while (*at) {
            if ((*at == L' ' || *at == L'\t') && !quoted)
                break;
            if (*at == L'"') {
                quoted = !quoted;
                if (out) {
                    wcscpy(at, at + 1);
                    continue;
                }
            }
            at++;
        }
        if (out)
            out[count] = start;
        count++;
        if (!*at)
            return count;
        if (out)
            *at = 0;
        at++;
    }
}

/* the option that a word names: the code, and the value after a colon. 0 when there is none. */
static int parse_option(PCWSTR word, PCWSTR *value)
{
    if (!*word)
        return 0;
    for (int i = 0; i < (int)RTL_NUMBER_OF(g_options); i++) {
        SIZE_T length = wcslen(g_options[i].Name);
        if (_wcsnicmp(g_options[i].Name, word, length))
            continue;
        if (!g_options[i].Value)
            return word[length] ? ':' : g_options[i].Code;
        if (word[length] == L':') {
            *value = word[length + 1] ? word + length + 1 : nullptr;
            return g_options[i].Code;
        }
        if (!word[length]) {
            *value = nullptr;
            return g_options[i].Code;
        }
    }
    return 0;
}

static BOOL parse_integer(PCWSTR text, int *out)
{
    int sign = 1, value = 0, digits = 0;
    if (*text == L'+')
        text++;
    else if (*text == L'-') {
        sign = -1;
        text++;
    }
    if (*text < L'0')
        return FALSE;
    while (*text >= L'0' && *text <= L'9') {
        value = value * 10 + (*text - L'0');
        digits++;
        text++;
    }
    if (!digits)
        return FALSE;
    *out = sign * value;
    return *text == 0;
}

static HRESULT option_error(const SETTINGS *settings, UINT id, PCWSTR text)
{
    if (settings->Logo)
        WshShowLogo();
    WshPrintF(RES_BASE + 21, id, text);
    return WSH_E_REPORTED;
}

/* the arguments of the line without the program: the options are taken out and the rest kept in order. */
HRESULT WshParseOptions(SETTINGS *s, int *count, PWSTR *arguments)
{
    int total = *count, kept = 0, options = 0;
    BOOL allowed = TRUE, stop = FALSE, batch_seen = FALSE, host_seen = FALSE, logo_seen = FALSE;
    for (int i = 0; i < total; i++) {
        PWSTR word = arguments[i];
        PCWSTR text = nullptr, value = nullptr;
        if (stop)
            goto keep;
        if (word[0] == L'/' && word[1] == L'/') {
            if (!word[2]) {
                stop = TRUE;
                continue;
            }
            text = word + 2;
        } else if (!allowed)
            goto keep;
        else if (word[0] == L'/')
            text = word + 1;
        else if (word[0] == L'-' && word[1]) {
            if (word[1] == L'-' && !word[2]) {
                stop = TRUE;
                continue;
            }
            text = word + 1;
        } else
            goto keep;
        switch (parse_option(text, &value)) {
        case '/':
            stop = TRUE;
            break;
        case '?':
            s->Help = TRUE;
            break;
        case 'B':
            if (batch_seen && !s->Batch)
                return option_error(s, 3305, nullptr);
            batch_seen = TRUE;
            s->Batch = s->BatchSet = TRUE;
            break;
        case 'I':
            if (batch_seen && s->Batch)
                return option_error(s, 3305, nullptr);
            batch_seen = TRUE;
            s->Batch = FALSE;
            s->BatchSet = TRUE;
            break;
        case 'D':
            s->Debug = s->DebugSet = TRUE;
            break;
        case 'X':
            s->Debug = s->DebugSet = TRUE;
            s->DebugCommand = TRUE;
            break;
        case 'E':
            if (!value)
                return option_error(s, 3313, nullptr);
            s->Engine = value;
            break;
        case 'H':
            if (!value)
                return option_error(s, 3307, nullptr);
            if (!_wcsicmp(value, L"cscript")) {
                if (host_seen && s->Host != 'C')
                    return option_error(s, 3305, nullptr);
                host_seen = TRUE;
                s->Host = 'C';
                s->ChangeHost = TRUE;
            } else if (!_wcsicmp(value, L"wscript")) {
                if (host_seen && s->Host != 'W')
                    return option_error(s, 3305, nullptr);
                host_seen = TRUE;
                s->Host = 'W';
                s->ChangeHost = TRUE;
            } else
                return option_error(s, 3308, nullptr);
            break;
        case 'J':
            if (!value)
                return option_error(s, 3317, nullptr);
            if (s->JobCount < (int)RTL_NUMBER_OF(s->Jobs))
                s->Jobs[s->JobCount++] = value;
            break;
        case 'S':
            if (options || total - i - 1 || kept)
                s->Save = TRUE;
            else
                s->SaveAlone = TRUE;
            break;
        case 'T': {
            int seconds;
            if (!value)
                return option_error(s, 3300, nullptr);
            if (!parse_integer(value, &seconds) || seconds < 0)
                return option_error(s, 3301, nullptr);
            s->Timeout = (DWORD)seconds;
            s->TimeoutSet = s->TimeoutOption = TRUE;
            break;
        }
        case 'U':
            s->Unicode = TRUE;
            break;
        case 'c': {
            int page;
            CPINFO info;
            if (!value)
                return option_error(s, 3315, nullptr);
            if (!parse_integer(value, &page))
                page = -1;
            if (!GetCPInfo((UINT)page, &info))
                return option_error(s, 3316, nullptr);
            s->UseCodePage = TRUE;
            s->CodePage = (UINT)page;
            break;
        }
        case 'l':
            if (logo_seen && !s->Logo)
                return option_error(s, 3305, nullptr);
            logo_seen = TRUE;
            s->Logo = s->LogoSet = s->LogoOption = TRUE;
            break;
        case 'n':
            if (logo_seen && s->Logo)
                return option_error(s, 3305, nullptr);
            logo_seen = TRUE;
            s->Logo = FALSE;
            s->LogoSet = s->LogoOption = TRUE;
            break;
        default:
            return option_error(s, 3302, word);
        }
        options++;
        continue;
    keep:
        arguments[kept++] = word;
        allowed = FALSE;
    }
    *count = kept;
    return S_OK;
}
