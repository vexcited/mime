/*
 * the environment: _environ and _wenviron, kept in sync with the
 * process environment block, and the getenv and _putenv families.
 */
#include "msvcrt_private.h"

char **MsvcrtEnviron;
wchar_t **MsvcrtWenviron;
char **MsvcrtInitenv;
wchar_t **MsvcrtWinitenv;

/* the exported data names of the same pointers. */
char **_environ;
wchar_t **_wenviron;
char **__initenv;
wchar_t **__winitenv;

static void sync_exports(void)
{
    _environ = MsvcrtEnviron;
    _wenviron = MsvcrtWenviron;
}

/* builds a malloc'ed array of copies of the strings of a block, skipping "=C:" style entries. */
static char **block_to_array_a(const char *block)
{
    size_t count = 0;
    for (const char *p = block; *p; p += strlen(p) + 1)
        if (*p != '=')
            count++;
    char **env = calloc(count + 1, sizeof(char *));
    if (!env)
        return NULL;
    size_t i = 0;
    for (const char *p = block; *p; p += strlen(p) + 1)
        if (*p != '=')
            env[i++] = _strdup(p);
    return env;
}

static wchar_t **block_to_array_w(const wchar_t *block)
{
    size_t count = 0;
    for (const wchar_t *p = block; *p; p += wcslen(p) + 1)
        if (*p != '=')
            count++;
    wchar_t **env = calloc(count + 1, sizeof(wchar_t *));
    if (!env)
        return NULL;
    size_t i = 0;
    for (const wchar_t *p = block; *p; p += wcslen(p) + 1)
        if (*p != '=')
            env[i++] = _wcsdup(p);
    return env;
}

void MsvcrtEnvInit(void)
{
    char *block = GetEnvironmentStringsA();
    if (block) {
        MsvcrtEnviron = block_to_array_a(block);
        FreeEnvironmentStringsA(block);
    }
    if (!MsvcrtEnviron)
        MsvcrtEnviron = calloc(1, sizeof(char *));
    /* __initenv is a copy that _putenv does not change. */
    size_t n = 0;
    while (MsvcrtEnviron && MsvcrtEnviron[n])
        n++;
    MsvcrtInitenv = calloc(n + 1, sizeof(char *));
    if (MsvcrtInitenv)
        for (size_t i = 0; i < n; i++)
            MsvcrtInitenv[i] = MsvcrtEnviron[i];
    __initenv = MsvcrtInitenv;
    sync_exports();
}

void MsvcrtWenvInit(void)
{
    if (MsvcrtWenviron)
        return;
    _lock(_ENV_LOCK);
    if (!MsvcrtWenviron) {
        wchar_t *block = GetEnvironmentStringsW();
        if (block) {
            MsvcrtWenviron = block_to_array_w(block);
            FreeEnvironmentStringsW(block);
        }
        if (!MsvcrtWenviron)
            MsvcrtWenviron = calloc(1, sizeof(wchar_t *));
        size_t n = 0;
        while (MsvcrtWenviron && MsvcrtWenviron[n])
            n++;
        MsvcrtWinitenv = calloc(n + 1, sizeof(wchar_t *));
        if (MsvcrtWinitenv)
            for (size_t i = 0; i < n; i++)
                MsvcrtWinitenv[i] = MsvcrtWenviron[i];
        __winitenv = MsvcrtWinitenv;
        sync_exports();
    }
    _unlock(_ENV_LOCK);
}

char ***__cdecl __p__environ(void)
{
    return &_environ;
}

wchar_t ***__cdecl __p__wenviron(void)
{
    return &_wenviron;
}

char ***__cdecl __p___initenv(void)
{
    return &__initenv;
}

wchar_t ***__cdecl __p___winitenv(void)
{
    return &__winitenv;
}

/* index of name in env, case insensitively, or -(count+1) when absent. */
static int find_a(char **env, const char *name, size_t len)
{
    int i = 0;
    for (; env && env[i]; i++)
        if (!_strnicmp(env[i], name, len) && env[i][len] == '=')
            return i;
    return -(i + 1);
}

static int find_w(wchar_t **env, const wchar_t *name, size_t len)
{
    int i = 0;
    for (; env && env[i]; i++)
        if (!_wcsnicmp(env[i], name, len) && env[i][len] == '=')
            return i;
    return -(i + 1);
}

/* replaces, adds or with an empty value removes the entry of option in *penv; takes ownership of option. */
static int update_a(char ***penv, char *option, size_t len, int remove)
{
    char **env = *penv;
    int i = find_a(env, option, len);
    if (i >= 0) {
        if (remove) {
            free(option);
            for (; env[i]; i++)
                env[i] = env[i + 1];
        } else {
            env[i] = option;
        }
        return 0;
    }
    if (remove) {
        free(option);
        return 0;
    }
    int count = -i - 1;
    char **grown = realloc(env, (count + 2) * sizeof(char *));
    if (!grown) {
        free(option);
        return -1;
    }
    grown[count] = option;
    grown[count + 1] = NULL;
    *penv = grown;
    return 0;
}

static int update_w(wchar_t ***penv, wchar_t *option, size_t len, int remove)
{
    wchar_t **env = *penv;
    int i = find_w(env, option, len);
    if (i >= 0) {
        if (remove) {
            free(option);
            for (; env[i]; i++)
                env[i] = env[i + 1];
        } else {
            env[i] = option;
        }
        return 0;
    }
    if (remove) {
        free(option);
        return 0;
    }
    int count = -i - 1;
    wchar_t **grown = realloc(env, (count + 2) * sizeof(wchar_t *));
    if (!grown) {
        free(option);
        return -1;
    }
    grown[count] = option;
    grown[count + 1] = NULL;
    *penv = grown;
    return 0;
}

/* entries replaced in the arrays are not freed: __initenv and returned getenv pointers may still use them. */
int MsvcrtSetEnvA(const char *option, int primary)
{
    const char *eq = option ? strchr(option, '=') : NULL;
    if (!eq || eq == option) {
        MsvcrtSetErrno(EINVAL);
        return -1;
    }
    size_t len = eq - option;
    int remove = !eq[1];
    char *copy = _strdup(option);
    if (!copy) {
        MsvcrtSetErrno(ENOMEM);
        return -1;
    }
    if (update_a(&MsvcrtEnviron, copy, len, remove) < 0) {
        MsvcrtSetErrno(ENOMEM);
        return -1;
    }
    if (MsvcrtWenviron && primary) {
        wchar_t *w = MsvcrtAToW(option);
        size_t wlen = w ? wcschr(w, '=') - w : 0;
        if (w)
            update_w(&MsvcrtWenviron, w, wlen, remove);
    }
    char *name = malloc(len + 1);
    if (name) {
        memcpy(name, option, len);
        name[len] = 0;
        if (!SetEnvironmentVariableA(name, remove ? NULL : eq + 1)) {
            free(name);
            MsvcrtSetErrno(EILSEQ);
            sync_exports();
            return -1;
        }
        free(name);
    }
    sync_exports();
    return 0;
}

int MsvcrtSetEnvW(const wchar_t *option, int primary)
{
    const wchar_t *eq = option ? wcschr(option, '=') : NULL;
    if (!eq || eq == option) {
        MsvcrtSetErrno(EINVAL);
        return -1;
    }
    MsvcrtWenvInit();
    size_t len = eq - option;
    int remove = !eq[1];
    wchar_t *copy = _wcsdup(option);
    if (!copy) {
        MsvcrtSetErrno(ENOMEM);
        return -1;
    }
    if (update_w(&MsvcrtWenviron, copy, len, remove) < 0) {
        MsvcrtSetErrno(ENOMEM);
        return -1;
    }
    char *a = MsvcrtWToA(option);
    if (a) {
        size_t alen = strchr(a, '=') - a;
        update_a(&MsvcrtEnviron, a, alen, remove);
    }
    wchar_t *name = malloc((len + 1) * sizeof(wchar_t));
    if (name) {
        memcpy(name, option, len * sizeof(wchar_t));
        name[len] = 0;
        if (!SetEnvironmentVariableW(name, remove ? NULL : eq + 1)) {
            free(name);
            MsvcrtSetErrno(EILSEQ);
            sync_exports();
            return -1;
        }
        free(name);
    }
    sync_exports();
    return 0;
}

int __cdecl _putenv(const char *option)
{
    if (!option) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    _lock(_ENV_LOCK);
    int r = MsvcrtSetEnvA(option, 1);
    _unlock(_ENV_LOCK);
    return r;
}

int __cdecl _wputenv(const wchar_t *option)
{
    if (!option) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    _lock(_ENV_LOCK);
    int r = MsvcrtSetEnvW(option, 1);
    _unlock(_ENV_LOCK);
    return r;
}

static errno_t putenv_s_impl(const char *name, const char *value)
{
    if (!name || !value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t n = strlen(name), v = strlen(value);
    char *s = malloc(n + v + 2);
    if (!s)
        return ENOMEM;
    memcpy(s, name, n);
    s[n] = '=';
    memcpy(s + n + 1, value, v + 1);
    int r = _putenv(s);
    free(s);
    return r ? *_errno() : 0;
}

errno_t __cdecl _putenv_s(const char *name, const char *value)
{
    return putenv_s_impl(name, value);
}

errno_t __cdecl _wputenv_s(const wchar_t *name, const wchar_t *value)
{
    if (!name || !value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t n = wcslen(name), v = wcslen(value);
    wchar_t *s = malloc((n + v + 2) * sizeof(wchar_t));
    if (!s)
        return ENOMEM;
    memcpy(s, name, n * sizeof(wchar_t));
    s[n] = '=';
    memcpy(s + n + 1, value, (v + 1) * sizeof(wchar_t));
    int r = _wputenv(s);
    free(s);
    return r ? *_errno() : 0;
}

static char *getenv_nolock(const char *name)
{
    size_t len = strlen(name);
    int i = find_a(MsvcrtEnviron, name, len);
    return i >= 0 ? MsvcrtEnviron[i] + len + 1 : NULL;
}

static wchar_t *wgetenv_nolock(const wchar_t *name)
{
    MsvcrtWenvInit();
    size_t len = wcslen(name);
    int i = find_w(MsvcrtWenviron, name, len);
    return i >= 0 ? MsvcrtWenviron[i] + len + 1 : NULL;
}

char *__cdecl getenv(const char *name)
{
    if (!name) {
        MSVCRT_INVALID(EINVAL);
        return NULL;
    }
    _lock(_ENV_LOCK);
    char *r = getenv_nolock(name);
    _unlock(_ENV_LOCK);
    return r;
}

wchar_t *__cdecl _wgetenv(const wchar_t *name)
{
    if (!name) {
        MSVCRT_INVALID(EINVAL);
        return NULL;
    }
    _lock(_ENV_LOCK);
    wchar_t *r = wgetenv_nolock(name);
    _unlock(_ENV_LOCK);
    return r;
}

errno_t __cdecl getenv_s(size_t *required, char *buf, size_t size, const char *name)
{
    if (!required || (!buf && size) || !name) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (buf && size)
        *buf = 0;
    _lock(_ENV_LOCK);
    char *v = getenv_nolock(name);
    errno_t r = 0;
    if (!v) {
        *required = 0;
    } else {
        *required = strlen(v) + 1;
        if (size) {
            if (*required > size) {
                MSVCRT_INVALID(ERANGE);
                r = ERANGE;
            } else {
                memcpy(buf, v, *required);
            }
        }
    }
    _unlock(_ENV_LOCK);
    return r;
}

errno_t __cdecl _wgetenv_s(size_t *required, wchar_t *buf, size_t size, const wchar_t *name)
{
    if (!required || (!buf && size) || !name) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (buf && size)
        *buf = 0;
    _lock(_ENV_LOCK);
    wchar_t *v = wgetenv_nolock(name);
    errno_t r = 0;
    if (!v) {
        *required = 0;
    } else {
        *required = wcslen(v) + 1;
        if (size) {
            if (*required > size) {
                MSVCRT_INVALID(ERANGE);
                r = ERANGE;
            } else {
                memcpy(buf, v, *required * sizeof(wchar_t));
            }
        }
    }
    _unlock(_ENV_LOCK);
    return r;
}

errno_t __cdecl _dupenv_s(char **pbuf, size_t *plen, const char *name)
{
    if (!pbuf || !name) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *pbuf = NULL;
    if (plen)
        *plen = 0;
    _lock(_ENV_LOCK);
    char *v = getenv_nolock(name);
    errno_t r = 0;
    if (v) {
        *pbuf = _strdup(v);
        if (!*pbuf)
            r = ENOMEM;
        else if (plen)
            *plen = strlen(v) + 1;
    }
    _unlock(_ENV_LOCK);
    return r;
}

errno_t __cdecl _wdupenv_s(wchar_t **pbuf, size_t *plen, const wchar_t *name)
{
    if (!pbuf || !name) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *pbuf = NULL;
    if (plen)
        *plen = 0;
    _lock(_ENV_LOCK);
    wchar_t *v = wgetenv_nolock(name);
    errno_t r = 0;
    if (v) {
        *pbuf = _wcsdup(v);
        if (!*pbuf)
            r = ENOMEM;
        else if (plen)
            *plen = wcslen(v) + 1;
    }
    _unlock(_ENV_LOCK);
    return r;
}

/* looks for file in the current directory, then in each directory of the variable. */
static errno_t searchenv_w(const wchar_t *file, const wchar_t *var, wchar_t *buf, size_t size)
{
    if (!file || !var || !buf || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *buf = 0;
    if (GetFileAttributesW(file) != INVALID_FILE_ATTRIBUTES) {
        if (!MsvcrtWfullpath(buf, file, size))
            return *_errno();
        return 0;
    }
    const wchar_t *path = _wgetenv(var);
    if (!path) {
        MsvcrtSetErrno(ENOENT);
        return ENOENT;
    }
    size_t flen = wcslen(file);
    while (*path) {
        const wchar_t *end = path;
        while (*end && *end != ';')
            end++;
        size_t dlen = end - path;
        if (dlen && dlen + flen + 2 < MAX_PATH) {
            wchar_t tmp[MAX_PATH];
            memcpy(tmp, path, dlen * sizeof(wchar_t));
            if (tmp[dlen - 1] != '\\' && tmp[dlen - 1] != '/')
                tmp[dlen++] = '\\';
            memcpy(tmp + dlen, file, (flen + 1) * sizeof(wchar_t));
            if (GetFileAttributesW(tmp) != INVALID_FILE_ATTRIBUTES) {
                if (wcslen(tmp) >= size) {
                    MsvcrtSetErrno(ERANGE);
                    return ERANGE;
                }
                wcscpy(buf, tmp);
                return 0;
            }
        }
        path = *end ? end + 1 : end;
    }
    MsvcrtSetErrno(ENOENT);
    return ENOENT;
}

errno_t __cdecl _wsearchenv_s(const wchar_t *file, const wchar_t *var, wchar_t *buf, size_t size)
{
    return searchenv_w(file, var, buf, size);
}

void __cdecl _wsearchenv(const wchar_t *file, const wchar_t *var, wchar_t *buf)
{
    searchenv_w(file, var, buf, MAX_PATH);
}

errno_t __cdecl _searchenv_s(const char *file, const char *var, char *buf, size_t size)
{
    if (!file || !var || !buf || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    wchar_t *wf = MsvcrtAToW(file), *wv = MsvcrtAToW(var);
    wchar_t wbuf[MAX_PATH];
    errno_t r = wf && wv ? searchenv_w(wf, wv, wbuf, MAX_PATH) : ENOMEM;
    free(wf);
    free(wv);
    *buf = 0;
    if (!r && !WideCharToMultiByte(CP_ACP, 0, wbuf, -1, buf, (int)size, NULL, NULL)) {
        MsvcrtSetErrno(ERANGE);
        r = ERANGE;
    }
    return r;
}

void __cdecl _searchenv(const char *file, const char *var, char *buf)
{
    _searchenv_s(file, var, buf, MAX_PATH);
}
