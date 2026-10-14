/* legacy processor contexts; extended vector state is not exposed by the translator. */
#include "kernelbase_private.h"

DECLSPEC_EXPORT DWORD64 WINAPI GetEnabledXStateFeatures(void)
{
#if defined(__x86_64__) || defined(__i386__)
    return 3;
#else
    return 0;
#endif
}

DECLSPEC_EXPORT BOOL WINAPI InitializeContext(void *buffer, DWORD flags, PCONTEXT *context, DWORD *length)
{
    if (!length || (flags & 0x40)) {
        SetLastError(flags & 0x40 ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* the size query passes no buffer and may pass no context either. */
    DWORD required = sizeof(CONTEXT) + 15;
    if (!buffer || *length < required) {
        *length = required;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (!context) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *context = (PCONTEXT)(((ULONG_PTR)buffer + 15) & ~(ULONG_PTR)15);
    memset(*context, 0, sizeof(CONTEXT));
    (*context)->ContextFlags = flags;
    *length = required;
    return TRUE;
}

DECLSPEC_EXPORT PVOID WINAPI LocateXStateFeature(PCONTEXT context, DWORD feature, DWORD *length)
{
    if (length)
        *length = 0;
#if defined(__x86_64__)
    if (context && feature < 2) {
        if (length)
            *length = feature ? 256 : 160;
        return feature ? (void *)&context->Xmm0 : (void *)&context->FltSave;
    }
#endif
    return NULL;
}

DECLSPEC_EXPORT BOOL WINAPI SetXStateFeaturesMask(PCONTEXT context, DWORD64 mask)
{
    if (!context || (mask & ~GetEnabledXStateFeatures())) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (mask)
        context->ContextFlags |= CONTEXT_FLOATING_POINT;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI CopyContext(PCONTEXT destination, DWORD flags, PCONTEXT source)
{
    if (!destination || !source || (flags & 0x40)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    flags &= source->ContextFlags;
#if defined(__x86_64__)
    if (flags & 1) {
        destination->Rsp = source->Rsp;
        destination->Rip = source->Rip;
        destination->SegCs = source->SegCs;
        destination->SegSs = source->SegSs;
        destination->EFlags = source->EFlags;
    }
    if (flags & 2) {
        destination->Rax = source->Rax;
        destination->Rcx = source->Rcx;
        destination->Rdx = source->Rdx;
        destination->Rbx = source->Rbx;
        memcpy(&destination->Rbp, &source->Rbp, 11 * sizeof(ULONG64));
    }
    if (flags & 4) {
        destination->SegDs = source->SegDs;
        destination->SegEs = source->SegEs;
        destination->SegFs = source->SegFs;
        destination->SegGs = source->SegGs;
    }
    if (flags & 8) {
        destination->FltSave = source->FltSave;
        destination->MxCsr = source->MxCsr;
    }
    if (flags & 16)
        memcpy(&destination->Dr0, &source->Dr0, 6 * sizeof(ULONG64));
#else
    if ((flags & (CONTEXT_FULL & 0xff)) != (CONTEXT_FULL & 0xff)) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    memcpy(destination, source, sizeof(*destination));
#endif
    destination->ContextFlags |= flags;
    return TRUE;
}
