/* user mode time helpers. */
#include "rtl.h"

#ifndef MIME_KERNEL
/* the time of the last clock tick, read from the shared page in one access. */
NTSTATUS NTAPI RtlQuerySystemTime(PLARGE_INTEGER time)
{
    time->QuadPart = *(volatile LONGLONG *)&USER_SHARED_DATA->SystemTime;
    return STATUS_SUCCESS;
}
#endif
