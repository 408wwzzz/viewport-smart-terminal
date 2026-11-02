#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#define MAX_CLIENTS     256
#define MAX_EVENTS      512
#define PORT            8888
#define BUFFER_SIZE     4096
#define LOG_FILE        "chatroom_log.txt"

/* ========== 全局数据 ========== */
static int client_sockets[MAX_CLIENTS];   /* 在线客户端套接字数组 */
static int client_count = 0;              /* 当前在线数 */
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE *log_fp = NULL;               /* 日志文件指针 */

/* ========== 工具函数 ========== */

/* 获取当前时间字符串 */
void get_time_str(char *buf, int buf_size)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", t);
}

/* 写日志（线程安全） */
void write_log(const char *format, ...)
{
    char time_buf[64];
    get_time_str(time_buf, sizeof(time_buf));

    va_list args;
    va_start(args, format);

    /* 同时输出到终端 */
    printf("[%s] ", time_buf);
    vprintf(format, args);
    printf("\n");
    fflush(stdout);

    va_end(args);

    /* 写入日志文件 */
    if (log_fp != NULL) {
        va_start(args, format);
        fprintf(log_fp, "[%s] ", time_buf);
        vfprintf(log_fp, format, args);
        fprintf(log_fp, "\n");
        fflush(log_fp);
        va_end(args);
    }
}

/* 设置文件描述符为非阻塞 */
int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* 广播消息给所有客户端（除了发送者） */
void broadcast_message(int sender_fd, const char *msg, int msg_len)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < client_count; i++) {
        int fd = client_sockets[i];
        if (fd != sender_fd && fd > 0) {
            int ret = send(fd, msg, msg_len, 0);
            if (ret < 0 && errno != EAGAIN) {
                write_log("发送给客户端 %d 失败: %s", fd, strerror(errno));
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

/* 添加客户端到在线列表 */
void add_client(int fd)
{
    pthread_mutex_lock(&clients_mutex);

    if (client_count < MAX_CLIENTS) {
        client_sockets[client_count++] = fd;
        write_log("客户端上线 (fd=%d), 当前在线: %d 人", fd, client_count);
    } else {
        write_log("客户端已满，拒绝 fd=%d", fd);
        close(fd);
    }

    pthread_mutex_unlock(&clients_mutex);
}

/* 从在线列表移除客户端 */
void remove_client(int fd)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < client_count; i++) {
        if (client_sockets[i] == fd) {
            /* 将最后一个元素移到当前位置 */
            client_sockets[i] = client_sockets[client_count - 1];
            client_count--;
            write_log("客户端下线 (fd=%d), 当前在线: %d 人", fd, client_count);
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
    close(fd);
}

/* 处理客户端消息 */
void handle_client_message(int client_fd)
{
    char buf[BUFFER_SIZE];
    int recv_len = recv(client_fd, buf, sizeof(buf) - 1, 0);

    if (recv_len <= 0) {
        /* 客户端断开连接 */
        if (recv_len == 0) {
            write_log("客户端 fd=%d 主动断开连接", client_fd);
        } else {
            write_log("接收客户端 fd=%d 数据出错: %s", client_fd, strerror(errno));
        }
        remove_client(client_fd);
        return;
    }

    buf[recv_len] = '\0';

    /* 去掉末尾的换行符用于日志 */
    char log_msg[BUFFER_SIZE];
    strncpy(log_msg, buf, sizeof(log_msg) - 1);
    log_msg[sizeof(log_msg) - 1] = '\0';
    int len = strlen(log_msg);
    while (len > 0 && (log_msg[len - 1] == '\n' || log_msg[len - 1] == '\r')) {
        log_msg[--len] = '\0';
    }

    write_log("收到消息 (fd=%d): %s", client_fd, log_msg);

    /* 构造带发送者标识的广播消息 */
    char broadcast_buf[BUFFER_SIZE + 64];
    snprintf(broadcast_buf, sizeof(broadcast_buf),
             "[客户端%d] %s\n", client_fd, buf);

    broadcast_message(client_fd, broadcast_buf, strlen(broadcast_buf));
}

/* ========== 主函数 ========== */
int main(int argc, char *argv[])
{
    int server_fd, epoll_fd;
    struct sockaddr_in server_addr;
    struct epoll_event ev, events[MAX_EVENTS];
    int port = PORT;

    /* 支持命令行指定端口 */
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "无效端口号: %s, 使用默认端口 %d\n", argv[1], PORT);
            port = PORT;
        }
    }

    /* 打开日志文件 */
    log_fp = fopen(LOG_FILE, "a");
    if (log_fp == NULL) {
        perror("打开日志文件失败");
        /* 不影响服务器运行，继续 */
    }

    /* 创建 TCP 套接字 */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket 创建失败");
        return -1;
    }

    /* 设置地址重用 */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* 绑定地址 */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;  /* 监听所有网络接口 */
    server_addr.sin_port        = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind 失败");
        close(server_fd);
        return -1;
    }

    /* 开始监听 */
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen 失败");
        close(server_fd);
        return -1;
    }

    /* 设置非阻塞 */
    set_nonblocking(server_fd);

    /* 创建 epoll 实例 */
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1 失败");
        close(server_fd);
        return -1;
    }

    /* 将服务器套接字加入 epoll */
    ev.events  = EPOLLIN | EPOLLET;  /* 边缘触发 */
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        perror("epoll_ctl 添加 server_fd 失败");
        close(server_fd);
        close(epoll_fd);
        return -1;
    }

    write_log("====== 聊天室服务器启动 ======");
    write_log("监听端口: %d, 等待客户端连接...", port);

    /* 主事件循环 */
    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait 失败");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == server_fd) {
                /* 新连接到达 */
                while (1) {
                    struct sockaddr_in client_addr;
                    socklen_t addr_len = sizeof(client_addr);
                    int client_fd = accept(server_fd,
                                           (struct sockaddr *)&client_addr,
                                           &addr_len);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;  /* 没有更多连接了 */
                        }
                        perror("accept 失败");
                        break;
                    }

                    char client_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr,
                              client_ip, sizeof(client_ip));

                    write_log("新连接来自 %s:%d (fd=%d)",
                              client_ip, ntohs(client_addr.sin_port), client_fd);

                    /* 设置客户端为非阻塞 */
                    set_nonblocking(client_fd);

                    /* 加入 epoll 监听 */
                    ev.events  = EPOLLIN | EPOLLET;
                    ev.data.fd = client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                        perror("epoll_ctl 添加 client_fd 失败");
                        close(client_fd);
                        continue;
                    }

                    /* 加入在线列表 */
                    add_client(client_fd);

                    /* 发送欢迎消息 */
                    char welcome[128];
                    snprintf(welcome, sizeof(welcome),
                             "[系统] 欢迎进入聊天室！当前在线 %d 人\n", client_count);
                    send(client_fd, welcome, strlen(welcome), 0);

                    /* 通知其他用户 */
                    char join_msg[128];
                    snprintf(join_msg, sizeof(join_msg),
                             "[系统] 客户端%d 进入了聊天室\n", client_fd);
                    broadcast_message(client_fd, join_msg, strlen(join_msg));
                }
            } else {
                /* 客户端数据到达或断开 */
                handle_client_message(fd);
            }
        }
    }

    /* 清理 */
    write_log("====== 聊天室服务器关闭 ======");
    close(server_fd);
    close(epoll_fd);

    /* 关闭所有客户端连接 */
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        close(client_sockets[i]);
    }
    client_count = 0;
    pthread_mutex_unlock(&clients_mutex);

    if (log_fp != NULL) {
        fclose(log_fp);
    }

    return 0;
}
