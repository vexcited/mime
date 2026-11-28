/*
 * a program that asks for a windows font the machine does not have asks the feature service for it: the fonts of
 * microsoft are an optional download. gdi32 is microsoft's file, so the request is made here, where the font is
 * created. nothing stands in for them, so a program whose font is still missing after the request is ended. the
 * request is made once for each family in a process, and only for the families the catalog of features on demand
 * has, so every other font name costs a few comparisons.
 */
#include "../win32k.h"
#include <mime/feature.h>
#include <mime/fonts.h>

extern const char *const g_catalog_font_names[];
extern const unsigned g_catalog_font_name_count;

PUCHAR NTAPI PsGetProcessImageFileName(PEPROCESS process);
HANDLE NTAPI PsGetProcessInheritedFromUniqueProcessId(PEPROCESS process);
NTSTATUS NTAPI ZwTerminateProcess(HANDLE process, NTSTATUS status);
NTSTATUS PsCreateUserProcess(PCUNICODE_STRING image, PCUNICODE_STRING cmdline, PCUNICODE_STRING cwd, ULONG64 request_id,
                             PVOID console, BOOLEAN elevated, PEPROCESS *out);

#define ASKED_MAX 256

typedef struct {
    HANDLE Pid;
    ULONG Hash;
} ASKED;

static ASKED g_asked[ASKED_MAX];
static volatile LONG g_asked_count;

static WCHAR fold(WCHAR c)
{
    return c >= L'A' && c <= L'Z' ? (WCHAR)(c + 32) : c;
}

/* the place of a family in the sorted names of the catalog, as lower case ascii. */
static BOOLEAN in_catalog(PCWSTR name)
{
    char key[LF_FACESIZE];
    ULONG n = 0;
    for (; name[n]; n++) {
        if (n + 1 >= LF_FACESIZE || name[n] > 0x7E || name[n] < 0x20)
            return FALSE;
        key[n] = (char)fold(name[n]);
    }
    key[n] = 0;
    ULONG low = 0, high = g_catalog_font_name_count;
    while (low < high) {
        ULONG middle = (low + high) / 2;
        const char *a = key, *b = g_catalog_font_names[middle];
        while (*a && *a == *b) {
            a++;
            b++;
        }
        if (*a == *b)
            return TRUE;
        if ((UCHAR)*a < (UCHAR)*b)
            high = middle;
        else
            low = middle + 1;
    }
    return FALSE;
}

/* TRUE the first time a process names the family. a full table asks for nothing more. */
static BOOLEAN first_time(PCWSTR family)
{
    ULONG hash = 2166136261u;
    for (PCWSTR p = family; *p; p++)
        hash = (hash ^ fold(*p)) * 16777619u;
    HANDLE pid = PsGetCurrentProcessId();
    LONG count = g_asked_count;
    for (LONG i = 0; i < count && i < ASKED_MAX; i++)
        if (g_asked[i].Pid == pid && g_asked[i].Hash == hash)
            return FALSE;
    LONG slot = InterlockedIncrement(&g_asked_count) - 1;
    if (slot >= ASKED_MAX) {
        InterlockedDecrement(&g_asked_count);
        return FALSE;
    }
    g_asked[slot] = (ASKED){pid, hash};
    return TRUE;
}

/*
 * the feature service and what it runs must not ask for what they are installing. the console host does not ask
 * either: it draws for another program, the service among them, and would ask on behalf of each one.
 */
static BOOLEAN is_the_service(void)
{
    PEPROCESS process = PsGetCurrentProcess();
    if (!_stricmp((const char *)PsGetProcessImageFileName(process), "conhost.exe"))
        return TRUE;
    ObReferenceObject(process);
    BOOLEAN service = FALSE;
    for (int depth = 0; process && depth < 6 && !service; depth++) {
        const char *name = (const char *)PsGetProcessImageFileName(process);
        service = !_stricmp(name, "OptionalFeature") || !_stricmp(name, "dism.exe") || !_stricmp(name, "fondue.exe");
        HANDLE parent = PsGetProcessInheritedFromUniqueProcessId(process);
        ObDereferenceObject(process);
        process = nullptr;
        if (!service && parent && !NT_SUCCESS(PsLookupProcessByProcessId(parent, &process)))
            process = nullptr;
    }
    if (process)
        ObDereferenceObject(process);
    return service;
}

static BOOLEAN installed(PCWSTR family)
{
    UserEnter();
    BOOLEAN there = FontFamilyInstalled(family);
    UserLeave();
    return there;
}

/* called where a program creates a font, with no lock held: the request waits for the user. */
void FontAskFor(PCWSTR face)
{
    static const MIME_FONT_SUBSTITUTE substitutes[] = {MIME_FONT_SUBSTITUTES};
    if (!face || !face[0])
        return;
    static const MIME_FONT_CHARSET_SUBSTITUTE charset_substitutes[] = {MIME_FONT_CHARSET_SUBSTITUTES};
    PCWSTR family = face;
    for (ULONG i = 0; i < RTL_NUMBER_OF(substitutes); i++)
        if (!_wcsicmp(family, substitutes[i].From)) {
            family = substitutes[i].To;
            break;
        }
    for (ULONG i = 0; family == face && i < RTL_NUMBER_OF(charset_substitutes); i++)
        if (!_wcsicmp(family, charset_substitutes[i].From))
            family = charset_substitutes[i].To;
    if (!in_catalog(family) || installed(family) || is_the_service() || !first_time(family))
        return;
    WCHAR line[200];
    ULONG n = 0;
    static const WCHAR head[] = L"OptionalFeatures.exe /enable-font:\"";
    for (PCWSTR p = head; *p; p++)
        line[n++] = *p;
    for (PCWSTR p = family; *p && n < RTL_NUMBER_OF(line) - 2; p++)
        if (*p != L'"')
            line[n++] = *p;
    line[n++] = L'"';
    line[n] = 0;
    UNICODE_STRING image = RTL_CONSTANT_STRING(L"C:\\Windows" MIME_FEATURE_PROGRAM), command;
    RtlInitUnicodeString(&command, line);
    PEPROCESS service = nullptr;
    NTSTATUS status = PsCreateUserProcess(&image, &command, nullptr, 0, (PVOID)(LONG_PTR)-1, FALSE, &service);
    if (NT_SUCCESS(status) && service) {
        KeWaitForSingleObject(service, UserRequest, UserMode, FALSE, nullptr);
        status = PsGetProcessExitStatus(service);
        ObDereferenceObject(service);
    }
    if (installed(family))
        return;
    W32Trace("win32k: the font %ls is not installed after the request (%08x), the program is closed\n", family, (ULONG)status);
    ZwTerminateProcess(NtCurrentProcess(), 1223 /* ERROR_CANCELLED */);
}
