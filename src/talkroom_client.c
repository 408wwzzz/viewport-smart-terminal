#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/select.h>

#include "talkroom_client.h"

#define BUFFER_SIZE     4096
#define QUEUE_SIZE      256        /* 消息队列容量 */
#define QUEUE_MSG_LEN   512        /* 单条消息最大长度 */

/* ========== 内部状态 ========== */
static int sock_fd = -1;
static volatile int running = 0;
static volatile int connected = 0;
static volatile int conn_lost = 0;        /* 连接丢失标志（仅报告一次） */
static pthread_t recv_tid;

/* ---- 消息队列（线程安全环形缓冲） ---- */
static char queue[QUEUE_SIZE][QUEUE_MSG_LEN];
static int queue_head = 0;                /* 下一个可读位置 */
static int queue_tail = 0;                /* 下一个可写位置 */
static int queue_count = 0;               /* 当前消息数 */
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ---- 行缓冲（处理 TCP 粘包/断行） ---- */
static char line_buf[BUFFER_SIZE];
static int line_len = 0;

/* ========== 内部函数 ========== */

/* 往队列尾部追加一条消息（线程安全，满则丢弃最旧消息） */
static void queue_push(const char *msg)
{
    pthread_mutex_lock(&queue_mutex);

    if (queue_count >= QUEUE_SIZE) {
        /* 丢弃最旧的一条 */
        queue_head = (queue_head + 1) % QUEUE_SIZE;
        queue_count--;
    }

    strncpy(queue[queue_tail], msg, QUEUE_MSG_LEN - 1);
    queue[queue_tail][QUEUE_MSG_LEN - 1] = '\0';
    queue_tail = (queue_tail + 1) % QUEUE_SIZE;
    queue_count++;

    pthread_mutex_unlock(&queue_mutex);
}

/* 从队列头部取出一条消息（线程安全） */
static int queue_pop(char *buf, int size)
{
    pthread_mutex_lock(&queue_mutex);

    if (queue_count <= 0) {
        pthread_mutex_unlock(&queue_mutex);
        return 0;
    }

    strncpy(buf, queue[queue_head], size - 1);
    buf[size - 1] = '\0';
    queue_head = (queue_head + 1) % QUEUE_SIZE;
    queue_count--;

    pthread_mutex_unlock(&queue_mutex);
    return 1;
}

/* 将收到的字节流按行拆分并入队 */
static void feed_line_buffer(const char *data, int len)
{
    for (int i = 0; i < len; i++) {
        char ch = data[i];

        if (ch == '\n') {
            /* 遇到换行 —— 输出一条完整消息 */
            if (line_len > 0) {
                line_buf[line_len] = '\0';

                /* 构造带时间戳的显示字符串 */
                char time_buf[16];
                time_t now = time(NULL);
                struct tm *t = localtime(&now);
                strftime(time_buf, sizeof(time_buf), "%H:%M:%S", t);

                char display[QUEUE_MSG_LEN];
                snprintf(display, sizeof(display),
                         "[%s] %s\n", time_buf, line_buf);

                queue_push(display);
                line_len = 0;
            }
        } else if (ch == '\r') {
            /* 忽略回车 */
            continue;
        } else {
            /* 普通字符 —— 追加到行缓冲 */
            if (line_len < (int)sizeof(line_buf) - 1) {
                line_buf[line_len++] = ch;
            }
            /* 行太长了就丢弃（避免溢出） */
        }
    }
}

/* 后台接收线程 */
static void *recv_thread_func(void *arg)
{
    (void)arg;
    char buf[BUFFER_SIZE];

    while (running) {
        int n = recv(sock_fd, buf, sizeof(buf) - 1, 0);

        if (n <= 0) {
            if (running) {
                queue_push("[系统] 与服务器断开连接\n");
                conn_lost = 1;
            }
            running = 0;
            connected = 0;
            break;
        }

        feed_line_buffer(buf, n);
    }

    return NULL;
}

/* ========== 公开 API ========== */

int talkroom_client_connect(const char *ip, int port, const char *nickname)
{
    if (connected || running) {
        return -1; /* 已经连接 */
    }

    struct sockaddr_in addr;

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("talkroom: socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "talkroom: invalid IP %s\n", ip);
        close(sock_fd);
        sock_fd = -1;
        return -1;
    }

    /* 非阻塞 connect + select 超时：避免在调用方线程（LVGL 主线程）上
       阻塞几十秒（TCP 默认重试约 75 秒） */
    int flags = fcntl(sock_fd, F_GETFL, 0);
    fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);

    int ret = connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        perror("talkroom: connect");
        close(sock_fd);
        sock_fd = -1;
        return -1;
    }

    if (ret != 0) {
        /* EINPROGRESS：等待可写，3 秒超时 */
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock_fd, &wfds);
        struct timeval tv = {3, 0};
        int sret = select(sock_fd + 1, NULL, &wfds, NULL, &tv);
        if (sret <= 0) {
            fprintf(stderr, "talkroom: connect 超时\n");
            close(sock_fd);
            sock_fd = -1;
            return -1;
        }
        /* 检查连接是否真正成功 */
        int soerr = 0;
        socklen_t soerr_len = sizeof(soerr);
        getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &soerr, &soerr_len);
        if (soerr != 0) {
            fprintf(stderr, "talkroom: connect 失败: %s\n", strerror(soerr));
            close(sock_fd);
            sock_fd = -1;
            return -1;
        }
    }

    /* 恢复阻塞模式 */
    fcntl(sock_fd, F_SETFL, flags);

    /* 发送昵称作为第一条消息（服务端以此注册客户端） */
    char join_msg[256];
    snprintf(join_msg, sizeof(join_msg), "%s\n", nickname);
    send(sock_fd, join_msg, strlen(join_msg), 0);

    /* 启动接收线程 */
    running   = 1;
    connected = 1;
    conn_lost = 0;
    line_len  = 0;
    queue_count = 0;
    queue_head  = 0;
    queue_tail  = 0;

    if (pthread_create(&recv_tid, NULL, recv_thread_func, NULL) != 0) {
        perror("talkroom: pthread_create");
        close(sock_fd);
        sock_fd   = -1;
        running   = 0;
        connected = 0;
        return -1;
    }
    pthread_detach(recv_tid);

    printf("talkroom: 已连接到 %s:%d\n", ip, port);
    return 0;
}

void talkroom_client_disconnect(void)
{
    if (!connected && !running) {
        return; /* 已经断开 */
    }

    running = 0;

    if (sock_fd > 0) {
        shutdown(sock_fd, SHUT_RDWR);  /* 唤醒阻塞中的 recv */
        close(sock_fd);
        sock_fd = -1;
    }

    connected = 0;
    conn_lost = 0;

    /* 清空残留队列 */
    pthread_mutex_lock(&queue_mutex);
    queue_count = 0;
    queue_head  = 0;
    queue_tail  = 0;
    line_len    = 0;
    pthread_mutex_unlock(&queue_mutex);

    printf("talkroom: 已断开连接\n");
}

int talkroom_client_connected(void)
{
    return connected ? 1 : 0;
}

int talkroom_client_send(const char *msg)
{
    char send_buf[BUFFER_SIZE + 64];

    if (!connected || sock_fd < 0) {
        return -1;
    }

    snprintf(send_buf, sizeof(send_buf), "%s\n", msg);

    int ret = send(sock_fd, send_buf, strlen(send_buf), 0);
    if (ret < 0) {
        if (errno == EPIPE || errno == ECONNRESET) {
            printf("talkroom: 发送失败，连接已断开\n");
            running   = 0;
            connected = 0;
            conn_lost = 1;
        }
        return -1;
    }

    return 0;
}

int talkroom_client_recv(char *buf, int size)
{
    /* 连接丢失只报告一次 */
    if (conn_lost) {
        conn_lost = 0;
        snprintf(buf, size, "[系统] 与服务器断开连接\n");
        return -1;
    }

    return queue_pop(buf, size);
}