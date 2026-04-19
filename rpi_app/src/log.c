#include "log.h"

static FILE* g_log_fp = NULL;

int log_init(const char* file_path)
{
    g_log_fp = fopen(file_path, "a");
    if(!g_log_fp)
    {
        perror("Cannot open log!");
        return -1;
    }
    return 0;
}

void log_deinit(void)
{
    fclose(g_log_fp);
}

void edge_log(Loglevel_t level, const char* format, ...)
{
    time_t rawtime;
    struct tm info;
    time(&rawtime);
    localtime_r(&rawtime, &info);
    
    char* level_str = NULL;
    char* color_code = NULL;
    switch (level) 
    {
        case LOG_INFO:
            level_str = "INFO";
            color_code = "\033[32m"; // 绿色
            break;
        case LOG_WARN:
            level_str = "WARN";
            color_code = "\033[33m"; // 黄色
            break;
        case LOG_ERROR:
            level_str = "ERROR";
            color_code = "\033[31m"; // 红色
            break;
        default:
            level_str = "UNKNOWN";
            color_code = "\033[0m";  // 默认色
            break;
    }
    printf("%s[%s]\033[0m", color_code, level_str);
    char buffer[80];
    strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", &info);
    printf("[%s] ", buffer);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    if (g_log_fp != NULL) {
        // 写入 [LEVEL][TIME] 部分
        fprintf(g_log_fp, "[%s][%s] ", level_str, buffer);

        // 写入具体内容
        va_list args2;
        va_start(args2, format);
        vfprintf(g_log_fp, format, args2); // 注意这里用的是 vfprintf，直接写入文件流
        fflush(g_log_fp);
        va_end(args2);

        fprintf(g_log_fp, "\n"); // 换行

    }
}