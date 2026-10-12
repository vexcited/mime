#include "mime_host.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static MimeLogLevel g_level = MIME_LOG_INFO;
static const char *g_prefix = "mime";
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

void mime_log_set_level(MimeLogLevel level)
{
    g_level = level;
}

void mime_log_set_prefix(const char *prefix)
{
    g_prefix = prefix;
}

void mime_logv(MimeLogLevel level, const char *fmt, va_list ap)
{
    static const char *const names[] = {"debug", "info", "warn", "error"};
    if (level < g_level)
        return;

    FILE *out = level >= MIME_LOG_WARN ? stderr : stdout;
    pthread_mutex_lock(&g_lock);
    if (!isatty(fileno(out))) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        struct tm tm;
        localtime_r(&ts.tv_sec, &tm);
        fprintf(out, "%02d:%02d:%02d.%03ld ", tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);
    }
    fprintf(out, "%s: %s: ", g_prefix, names[level]);
    vfprintf(out, fmt, ap);
    fputc('\n', out);
    fflush(out);
    pthread_mutex_unlock(&g_lock);
}

void mime_log(MimeLogLevel level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    mime_logv(level, fmt, ap);
    va_end(ap);
}

void mime_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    mime_logv(MIME_LOG_ERROR, fmt, ap);
    va_end(ap);
    exit(1);
}
