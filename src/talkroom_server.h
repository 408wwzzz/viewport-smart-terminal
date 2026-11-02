#ifndef __TALKROOM_SERVER_H__
#define __TALKROOM_SERVER_H__

/**
 * 启动聊天室服务端（后台线程方式运行，不阻塞调用者）
 * @port: 监听端口（如 8888）
 * 返回: 0 成功, -1 失败
 * 说明: 内部防重复启动，已运行时再次调用直接返回 0
 */
int talkroom_server_start(int port);

/**
 * 服务端是否正在运行
 * 返回: 1 运行中, 0 未运行
 */
int talkroom_server_running(void);

#endif /* __TALKROOM_SERVER_H__ */
