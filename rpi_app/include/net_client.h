#ifndef EDGE_NET_H
#define EDGE_NET_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// 初始化：创建 socket, 设为非阻塞
int net_client_init(const char *ip, int port);

int net_client_deinit(void);

int net_client_get_fd(void);

int net_client_check_connect_status(void);

// 发送数据：尝试非阻塞发送
// 返回值：>0 成功发送的字节数, 0 缓冲区满, -1 彻底断开
int net_client_send(const char *data, size_t len);

// 状态检查：判断当前是否连接正常
int net_client_check_connect_status(void);



#endif