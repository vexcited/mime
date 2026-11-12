/* the driver's commands by name, as vkGetInstanceProcAddr and vkGetDeviceProcAddr give them. */
#include "vkm.h"

static const VkmEntry *const g_tables[] = {
    vkm_instance_entries, vkm_device_entries, vkm_memory_entries, vkm_descriptor_entries,
    vkm_shader_entries,   vkm_cmd_entries,    vkm_xfer_entries,   vkm_query_entries, vkm_wsi_entries,   vkm_accel_entries, vkm_rt_entries,
};

static PFN_vkVoidFunction lookup(const char *name)
{
    if (!name || strncmp(name, "vk", 2))
        return nullptr;
    for (size_t t = 0; t < MIME_ARRAY_COUNT(g_tables); t++)
        for (const VkmEntry *e = g_tables[t]; e->name; e++)
            if (!strcmp(e->name, name))
                return e->fn;
    return nullptr;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char *name)
{
    return lookup(name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkm_GetInstanceProcAddr(VkInstance instance, const char *name)
{
    if (name && !strcmp(name, "vkGetInstanceProcAddr"))
        return (PFN_vkVoidFunction)vkm_GetInstanceProcAddr;
    if (name && !strcmp(name, "vkGetDeviceProcAddr"))
        return (PFN_vkVoidFunction)GetDeviceProcAddr;
    return lookup(name);
}
