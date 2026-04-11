#ifndef EDGE_UART_H
#define EDGE_UART_H

#include <termios.h>
#include <fcntl.h>
#include <unistd.h>

#define UART_DEVICE "/dev/ttyAMA0"

int uart_init(const char* device, int baud); // TODO: 增加波特率的传递

#endif 