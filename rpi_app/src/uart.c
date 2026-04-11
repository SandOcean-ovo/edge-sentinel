#include "uart.h"

int uart_init(const char* device, int baud) 
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) return -1;

    struct termios options;
    tcgetattr(fd, &options);

    // 设置波特率 (例如 B115200)
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);

    // 重点：设置为 Raw Mode (原始模式)，不处理特殊字符
    options.c_cflag |= (CLOCAL | CREAD); // 允许接收
    options.c_cflag &= ~PARENB;          // 无校验
    options.c_cflag &= ~CSTOPB;          // 1位停止位
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;              // 8位数据位
    
    // 禁用回显、信号字符等
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    // 禁用输入输出处理（防止换行符转换等）
    options.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
    options.c_oflag &= ~OPOST;

    tcsetattr(fd, TCSANOW, &options);
    return fd;
}