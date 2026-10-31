/*
 * PdhFormatFromRawValue: a counter value from two raw samples of a counter type, by the formulas of the types. the
 * first value of a raw sample is the count and the second one what it is measured against: the time of the sample in
 * the ticks of the type for the timers and rates, the base counter for the fractions and averages.
 */
#define PDH_FUNCTION LONG WINAPI
#include <mime/win32/pdh.h>

#define STATUS_NEGATIVE_DENOMINATOR ((PDH_STATUS)0x800007D6L)
#define STATUS_NEGATIVE_TIMEBASE ((PDH_STATUS)0x800007D7L)
#define STATUS_NEGATIVE_VALUE ((PDH_STATUS)0x800007D8L)

enum {
    TYPE_RAWCOUNT = 0x10000,
    TYPE_LARGE_RAWCOUNT = 0x10100,
    TYPE_COUNTER_COUNTER = 0x10410400,
    TYPE_BULK_COUNT = 0x10410500,
    TYPE_COUNTER_TIMER = 0x20410500,
    TYPE_COUNTER_TIMER_INV = 0x21410500,
    TYPE_MULTI_TIMER = 0x22410500,
    TYPE_MULTI_TIMER_INV = 0x23410500,
    TYPE_100NSEC_MULTI_TIMER = 0x22510500,
    TYPE_100NSEC_MULTI_TIMER_INV = 0x23510500,
    TYPE_RAW_FRACTION = 0x20020400,
    TYPE_LARGE_RAW_FRACTION = 0x20020500,
    TYPE_SAMPLE_FRACTION = 0x20c20400,
    TYPE_AVERAGE_TIMER = 0x30020400,
    TYPE_AVERAGE_BULK = 0x40020500,
};

PDH_FUNCTION PdhFormatFromRawValue(DWORD type, DWORD format, LONGLONG *time_base, PPDH_RAW_COUNTER current,
                                   PPDH_RAW_COUNTER previous, PPDH_FMT_COUNTERVALUE value)
{
    DWORD kind = format & (PDH_FMT_LONG | PDH_FMT_DOUBLE | PDH_FMT_LARGE);
    if (!value || !current || !previous || (kind != PDH_FMT_LONG && kind != PDH_FMT_DOUBLE && kind != PDH_FMT_LARGE))
        return PDH_INVALID_ARGUMENT;
    if ((current->CStatus != PDH_CSTATUS_VALID_DATA && current->CStatus != PDH_CSTATUS_NEW_DATA) ||
        (previous->CStatus != PDH_CSTATUS_VALID_DATA && previous->CStatus != PDH_CSTATUS_NEW_DATA)) {
        value->CStatus = PDH_CSTATUS_INVALID_DATA;
        return PDH_INVALID_DATA;
    }
    double d_count = (double)(current->FirstValue - previous->FirstValue);
    double d_base = (double)(current->SecondValue - previous->SecondValue);
    double multi = current->MultiCount ? (double)current->MultiCount : 1;
    double frequency = time_base ? (double)*time_base : 0;
    double result = 0;
    BOOL percent = FALSE;
    PDH_STATUS status = ERROR_SUCCESS;
    switch (type) {
    case TYPE_RAWCOUNT:
    case TYPE_LARGE_RAWCOUNT:
    case 0x100:
    case 0:
        result = (double)current->FirstValue;
        break;
    case TYPE_COUNTER_COUNTER:
    case TYPE_BULK_COUNT:
        if (d_base <= 0 || frequency <= 0)
            status = STATUS_NEGATIVE_TIMEBASE;
        else if (d_count < 0)
            status = STATUS_NEGATIVE_VALUE;
        else
            result = d_count * frequency / d_base;
        break;
    case TYPE_COUNTER_TIMER:
    case TYPE_COUNTER_TIMER_INV:
    case TYPE_MULTI_TIMER:
    case TYPE_MULTI_TIMER_INV:
    case PERF_100NSEC_TIMER:
    case PERF_100NSEC_TIMER_INV:
    case TYPE_100NSEC_MULTI_TIMER:
    case TYPE_100NSEC_MULTI_TIMER_INV: {
        BOOL inverse = (type & 0x01000000) != 0, many = (type & 0x02000000) != 0;
        if (d_base <= 0) {
            status = STATUS_NEGATIVE_TIMEBASE;
            break;
        }
        double busy = d_count / d_base;
        if (!inverse && busy < 0) {
            status = STATUS_NEGATIVE_VALUE;
            break;
        }
        result = 100 * (inverse ? (many ? multi : 1) - busy : busy);
        if (many && !inverse)
            result /= multi;
        percent = !many;
        break;
    }
    case TYPE_RAW_FRACTION:
    case TYPE_LARGE_RAW_FRACTION:
        if (current->SecondValue <= 0)
            status = STATUS_NEGATIVE_DENOMINATOR;
        else
            result = 100 * (double)current->FirstValue / (double)current->SecondValue;
        percent = TRUE;
        break;
    case TYPE_SAMPLE_FRACTION:
        if (d_base <= 0)
            status = STATUS_NEGATIVE_DENOMINATOR;
        else if (d_count < 0)
            status = STATUS_NEGATIVE_VALUE;
        else
            result = 100 * d_count / d_base;
        percent = TRUE;
        break;
    case TYPE_AVERAGE_TIMER:
        if (d_base <= 0)
            status = STATUS_NEGATIVE_DENOMINATOR;
        else if (frequency <= 0)
            status = STATUS_NEGATIVE_TIMEBASE;
        else
            result = d_count / frequency / d_base;
        break;
    case TYPE_AVERAGE_BULK:
        if (d_base <= 0)
            status = STATUS_NEGATIVE_DENOMINATOR;
        else
            result = d_count / d_base;
        break;
    default:
        return PDH_NOT_IMPLEMENTED;
    }
    if (status) {
        value->CStatus = status;
        return status;
    }
    /* a percentage is capped at 100 unless the caller asks for the value as it is. */
    if (percent && !(format & PDH_FMT_NOCAP100) && result > 100)
        result = 100;
    if (format & PDH_FMT_1000)
        result *= 1000;
    value->CStatus = PDH_CSTATUS_VALID_DATA;
    if (kind == PDH_FMT_DOUBLE)
        value->doubleValue = result;
    else if (kind == PDH_FMT_LARGE)
        value->largeValue = (LONGLONG)result;
    else
        value->longValue = (LONG)result;
    return ERROR_SUCCESS;
}
