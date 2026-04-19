#include "conf.h"

/* 工具函数，用于去除字符串前后的空格等 */
static void trim(char *str)
{
    char *p = str;
    int l = strlen(p);

    while (l > 0 && isspace(p[l - 1]))
    {
        p[l - 1] = '\0';
        l--;
    }

    while (l > 0 && isspace(*p))
    {
        memmove(p, p + 1, strlen(p)); /* 尾部的空格都变成 '\0' 了，不会被移动，不用害怕内存重叠 */
    }
}

int load_config(const char *file_path, GatewayConfig_t *config)
{
    FILE *pfile = fopen(file_path, "r"); /* 以只读模式打开需要读取的文件 */
    if (!pfile)
    {
        perror("Cannot open file!");
        return 1;
    }

    char line[128];

    while (fgets(line, sizeof(line), pfile))
    {
        if (line[0] == '\n' || line[0] == '#' || line[0] == '\r')
            continue; /* 忽略空行、以#开头的注释行 */

        char *sep = strchr(line, '=');
        if (!sep)
            continue; /* 该行非法，跳过 */

        *sep = '\0';
        char *key = line;      /* 前半部分是键 */
        char *value = sep + 1; /* 后半部分是值 */

        trim(key);
        trim(value);

        if (strcmp(key, "baudrate") == 0)
        {
            config->baudrate = atoi(value);
        }
        else if (strcmp(key, "temperture_threshold") == 0)
        {
            config->temp_threshold = atof(value);
        }
        else if (strcmp(key, "device_id") == 0)
        {
            strncpy(config->device_id, value, sizeof(config->device_id) - 1);
            config->device_id[sizeof(config->device_id) - 1] = '\0';
        }
        else if (strcmp(key, "uart_dev") == 0)
        {
            strncpy(config->uart_dev, value, sizeof(config->uart_dev) - 1);
            config->uart_dev[sizeof(config->uart_dev) - 1] = '\0';
        }
        else if (strcmp(key, "log_file") == 0)
        {
            strncpy(config->log_file, value, sizeof(config->log_file) - 1);
            config->log_file[sizeof(config->log_file) - 1] = '\0';
        }
        else if (strcmp(key, "database_file") == 0)
        {
            strncpy(config->db_path, value, sizeof(config->db_path) - 1);
            config->db_path[sizeof(config->db_path) - 1] = '\0';
        }
        else if (strcmp(key, "ip") == 0)
        {
            strncpy(config->ip, value, sizeof(config->ip) - 1);
            config->ip[sizeof(config->ip) - 1] = '\0';
        }
        else if (strcmp(key, "port") == 0)
        {
            config->port = atoi(value);
        }
    }

    fclose(pfile);
    return 0;
}
