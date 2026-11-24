

#include "system/includes.h"
#include "typedef.h"
#include "asm/pap.h"
#include "lcd_drive.h"
#include "lcd_config.h"

/*  ST7789V驱动说明 该驱动测试时使用的 wl80 79系列
 *  由于该IC推屏能力不够强 推屏的帧数较低 大概在25帧左右
 *  在推屏过程中需要使用TE屏幕帧中断 不然会有条纹
 *  由于ST7789V横屏配置无法调出没有条纹的配置
 *  所有只能使用竖屏加RGB旋转来实现UI横屏显示
 */

/* //pap的这个三个配置如下 在板级文件中进行修改
    .timing_setup 	= 0,					//具体看pap.h
    .timing_hold  	= 0,					//具体看pap.h
    .timing_width 	= 1,					//具体看pap.h
*/

#if TCFG_LCD_JD9853_ENABLE


#define READ_ID 	0x04
#define REGFLAG_DELAY 0x45

void JD9853_SetRange(u16 xs, u16 xe, u16 ys, u16 ye)
{
    /******UI每次发送数据都会调用开窗告诉屏幕要刷新那个区域***********/
    set_lcd_ui_x_y(xs, xe, ys, ye);
}
void JD9853_SetRange_1(u16 xs, u16 xe, u16 ys, u16 ye)
{
    WriteCOM(0x2A);
    WriteDAT_8(ys >> 8);
    WriteDAT_8(ys);
    WriteDAT_8(ye >> 8);
    WriteDAT_8(ye);
    WriteCOM(0x2B);
    WriteDAT_8(xs >> 8);
    WriteDAT_8(xs);
    WriteDAT_8(xe >> 8);
    WriteDAT_8(xe);

}

void JD9853_clear_screen(u32 color)
{
    lcd_interface_non_block_wait();
    WriteCOM(0x2c);

    u8 *buf = malloc(LCD_W * LCD_H * 2);
    if (!buf) {
        printf("no men in %s \n", __func__);
        return;
    }
    for (u32 i = 0; i < LCD_W * LCD_H; i++) {
        buf[2 * i] = (color >> 8) & 0xff;
        buf[2 * i + 1] = color & 0xff;
    }
    WriteDAT_DMA(buf, LCD_W * LCD_H * 2);
    free(buf);
}

void JD9853_Fill(u8 *img, u16 len)
{
    lcd_interface_non_block_wait();
    WriteCOM(0x2c);
    WriteDAT_DMA(img, len);
}

void JD9853_SleepInMode(void)
{
    WriteCOM(0x10); //Sleep in
    lcd_delay(120); //Delay 120ms
}

void JD9853_SleepOutMode(void)
{
    WriteCOM(0x11); //Sleep out
    lcd_delay(120);  //Delay 120ms
}

void st7789_shown_image(u8 *buff, u16 x_addr, u16 y_addr, u16 width, u16 height)
{
    lcd_interface_non_block_wait();
    JD9853_SetRange(x_addr, y_addr, width, height);
    WriteDAT_DMA(buff, width * height * 2);
}

static void JD9853_set_direction(u8 dir)
{

}

static void JD9853_draw(u8 *map, u32 size)//获取Ui发送出来的数据
{
    ui_send_data_ready(map, size);
}

static void JD9853_draw_1(u8 *buf, u32 size, int width, int height)//获取camera发送出来的数据 //数据帧数以camera为基准
{
    camera_send_data_ready(buf, size, width, height);
}

static void JD9853_reset(void)
{
    printf("reset \n");
    lcd_rst_pinstate(1);
    lcd_rs_pinstate(1);
    lcd_cs_pinstate(1);

    lcd_rst_pinstate(1);
    lcd_delay(60);
    lcd_rst_pinstate(0);
    lcd_delay(10);
    lcd_rst_pinstate(1);
    lcd_delay(100);
}

typedef struct {
    u8 cmd;
    u8 cnt;
    u8 dat[128];
} InitCode;

/* NT1137G0020-V2 = JD9853，240x296。
   下面是 JD9853 参考初始化序列（公开 esp_lcd_jd9853 默认表），
   只把窗口改成 240x296；gamma/电源参数如屏厂给专用表再替换。 */
static const InitCode code1[] = {
    {0x11, 0},                  // sleep out
    {REGFLAG_DELAY, 120},
    {0xDF, 2, {0x98, 0x53}},
    {0xDF, 2, {0x98, 0x53}},
    {0xB2, 1, {0x23}},
    {0xB7, 4, {0x00, 0x47, 0x00, 0x6F}},
    {0xBB, 6, {0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0}},
    {0xC0, 2, {0x44, 0xA4}},
    {0xC1, 1, {0x16}},
    {0xC3, 8, {0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77}},
    {0xC4, 12, {0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82}},
    {0xC8, 32, {0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
                0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00}},
    {0xD0, 5, {0x04, 0x06, 0x6B, 0x0F, 0x00}},
    {0xD7, 2, {0x00, 0x30}},
    {0xE6, 1, {0x14}},
    {0xDE, 1, {0x01}},
    {0xB7, 5, {0x03, 0x13, 0xEF, 0x35, 0x35}},
    {0xC1, 3, {0x14, 0x15, 0xC0}},
    {0xC2, 2, {0x06, 0x3A}},
    {0xC4, 2, {0x72, 0x12}},
    {0xBE, 1, {0x00}},
    {0xDE, 1, {0x02}},
    {0xE5, 3, {0x00, 0x02, 0x00}},
    {0xE5, 3, {0x01, 0x02, 0x00}},
    {0xDE, 1, {0x00}},
    {0x35, 1, {0x00}},
    {0x3A, 1, {0x05}},          // RGB565
    {0x2A, 4, {0x00, 0x00, 0x00, 0xEF}},    // X 0..239
    {0x2B, 4, {0x00, 0x00, 0x01, 0x27}},    // Y 0..295
    {0xDE, 1, {0x02}},
    {0xE5, 3, {0x00, 0x02, 0x00}},
    {0xDE, 1, {0x00}},
    {0x29, 0},                  // display on
    {REGFLAG_DELAY, 20},
};

static void JD9853_init_code(const InitCode *code, u8 cnt)
{
    for (u8 i = 0; i < cnt; i++) {
        if (code[i].cmd == REGFLAG_DELAY) {
            lcd_delay(code[i].cnt);
        } else {
            WriteCOM(code[i].cmd);
            for (u8 j = 0; j < code[i].cnt; j++) {
                WriteDAT_8(code[i].dat[j]);
            }
        }
    }
}

static void JD9853_led_ctrl(u8 status)
{
    //背光控制以及放在//lcd_te_driver.c 优化开机显示
    /*lcd_bl_pinstate(status);*/
}

void JD9853_test(void)
{
    lcd_bl_pinstate(lcd_bk_on);
    while (1) {
        os_time_dly(100);
        JD9853_clear_screen(BLUE);
        printf("LCD_JD9853_TSET_BLUE\n");
        os_time_dly(100);
        JD9853_clear_screen(GREEN);
        printf("LCD_JD9853_TSET_GREEN\n");
        os_time_dly(100);
        JD9853_clear_screen(RED);
        printf("LCD_JD9853_TSET_RED\n");
        os_time_dly(100);
        JD9853_clear_screen(YELLOW);
        printf("LCD_JD9853_TSET_YELLOW\n");
    }
}

static int JD9853_init(void)
{
    printf("LCD_JD9853 init_start\n");
    JD9853_reset();
    JD9853_init_code(code1, sizeof(code1) / sizeof(code1[0]));
    JD9853_set_direction(ROTATE_DEGREE_90);
    init_TE(JD9853_Fill);
    /*JD9853_test();*/
    printf("LCD_JD9853 config succes\n");
    return 0;
}


REGISTER_LCD_DEV(LCD_JD9853) = {
    .name              = "JD9853",
    .lcd_width         = LCD_W,
    .lcd_height        = LCD_H,
    .color_format      = LCD_COLOR_RGB565,
    .column_addr_align = 1,
    .row_addr_align    = 1,
    .LCD_Init          = JD9853_init,
    .SetDrawArea       = JD9853_SetRange,
    .LCD_Draw          = JD9853_draw,
    .LCD_Draw_1        = JD9853_draw_1,
    .LCD_DrawToDev     = JD9853_Fill,//应用层直接到设备接口层，需要做好缓冲区共用互斥，慎用！
    .LCD_ClearScreen   = JD9853_clear_screen,
    .Reset             = JD9853_reset,
    .BackLightCtrl     = JD9853_led_ctrl,
};

#endif


