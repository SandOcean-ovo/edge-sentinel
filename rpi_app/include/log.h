#ifndef EDGE_LOG_H
#define EDGE_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

typedef enum
{
    LOG_INFO = 0,
    LOG_WARN = 1,
    LOG_ERROR = 2,
} Loglevel_t;

int log_init(const char* file_path);

void log_deinit();

void edge_log(Loglevel_t level, const char* format, ...);



#endif