#include "../kernelbase_private.h"

void WINAPI RaiseException(DWORD code, DWORD flags, DWORD count, const ULONG_PTR *arguments)
{
    EXCEPTION_RECORD record = {0};
    record.ExceptionCode = code;
    record.ExceptionFlags = flags & EXCEPTION_NONCONTINUABLE;
    if (arguments) {
        record.NumberParameters = count > EXCEPTION_MAXIMUM_PARAMETERS ? EXCEPTION_MAXIMUM_PARAMETERS : count;
        memcpy(record.ExceptionInformation, arguments, record.NumberParameters * sizeof(ULONG_PTR));
    }
    RtlRaiseException(&record);
}
