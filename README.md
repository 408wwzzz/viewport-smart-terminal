# Viewport 智能交互终端

> 面向公共空间数字标牌的嵌入式 Linux 交互终端：终端侧提供图集浏览、视频点播与互动游戏，服务端通过局域网完成多终端消息下发与同步。

面向场景：嵌入式 Linux 应用开发 / LVGL 图形界面 / 多进程与网络编程。

---

## 1. 为什么做这个

景区游客中心、商场楼层导视、医院候诊区这类场所，目前大量还在用「插 U 盘循环播放」的广告屏：内容是固定的，游客只能看不能操作，运营方改一次素材就要派人跑现场换卡。

本项目把这类屏拆成两层来做：终端侧做成可触摸交互的界面（翻图集、点播宣传片、玩一局小游戏延长停留时间），服务端做成可联网的消息通道，让运营人员在一个后台把素材和通知推到多块屏上。

## 2. 功能模块

| 模块 | 实现要点 | 代码位置 |
|---|---|---|
| 登录鉴权 | 双输入框共享一套软键盘，由焦点事件驱动键盘显隐；密码框掩码；两级校验状态机，任一步失败即清空并复位 | `src/face_change.c` `login_interface()` |
| 电子相册 | 复用同一个图片控件，只替换图像源不重建对象；索引取模实现首尾循环 | `src/face_change.c` `album_prev_callback()` / `album_next_callback()` |
| 宣传片点播 | `fork` + `execlp` 拉起 `mplayer -slave`，命名管道下发控制指令；支持暂停/继续、上下一集、快进；独立线程 `waitpid` 回收子进程 | `src/face_change.c` `mplayer_start()` / `mplayer_send_cmd()` / `mplayer_monitor_func()` |
| 互动游戏 | 2048：压缩—合并—再压缩，触摸手势方向判定，Top5 排行榜文件持久化 | `src/face_change.c` `move_left/right/up/down()` / `game2048_touch_cb()` |
| 多终端消息 | epoll 边缘触发 + 全非阻塞 I/O，支持 256 连接，收发日志落盘 | `src/talkroom_server.c` |
| 终端侧聊天 | 轮询定时器拉取新消息，消息区限量追加避免无限增长 | `src/face_change.c` `chat_timer_callback()` / `chat_append_limited()` |

## 3. 几个实现上的选择

**视频播放为什么用子进程而不是自己解码**

`mplayer` 的 `-slave` 模式提供了基于标准输入输出的命令行接口，配合 `-vo fbdev2` 可以直接写 framebuffer。这样播放器与界面进程彻底隔离：播放器崩溃不会带走界面，界面退出时杀掉子进程即可。管道用命名 FIFO，是因为要双向（一条下发命令，一条读状态），用无名管道需要额外管理两个 fd 的方向。

启动时若 FIFO 已存在会先清理再 `mkfifo`，避免上次异常退出留下的残留管道导致 `open` 阻塞。

**epoll 为什么用边缘触发**

服务端用 `EPOLLIN | EPOLLET`，配合全部 socket 设为非阻塞。边缘触发只在状态变化时通知一次，因此每次事件必须循环读到 `EAGAIN` 为止，否则会丢数据。代价是写起来更小心，收益是避免了水平触发下的事件重复通知。

**手势方向怎么判定**

触摸事件里取按下点与当前点的坐标差，横向差绝对值超过 30 像素才判定为横向滑动，否则看纵向差。先判主轴、再定方向，避免斜向滑动被误判成两个方向同时触发。

## 4. 目录结构

```text
.
├── main.c                    # 入口，初始化 LVGL 与显示
├── src/
│   ├── face_change.c         # 终端主界面：登录 / 相册 / 视频 / 2048 / 聊天
│   ├── talkroom_server.c     # epoll 消息服务端
│   ├── talkroom_client.c     # 终端侧通信客户端
│   └── object.c              # 界面基础对象封装
├── face_change.h
├── object.h
├── lv_mygec_font.h           # 中文字库
├── lv_conf.h                 # LVGL 配置
├── Makefile                  # 本机构建
└── CMakeLists.txt            # 交叉编译构建
```

## 5. 编译运行

依赖：

```text
LVGL v9.x
gcc / 交叉编译工具链（ARM64）
mplayer（需支持 -vo fbdev2 与 -slave）
```

LVGL 主体不在本仓库内，按下述方式准备后即可编译：

```bash
# 1. 获取 LVGL v9 源码，放到项目根目录的 lvgl/
git clone -b release/v9.0 https://gitee.com/mirrors/lvgl.git lvgl

# 2. 本机构建（需要 framebuffer 设备）
make -j4
./demo

# 3. 交叉编译到 ARM64 开发板
mkdir -p build-arm64 && cd build-arm64
cmake .. -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc
make -j4
```

服务端单独编译：

```bash
gcc -O2 -pthread src/talkroom_server.c -o talk_server
./talk_server          # 默认监听 8888
```

## 6. 已知局限

1. **LVGL 未内置于仓库**。仓库只保留应用层代码，需要按第 5 节自行准备 `lvgl/` 目录才能编译。
2. **仅在一块 1024×600 的 ARM64 开发板上实测**，其他分辨率需要改 `lv_conf.h` 与界面坐标常量。
3. **服务端连接数按 256 静态分配**，未做大压力测试，长连接下的内存增长未验证。
4. **视频格式未做兼容处理**，依赖 mplayer 自身的解码能力。
5. **消息同步为轮询模型**，轮询间隔内新消息会有可见延迟。

## 7. 声明

个人学习项目，用于验证嵌入式 Linux 下的图形界面、多进程通信与网络并发编程能力。
代码可自由参考，转载注明出处。
