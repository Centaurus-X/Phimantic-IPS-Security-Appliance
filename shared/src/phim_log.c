#include "phimantec/phim_log.h"
#include "phimantec/phim_time.h"

#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static phim_log_level_t g_log_level = PHIM_LOG_INFO;

static const char *phim_log_level_name(phim_log_level_t level)
{
    switch (level) {
        case PHIM_LOG_DEBUG: return "DEBUG";
        case PHIM_LOG_INFO: return "INFO";
        case PHIM_LOG_WARN: return "WARN";
        case PHIM_LOG_ERROR: return "ERROR";
        default: return "LOG";
    }
}

void phim_log_set_level(phim_log_level_t level)
{
    pthread_mutex_lock(&g_log_lock);
    g_log_level = level;
    pthread_mutex_unlock(&g_log_lock);
}

void phim_log_vmsg(phim_log_level_t level, const char *file, int line, const char *fmt, va_list ap)
{
    if (level < g_log_level) {
        return;
    }

    pthread_mutex_lock(&g_log_lock);
    (void)fprintf(stderr, "[%llu] %-5s %s:%d: ",
                  (unsigned long long)phim_now_ms(), phim_log_level_name(level), file, line);
    (void)vfprintf(stderr, fmt, ap);
    (void)fputc('\n', stderr);
    pthread_mutex_unlock(&g_log_lock);
}

void phim_log_msg(phim_log_level_t level, const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    phim_log_vmsg(level, file, line, fmt, ap);
    va_end(ap);
}
