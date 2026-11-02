#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include"face_change.h"
#include<stdio.h>
#include"always.h"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include "talkroom_client.h"
lv_obj_t *ui_main;//主界面
lv_obj_t *ui_A;//A界面
lv_obj_t *ui_B;//B界面
lv_obj_t *ui_C;//C界面
lv_obj_t *ui_D;//D界面
lv_obj_t *ui_login;//登录界面
lv_obj_t *ui_book;//电子相册界面
static int username_verified = 0;//用户名是否验证通过

#define MAIN_UI_BUTTON_A "A:/WZZ/32.bmp"
#define MAIN_UI_BUTTON_B "A:/WZZ/14.bmp"
#define MAIN_UI_BUTTON_C "A:/WZZ/31.bmp"
#define MAIN_UI_BUTTON_D "A:/WZZ/30.bmp"
#define MAIN_UI_BUTTON_L "A:/WZZ/left.bmp"
#define MAIN_UI_BUTTON_R "A:/WZZ/right.bmp"
#define MAIN_UI_BUTTON_E "A:/WZZ/17.bmp"
#define MAIN_UI_BUTTON_CC "A:/WZZ/CC.bmp"
#define MAIN_UI_BUTTON_DD "A:/WZZ/DD.bmp"
#define CORRECT_USERNAME "wzz"  // 正确用户名
#define CORRECT_PASSWORD "1"  // 正确密码

/* ========== 聊天室 全局状态 ========== */
static lv_obj_t *chat_display;        /* 消息显示区域 */
static lv_obj_t *chat_status_label;   /* 连接状态标签 */
static lv_obj_t *chat_input;          /* 消息输入框 */
static lv_obj_t *chat_connect_btn;    /* 连接/断开按钮 */
static lv_obj_t *chat_connect_label;  /* 按钮文字 */
static lv_obj_t *chat_send_btn;       /* 发送按钮 */
static lv_timer_t *chat_timer;        /* 轮询定时器 */
static char chat_nickname[32] = "开发板"; /* 聊天昵称 */
#define CHAT_SERVER_IP "192.168.31.110" /* 虚拟机IP地址，请根据实际修改 */

// 函数原型声明
void disable_scroll(lv_obj_t *obj);
void ui_main_buttonD_callback(lv_event_t *e);
/*==================== 视频播放器相关 ====================*/
// 视频文件数组
const char *video_list[] = {
    "0.mp4",
    "1.mp4",
    "2.mp4",
    "3.mp4",
    "4.mp4",
    "5.mp4"
};
#define VIDEO_COUNT (sizeof(video_list) / sizeof(video_list[0]))

// 有名管道路径
#define FIFO_PATH "/1.fifo"

// 当前播放索引
static int current_video_idx = 0;

// mplayer是否正在播放
static int mplayer_running = 0;

// mplayer子进程PID
static pid_t mplayer_pid = -1;

// 有名管道文件描述符
static int fifo_fd = -1;

// 检测自动播放结束的线程
static pthread_t mplayer_monitor_tid;
/*==================== mplayer 控制函数 ====================*/

/* 检测 mplayer 播放结束的线程 */
static void *mplayer_monitor_func(void *arg)
{
    pid_t pid = (pid_t)(long)arg;
    int status;

    if (waitpid(pid, &status, 0) == pid) {
        printf("mplayer 进程已退出 (status=%d)\n", WEXITSTATUS(status));
        /* 仅当仍是当前播放进程时才复位状态，避免误清新视频的状态 */
        if (mplayer_pid == pid) {
            mplayer_running = 0;
            mplayer_pid = -1;
        }
    }
    return NULL;
}

/**
 * mplayer_start: 启动mplayer播放指定视频
 * @file_name: 视频文件路径
 * 返回值: 成功0, 失败-1
 */
int mplayer_start(const char *file_name)
{
    // 1. 创建有名管道（如果不存在）
    if (access(FIFO_PATH, F_OK) != 0) {
        if (mkfifo(FIFO_PATH, 0666) != 0) {
            perror("mkfifo fail");
            return -1;
        }
    }

    // 2. fork 子进程直接 exec mplayer
    //    （取代 system("...&")，可获得真实 PID 供监视线程 waitpid）
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork fail");
        return -1;
    }
    if (pid == 0) {
        /* 子进程 */
        execlp("mplayer", "mplayer",
               "-vo", "fbdev2", "-slave", "-quiet",
               "-input", "file=/1.fifo",
               "-geometry", "112:75", "-zoom", "-x", "800", "-y", "450",
               file_name, NULL);
        perror("execlp mplayer fail");
        _exit(127);
    }

    printf("开始播放: %s (pid=%d)\n", file_name, pid);
    mplayer_pid = pid;
    mplayer_running = 1;

    // 3. 带超时打开有名管道（写端）
    //    O_NONBLOCK 使 open 在 mplayer 未就绪时立即返回 ENXIO，
    //    避免 mplayer 启动失败时永久阻塞整个程序
    fifo_fd = -1;
    for (int i = 0; i < 20; i++) {  /* 最多等待 2 秒 */
        fifo_fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
        if (fifo_fd >= 0) break;
        if (errno == ENXIO) {
            usleep(100000);
            continue;
        }
        perror("open fifo fail");
        break;
    }

    if (fifo_fd < 0) {
        printf("mplayer 未就绪，停止本次播放\n");
        mplayer_running = 0;
        mplayer_pid = -1;
        return -1;
    }

    // 恢复阻塞模式，保证控制命令完整写入
    int flags = fcntl(fifo_fd, F_GETFL, 0);
    fcntl(fifo_fd, F_SETFL, flags & ~O_NONBLOCK);

    // 4. 启动监视线程：视频自然播完或退出时同步状态
    pthread_create(&mplayer_monitor_tid, NULL, mplayer_monitor_func,
                   (void *)(long)pid);
    pthread_detach(mplayer_monitor_tid);

    return 0;
}

/**
 * mplayer_send_cmd: 通过有名管道发送控制命令
 * @cmd: 命令字符串（需包含\n）
 */
void mplayer_send_cmd(const char *cmd)
{
    if (fifo_fd > 0) {
        int ret = write(fifo_fd, cmd, strlen(cmd));
        if (ret < 0) {
            perror("write fifo fail");
        }
    }
}

/**
 * mplayer_stop: 停止mplayer播放
 */
void mplayer_stop(void)
{
    if (mplayer_running) {
        mplayer_send_cmd("quit\n");
        usleep(200000);
        if (fifo_fd > 0) {
            close(fifo_fd);
            fifo_fd = -1;
        }
        mplayer_running = 0;
        printf("播放已停止\n");
    }
}

/**
 * mplayer_play_next: 播放下一个视频
 */
void mplayer_play_next(void)
{
    // 先停止当前播放
    mplayer_stop();
    usleep(300000);

    // 切换到下一个
    current_video_idx = (current_video_idx + 1) % VIDEO_COUNT;
    printf("切换到下一个视频: %s (索引:%d)\n", video_list[current_video_idx], current_video_idx);

    // 播放新视频
    mplayer_start(video_list[current_video_idx]);
}

/**
 * mplayer_play_prev: 播放上一个视频
 */
void mplayer_play_prev(void)
{
    // 先停止当前播放
    mplayer_stop();
    usleep(300000);

    // 切换到上一个
    current_video_idx = (current_video_idx - 1 + VIDEO_COUNT) % VIDEO_COUNT;
    printf("切换到上一个视频: %s (索引:%d)\n", video_list[current_video_idx], current_video_idx);

    // 播放新视频
    mplayer_start(video_list[current_video_idx]);
}
/*==================== 界面C 视频播放器回调函数 ====================*/

// 停止/暂停按钮回调
void ui_main_buttonstop_callback(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED) {
        printf("暂停/继续按钮被按下\n");
        if (mplayer_running) {
            // 发送暂停命令（再按一次恢复播放）
            mplayer_send_cmd("pause\n");
            printf("发送暂停/继续指令\n");
        }
    }
}

// 下一首按钮回调
void ui_main_buttonnext_callback(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED) {
        printf("下一首按钮被按下\n");
        mplayer_play_next();
    }
}

// 上一首按钮回调
void ui_main_buttonprev_callback(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED) {
        printf("上一首按钮被按下\n");
        mplayer_play_prev();
    }
}

// 快进按钮回调
void ui_main_buttonfast_callback(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED) {
        printf("快进按钮被按下\n");
        if (mplayer_running) {
            // seek +10 表示快进10秒（相对当前位置）
            mplayer_send_cmd("seek +10\n");
            printf("快进10秒\n");
        }
    }
}


//在主界面上按下按钮A ----> 界面A 回调函数
void ui_main_buttonA_callback(lv_event_t * e)
{
    printf("ui_main_buttonA_callback\n");
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_SHORT_CLICKED) //如果点击按键A
    {
        printf("click A \n");
//切换界面A
    lv_obj_add_flag(ui_main, LV_OBJ_FLAG_HIDDEN); //添加隐藏属性
    lv_obj_remove_flag(ui_A, LV_OBJ_FLAG_HIDDEN);
    }
}

//主界面 按钮C 回调函数
void ui_main_buttonC_callback(lv_event_t*e)
{
    printf("ui_main-buttonC_callback\n");
    lv_event_code_t code3=lv_event_get_code(e);
    if(code3 == LV_EVENT_SHORT_CLICKED)
    {
        printf("click c\n");
//切换界面C
        lv_obj_add_flag(ui_main,LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui_C, LV_OBJ_FLAG_HIDDEN);
        //自动播放第一个视频
        if(!mplayer_running)
        {
            current_video_idx=0;
            mplayer_start(video_list[current_video_idx]);
        }   
    }
}

//返回按钮 回调函数
void ui_main_buttonE_callback(lv_event_t * e)
{
    printf("ui_main_buttonE_callback\n");
    lv_event_code_t code3 = lv_event_get_code(e);
    if(code3 == LV_EVENT_SHORT_CLICKED) //如果点击按键e
    {
        printf("click E \n");
        //如果从界面c返回，停止播放视频
        if(!lv_obj_has_flag(ui_C,LV_OBJ_FLAG_HIDDEN))
        {
            mplayer_stop();
        }
        //如果从界面D返回，断开聊天连接
        if(!lv_obj_has_flag(ui_D,LV_OBJ_FLAG_HIDDEN))
        {
            talkroom_client_disconnect();
            if(chat_timer) {
                lv_timer_del(chat_timer);
                chat_timer = NULL;
            }
            /* 重置 UI 状态，避免再次进入 D 界面时残留"已连接/断开" */
            lv_label_set_text(chat_status_label, "状态: 未连接");
            lv_label_set_text(chat_connect_label, "连接");
        }
//切回主界面
    lv_obj_remove_flag(ui_main, LV_OBJ_FLAG_HIDDEN); //添加隐藏属性
    lv_obj_add_flag(ui_A, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_B, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_C, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_D, LV_OBJ_FLAG_HIDDEN);
    }
}
// 相册图片数组
#define ALBUM_PHOTO_1 "A:/WZZ/5.bmp"
#define ALBUM_PHOTO_2 "A:/WZZ/7.bmp"
#define ALBUM_PHOTO_3 "A:/WZZ/8.bmp"
#define ALBUM_PHOTO_4 "A:/WZZ/9.bmp"
#define ALBUM_PHOTO_5 "A:/WZZ/16.bmp"

const char *album_photos[] = {ALBUM_PHOTO_1, ALBUM_PHOTO_2, ALBUM_PHOTO_3, ALBUM_PHOTO_4, ALBUM_PHOTO_5};
#define ALBUM_COUNT (sizeof(album_photos)/sizeof(album_photos[0]))

lv_obj_t *album_img;  // 相册图片控件
int album_idx = 0;    // 当前图片索引






// 相册上一张
void album_prev_callback(lv_event_t *e)
{
    album_idx = (album_idx - 1 + ALBUM_COUNT) % ALBUM_COUNT;
    lv_image_set_src(album_img, album_photos[album_idx]);
    printf("上一张: %d\n", album_idx + 1);
}

// 相册下一张
void album_next_callback(lv_event_t *e)
{
    album_idx = (album_idx + 1) % ALBUM_COUNT;
    lv_image_set_src(album_img, album_photos[album_idx]);
    printf("下一张: %d\n", album_idx + 1);
}

// 相册返回
void album_back_callback(lv_event_t *e)
{
    lv_obj_add_flag(ui_A, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui_main, LV_OBJ_FLAG_HIDDEN);
}


//界面A设计
void faceA()
{
    ui_A = lv_obj_create(lv_screen_active());
    lv_obj_set_size(ui_A, 1024, 600);
    
    // 标题
     // 创建标签作为按钮子对象
     static lv_style_t style_photo;
     lv_style_init(&style_photo);
     lv_obj_t *title = lv_label_create(ui_A);
    lv_style_set_text_font(&style_photo, &lv_font_montserrat_40);
    lv_style_set_text_color(&style_photo,lv_color_hex(0x030303));
    lv_label_set_text(title, "电子相册");
    lv_style_set_text_font(&style_photo, &always);
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_obj_add_style(title,&style_photo,0); 
    
    // 主图片显示
    album_img = lv_image_create(ui_A);
    lv_obj_set_size(album_img, 1024, 480);
    lv_obj_set_align(album_img, LV_ALIGN_CENTER);
    
    lv_image_set_src(album_img, album_photos[0]);
    
    // 上一张按钮
    lv_obj_t *btn_prev = lv_button_create(ui_A);
    lv_obj_set_size(btn_prev, 80, 60);
    lv_obj_align(btn_prev, LV_ALIGN_LEFT_MID, 50, 0);
    lv_obj_add_event_cb(btn_prev, album_prev_callback, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *label_prev = lv_label_create(btn_prev);
    static lv_style_t prev;
     lv_style_init(&prev);
    lv_label_set_text(label_prev, "上一张");
    lv_obj_set_align(label_prev, LV_ALIGN_CENTER);
    lv_style_set_text_font(&prev, &lv_font_montserrat_22);
    lv_style_set_text_font(&prev, &always);
    lv_obj_add_style(label_prev,&prev,0);
    
       // 下一张按钮
    lv_obj_t *btn_next = lv_button_create(ui_A);
    lv_obj_set_size(btn_next, 80, 60);
    lv_obj_align(btn_next, LV_ALIGN_RIGHT_MID, -50, 0);
    lv_obj_add_event_cb(btn_next, album_next_callback, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *label_next = lv_label_create(btn_next);
    static lv_style_t next;
     lv_style_init(&next);
    lv_label_set_text(label_next, "下一张");
    lv_obj_set_align(label_next, LV_ALIGN_CENTER);
    lv_style_set_text_font(&next, &lv_font_montserrat_22);
    lv_style_set_text_font(&next, &always);
    lv_obj_add_style(label_next,&next,0);
    
    // 返回按钮
    lv_obj_t *btn_back = lv_button_create(ui_A);
    lv_obj_set_size(btn_back, 80, 40);
    lv_obj_set_align(btn_back, LV_ALIGN_BOTTOM_RIGHT);
    lv_obj_set_pos(btn_back, -20, -20);
    lv_obj_add_event_cb(btn_back, album_back_callback, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *label_back = lv_label_create(btn_back);
    static lv_style_t back;
     lv_style_init(&back);
    lv_label_set_text(label_back, "返回");
    lv_obj_set_align(label_back, LV_ALIGN_CENTER);
    lv_style_set_text_font(&back, &lv_font_montserrat_22);
    lv_style_set_text_font(&back, &always);
    lv_obj_add_style(label_back,&back,0);
    
    // 默认隐藏
    lv_obj_add_flag(ui_A, LV_OBJ_FLAG_HIDDEN);
    album_idx = 0;
    disable_scroll(ui_A);

}
///=========界面B 游戏2048设计============///
// 2048游戏数据
static int game2048_grid[4][4] = {0};
static lv_obj_t *game2048_tiles[4][4];
static int game2048_score = 0;
static lv_obj_t *game2048_score_label;

// 排行榜数据 - 保存前5名分数
#define MAX_SCORES 5
static int game2048_scores[MAX_SCORES] = {0};
static int score_count = 0;

// 排行榜窗口指针
lv_obj_t *leaderboard_win;

// 获取方块颜色
lv_color_t get_tile_color(int value)
{
    switch(value) {
        case 0: return lv_color_hex(0xcdc1b4);
        case 2: return lv_color_hex(0xeee4da);
        case 4: return lv_color_hex(0xede0c8);
        case 8: return lv_color_hex(0xf2b179);
        case 16: return lv_color_hex(0xf59563);
        case 32: return lv_color_hex(0xf67c5f);
        case 64: return lv_color_hex(0xf65e3b);
        case 128: return lv_color_hex(0xedcf72);
        case 256: return lv_color_hex(0xedcc61);
        case 512: return lv_color_hex(0xedc850);
        case 1024: return lv_color_hex(0xedc53f);
        case 2048: return lv_color_hex(0xedc22e);
        default: return lv_color_hex(0x3c3a32);
    }
}

// 获取文字颜色
lv_color_t get_text_color(int value)
{
    return (value <= 4) ? lv_color_hex(0x776e65) : lv_color_hex(0xf9f6f2);
}

// 更新界面
void update_game2048_display(void)
{
    int i, j;
    for(i = 0; i < 4; i++) {
        for(j = 0; j < 4; j++) {
            lv_obj_t *tile = game2048_tiles[i][j];
            lv_obj_t *label = lv_obj_get_child(tile, 0);
            
            if(game2048_grid[i][j] == 0) {
                lv_label_set_text(label, "");
            } else {
                char num_str[10];
                snprintf(num_str, sizeof(num_str), "%d", game2048_grid[i][j]);
                lv_label_set_text(label, num_str);
            }
            lv_obj_set_style_bg_color(tile, get_tile_color(game2048_grid[i][j]), 0);
            lv_obj_set_style_text_color(label, get_text_color(game2048_grid[i][j]), 0);
        }
    }
    char score_str[30];
    snprintf(score_str, sizeof(score_str), "Score: %d", game2048_score);
    lv_label_set_text(game2048_score_label, score_str);
}

// 禁止界面移动/滚动
void disable_scroll(lv_obj_t *obj)
{
    // 清除可滚动标志
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    // 设置滚动方向为无
    lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
    // 关闭滚动条
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

// 检查游戏是否结束（无法移动和合并）
int is_game_over(void)
{
    int i, j;
    // 检查是否有空白格子
    for(i = 0; i < 4; i++) {
        for(j = 0; j < 4; j++) {
            if(game2048_grid[i][j] == 0) {
                return 0;
            }
        }
    }
    // 检查水平方向是否有相邻相同数字
    for(i = 0; i < 4; i++) {
        for(j = 0; j < 3; j++) {
            if(game2048_grid[i][j] == game2048_grid[i][j+1]) {
                return 0;
            }
        }
    }
    // 检查垂直方向是否有相邻相同数字
    for(j = 0; j < 4; j++) {
        for(i = 0; i < 3; i++) {
            if(game2048_grid[i][j] == game2048_grid[i+1][j]) {
                return 0;
            }
        }
    }
    return 1;
}

// 游戏结束窗口指针
lv_obj_t *game_over_win;

// 游戏结束窗口重新开始回调
void game_over_restart_callback(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_SHORT_CLICKED)
    {
        lv_obj_del(game_over_win);
        game_over_win = NULL;  // 防止悬空指针
        memset(game2048_grid, 0, sizeof(game2048_grid));
        game2048_score = 0;
        game2048_grid[0][0] = 2;
        game2048_grid[3][3] = 2;
        update_game2048_display();
    }
}

// 排行榜数据文件
#define SCORE_FILE "game2048_scores.txt"

// 保存排行榜到文件
void save_scores_to_file(void)
{
    FILE *fp = fopen(SCORE_FILE, "w");
    if(fp == NULL) {
        printf("排行榜文件写入失败: %s\n", SCORE_FILE);
        return;
    }
    int i;
    for(i = 0; i < score_count; i++) {
        fprintf(fp, "%d\n", game2048_scores[i]);
    }
    fclose(fp);
}

// 从文件加载排行榜（启动时调用）
void load_scores_from_file(void)
{
    FILE *fp = fopen(SCORE_FILE, "r");
    if(fp == NULL) {
        return; // 文件不存在，排行榜为空
    }
    char line[32];
    score_count = 0;
    while(score_count < MAX_SCORES && fgets(line, sizeof(line), fp) != NULL) {
        int s = 0;
        if(sscanf(line, "%d", &s) == 1 && s > 0) {
            game2048_scores[score_count++] = s;
        }
    }
    fclose(fp);
}

// 保存分数到排行榜
void save_score(int score)
{
    int i, j;
    // 找到插入位置
    for(i = 0; i < score_count; i++) {
        if(score > game2048_scores[i]) {
            break;
        }
    }

    // 如果分数可以进入排行榜
    if(i < MAX_SCORES) {
        // 向后移动分数
        for(j = score_count > MAX_SCORES ? MAX_SCORES - 1 : score_count; j > i; j--) {
            game2048_scores[j] = game2048_scores[j-1];
        }
        // 插入新分数
        game2048_scores[i] = score;
        if(score_count < MAX_SCORES) {
            score_count++;
        }
        // 持久化到文件
        save_scores_to_file();
    }
}

// 关闭排行榜窗口回调
void leaderboard_close_callback(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_SHORT_CLICKED) {
        lv_obj_del(leaderboard_win);
        leaderboard_win = NULL; // 防止悬空指针
    }
}

// 显示排行榜窗口
void show_leaderboard(lv_event_t *e)
{
    (void)e; // 未使用参数
    // 若排行榜窗口已存在，先删除，避免多次点击叠加
    if(leaderboard_win != NULL) {
        lv_obj_del(leaderboard_win);
        leaderboard_win = NULL;
    }
    leaderboard_win = lv_obj_create(lv_screen_active());
    lv_obj_set_size(leaderboard_win, 400, 350);
    lv_obj_center(leaderboard_win);
    
    static lv_style_t style_win;
    lv_style_init(&style_win);
    lv_style_set_bg_color(&style_win, lv_color_hex(0xbbada0));
    lv_style_set_border_width(&style_win, 2);
    lv_style_set_border_color(&style_win, lv_color_hex(0x776e65));
    lv_obj_add_style(leaderboard_win, &style_win, 0);
    
    lv_obj_t *title = lv_label_create(leaderboard_win);
    lv_label_set_text(title, "排行榜");
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    static lv_style_t style_title;
    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_36);
    lv_style_set_text_font(&style_title, &always);
    lv_style_set_text_color(&style_title, lv_color_hex(0xf9f6f2));
    lv_obj_add_style(title, &style_title, 0);
    
    // 显示排行榜列表
    int i;
    for(i = 0; i < score_count && i < MAX_SCORES; i++) {
        lv_obj_t *rank_label = lv_label_create(leaderboard_win);
        char rank_str[50];
        snprintf(rank_str, sizeof(rank_str), "%d.  分数: %d", i + 1, game2048_scores[i]);
        lv_label_set_text(rank_label, rank_str);
        lv_obj_align(rank_label, LV_ALIGN_TOP_MID, 0, 60 + i * 45);
        
        static lv_style_t style_rank;
        lv_style_init(&style_rank);
        lv_style_set_text_font(&style_rank, &lv_font_montserrat_24);
        lv_style_set_text_font(&style_rank, &always);
        lv_style_set_text_color(&style_rank, lv_color_hex(0xf9f6f2));
        lv_obj_add_style(rank_label, &style_rank, 0);
    }
    
    // 如果没有分数
    if(score_count == 0) {
        lv_obj_t *empty_label = lv_label_create(leaderboard_win);
        lv_label_set_text(empty_label, "No scores yet!");
        lv_obj_align(empty_label, LV_ALIGN_CENTER, 0, 0);
        
        static lv_style_t style_empty;
        lv_style_init(&style_empty);
        lv_style_set_text_font(&style_empty, &lv_font_montserrat_24);
        lv_style_set_text_color(&style_empty, lv_color_hex(0xf9f6f2));
        lv_obj_add_style(empty_label, &style_empty, 0);
    }
    
    // 关闭按钮
    lv_obj_t *close_btn = lv_button_create(leaderboard_win);
    lv_obj_set_size(close_btn, 150, 50);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    static lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_color_hex(0x8f7a66));
    lv_obj_add_style(close_btn, &style_btn, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(close_btn, leaderboard_close_callback, LV_EVENT_SHORT_CLICKED, NULL);
    
    lv_obj_t *btn_label = lv_label_create(close_btn);
    lv_label_set_text(btn_label, "关闭");
    lv_style_set_text_font(&style_btn, &always);

}

// 游戏结束处理 - 创建新窗口
void game_over(void)
{
    // 保存当前分数到排行榜
    save_score(game2048_score);
    
    game_over_win = lv_obj_create(lv_screen_active());
    lv_obj_set_size(game_over_win, 400, 350);
    lv_obj_center(game_over_win);
    
    static lv_style_t style_win;
    lv_style_init(&style_win);
    lv_style_set_bg_color(&style_win, lv_color_hex(0xbbada0));
    lv_style_set_border_width(&style_win, 2);
    lv_style_set_border_color(&style_win, lv_color_hex(0x776e65));
    lv_obj_add_style(game_over_win, &style_win, 0);
    
    lv_obj_t *title = lv_label_create(game_over_win);
    lv_label_set_text(title, "游戏结束");
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    static lv_style_t style_title;
    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_40);
     lv_style_set_text_font(&style_title, &always);
    lv_style_set_text_color(&style_title, lv_color_hex(0xf9f6f2));
    lv_obj_add_style(title, &style_title, 0);
    
    lv_obj_t *score_label = lv_label_create(game_over_win);
    char score_str[30];
    snprintf(score_str, sizeof(score_str), "分数: %d", game2048_score);
    lv_label_set_text(score_label, score_str);
    lv_obj_align(score_label, LV_ALIGN_CENTER, 0, -30);
    static lv_style_t style_score;
    lv_style_init(&style_score);
    lv_style_set_text_font(&style_score, &lv_font_montserrat_24);
    lv_style_set_text_font(&style_score, &always);
    lv_style_set_text_color(&style_score, lv_color_hex(0xf9f6f2));
    lv_obj_add_style(score_label, &style_score, 0);
    
    lv_obj_t *hint = lv_label_create(game_over_win);
    lv_label_set_text(hint, "不能再动了!");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 10);
    static lv_style_t style_hint;
    lv_style_init(&style_hint);
    lv_style_set_text_font(&style_hint, &lv_font_montserrat_20);
    lv_style_set_text_font(&style_hint, &always);
    lv_style_set_text_color(&style_hint, lv_color_hex(0xf9f6f2));
    lv_obj_add_style(hint, &style_hint, 0);
    
    lv_obj_t *restart_btn = lv_button_create(game_over_win);
    lv_obj_set_size(restart_btn, 130, 50);
    lv_obj_align(restart_btn, LV_ALIGN_BOTTOM_LEFT, 30, -30);
    static lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_color_hex(0x8f7a66));
    lv_obj_add_style(restart_btn, &style_btn, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(restart_btn, game_over_restart_callback, LV_EVENT_SHORT_CLICKED, NULL);
    
    lv_obj_t *btn_label = lv_label_create(restart_btn);
    lv_label_set_text(btn_label, "重新开始");
    lv_obj_set_align(btn_label, LV_ALIGN_CENTER);
    lv_style_set_text_font(&style_btn, &always);
    
    // 排行榜按钮
    static lv_style_t style_leaderboard;
    lv_style_init(&style_leaderboard);
    lv_obj_t *leaderboard_btn = lv_button_create(game_over_win);
    lv_obj_set_size(leaderboard_btn, 130, 50);
    lv_obj_align(leaderboard_btn, LV_ALIGN_BOTTOM_RIGHT, -30, -30);
    lv_obj_add_style(leaderboard_btn, &style_btn, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(leaderboard_btn, show_leaderboard, LV_EVENT_SHORT_CLICKED, NULL);
    
    lv_obj_t *leaderboard_label = lv_label_create(leaderboard_btn);
    lv_label_set_text(leaderboard_label, "排行榜");
    lv_style_set_text_font(&style_leaderboard, &always);
}

// 在空白位置添加数字2
void add_random_tile(void)
{
    static int seed_init = 0;
    if(!seed_init) {
        srand((unsigned int)time(NULL));
        seed_init = 1;
    }
    
    int i, j;
    int empty_count = 0;
    int empty_pos[16][2];

    for(i = 0; i < 4; i++) {
        for(j = 0; j < 4; j++) {
            if(game2048_grid[i][j] == 0) {
                empty_pos[empty_count][0] = i;
                empty_pos[empty_count][1] = j;
                empty_count++;
            }
        }
    }
    
    if(empty_count > 0) {
        int idx = rand() % empty_count;
        game2048_grid[empty_pos[idx][0]][empty_pos[idx][1]] = (rand() % 10 == 0) ? 4 : 2;
    }
}

// 2048移动逻辑 - 向左
int move_left(void)
{
    int i, j, k;
    int moved = 0;
    
    for(i = 0; i < 4; i++) {
        int arr[4] = {0};
        int arr_idx = 0;
        
        for(j = 0; j < 4; j++) {
            if(game2048_grid[i][j] != 0) {
                arr[arr_idx++] = game2048_grid[i][j];
            }
        }
        
        for(j = 0; j < 3; j++) {
            if(arr[j] != 0 && arr[j] == arr[j+1]) {
                arr[j] *= 2;
                game2048_score += arr[j];
                arr[j+1] = 0;
                moved = 1;
            }
        }
        
        arr_idx = 0;
        for(j = 0; j < 4; j++) {
            if(arr[j] != 0) {
                arr[arr_idx++] = arr[j];
            }
        }
        for(k = arr_idx; k < 4; k++) {
            arr[k] = 0;
        }
        
        for(j = 0; j < 4; j++) {
            if(game2048_grid[i][j] != arr[j]) {
                moved = 1;
                game2048_grid[i][j] = arr[j];
            }
        }
    }
    
    return moved;
}

// 2048移动逻辑 - 向右
int move_right(void)
{
    int i, j, k;
    int moved = 0;
    
    for(i = 0; i < 4; i++) {
        int arr[4] = {0};
        int arr_idx = 3;
        
        for(j = 3; j >= 0; j--) {
            if(game2048_grid[i][j] != 0) {
                arr[arr_idx--] = game2048_grid[i][j];
            }
        }
        
        for(j = 3; j > 0; j--) {
            if(arr[j] != 0 && arr[j] == arr[j-1]) {
                arr[j] *= 2;
                game2048_score += arr[j];
                arr[j-1] = 0;
                moved = 1;
            }
        }
        
        arr_idx = 3;
        for(j = 3; j >= 0; j--) {
            if(arr[j] != 0) {
                arr[arr_idx--] = arr[j];
            }
        }
        for(k = 0; k <= arr_idx; k++) {
            arr[k] = 0;
        }
        
        for(j = 0; j < 4; j++) {
            if(game2048_grid[i][j] != arr[j]) {
                moved = 1;
                game2048_grid[i][j] = arr[j];
            }
        }
    }
    
    return moved;
}

// 2048移动逻辑 - 向上
int move_up(void)
{
    int i, j, k;
    int moved = 0;
    
    for(j = 0; j < 4; j++) {
        int arr[4] = {0};
        int arr_idx = 0;
        
        for(i = 0; i < 4; i++) {
            if(game2048_grid[i][j] != 0) {
                arr[arr_idx++] = game2048_grid[i][j];
            }
        }
        
        for(i = 0; i < 3; i++) {
            if(arr[i] != 0 && arr[i] == arr[i+1]) {
                arr[i] *= 2;
                game2048_score += arr[i];
                arr[i+1] = 0;
                moved = 1;
            }
        }
        
        arr_idx = 0;
        for(i = 0; i < 4; i++) {
            if(arr[i] != 0) {
                arr[arr_idx++] = arr[i];
            }
        }
        for(k = arr_idx; k < 4; k++) {
            arr[k] = 0;
        }
        
        for(i = 0; i < 4; i++) {
            if(game2048_grid[i][j] != arr[i]) {
                moved = 1;
                game2048_grid[i][j] = arr[i];
            }
        }
    }
    
    return moved;
}

// 2048移动逻辑 - 向下
int move_down(void)
{
    int i, j, k;
    int moved = 0;
    
    for(j = 0; j < 4; j++) {
        int arr[4] = {0};
        int arr_idx = 3;
        
        for(i = 3; i >= 0; i--) {
            if(game2048_grid[i][j] != 0) {
                arr[arr_idx--] = game2048_grid[i][j];
            }
        }
        
        for(i = 3; i > 0; i--) {
            if(arr[i] != 0 && arr[i] == arr[i-1]) {
                arr[i] *= 2;
                game2048_score += arr[i];
                arr[i-1] = 0;
                moved = 1;
            }
        }
        
        arr_idx = 3;
        for(i = 3; i >= 0; i--) {
            if(arr[i] != 0) {
                arr[arr_idx--] = arr[i];
            }
        }
        for(k = 0; k <= arr_idx; k++) {
            arr[k] = 0;
        }
        
        for(i = 0; i < 4; i++) {
            if(game2048_grid[i][j] != arr[i]) {
                moved = 1;
                game2048_grid[i][j] = arr[i];
            }
        }
    }
    
    return moved;
}

// 触摸滑动相关变量
static int touch_start_x = 0;
static int touch_start_y = 0;

// 2048触摸事件处理
void game2048_touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if(code == LV_EVENT_PRESSED) {
        lv_point_t point;
        //获得点击事件的坐标
        lv_indev_get_point(lv_indev_get_act(), &point);
        touch_start_x = point.x;
        touch_start_y = point.y;
    }
    else if(code == LV_EVENT_RELEASED) {
        lv_point_t point;
        //获得松开事件的坐标
        lv_indev_get_point(lv_indev_get_act(), &point);
        int touch_end_x = point.x;
        int touch_end_y = point.y;
        //计算滑动距离
        int dx = touch_end_x - touch_start_x;
        int dy = touch_end_y - touch_start_y;
        int moved = 0;
        //判断是否移动
        if(abs(dx) > abs(dy)) {
            if(abs(dx) > 30) { 
                if(dx > 0) {
                    moved = move_right();
                } else {
                    moved = move_left();
                }
            }
        } else {
            if(abs(dy) > 30) {
                if(dy > 0) {
                    moved = move_down();
                } else {
                    moved = move_up();
                }
            }
        }
        
        if(moved) {
            //添加随机数
            add_random_tile();
            //更新显示
            update_game2048_display();
            
            //检查游戏是否结束
            if(is_game_over()) {
                game_over();
            }
        }
    }
}

// 2048游戏返回按钮回调
void game2048_back_callback(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_SHORT_CLICKED)
    {
        lv_obj_add_flag(ui_B, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui_main, LV_OBJ_FLAG_HIDDEN);
    }
}
/*主界面 按钮B 回调函数*/
void ui_main_buttonB_callback(lv_event_t * e)
{
    printf("ui_main_buttonB_callback\n");
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_SHORT_CLICKED)
    {
        memset(game2048_grid, 0, sizeof(game2048_grid));
        game2048_score = 0;
        add_random_tile();
        add_random_tile();
        update_game2048_display();
        
        lv_obj_add_flag(ui_main, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui_B, LV_OBJ_FLAG_HIDDEN);
    }
}
//界面B设计 2048游戏
void faceB()
{
   ui_B = lv_obj_create(lv_screen_active());
    lv_obj_set_size(ui_B, 1024, 600);
    //设置背景颜色
    static lv_style_t style_bg;
    lv_style_init(&style_bg);
    lv_style_set_bg_color(&style_bg, lv_color_hex(0xbbada0));
    lv_obj_add_style(ui_B, &style_bg, 0);
    //设置返回按钮
    static lv_style_t style_back_btn;
    lv_style_init(&style_back_btn);
    lv_style_set_bg_color(&style_back_btn, lv_color_hex(0xff0000));
    lv_obj_t *btn_back = lv_button_create(ui_B);
    lv_obj_set_size(btn_back, 100, 50);
    lv_obj_set_align(btn_back, LV_ALIGN_TOP_LEFT);
    lv_obj_add_style(btn_back, &style_back_btn, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_back, game2048_back_callback, LV_EVENT_SHORT_CLICKED, NULL);
    //设置返回按钮文本
    static lv_style_t style_label;
    lv_style_init(&style_label);
    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, "返回");
    lv_style_set_text_font(&style_label, &always);
    lv_obj_set_align(label_back, LV_ALIGN_CENTER);
    lv_obj_add_style(label_back, &style_label, 0);

    //设置标题
    static lv_style_t style_title;
    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_40);
    lv_style_set_text_color(&style_title, lv_color_hex(0xf9f6f2));
    lv_obj_t *title = lv_label_create(ui_B);
    lv_label_set_text(title, "2048");
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_obj_add_style(title, &style_title, 0);
    //设置分数标签
    static lv_style_t style_score;
    lv_style_init(&style_score);
    lv_style_set_text_font(&style_score, &lv_font_montserrat_24);
    lv_style_set_text_color(&style_score, lv_color_hex(0xf9f6f2));
    game2048_score_label = lv_label_create(ui_B);
    lv_label_set_text(game2048_score_label, "分数: 0");
    lv_style_set_text_font(&style_score, &always);
    lv_obj_align(game2048_score_label, LV_ALIGN_LEFT_MID,0,60);
    lv_obj_add_style(game2048_score_label, &style_score, 0);
    //设置排行榜按钮（游戏界面独立入口，位于分数下方）
    static lv_style_t style_lb_btn;
    lv_style_init(&style_lb_btn);
    lv_style_set_bg_color(&style_lb_btn, lv_color_hex(0x8f7a66));
    lv_obj_t *btn_leaderboard = lv_button_create(ui_B);
    lv_obj_set_size(btn_leaderboard, 100, 50);
    lv_obj_align(btn_leaderboard, LV_ALIGN_LEFT_MID, 0, 120);
    lv_obj_add_style(btn_leaderboard, &style_lb_btn, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_leaderboard, show_leaderboard, LV_EVENT_SHORT_CLICKED, NULL);
    static lv_style_t style_lb_label;
    lv_style_init(&style_lb_label);
    lv_obj_t *label_leaderboard = lv_label_create(btn_leaderboard);
    lv_label_set_text(label_leaderboard, "排行榜");
    lv_style_set_text_font(&style_lb_label, &always);
    lv_obj_set_align(label_leaderboard, LV_ALIGN_CENTER);
    lv_obj_add_style(label_leaderboard, &style_lb_label, 0);
    //设置网格容器
    lv_obj_t *grid_container = lv_obj_create(ui_B);
    lv_obj_set_size(grid_container, 420, 420);
    lv_obj_set_align(grid_container, LV_ALIGN_CENTER);
    static lv_style_t style_grid;
    lv_style_init(&style_grid);
    lv_style_set_bg_color(&style_grid, lv_color_hex(0xbbada0));
    lv_obj_add_style(grid_container, &style_grid, 0);
    

    //设置网格
       int i, j;
    for(i = 0; i < 4; i++) {
        for(j = 0; j < 4; j++) {
            game2048_tiles[i][j] = lv_obj_create(grid_container);
            lv_obj_set_size(game2048_tiles[i][j], 90, 90);
            lv_obj_set_pos(game2048_tiles[i][j], j * 100-8, i * 100-8);
            //设置网格样式
            static lv_style_t style_tile;
            lv_style_init(&style_tile);
            lv_style_set_bg_color(&style_tile, lv_color_hex(0xcdc1b4));
            lv_obj_add_style(game2048_tiles[i][j], &style_tile, 0);
            //设置网格标签
            lv_obj_t *label = lv_label_create(game2048_tiles[i][j]);
            lv_obj_center(label);
            static lv_style_t style_label;
            lv_style_init(&style_label);
            lv_style_set_text_font(&style_label, &lv_font_montserrat_32);
            lv_style_set_text_align(&style_label, LV_TEXT_ALIGN_CENTER);
            lv_obj_add_style(label, &style_label, 0);
            lv_label_set_text(label, "");
        }
    }
    //设置提示标签
    static lv_style_t style_hint;
    lv_style_init(&style_hint);
    lv_style_set_text_font(&style_hint, &lv_font_montserrat_20);
    lv_style_set_text_color(&style_hint, lv_color_hex(0xf9f6f2));
    lv_obj_t *hint = lv_label_create(ui_B);

    lv_label_set_text(hint, "试试滑动");
    lv_style_set_text_font(&style_hint, &always);
    lv_obj_set_align(hint, LV_ALIGN_BOTTOM_MID);
    lv_obj_add_style(hint, &style_hint, 0);
    //设置触摸事件
    lv_obj_add_event_cb(ui_B, game2048_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ui_B, game2048_touch_cb, LV_EVENT_RELEASED, NULL);
    
    
    lv_obj_add_flag(ui_B, LV_OBJ_FLAG_HIDDEN);
    disable_scroll(ui_B);
    disable_scroll(grid_container);//默认隐藏

    // 启动时从文件加载排行榜记录
    load_scores_from_file();
}

//密码输入框与键盘
void textarea_event_cb(lv_event_t *e)
{
    lv_obj_t *key=lv_event_get_user_data(e);
    lv_event_code_t code=lv_event_get_code(e);
    lv_obj_t *text1=lv_event_get_target(e);
    if(code == LV_EVENT_FOCUSED)
    {
        lv_obj_remove_flag(key,LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(key,text1);
    }
    else if(code == LV_EVENT_DEFOCUSED)
    {
        //失去聚焦 隐藏软键盘，并且取消关联
        lv_obj_add_flag(key,LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(key,NULL);
    }
    else if(code == LV_EVENT_READY)
    {
        // 用户点击键盘"完成"按钮，验证密码
        const char *input = lv_textarea_get_text(text1);
        printf("输入的密码: %s\n", input);
        
        if(strcmp(input, CORRECT_PASSWORD) == 0 && username_verified)
        {
            printf("用户名和密码都正确！切换到主界面\n");
            lv_obj_add_flag(key, LV_OBJ_FLAG_HIDDEN);
            lv_keyboard_set_textarea(key, NULL);
            
            // 隐藏登录界面，显示主界面
            lv_obj_add_flag(ui_login, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ui_main, LV_OBJ_FLAG_HIDDEN);
            username_verified = 0;
        }
        else
        {
            printf("密码错误或用户名未验证！请重新输入\n");
            
            lv_textarea_set_text(text1, "");  // 清空输入框
            username_verified = 0;
        }
    }
}
//用户名输入框与键盘
void textarea_event_cb2(lv_event_t *e)
{
    lv_obj_t *key=lv_event_get_user_data(e);
    lv_event_code_t code2=lv_event_get_code(e);
    lv_obj_t *text2=lv_event_get_target(e);
    if(code2 == LV_EVENT_FOCUSED)
    {
        lv_obj_remove_flag(key,LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(key,text2);
    }
    else if(code2 == LV_EVENT_DEFOCUSED)
    {
        //失去聚焦 隐藏软键盘，并且取消关联
        lv_obj_add_flag(key,LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(key,NULL);
    }
    else if(code2 == LV_EVENT_READY)
    {
        // 用户点击键盘"完成"按钮，验证用户名
        const char *input = lv_textarea_get_text(text2);
        printf("输入的用户名: %s\n", input);
        
        if(strcmp(input, CORRECT_USERNAME) == 0)
        {
            printf("用户名正确，请输入密码\n");
            username_verified = 1;
            lv_obj_add_flag(key, LV_OBJ_FLAG_HIDDEN);
            lv_keyboard_set_textarea(key, NULL);
        }
        else
        {
            printf("用户名错误！请重新输入\n");
            username_verified = 0;
            lv_textarea_set_text(text2, "");  // 清空输入框
        }
    }
}
// 创建电子相册界面

    

//登录界面
void login_interface()
{
    //登录的界面
    ui_login = lv_obj_create(lv_screen_active());
    lv_obj_set_size(ui_login,1024,600);
    lv_obj_t *imageLogin = lv_image_create(ui_login);
    lv_image_set_src(imageLogin,MAIN_UI_BUTTON_D);
    lv_obj_set_align(imageLogin,LV_ALIGN_CENTER);
    //键盘与输入框的
    lv_obj_t * key = lv_keyboard_create(ui_login);
    lv_obj_t *text1=lv_textarea_create(ui_login);
    lv_obj_t *text2=lv_textarea_create(ui_login);
    //密码框的大小与位置
    
    lv_obj_set_size(text1,200,100);
    lv_obj_set_pos(text1,20,100);
//密码
    static lv_style_t style_hint;
    lv_style_init(&style_hint);
    lv_obj_t *lab_password=lv_label_create(ui_login);
    lv_label_set_text(lab_password,"password");
    lv_style_set_text_font(&style_hint, &always);
    lv_obj_set_pos(lab_password,250,100);
    lv_obj_add_style(lab_password,&style_hint,0);
    
    //用户名
    static lv_style_t style_name;
    lv_style_init(&style_name);
    lv_obj_t *lab_username=lv_label_create(ui_login);
    lv_label_set_text(lab_username,"用户名");
    lv_style_set_text_font(&style_name, &always);
    lv_obj_set_pos(lab_username,250,0);
    lv_obj_add_style(lab_username,&style_name,0);

//账号框的大小与位置
    lv_obj_set_size(text2,200,100);
    lv_obj_set_pos(text2,20,0);
    //账号框的属性（用户名不遮罩）
    lv_textarea_set_one_line(text2,true);
    //密码框的属性
    lv_textarea_set_one_line(text1,true);
    lv_textarea_set_password_mode(text1, true);
    lv_keyboard_set_textarea(key,text1);
    lv_keyboard_set_textarea(key,text2);
    lv_obj_add_flag(key,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(text1,textarea_event_cb,LV_EVENT_ALL,key);
    lv_obj_add_event_cb(text2,textarea_event_cb2,LV_EVENT_ALL,key);

}
//界面C设计
void faceC()
{
    ui_C = lv_obj_create(lv_screen_active());
    lv_obj_set_size(ui_C,1024,600);
    //设置一个退出按钮
    lv_obj_t *buttonEC=lv_button_create(ui_C);
    lv_obj_set_size(buttonEC,80,40);
    lv_obj_set_align(buttonEC,LV_ALIGN_TOP_RIGHT);
    lv_obj_add_event_cb(buttonEC,ui_main_buttonE_callback,LV_EVENT_SHORT_CLICKED,NULL);
    lv_obj_t *lab_ec=lv_label_create(buttonEC);
    static lv_style_t ec;
     lv_style_init(&ec);
     lv_style_set_text_font(&ec, &lv_font_montserrat_22);
     lv_style_set_text_font(&ec, &always);
    lv_label_set_text(lab_ec,"退出");
    lv_obj_set_align(lab_ec,LV_ALIGN_CENTER);
    lv_obj_add_style(lab_ec,&ec,0);
    
    //设置一个停止按钮
    lv_obj_t *buttonstop=lv_button_create(ui_C);
    lv_obj_set_size(buttonstop,80,40);
    lv_obj_set_align(buttonstop,LV_ALIGN_TOP_LEFT);
    lv_obj_add_event_cb(buttonstop,ui_main_buttonstop_callback,LV_EVENT_SHORT_CLICKED,NULL);
    lv_obj_t *lab_stop=lv_label_create(buttonstop);
    static lv_style_t stop;
     lv_style_init(&stop);
     lv_style_set_text_font(&stop, &lv_font_montserrat_22);
     lv_style_set_text_font(&stop, &always);
    lv_label_set_text(lab_stop,"暂停");
    lv_obj_set_align(lab_stop,LV_ALIGN_CENTER);
    lv_obj_add_style(lab_stop,&stop,0);
    

    //设置一个下一首按钮
    lv_obj_t *buttonnext=lv_button_create(ui_C);
    lv_obj_set_size(buttonnext,80,40);
    lv_obj_set_align(buttonnext,LV_ALIGN_BOTTOM_RIGHT);
    lv_obj_add_event_cb(buttonnext,ui_main_buttonnext_callback,LV_EVENT_SHORT_CLICKED,NULL);
    lv_obj_t *lab_next=lv_label_create(buttonnext);
    static lv_style_t next;
     lv_style_init(&next);
     lv_style_set_text_font(&next, &lv_font_montserrat_22);
     lv_style_set_text_font(&next, &always);
    lv_label_set_text(lab_next,"下一个");
    lv_obj_set_align(lab_next,LV_ALIGN_CENTER);
    lv_obj_add_style(lab_next,&next,0);

    //设置一个上一首按钮
    lv_obj_t *buttonprev=lv_button_create(ui_C);
    lv_obj_set_size(buttonprev,80,40);
    lv_obj_set_align(buttonprev,LV_ALIGN_BOTTOM_LEFT);
    lv_obj_add_event_cb(buttonprev,ui_main_buttonprev_callback,LV_EVENT_SHORT_CLICKED,NULL);
    lv_obj_t *lab_prev=lv_label_create(buttonprev);
    static lv_style_t prev;
     lv_style_init(&prev);
     lv_style_set_text_font(&prev, &lv_font_montserrat_22);
     lv_style_set_text_font(&prev, &always);
    lv_label_set_text(lab_prev,"上一个");
    lv_obj_set_align(lab_prev,LV_ALIGN_CENTER);
    lv_obj_add_style(lab_prev,&prev,0);
    //设置一个快进按钮
    lv_obj_t *buttonfast=lv_button_create(ui_C);
    lv_obj_set_size(buttonfast,80,40);
    lv_obj_set_align(buttonfast,LV_ALIGN_BOTTOM_MID);
    lv_obj_add_event_cb(buttonfast,ui_main_buttonfast_callback,LV_EVENT_SHORT_CLICKED,NULL);
    lv_obj_t *lab_fast=lv_label_create(buttonfast);
    static lv_style_t fast;
     lv_style_init(&fast);
     lv_style_set_text_font(&fast, &lv_font_montserrat_22);
     lv_style_set_text_font(&fast, &always);
    lv_label_set_text(lab_fast,"快进");
    lv_obj_set_align(lab_fast,LV_ALIGN_CENTER);
    lv_obj_add_style(lab_fast,&fast,0);

    disable_scroll(ui_C);
    lv_obj_add_flag(ui_C,LV_OBJ_FLAG_HIDDEN);
}
////////--------D界面聊天室 代码部分-----------

/* ---- 聊天显示追加：限制总长度，防止无限增长耗尽内存 ---- */
#define CHAT_MAX_CHARS 20000

static void chat_append_limited(lv_obj_t *ta, const char *msg)
{
    const char *cur = lv_textarea_get_text(ta);
    size_t cur_len = (cur != NULL) ? strlen(cur) : 0;
    size_t msg_len = strlen(msg);

    if (cur_len + msg_len > CHAT_MAX_CHARS) {
        /* 丢弃开头旧内容，保留末尾完整行 */
        size_t keep = CHAT_MAX_CHARS - msg_len;
        size_t start = (cur_len > keep) ? cur_len - keep : 0;
        while (start < cur_len && cur[start] != '\n') start++;
        if (start < cur_len) start++;  /* 跳过换行符，从下一行开始 */
        keep = cur_len - start;
        if (keep > 0) {
            static char tmp[CHAT_MAX_CHARS];
            memcpy(tmp, cur + start, keep);
            tmp[keep] = '\0';
            lv_textarea_set_text(ta, tmp);
        }
    }
    lv_textarea_add_text(ta, msg);
}

/* ---- 聊天定时器：每 200ms 轮询一次消息队列 ---- */
static void chat_timer_callback(lv_timer_t *timer)
{
    char msg[512] = "";
    int has_new = 0;

    while (1) {
        int ret = talkroom_client_recv(msg, sizeof(msg));
        if (ret == 1) {
            chat_append_limited(chat_display, msg);
            has_new = 1;
        } else if (ret == -1) {
            chat_append_limited(chat_display, msg);
            has_new = 1;
            break;
        } else {
            break;
        }
    }

    if (has_new) {
        /* 自动滚动到底部 */
        lv_obj_scroll_to_y(chat_display, 100000, LV_ANIM_OFF);
    }

    /* 检查是否断开了 */
    if (!talkroom_client_connected() && chat_timer != NULL) {
        lv_label_set_text(chat_status_label, "状态: 未连接");
        lv_label_set_text(chat_connect_label, "连接");
        lv_timer_del(chat_timer);
        chat_timer = NULL;
    }
}

/* ---- 连接/断开按钮回调 ---- */
static void chat_connect_btn_callback(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_SHORT_CLICKED) return;

    if (talkroom_client_connected()) {
        talkroom_client_disconnect();
        lv_label_set_text(chat_status_label, "状态: 未连接");
        lv_label_set_text(chat_connect_label, "连接");
        if (chat_timer) {
            lv_timer_del(chat_timer);
            chat_timer = NULL;
        }
        chat_append_limited(chat_display, "[系统] 已断开连接\n");
        printf("聊天室已断开\n");
    } else {
        if (talkroom_client_connect(CHAT_SERVER_IP, 8888, chat_nickname) == 0) {
            lv_label_set_text(chat_status_label, "状态: 已连接");
            lv_label_set_text(chat_connect_label, "断开");
            chat_timer = lv_timer_create(chat_timer_callback, 200, NULL);
            chat_append_limited(chat_display, "[系统] 已连接到聊天室\n");
            printf("聊天室已连接\n");
        } else {
            chat_append_limited(chat_display, "[系统] 连接失败，请检查服务器\n");
            printf("聊天室连接失败\n");
        }
    }
}

/* ---- 发送按钮回调 ---- */
static void chat_send_btn_callback(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_SHORT_CLICKED) return;

    const char *text = lv_textarea_get_text(chat_input);
    if (text == NULL || strlen(text) == 0) return;

    if (!talkroom_client_connected()) {
        chat_append_limited(chat_display, "[系统] 未连接，无法发送消息\n");
        lv_textarea_set_text(chat_input, "");
        return;
    }

    /* 通过 API 发送 */
    char send_buf[512];
    snprintf(send_buf, sizeof(send_buf), "[%s] %s", chat_nickname, text);
    if (talkroom_client_send(send_buf) != 0) {
        chat_append_limited(chat_display, "[系统] 消息发送失败\n");
        lv_textarea_set_text(chat_input, "");
        return;
    }

    /* 本地回显（服务端不会回发给发送者） */
    char time_buf[16];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", t);

    char echo[512];
    snprintf(echo, sizeof(echo), "[%s] [我] %s\n", time_buf, text);
    chat_append_limited(chat_display, echo);

    /* 自动滚动到底部 */
    lv_obj_scroll_to_y(chat_display, 100000, LV_ANIM_OFF);

    /* 清空输入框 */
    lv_textarea_set_text(chat_input, "");
}

/* ---- 输入框键盘事件回调 ---- */
static void chat_input_event_cb(lv_event_t *e)
{
    lv_obj_t *key = lv_event_get_user_data(e);
    lv_obj_t *input = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_obj_remove_flag(key, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(key, input);       /* 修复: 使用 input 而非 text2 */
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(key, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(key, NULL);
    } else if (code == LV_EVENT_READY) {
        /* 键盘"完成" → 发送消息 */
        const char *text = lv_textarea_get_text(input);
        if (text != NULL && strlen(text) > 0) {
            if (talkroom_client_connected()) {
                char send_buf[512];
                snprintf(send_buf, sizeof(send_buf), "[%s] %s", chat_nickname, text);
                if (talkroom_client_send(send_buf) != 0) {
                    chat_append_limited(chat_display, "[系统] 消息发送失败\n");
                } else {
                    /* 本地回显 */
                    char time_buf[16];
                    time_t now = time(NULL);
                    struct tm *t = localtime(&now);
                    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", t);

                    char echo[512];
                    snprintf(echo, sizeof(echo), "[%s] [我] %s\n", time_buf, text);
                    chat_append_limited(chat_display, echo);
                    lv_obj_scroll_to_y(chat_display, 100000, LV_ANIM_OFF);
                }
            } else {
                chat_append_limited(chat_display, "[系统] 未连接，无法发送消息\n");
            }
        }
        lv_textarea_set_text(input, "");
        lv_obj_add_flag(key, LV_OBJ_FLAG_HIDDEN);
    }
}

//按键D的回调函数
void ui_main_buttonD_callback(lv_event_t * e)
{
    printf("ui_main_buttonD_callback\n");
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_SHORT_CLICKED) //如果点击按键D
    {
        printf("click D \n");
//切换界面D
    lv_obj_add_flag(ui_main, LV_OBJ_FLAG_HIDDEN); //添加隐藏属性
    lv_obj_remove_flag(ui_D, LV_OBJ_FLAG_HIDDEN);
    }
}
//界面D设计
void faceD()
{
    ui_D = lv_obj_create(lv_screen_active());
    lv_obj_set_size(ui_D, 1024, 600);

    /* ---- 背景 ---- */
    static lv_style_t style_bg;
    lv_style_init(&style_bg);
    lv_style_set_bg_color(&style_bg, lv_color_hex(0x1a1a2e));
    lv_obj_add_style(ui_D, &style_bg, 0);

    /* ======== 顶部状态栏 ======== */
    /* 退出按钮（右上角） */
    lv_obj_t *buttonExit = lv_button_create(ui_D);
    lv_obj_set_size(buttonExit, 80, 40);
    lv_obj_set_align(buttonExit, LV_ALIGN_TOP_RIGHT);
    lv_obj_add_event_cb(buttonExit, ui_main_buttonE_callback, LV_EVENT_SHORT_CLICKED, NULL);

    lv_obj_t *lab_exit = lv_label_create(buttonExit);
    static lv_style_t style_exit;
    lv_style_init(&style_exit);
    lv_style_set_text_font(&style_exit, &lv_font_montserrat_22);
    lv_style_set_text_font(&style_exit, &always);
    lv_label_set_text(lab_exit, "返回");
    lv_obj_set_align(lab_exit, LV_ALIGN_CENTER);
    lv_obj_add_style(lab_exit, &style_exit, 0);

    /* 状态标签（左上角） */
    chat_status_label = lv_label_create(ui_D);
    lv_label_set_text(chat_status_label, "状态: 未连接");
    static lv_style_t style_status;
    lv_style_init(&style_status);
    lv_style_set_text_font(&style_status, &lv_font_montserrat_22);
    lv_style_set_text_font(&style_status, &always);
    lv_style_set_text_color(&style_status, lv_color_hex(0x00ff00));
    lv_obj_add_style(chat_status_label, &style_status, 0);
    lv_obj_set_pos(chat_status_label, 10, 5);

    /* 连接/断开按钮（状态标签右边） */
    chat_connect_btn = lv_button_create(ui_D);
    lv_obj_set_size(chat_connect_btn, 80, 40);
    lv_obj_set_pos(chat_connect_btn, 270, 5);
    lv_obj_add_event_cb(chat_connect_btn, chat_connect_btn_callback, LV_EVENT_SHORT_CLICKED, NULL);

    chat_connect_label = lv_label_create(chat_connect_btn);
    lv_label_set_text(chat_connect_label, "连接");
    static lv_style_t style_conn;
    lv_style_init(&style_conn);
    lv_style_set_text_font(&style_conn, &lv_font_montserrat_22);
    lv_style_set_text_font(&style_conn, &always);
    lv_obj_add_style(chat_connect_label, &style_conn, 0);
    lv_obj_set_align(chat_connect_label, LV_ALIGN_CENTER);

    /* ======== 聊天消息显示区域 ======== */
    chat_display = lv_textarea_create(ui_D);
    lv_obj_set_size(chat_display, 1000, 420);
    lv_obj_set_pos(chat_display, 12, 55);

    /* 设置显示样式 */
    static lv_style_t style_display;
    lv_style_init(&style_display);
    lv_style_set_bg_color(&style_display, lv_color_hex(0x16213e));
    lv_style_set_text_color(&style_display, lv_color_hex(0xe0e0e0));
    lv_style_set_text_font(&style_display, &lv_font_montserrat_20);
    lv_style_set_text_font(&style_display, &always);
    lv_style_set_border_width(&style_display, 1);
    lv_style_set_border_color(&style_display, lv_color_hex(0x0f3460));
    lv_obj_add_style(chat_display, &style_display, 0);

    /* 超出内容可滚动 */
    lv_obj_add_flag(chat_display, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(chat_display, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(chat_display, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    /* 初始提示 */
    chat_append_limited(chat_display, "[系统] 欢迎使用聊天室！请先点击[连接]加入\n");

    /* ======== 底部输入栏 ======== */
    /* 键盘 */
    lv_obj_t *key = lv_keyboard_create(ui_D);
    lv_obj_add_flag(key, LV_OBJ_FLAG_HIDDEN);

    /* 输入框 */
    chat_input = lv_textarea_create(ui_D);
    lv_obj_set_size(chat_input, 830, 45);
    lv_obj_set_pos(chat_input, 12, 490);
    lv_textarea_set_one_line(chat_input, true);
    lv_textarea_set_placeholder_text(chat_input, "输入消息...");

    static lv_style_t style_input;
    lv_style_init(&style_input);
    lv_style_set_text_font(&style_input, &lv_font_montserrat_22);
    lv_style_set_text_font(&style_input, &always);
    lv_obj_add_style(chat_input, &style_input, 0);

    lv_obj_add_event_cb(chat_input, chat_input_event_cb, LV_EVENT_ALL, key);

    /* 发送按钮 */
    chat_send_btn = lv_button_create(ui_D);
    lv_obj_set_size(chat_send_btn, 80, 45);
    lv_obj_set_pos(chat_send_btn, 855, 490);
    lv_obj_add_event_cb(chat_send_btn, chat_send_btn_callback, LV_EVENT_SHORT_CLICKED, NULL);

    lv_obj_t *lab_send = lv_label_create(chat_send_btn);
    lv_label_set_text(lab_send, "发送");
    static lv_style_t style_send;
    lv_style_init(&style_send);
    lv_style_set_text_font(&style_send, &lv_font_montserrat_22);
    lv_style_set_text_font(&style_send, &always);
    lv_obj_add_style(lab_send, &style_send, 0);
    lv_obj_set_align(lab_send, LV_ALIGN_CENTER);

    /* 默认隐藏 D 界面 */
    lv_obj_add_flag(ui_D, LV_OBJ_FLAG_HIDDEN);
}
//主界面设计
void main_interface(void)
    {
//创建一个ui_main;
    ui_main = lv_obj_create(lv_screen_active());
    lv_obj_set_size(ui_main,1024,600);
    lv_obj_t *imageM = lv_image_create(ui_main);
    lv_image_set_src(imageM,MAIN_UI_BUTTON_C);
    lv_obj_set_align(imageM,LV_ALIGN_CENTER);

//在活动屏幕上创建两个按钮，作为切换界面A，B的按键
    lv_obj_t *buttonA = lv_button_create(ui_main);
//位置，大小
    lv_obj_set_size(buttonA,100,100);
    lv_obj_set_align(buttonA,LV_ALIGN_LEFT_MID);
//按键添加样式
    static lv_style_t style_A;
    lv_style_init(&style_A);
//设置样式
    lv_style_set_bg_color(&style_A,lv_color_hex(0xff0000));
    lv_obj_add_style(buttonA,&style_A,LV_STATE_DEFAULT);
    
//在按键A上创建一个图片控件，并且指定要显示的按键A的图片
    lv_obj_t *imageA = lv_image_create(buttonA);
    lv_image_set_src(imageA,MAIN_UI_BUTTON_L);
    lv_obj_set_align(imageA,LV_ALIGN_CENTER);
//图片添加样式，根据具体效果来
//按键B
    lv_obj_t *buttonB = lv_button_create(ui_main);
    lv_obj_set_size(buttonB,100,100);
    lv_obj_set_align(buttonB,LV_ALIGN_RIGHT_MID);
    static lv_style_t style_B;
    lv_style_init(&style_B);
    lv_style_set_bg_color(&style_B,lv_color_hex(0xff0000));
    lv_obj_add_style(buttonB,&style_B,LV_STATE_DEFAULT);
    lv_obj_t *imageB = lv_image_create(buttonB);
    lv_image_set_src(imageB,MAIN_UI_BUTTON_R);
    lv_obj_set_align(imageB,LV_ALIGN_CENTER);

//按键C
    lv_obj_t *buttonC = lv_button_create(ui_main);
    lv_obj_set_size(buttonC,100,100);
    lv_obj_set_align(buttonC,LV_ALIGN_CENTER);
    static lv_style_t style_C;
    lv_style_init(&style_C);
    lv_style_set_bg_color(&style_C,lv_color_hex(0xff0000));
    lv_obj_add_style(buttonC,&style_C,LV_STATE_DEFAULT);
    lv_obj_t *imageC = lv_image_create(buttonC);
    lv_image_set_src(imageC,MAIN_UI_BUTTON_CC);
    lv_obj_set_align(imageC,LV_ALIGN_CENTER);

//按键D
    lv_obj_t *buttonD = lv_button_create(ui_main);
    lv_obj_set_size(buttonD,100,100);
    lv_obj_set_align(buttonD,LV_ALIGN_TOP_MID);
    static lv_style_t style_D;
    lv_style_init(&style_D);
    lv_style_set_bg_color(&style_D,lv_color_hex(0xff0000));
    lv_obj_add_style(buttonD,&style_D,LV_STATE_DEFAULT);
    lv_obj_t *imageD = lv_image_create(buttonD);
    lv_image_set_src(imageD,MAIN_UI_BUTTON_DD);
    lv_obj_set_align(imageD,LV_ALIGN_CENTER);

//设置主界面以及界面A/B的属性
    
    
    disable_scroll(ui_main);
//主界面显示
lv_obj_remove_flag(ui_main, LV_OBJ_FLAG_HIDDEN); 

    
//添加A，B的回调函数、
lv_obj_add_event_cb(buttonA,ui_main_buttonA_callback,LV_EVENT_SHORT_CLICKED,NULL);
lv_obj_add_event_cb(buttonB,ui_main_buttonB_callback,LV_EVENT_SHORT_CLICKED,NULL);
lv_obj_add_event_cb(buttonC,ui_main_buttonC_callback,LV_EVENT_SHORT_CLICKED,NULL);
lv_obj_add_event_cb(buttonD,ui_main_buttonD_callback,LV_EVENT_SHORT_CLICKED,NULL);
}



void test09(){
    //buzzer_init();  // 初始化蜂鸣器
    // 忽略 SIGPIPE：mplayer 已退出时写 FIFO、聊天 socket 断开时发送，
    // 否则默认动作会杀死整个 GUI 进程
    signal(SIGPIPE, SIG_IGN);
    main_interface();
    faceA();//相册
    faceB(); //游戏
    faceC();//视频
    faceD();//聊天
    login_interface();//登录

    
    // 初始显示登录界面，隐藏其他界面
    lv_obj_add_flag(ui_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_A, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_B, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_C, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_D, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui_login, LV_OBJ_FLAG_HIDDEN);
}