#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
// #include"object.h"
#include"lv_mygec_font .h"
#include"face_change.h"
#include<stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
//#include "chaoshengbo.h"

// void test03(){
//     //设置样式
//     static lv_style_t style;
//     lv_style_init(&style);
    
//     //在窗口对象中创建标签，并且显示红色的字
//     lv_obj_t*src=lv_screen_active();
//     lv_obj_t *obj3=lv_obj_create(src);
//     lv_obj_set_size(obj3,400,200);
//     lv_obj_set_pos(obj3,600,50);

//     lv_obj_t *obj2=lv_obj_create(src);
//     lv_obj_set_size(obj2,400,400);
//     lv_obj_set_pos(obj2,0,100);

//     lv_obj_t *gif=lv_gif_create(obj2);
//     lv_gif_set_src(gif, "A:/WZZ/3.gif");
//     lv_obj_set_align(gif,LV_ALIGN_CENTER);

//     // lv_obj_t *button1=lv_button_create(obj3);
//     // lv_obj_set_size(button1,100,50);
//     // lv_obj_set_pos(button1,600,50);
//     // lv_obj_t *lab1 =lv_label_create(button1);
//     // lv_label_set_text(lab1,"button1");

//     lv_obj_t *lab =lv_label_create(obj3);
//     lv_label_set_text(lab,"王振卓");
//     //v_label_set_text_fmt(lab,"王振卓%d",8);
//      lv_obj_set_align(lab,LV_ALIGN_CENTER);
//     lv_style_set_text_font(&style, &lv_font_montserrat_40);
//     lv_style_set_text_color(&style,lv_color_hex(0xEE82EE));
//     lv_obj_add_style(obj3,&style,0);
//     lv_style_set_text_font(&style, &lv_mygec_font );
// }
// void test04(){
//     lv_obj_t *button1=lv_button_create(lv_screen_active());
//     lv_obj_set_size(button1,300,200);
//     lv_obj_set_pos(button1,400,400);
//     lv_obj_t *lab1 =lv_label_create(button1);
//     lv_label_set_text(lab1,"button1");
//     lv_obj_set_align(lab1,LV_ALIGN_CENTER);
// }

// void nine(){
//     int i=0;
//     uint32_t a[9]={0xff0000,0xff7f00,0xffff00,0x7cfc00,0x7fffd4,0x63b8ff,0x551a8b,0x030303,0x8b7b8b};
//     for(i=0;i<9;i++){
//         lv_obj_t *obj=lv_obj_create(lv_screen_active());
//         lv_obj_set_size(obj,200,100);
//         lv_obj_set_align(obj,i+1);
//         lv_obj_set_style_bg_color(obj,lv_color_hex(a[i]),0);
//     }
// }
// void game_table()
// {
//      lv_obj_t*src=lv_screen_active();
//      lv_obj_t *bmp= lv_image_create(src);
//      lv_image_set_src(bmp,"A:/WZZ/3.bmp");
//     //初始化类型
//     static lv_style_t style;
//     lv_style_init(&style);
// }
// static void mytest_callback(lv_event_t *e)
// {
//     LV_LOG_USER("===>callback");
//     //获取用户数据
//     lv_obj_t *lab =lv_event_get_user_data(e);
//     //获取事件的编码
//     lv_event_code_t code=lv_event_get_code(e);
//     switch(code){
//         case LV_EVENT_PRESSED:
//         lv_label_set_text(lab,"MY Button PRESSED");
//         break;
//         case LV_EVENT_PRESSING:
//         lv_label_set_text(lab,"My Botton PRESSING");
//         LV_LOG_USER("===> PRESSING");
//         break;
//         case LV_EVENT_SHORT_CLICKED:
//         lv_label_set_text(lab,"MY Button one");
//         break;
//     }
// }

//     void gesture_event_cb(lv_event_t * e)
//     {
//     lv_indev_t * indev = lv_indev_get_act();
//     lv_dir_t dir = lv_indev_get_gesture_dir(indev);
//     int move_dir = -1;
//     switch(dir)
//         {
//         case LV_DIR_TOP: //上滑
//         move_dir = 1; printf("up\n");
//         break;
//         case LV_DIR_BOTTOM : //下滑
//         move_dir = 2; printf("down\n");
//         break;
//         case LV_DIR_LEFT : //左滑
//         move_dir = 3; printf("left\n");
//         break;
//         case LV_DIR_RIGHT : //右滑
//         move_dir = 4; printf("right\n");
//         break;
//         }
//     }

//     //获取坐标
// int get_direction()
// {
//     int dx = start_point.x - end_point.x;
//     int dy = start_point.y - end_point.y;
//     int abs_dx = abs(dx); //绝对值
//     int abs_dy = abs(dy);
//     if(abs_dx < abs_dy) // 上/下
//     {
//         return dy > 0 ? 1 : 2;
//     }
//     else
//     {
//         return dx > 0 ? 3 : 4;
//     }
// }

// static void ABS_enent_cb(lv_event_t * e)
// {
// //获取事件的编码
//     lv_event_code_t code = lv_event_get_code(e);
//     lv_point_t current_point;
//     lv_point_t end1_point;
//     //获取输入的设备
//     lv_indev_t * indev = lv_indev_get_act();
//     //获取点击坐标
//     lv_indev_get_point(indev, &current_point);
//     switch(code)
//         {
//         case LV_EVENT_PRESSED://第一次按下
//         start_point = current_point;
//         //松开
//         case LV_EVENT_RELEASED://松开
//         //再去获取一次坐标
//         lv_indev_get_point(indev, &end1_point);
//         end_point = end1_point;//
//         int dir = get_direction();
//         switch(dir)
//         {
//         case 1: printf("up\n");break;
//         case 2:printf("down\n");break;
//         case 3:printf("left\n");break;
//         case 4:printf("right\n");break;
//         }
//     }
// }
// void text(){
    
//     lv_obj_t * button1 = lv_button_create(lv_screen_active());
//     lv_obj_set_size(button1,300,200);
//     lv_obj_set_pos(button1,400,300);
//     lv_obj_set_align(button1,LV_ALIGN_CENTER);
//     lv_obj_t * lab = lv_label_create(button1);
//     lv_label_set_text(lab, "button1");
    
//     lv_obj_add_event_cb(button1, mytest_callback, LV_EVENT_ALL,lab);
// }

//输入框与键盘
// void textarea_event_cb(lv_event_t *e)
// {
//     lv_obj_t *key=lv_event_get_user_data(e);
//     lv_event_code_t code=lv_event_get_code(e);
//     lv_obj_t *text=lv_event_get_target(e);
//     if(code == LV_EVENT_FOCUSED)
//     {
//         lv_obj_remove_flag(key,LV_OBJ_FLAG_HIDDEN);
//         lv_keyboard_set_textarea(key,text);
//     }
//     else if(code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY)
//     {
//         //失去聚焦 隐藏软键盘，并且取消关联
//         lv_obj_add_flag(key,LV_OBJ_FLAG_HIDDEN);
//         lv_keyboard_set_textarea(key,NULL);
//     }
// }

// void test05(){
//     lv_obj_t * key = lv_keyboard_create(lv_screen_active());
//     lv_obj_t *text=lv_textarea_create(lv_screen_active());
//     lv_obj_set_size(text,200,100);
//     lv_obj_set_align(text,LV_ALIGN_TOP_MID);
//     lv_textarea_set_one_line(text,true);
//     lv_textarea_set_password_mode(text, true);
//     lv_keyboard_set_textarea(key,text);
//     lv_obj_add_flag(key,LV_OBJ_FLAG_HIDDEN);
//     lv_obj_add_event_cb(text,textarea_event_cb,LV_EVENT_ALL,key);
// }

// void timer_cb(lv_timer_t*timer)
// {
//     printf("你瞅啥\n");
// }
// void demo_time()
// {
//     //1.创建一个定时器，每隔1000ms就会自动调用timer_cb
//     lv_timer_t *timer=lv_timer_create(timer_cb,1000,NULL);
//     //2.设置重复的次数，-1表示一直重复
//     lv_timer_set_repeat_count(timer,-1);
//     //3.启动定时器
//     lv_timer_enable(true);
// }

int main(void)
{
    lv_init();
    

    /*Linux frame buffer device init*/
    lv_display_t * disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, "/dev/fb0");

    /*Create a Demo*/
    lv_indev_t * indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event6");
    // lv_demo_widgets();
    // lv_demo_widgets_start_slideshow();
    //object();
    // nine();
    test09();
   
    /*Handle LVGL tasks*/
    while(1) {
        lv_timer_handler();
        usleep(5000);
    }

    return 0;
}