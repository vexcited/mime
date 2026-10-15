/* time helpers of user mode (rtl). */
#include "rtl.h"

#ifndef MIME_KERNEL
/* the system time of the last clock tick, read in one access from the shared page. */
NTSTATUS NTAPI RtlQuerySystemTime(PLARGE_INTEGER time)
{
    time->QuadPart = *(volatile LONGLONG *)&USER_SHARED_DATA->SystemTime;
    return STATUS_SUCCESS;
}
#endif
