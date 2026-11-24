/* 5 点触摸靶子 UI
 *
 * 原生竖屏 240x296，RGB565 小端；四角 + 中心共 5 个靶环。
 * 手指按下/移动时实时画红色十字，松开后保留最后位置；只画十字，不显示数字。
 * 全窗口推屏走 LCD_TURN_0 直拷，触摸原生坐标与像素 1:1。
 */

#include "system/includes.h"
#include "app_config.h"
#include "lcd_drive.h"
#include "lcd_data_driver.h"

#if defined(CONFIG_BOARD_7912AB) && TCFG_LCD_JD9853_ENABLE

#define TG_W                LCD_W
#define TG_H                LCD_H
#define TG_FB_BYTES         (TG_W * TG_H * 2)

/* 逻辑颜色（标准 RGB565）；tg_panel_color() 会补偿面板的反色+BGR 显示 */
#define TG_BG_COLOR         0xFFFF  /* 白底 */
#define TG_RING_COLOR       0x001F  /* 蓝色靶环 */
#define TG_RING2_COLOR      0x4208  /* 深灰内环/中心点 */
#define TG_CROSS_COLOR      0x0000  /* 黑色十字 */

#define TG_MARGIN           26
#define TG_R_OUTER          12
#define TG_R_INNER          7
#define TG_CROSS_ARM        20      /* 十字臂长，原来 10 */
#define TG_CROSS_GAP        4
#define TG_CROSS_THICK      3       /* 线宽，原来 1 */
#define TG_FRAME_MS         50      /* ~20fps，40MHz SPI 全屏约 28ms/帧 */

static u8 *tg_bg;                   /* 静态靶点层 */
static u8 *tg_fb;                   /* 合成后推屏缓冲 */

extern int spt5113c_get_touch(int *x, int *y, int *down);

/* 面板实测为“反色 + BGR”顺序：把标准 RGB565 转成面板期望值。
   显示效果 = swap_rb(~写入值)，因此写入 ~swap_rb(目标色) 即可。 */
static u16 tg_panel_color(u16 c)
{
    u16 r = (c >> 11) & 0x1F;
    u16 g = (c >> 5) & 0x3F;
    u16 b = c & 0x1F;
    return (u16)~((b << 11) | (g << 5) | r);
}

static void tg_pixel(u8 *b, int x, int y, u16 color)
{
    if (x < 0 || x >= TG_W || y < 0 || y >= TG_H) {
        return;
    }
    color = tg_panel_color(color);
    u8 *p = b + (y * TG_W + x) * 2;
    p[0] = color & 0xff;
    p[1] = color >> 8;
}

static void tg_hline(u8 *b, int x0, int x1, int y, u16 color)
{
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    for (int x = x0; x <= x1; x++) {
        tg_pixel(b, x, y, color);
    }
}

static void tg_vline(u8 *b, int x, int y0, int y1, u16 color)
{
    if (y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    for (int y = y0; y <= y1; y++) {
        tg_pixel(b, x, y, color);
    }
}

/* 中点画圆，画 8 个对称点 */
static void tg_ring(u8 *b, int cx, int cy, int r, u16 color)
{
    int x = r, y = 0, err = 1 - r;

    while (x >= y) {
        tg_pixel(b, cx + x, cy + y, color);
        tg_pixel(b, cx + y, cy + x, color);
        tg_pixel(b, cx - y, cy + x, color);
        tg_pixel(b, cx - x, cy + y, color);
        tg_pixel(b, cx - x, cy - y, color);
        tg_pixel(b, cx - y, cy - x, color);
        tg_pixel(b, cx + y, cy - x, color);
        tg_pixel(b, cx + x, cy - y, color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

static void tg_target(u8 *b, int cx, int cy)
{
    tg_ring(b, cx, cy, TG_R_OUTER, TG_RING_COLOR);
    tg_ring(b, cx, cy, TG_R_INNER, TG_RING2_COLOR);
    tg_pixel(b, cx, cy, TG_RING2_COLOR);
    tg_pixel(b, cx + 1, cy, TG_RING2_COLOR);
    tg_pixel(b, cx, cy + 1, TG_RING2_COLOR);
    tg_pixel(b, cx + 1, cy + 1, TG_RING2_COLOR);
}

static void tg_cross(u8 *b, int x, int y)
{
    int t = TG_CROSS_THICK / 2;

    for (int d = -t; d <= t; d++) {
        tg_hline(b, x - TG_CROSS_ARM, x - TG_CROSS_GAP, y + d, TG_CROSS_COLOR);
        tg_hline(b, x + TG_CROSS_GAP, x + TG_CROSS_ARM, y + d, TG_CROSS_COLOR);
        tg_vline(b, x + d, y - TG_CROSS_ARM, y - TG_CROSS_GAP, TG_CROSS_COLOR);
        tg_vline(b, x + d, y + TG_CROSS_GAP, y + TG_CROSS_ARM, TG_CROSS_COLOR);
    }
}

static void tg_draw_static(u8 *b)
{
    u16 *p = (u16 *)b;
    u16 bg = tg_panel_color(TG_BG_COLOR);

    for (int i = 0; i < TG_W * TG_H; i++) {
        p[i] = bg;
    }

    tg_target(b, TG_MARGIN, TG_MARGIN);
    tg_target(b, TG_W - 1 - TG_MARGIN, TG_MARGIN);
    tg_target(b, TG_W / 2, TG_H / 2);
    tg_target(b, TG_MARGIN, TG_H - 1 - TG_MARGIN);
    tg_target(b, TG_W - 1 - TG_MARGIN, TG_H - 1 - TG_MARGIN);
}

static void tg_present(int x, int y, int show)
{
    memcpy(tg_fb, tg_bg, TG_FB_BYTES);
    if (show) {
        tg_cross(tg_fb, x, y);
    }
    lcd_set_draw_area(0, TG_W - 1, 0, TG_H - 1);
    ui_send_data_ready(tg_fb, TG_FB_BYTES);
}

static void tg_task(void *p)
{
    int lx = -1, ly = -1;
    int ldown = 0, pending = 0;
    u32 last = 0;

    (void)p;

    tg_bg = malloc(TG_FB_BYTES);
    tg_fb = malloc(TG_FB_BYTES);
    if (!tg_bg || !tg_fb) {
        printf("TOUCH_TARGET: malloc fail\r\n");
        free(tg_bg);
        free(tg_fb);
        tg_bg = tg_fb = NULL;
        return;
    }

    tg_draw_static(tg_bg);

    /* 背光由 te_send_task 收到第 3 帧后才打开，初始化连续推 3 帧 */
    for (int i = 0; i < 3; i++) {
        tg_present(0, 0, 0);
        os_time_dly(5);         /* 50ms，等 TE 发完一帧 */
    }
    printf("TOUCH_TARGET: start %dx%d, 5 targets\r\n", TG_W, TG_H);

    while (1) {
        int x = 0, y = 0, down = 0;
        int immediate = 0;

        spt5113c_get_touch(&x, &y, &down);

        if (down) {
            if (!ldown || x != lx || y != ly) {
                lx = x;
                ly = y;
                pending = 1;    /* 跨轮保留，节流窗口内的移动不会丢 */
            }
            ldown = 1;
        } else if (ldown) {
            ldown = 0;
            pending = 1;
            immediate = 1;      /* 松开时立即把最后位置画一次 */
        }

        u32 now = timer_get_ms();
        if (pending && (immediate || now - last >= TG_FRAME_MS)) {
            last = now;
            pending = 0;
            tg_present(lx, ly, 1);
        }

        os_time_dly(1);         /* 10ms 轮询 */
    }
}

void ui_touch_target_start(void)
{
    thread_fork("ui_touch_target", 11, 2048, 32, 0, tg_task, NULL);
}

#endif /* CONFIG_BOARD_7912AB && TCFG_LCD_JD9853_ENABLE */
