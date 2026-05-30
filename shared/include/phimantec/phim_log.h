#ifndef PHIM_LOG_H
#define PHIM_LOG_H

#include <stdarg.h>

typedef enum phim_log_level_e {
    PHIM_LOG_DEBUG = 0,
    PHIM_LOG_INFO = 1,
    PHIM_LOG_WARN = 2,
    PHIM_LOG_ERROR = 3
} phim_log_level_t;

void phim_log_set_level(phim_log_level_t level);
void phim_log_msg(phim_log_level_t level, const char *file, int line, const char *fmt, ...);
void phim_log_vmsg(phim_log_level_t level, const char *file, int line, const char *fmt, va_list ap);

#define PHIM_LOGD(...) phim_log_msg(PHIM_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define PHIM_LOGI(...) phim_log_msg(PHIM_LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define PHIM_LOGW(...) phim_log_msg(PHIM_LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define PHIM_LOGE(...) phim_log_msg(PHIM_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif
