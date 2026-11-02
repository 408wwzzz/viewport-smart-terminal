#ifndef __TALKROOM_CLIENT_H__
#define __TALKROOM_CLIENT_H__

/**
 * 连接聊天室服务端，并启动后台接收线程
 * @ip:       服务端IP地址
 * @port:     服务端端口
 * @nickname: 聊天昵称（连接成功后自动发给服务端注册）
 * 返回: 0 成功, -1 失败（含已连接的情况）
 */
int talkroom_client_connect(const char *ip, int port, const char *nickname);

/**
 * 断开连接并清理（可重复调用）
 */
void talkroom_client_disconnect(void);

/**
 * 是否已连接
 * 返回: 1 已连接, 0 未连接
 */
int talkroom_client_connected(void);

/**
 * 发送一条聊天消息（内部自动补换行符）
 * 返回: 0 成功, -1 失败
 */
int talkroom_client_send(const char *msg);

/**
 * 非阻塞取出一条服务端发来的消息（供 LVGL 定时器轮询调用）
 * @buf:  输出缓冲区
 * @size: 缓冲区大小
 * 返回: 1 取到一条消息; 0 暂无消息; -1 连接已丢失（仅通知一次）
 */
int talkroom_client_recv(char *buf, int size);

#endif /* __TALKROOM_CLIENT_H__ */
