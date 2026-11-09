#pragma once

/* how windows names a gpu and numbers its driver, shared by the kernel's display key and dxgi's adapters. */

static inline BOOL MimeGpuCodename(const char *text, ULONG length)
{
    if (length > 5 && (!memcmp(text, "RADV ", 5) || !memcmp(text, "LLVM ", 5)))
        return TRUE;
    if (length > 4 && !memcmp(text, "NVK ", 4))
        return TRUE;
    for (ULONG i = 0; i < length; i++)
        if (!((text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= '0' && text[i] <= '9') || text[i] == ' ' ||
              text[i] == '-'))
            return FALSE;
    return length != 0;
}

/*
 * vulkan's device name as the windows driver gives it. mesa ends it with its driver and the chip, so "AMD Custom
 * GPU 0932 (RADV VANGOGH)" is "AMD Custom GPU 0932", and writes "(tm)" where windows writes "(TM)".
 */
static inline void MimeGpuName(const char *name, WCHAR *out, ULONG count)
{
    ULONG length = 0;
    while (name[length])
        length++;
    if (length > 3 && name[length - 1] == ')') {
        ULONG open = length - 1;
        while (open && name[open] != '(')
            open--;
        if (open > 1 && name[open - 1] == ' ' && MimeGpuCodename(name + open + 1, length - open - 2))
            length = open - 1;
    }
    ULONG n = 0;
    for (; n < length && n + 1 < count; n++)
        out[n] = (WCHAR)(unsigned char)name[n];
    out[n] = 0;
    for (ULONG i = 0; i + 2 < n; i++) {
        if (out[i] != '(')
            continue;
        if (out[i + 1] == 'r' && out[i + 2] == ')') {
            out[i + 1] = 'R';
        } else if (i + 3 < n && out[i + 1] == 't' && out[i + 2] == 'm' && out[i + 3] == ')') {
            out[i + 1] = 'T';
            out[i + 2] = 'M';
        }
    }
}

/*
 * the driver version as windows numbers the package: nvidia's in the last digit of the third part and the fourth,
 * so 580.95 is 32.0.15.8095, the others from vulkan's major, minor and patch, mesa 26.1.99 being 31.0.2601.99.
 */
static inline void MimeGpuDriverVersion(ULONG vendor, ULONG driver, USHORT version[4])
{
    version[1] = 0;
    if (vendor == 0x10DE) {
        ULONG n = (driver >> 22) * 100 + ((driver >> 14) & 0xFF);
        version[0] = 32;
        version[2] = (USHORT)(10 + n / 10000);
        version[3] = (USHORT)(n % 10000);
    } else {
        version[0] = 31;
        version[2] = (USHORT)(((driver >> 22) & 0x7F) * 100 + ((driver >> 12) & 0x3FF));
        version[3] = (USHORT)(driver & 0xFFF);
    }
}

/* the company that ships the vendor's windows driver, as its package names itself. */
static inline const WCHAR *MimeGpuProvider(ULONG vendor)
{
    switch (vendor) {
    case 0x10DE:
        return L"NVIDIA";
    case 0x1002:
        return L"Advanced Micro Devices, Inc.";
    case 0x8086:
        return L"Intel Corporation";
    default:
        return L"Microsoft";
    }
}
