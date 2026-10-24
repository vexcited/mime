/*
 * opengl32 for x64 programs: every GL and wgl function is a stub calling the native opengl32.dll in the same process
 * through xta64, see mime/mx64.h. this part loads the native image and answers wglGetProcAddress with the stubs.
 */
#include <mime/nt.h>
#include <mime/mx64.h>
#include <mime/win32/winbase.h>

NTSTATUS MxNativeLoad(ULONG table, PCWSTR file, const char *export_name);

static HMODULE g_self;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    g_self = instance;
    return NT_SUCCESS(MxNativeLoad(MX_NATIVE_OPENGL32, L"opengl32.dll", "OpenGL32X64Thunks"));
}

PROC WINAPI wglGetProcAddress(LPCSTR name)
{
    return name ? GetProcAddress(g_self, name) : nullptr;
}
