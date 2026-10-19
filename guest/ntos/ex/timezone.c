/*
 * the offset of the clock from utc, which follows the rules of the machine's zone. Windows keeps the rules the kernel
 * reads in the key TimeZoneInformation and works out from them which period it is, standard or daylight time, and
 * the instant of the next change; it writes the offset into the shared data page (TimeZoneBias, with TimeZoneId and the
 * period the offset is good for), sets a timer for the change and, when it comes, does it all again and tells the
 * windows of the desktop the clock changed. Here the same: the rules come from the key (cm/boot.c writes the ones of the
 * machine's zone for the year), the timer is a kernel timer, and the windows hear WM_TIMECHANGE through win32k.
 * See docs/ntoskrnl.md.
 */
#include "../ntos.h"

#define REG_BINARY 3
#define REG_DWORD 4

#define KEY_TIME_ZONE L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\TimeZoneInformation"
#define MINUTE_HNS 600000000ll
#define DAY_HNS 864000000000ll

typedef struct TZ_DATE {
    USHORT Year, Month, DayOfWeek, Day, Hour, Minute, Second, Milliseconds;
} TZ_DATE;

typedef struct TZ_RULES {
    LONG Bias, StandardBias, DaylightBias;
    TZ_DATE StandardDate, DaylightDate;
} TZ_RULES;

static KTIMER g_timer;
static KDPC g_dpc;
static KEVENT g_event;
static TZ_RULES g_rules;
static ULONG g_rules_year;
static LONG g_bias_minutes; /* of the period now, utc = local + bias */

/* ------------------------------------------------------------ calendar, in 100 ns from 1601 */

/* days from 1601-01-01 to a day of the proleptic Gregorian calendar. */
static LONG64 days_from_civil(LONG64 year, ULONG month, ULONG day)
{
    year -= month <= 2;
    LONG64 era = (year >= 0 ? year : year - 399) / 400;
    ULONG yoe = (ULONG)(year - era * 400);
    ULONG doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    ULONG doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (LONG64)doe - 719468 + 134774;
}

static ULONG year_of(LONG64 filetime)
{
    LONG64 days = filetime / DAY_HNS - 134774 + 719468;
    LONG64 era = (days >= 0 ? days : days - 146096) / 146097;
    LONG64 doe = days - era * 146097;
    LONG64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    LONG64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    LONG64 mp = (5 * doy + 2) / 153;
    return (ULONG)(yoe + era * 400 + (mp >= 10 ? 1 : 0));
}

static ULONG days_in_month(ULONG year, ULONG month)
{
    static const UCHAR days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        return 29;
    return days[month - 1];
}

/* the local time a rule changes the clock in a year, FALSE for a rule that names no month. */
static BOOLEAN cutover(const TZ_DATE *d, ULONG year, LONG64 *local)
{
    if (!d->Month || d->Month > 12)
        return FALSE;
    LONG64 day;
    if (d->Year) {
        /* a date of one year. */
        day = days_from_civil(d->Year, d->Month, d->Day && d->Day <= days_in_month(d->Year, d->Month) ? d->Day : 1);
    } else {
        /* the n-th weekday of the month, the fifth meaning the last. */
        LONG64 first = days_from_civil(year, d->Month, 1);
        ULONG first_weekday = (ULONG)((first + 1) % 7); /* 1601-01-01 was a monday */
        ULONG nth = d->Day ? (d->Day > 5 ? 5 : d->Day) : 1;
        ULONG date = 1 + (d->DayOfWeek + 7 - first_weekday) % 7 + (nth - 1) * 7;
        while (date > days_in_month(year, d->Month))
            date -= 7;
        day = first + date - 1;
    }
    *local = day * DAY_HNS + ((LONG64)d->Hour * 3600 + (LONG64)d->Minute * 60 + d->Second) * 10000000ll +
             (LONG64)d->Milliseconds * 10000;
    return TRUE;
}

/* ------------------------------------------------------------ rules */

static BOOLEAN read_dword(PCWSTR name, LONG *value)
{
    ULONG type = 0, length = sizeof(*value);
    return NT_SUCCESS(CmQueryValue(KEY_TIME_ZONE, name, &type, value, &length)) && type == REG_DWORD && length >= sizeof(*value);
}

static BOOLEAN read_date(PCWSTR name, TZ_DATE *date)
{
    ULONG type = 0, length = sizeof(*date);
    return NT_SUCCESS(CmQueryValue(KEY_TIME_ZONE, name, &type, date, &length)) && type == REG_BINARY && length >= sizeof(*date);
}

static BOOLEAN load_rules(TZ_RULES *rules)
{
    memset(rules, 0, sizeof(*rules));
    return read_dword(L"Bias", &rules->Bias) && read_dword(L"StandardBias", &rules->StandardBias) &&
           read_dword(L"DaylightBias", &rules->DaylightBias) && read_date(L"StandardStart", &rules->StandardDate) &&
           read_date(L"DaylightStart", &rules->DaylightDate);
}

/*
 * which period the instant is in, its offset in minutes, and the instant of the next change (0 for none). A zone
 * whose rules lack a month for either change has no daylight time, and the offset is its bias.
 */
static void evaluate(const TZ_RULES *r, LONG64 now, LONG *bias, ULONG *id, LONG64 *since, LONG64 *next)
{
    *since = 0;
    *next = 0;
    LONG64 probe;
    if (!cutover(&r->StandardDate, 2000, &probe) || !cutover(&r->DaylightDate, 2000, &probe)) {
        *bias = r->Bias;
        *id = 0;
        return;
    }
    struct {
        LONG64 utc;
        BOOLEAN daylight;
    } changes[6];
    ULONG count = 0;
    ULONG year = year_of(now - (LONG64)(r->Bias + r->StandardBias) * MINUTE_HNS);
    for (ULONG y = year - 1; y <= year + 1; y++) {
        LONG64 local;
        /* the change to daylight time is told in standard time, the change back in daylight time. */
        if (cutover(&r->DaylightDate, y, &local)) {
            changes[count].utc = local + (LONG64)(r->Bias + r->StandardBias) * MINUTE_HNS;
            changes[count++].daylight = TRUE;
        }
        if (cutover(&r->StandardDate, y, &local)) {
            changes[count].utc = local + (LONG64)(r->Bias + r->DaylightBias) * MINUTE_HNS;
            changes[count++].daylight = FALSE;
        }
    }
    for (ULONG i = 1; i < count; i++)
        for (ULONG j = i; j > 0 && changes[j - 1].utc > changes[j].utc; j--) {
            typeof(changes[0]) swap = changes[j];
            changes[j] = changes[j - 1];
            changes[j - 1] = swap;
        }
    BOOLEAN daylight = FALSE;
    for (ULONG i = 0; i < count; i++) {
        if (changes[i].utc <= now) {
            daylight = changes[i].daylight;
            *since = changes[i].utc;
        } else {
            *next = changes[i].utc;
            break;
        }
    }
    *bias = r->Bias + (daylight ? r->DaylightBias : r->StandardBias);
    *id = daylight ? 2 : 1;
}

/* ------------------------------------------------------------ the page and the timer */

static void write_page(LONG bias_minutes, ULONG id, LONG64 start, LONG64 end)
{
    KUSER_SHARED_DATA *k = SharedUserData;
    LONG64 bias = (LONG64)bias_minutes * MINUTE_HNS;
    /* the stamp is odd while the fields change, and the bias is read until its two halves of the high part agree. */
    __atomic_add_fetch(&k->TimeZoneBiasStamp, 1, __ATOMIC_SEQ_CST);
    k->TimeZoneBias.High2Time = (LONG)(bias >> 32);
    k->TimeZoneBias.LowPart = (ULONG)bias;
    k->TimeZoneBias.High1Time = (LONG)(bias >> 32);
    k->TimeZoneId = id;
    k->TimeZoneBiasEffectiveStart.QuadPart = start;
    k->TimeZoneBiasEffectiveEnd.QuadPart = end;
    __atomic_add_fetch(&k->TimeZoneBiasStamp, 1, __ATOMIC_SEQ_CST);
}

/* works the period out again and writes it. returns whether the offset of the clock changed. */
static BOOLEAN refresh(BOOLEAN boot)
{
    LARGE_INTEGER now;
    KeQuerySystemTime(&now);
    ULONG year = year_of(now.QuadPart);
    /* the rules of a zone that changed them are those of the year the clock is in. */
    if (HalBootInfo->timezone_key[0] && year != g_rules_year) {
        if (!boot)
            CmRefreshTimeZoneRules(year);
        TZ_RULES rules;
        if (load_rules(&rules))
            g_rules = rules;
        g_rules_year = year;
    }
    LONG bias = g_bias_minutes;
    ULONG id = 0;
    LONG64 since = 0, next = 0;
    if (g_rules_year) {
        evaluate(&g_rules, now.QuadPart, &bias, &id, &since, &next);
        /* the host is the truth about its own clock: rules that give neither of the zone's two offsets for the host's
           are not the host's zone, and the offset the machine started with stays, with no change to come. */
        if (boot && bias != HalBootInfo->timezone_bias && g_rules.Bias + g_rules.DaylightBias != HalBootInfo->timezone_bias &&
            g_rules.Bias + g_rules.StandardBias != HalBootInfo->timezone_bias) {
            KiLogPrintf("time zone: the rules give %d minutes and the host %d, the host's stays", bias, HalBootInfo->timezone_bias);
            bias = HalBootInfo->timezone_bias;
            id = 0;
            next = 0;
        }
    } else {
        bias = HalBootInfo->timezone_bias;
    }
    /* the rules are read again when the year changes. */
    LONG64 new_year = days_from_civil(year + 1, 1, 1) * DAY_HNS + (LONG64)(g_rules.Bias + g_rules.StandardBias) * MINUTE_HNS;
    if (g_rules_year && (!next || next > new_year))
        next = new_year;
    BOOLEAN changed = bias != g_bias_minutes;
    g_bias_minutes = bias;
    write_page(bias, id, since ? since : now.QuadPart, next ? next - 10000 : 0);
    if (changed && !boot)
        CmSetValue(KEY_TIME_ZONE, L"ActiveTimeBias", REG_DWORD, &(ULONG){(ULONG)bias}, sizeof(ULONG));
    if (next) {
        LARGE_INTEGER due = {.QuadPart = next};
        KeSetTimer(&g_timer, due, &g_dpc);
    }
    return changed;
}

static void NTAPI timer_dpc(PKDPC dpc, PVOID context, PVOID a1, PVOID a2)
{
    KeSetEvent(&g_event, IO_NO_INCREMENT, FALSE);
}

static void NTAPI worker(PVOID context)
{
    for (;;) {
        KeWaitForSingleObject(&g_event, Executive, KernelMode, FALSE, nullptr);
        if (refresh(FALSE)) {
            KiLogPrintf("time zone: the offset of the clock is %d minutes now", g_bias_minutes);
            PsWin32TimeChange();
        }
    }
}

void ExInitializeTimeZone(void)
{
    KeInitializeTimer(&g_timer);
    KeInitializeDpc(&g_dpc, timer_dpc, nullptr);
    KeInitializeEvent(&g_event, SynchronizationEvent, FALSE);
    g_bias_minutes = HalBootInfo->timezone_bias;
    refresh(TRUE);
    CmSetValue(KEY_TIME_ZONE, L"ActiveTimeBias", REG_DWORD, &(ULONG){(ULONG)g_bias_minutes}, sizeof(ULONG));
    if (!NT_SUCCESS(PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, worker, nullptr)))
        KiLogPrintf("time zone: no thread to follow daylight time");
}
