#include <mime/nt.h>

/*
 * like every dll of windows, the dlls of mime carry a load configuration with a security cookie. the loader of windows
 * refuses a dll of subsystem 6.3 or later that has an entry point and no cookie holding this default, which it replaces.
 */
ULONG_PTR __security_cookie = 0x2B992DDFA232;

/* IMAGE_LOAD_CONFIG_DIRECTORY64 as windows 11 26300 links it, 0x148 bytes. only the size and the cookie are told. */
const struct {
    ULONG Size;
    UCHAR Before[0x54];
    ULONG_PTR *SecurityCookie;
    UCHAR After[0xE8];
} _load_config_used = {.Size = 0x148, .SecurityCookie = &__security_cookie};
