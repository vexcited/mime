#include "ntdll_private.h"

NTDLL_EXPORT ULONG NTAPI DbgPrompt(PCSTR prompt, PSTR response, ULONG capacity)
{
    ULONG length = (USHORT)strlen(prompt);
    if (length > 512)
        length = 512;
    WCHAR wide[512];
    ULONG bytes = 0;
    RtlUTF8ToUnicodeN(wide, sizeof(wide), &bytes, prompt, length);
    UNICODE_STRING text = {(USHORT)bytes, (USHORT)bytes, wide};
    if (bytes)
        NtDisplayString(&text);
    /* mime has no debugger input transport, so no response bytes are available. */
    return 0;
}
