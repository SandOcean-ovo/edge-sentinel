#include "uart.h"

int uart_init(const char *device, int baud)
{

    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1)
        return -1;

    struct termios options;
    if (tcgetattr(fd, &options) != 0)
    {
        close(fd);
        return -1;
    }

    // 2. 根据传入参数动态设置波特率
    speed_t my_baud;
    switch (baud)
    {
    case 9600:
        my_baud = B9600;
        break;
    case 115200:
        my_baud = B115200;
        break;
    default:
        my_baud = B115200; // 默认 115200
    }
    cfsetispeed(&options, my_baud);
    cfsetospeed(&options, my_baud);

    // 3. 【核心修改】使用官方推荐的原始模式设置函数
    // 这行代码会自动完成你原本手动设置的那一堆标志位（ECHO, ICANON, IXON 等）
    cfmakeraw(&options);

    // 4. 设置硬件控制位（8N1）
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    // 5. 【新增】设置读取超时，避免 read 函数瞬间返回或永久卡死
    // VMIN = 0, VTIME = 10 表示 read 会等待最多 1 秒，若有数据则提前返回
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 10;

    // 6. 清除缓冲区并应用
    tcflush(fd, TCIOFLUSH);
    if (tcsetattr(fd, TCSANOW, &options) != 0)
    {
        close(fd);
        return -1;
    }

    return fd;
}