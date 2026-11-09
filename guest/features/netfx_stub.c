/*
 * The handler of the .NET Framework until guest/lib/cbs provides it. A definition in another object file wins over this
 * one. A null package is how OptionalFeatures.exe asks whether the handler is there.
 */
#include <mime/feature.h>

__attribute__((weak)) DWORD NetfxInstallFromPackage(PCWSTR package, PCWSTR root, MIME_FEATURE_PROGRESS progress,
                                                    void *context)
{
    return ERROR_NOT_SUPPORTED;
}
