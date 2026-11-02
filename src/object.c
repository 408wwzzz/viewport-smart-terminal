#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include"object.h"

void object(void){
    //创建一个活动屏幕
    lv_obj_t*src=lv_screen_active();
    
    //在活动屏幕上创建一个子对象
    lv_obj_t *win=lv_obj_create(src);
    
    lv_obj_t * image=lv_image_create(src);
    lv_image_set_src(image,"A:/WZZ/11.bmp");
    //设置object这个对象的宽为w，高为h
     lv_obj_set_size(win,600,400);
     //指定窗口的位置
     lv_obj_set_align(win,LV_ALIGN_CENTER);

    // //初始化样式
    // static lv_style_t style;
    // lv_style_init(&style);
    //图片的显示

    
    
    // //设置背景颜色
    // lv_style_set_bg_color(&style,lv_color_hex(0xEE82EE));
    // //设置背景透明度
    // lv_style_set_bg_opa(&style,LV_OPA_70);
    // //设置边框的颜色
    // lv_style_set_border_color(&style,lv_color_hex(0x8b6914));
    // //设置边框的透明度
    // lv_style_set_border_opa(&style,LV_OPA_70);
    // //设置边框的弧度
    // lv_style_set_radius(&style,40);
    // //设置边框的范围
    // lv_style_set_border_side(&style,LV_BORDER_SIDE_FULL);
    // //设置边框的宽度
    // lv_style_set_border_width(&style,30);
    
    // lv_obj_add_style(win,&style,LV_PART_MAIN);
    


}