/* what the system part of the framework registers: the install roots the runtime finder reads, and a few policies. */
#include "cbs.h"
#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#endif

#define HOME "SOFTWARE\\Microsoft\\.NETFramework"

typedef struct VALUE_SET {
    const CBS_LAYOUT *Layout;
    const CBS_TARGET *Target;
} VALUE_SET;

static int set_string(const VALUE_SET *s, int view, const char *key, const char *name, const char *text)
{
    const CBS_TARGET *t = s->Target;
    if (t->CreateKey(t->Context, view, key))
        return CBS_TARGET_FAILED;
    return t->SetValue(t->Context, view, key, name, CBS_REG_SZ, text, strlen(text) + 1) ? CBS_TARGET_FAILED : CBS_OK;
}

static int set_dword(const VALUE_SET *s, int view, const char *key, const char *name, uint32_t value)
{
    const CBS_TARGET *t = s->Target;
    uint8_t data[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    if (t->CreateKey(t->Context, view, key))
        return CBS_TARGET_FAILED;
    return t->SetValue(t->Context, view, key, name, CBS_REG_DWORD, data, 4) ? CBS_TARGET_FAILED : CBS_OK;
}

static char *root_path(const CBS_LAYOUT *layout, const char *folder, char *out, size_t size)
{
    const char *tail = "\\Microsoft.NET\\";
    size_t a = strlen(layout->Windows), b = strlen(tail), c = strlen(folder);
    if (a + b + c + 2 > size)
        return nullptr;
    memcpy(out, layout->Windows, a);
    memcpy(out + a, tail, b);
    memcpy(out + a + b, folder, c);
    out[a + b + c] = '\\';
    out[a + b + c + 1] = 0;
    return out;
}

int CbsInstallFrameworkRegistry(const CBS_LAYOUT *layout, const CBS_TARGET *target)
{
    VALUE_SET s = {layout, target};
    char root[520];
    int status;
    /* the view of the system's own processor: the 64-bit one, or the only one on a 32-bit system. */
    const int native_view = layout->Is64Bit ? 64 : 32;
    const char *folder = layout->Native == CBS_ARCH_ARM64 ? "FrameworkArm64" : layout->Is64Bit ? "Framework64" : "Framework";
    if (!root_path(layout, folder, root, sizeof(root)))
        return CBS_BAD;
    status = set_string(&s, native_view, HOME, "InstallRoot", root);
    if (status != CBS_OK)
        return status;
    if (layout->Is64Bit) {
        status = set_dword(&s, native_view, HOME, "Enable64Bit", 1);
        if (status != CBS_OK)
            return status;
        if (!root_path(layout, "Framework", root, sizeof(root)))
            return CBS_BAD;
        status = set_string(&s, 32, HOME, "InstallRoot", root);
        if (status != CBS_OK)
            return status;
    }
    static const struct {
        const char *Name, *Version;
    } namespaces[] = {
        {"http://schemas.microsoft.com/netfx/2007/xaml/presentation", "3.0"},
        {"http://schemas.microsoft.com/netfx/2009/xaml/presentation", "4.0"},
        {"http://schemas.microsoft.com/winfx/2006/xaml/presentation", "3.0"},
    };
    for (int view = 32; view <= 64; view += 32) {
        if (view == 64 && !layout->Is64Bit)
            break;
        for (unsigned i = 0; i < sizeof(namespaces) / sizeof(namespaces[0]); i++) {
            status = set_string(&s, view, HOME "\\Windows Presentation Foundation\\Namespaces", namespaces[i].Name,
                                namespaces[i].Version);
            if (status != CBS_OK)
                return status;
        }
    }
    /* the policy of the native image generator: what it leaves out and the default it assumes for old programs. */
    status = set_dword(&s, native_view, HOME "\\NGen\\Policy\\v2.0", "OptimizeUsedBinaries", 1);
    if (status != CBS_OK)
        return status;
    return set_string(&s, native_view, HOME "\\NGen\\Policy\\v4.0\\AssemblyExclusion", "100", "clientexclusionlist.xml");
}
