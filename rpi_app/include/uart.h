#ifndef EDGE_UART_H
#define EDGE_UART_H

#include <termios.h>
#include <fcntl.h>

#define UART_DEVICE "/dev/serial0"

int uart_init(const char* device, int baud); // TODO: 增加波特率的传递

#endif 