#include "net_client.h"

static int g_sockfd = -1;

static int set_nonblocking(int fd)
{
    // 1. 获取原来的标志位
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;

    // 2. 在原有标志位基础上，加上 O_NONBLOCK
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int net_client_init(const char *ip, int port)
{
    // 1. 创建 TCP socket (IPv4, 流式传输)
    g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sockfd < 0)
    {
        perror("socket error");
        return -1;
    }

    // 开启Keepalive
    int keepalive = 1;
    int keepidle = 10; // 10秒空闲后开始发探测包
    int keepinterval = 3; // 探测包间隔3秒
    int keepcount = 3; // 探测失败3次后认定连接断开
    int keep_rc = 0;

    keep_rc &= setsockopt(g_sockfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    keep_rc &= setsockopt(g_sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    keep_rc &= setsockopt(g_sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepinterval, sizeof(keepinterval));
    keep_rc &= setsockopt(g_sockfd, IPPROTO_TCP, TCP_KEEPCNT, &keepcount, sizeof(keepcount));

    if (keep_rc < 0)
    {
        perror("setsockopt keepalive error");
        close(g_sockfd);
        g_sockfd = -1;
        return -1;
    }

    if (set_nonblocking(g_sockfd) < 0)
    {
        perror("set_nonblocking error");
        close(g_sockfd);
        g_sockfd = -1;
        return -1;
    }

    // 2. 配置目标服务器的地址结构体
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);            // 端口转为网络字节序
    inet_pton(AF_INET, ip, &server_addr.sin_addr); // IP 字符串转二进制

    int ret = connect(g_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    if (ret == 0)
    {
        return 0;
    }
    else
    {
        if (errno == EINPROGRESS)
        {
            return 0;
        }
        perror("connect error");
        close(g_sockfd);
        g_sockfd = -1;
        return -1;
    }
}

int net_client_deinit(void)
{
    if (g_sockfd >= 0)
    {
        close(g_sockfd);
        g_sockfd = -1;
    }
    return 0;
}

int net_client_get_fd(void)
{
    return g_sockfd;
}

int net_client_check_connect_status(void)
{
    if (g_sockfd < 0)
        return -1;

    int err = 0;
    socklen_t len = sizeof(err);

    // 使用 getsockopt 获取 SO_ERROR
    if (getsockopt(g_sockfd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
    {
        // getsockopt 本身调用失败
        perror("getsockopt error");
        return -1;
    }

    if (err != 0)
    {
        // err 不为 0，说明异步连接失败了（例如 Connection Refused）
        // 打印错误码对应的字符串：strerror(err)
        perror(strerror(err));
        return -1;
    }

    // 到这里，说明三次握手真正成功了！
    return 0;
}

int net_client_send(const char *data, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        ssize_t ret = send(g_sockfd, (char *)data + sent,
                           len - sent, MSG_NOSIGNAL);
        if (ret > 0)
        {
            sent += ret;
        }
        else if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN)
            {
                // 非阻塞缓冲区满，需要等 EPOLLOUT
                // 简单处理：返回已发送量，让上层知道
                return (int)sent;
            }
            return -1; // 真正错误
        }
    }
    return (int)sent; // 一定等于 len
}