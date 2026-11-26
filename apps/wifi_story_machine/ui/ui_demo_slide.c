/* UI Demo：向日葵开机画面 + 三页左右滑动菜单 + 上下滑设置面板
 *
 * 页面/设置/向日葵全部使用预渲染的抗锯齿图片（RLE 压缩），
 * 物理横屏 296x240 绘制，推屏前旋转成 LCD 原生 240x296。
 */

#include "system/includes.h"
#include "app_config.h"
#include "ui_api.h"
#include "lcd_drive.h"
#include "lcd_data_driver.h"
#include "ui_demo_assets.h"

#if defined(CONFIG_BOARD_7912AB) && TCFG_LCD_JD9853_ENABLE

#define DP_W                UI_DEMO_W
#define DP_H                UI_DEMO_H
#define DP_FB_SIZE          (DP_W * DP_H * 2)
#define DP_LCD_SIZE         (LCD_W * LCD_H * 2)

static u8 *dp_fb;               /* 横屏物理 buffer */
static u8 *dp_lcd;              /* 旋转后的 LCD 原生 buffer */
static u8 *dp_pages[4];         /* 蓝牙 / SD / 豆包 / 设置 */
static u8 *dp_splash_bg;

extern int spt5113c_get_touch(int *x, int *y, int *down);

/* ---------------- RLE 解码 ---------------- */

static void dp_rle_decode(const u8 *p, u8 *dst, int npix)
{
    int n = 0;
    u8 *out = dst;

    while (n < npix) {
        u16 color = p[0] | (p[1] << 8);
        u16 count = p[2] | (p[3] << 8);
        p += 4;

        u8 lo = color & 0xff;
        u8 hi = color >> 8;
        for (int i = 0; i < count && n < npix; i++, n++) {
            *out++ = lo;
            *out++ = hi;
        }
    }
}

static void dp_pixel(int x, int y, u16 color)
{
    if (x < 0 || x >= DP_W || y < 0 || y >= DP_H) {
        return;
    }
    u8 *p = dp_fb + (y * DP_W + x) * 2;
    p[0] = color & 0xff;
    p[1] = color >> 8;
}

/* 把 RLE 图块解到 (x0,y0) 窗口，越界自动裁剪 */
static void dp_rle_blit(const u8 *p, int x0, int y0, int w, int h)
{
    int total = w * h;
    int n = 0, x = 0, y = 0;

    while (n < total) {
        u16 color = p[0] | (p[1] << 8);
        u16 count = p[2] | (p[3] << 8);
        p += 4;

        for (int i = 0; i < count && n < total; i++, n++) {
            dp_pixel(x0 + x, y0 + y, color);
            if (++x >= w) {
                x = 0;
                y++;
            }
        }
    }
}

/* 把整页 buffer 贴到 (xo, yo)，逐行 memcpy + 裁剪 */
static void dp_blit(const u8 *src, int xo, int yo)
{
    int sx0 = 0, sx1 = DP_W;
    int dx0 = xo;

    if (dx0 < 0) {
        sx0 = -dx0;
        dx0 = 0;
    }
    if (dx0 + (sx1 - sx0) > DP_W) {
        sx1 = DP_W - dx0 + sx0;
    }
    if (sx1 <= sx0) {
        return;
    }

    for (int y = 0; y < DP_H; y++) {
        int dy = yo + y;
        if (dy < 0 || dy >= DP_H) {
            continue;
        }
        memcpy(dp_fb + (dy * DP_W + dx0) * 2,
               src + (y * DP_W + sx0) * 2,
               (sx1 - sx0) * 2);
    }
}

/* ---------------- 推屏 ---------------- */

static void dp_flush(void)
{
    /* 物理 (px,py) -> 原生 (py, 295-px) */
    for (int py = 0; py < DP_H; py++) {
        const u8 *src = dp_fb + py * DP_W * 2;
        for (int px = 0; px < DP_W; px++) {
            int nx = py;
            int ny = LCD_H - 1 - px;
            u8 *dst = dp_lcd + (ny * LCD_W + nx) * 2;
            dst[0] = src[2 * px];
            dst[1] = src[2 * px + 1];
        }
    }
    lcd_set_draw_area(0, LCD_W - 1, 0, LCD_H - 1);
    ui_send_data_ready(dp_lcd, DP_LCD_SIZE);
}

/* ---------------- 页面 ---------------- */

static void draw_page(int page, int xo)
{
    dp_blit(dp_pages[page], xo, 0);
}

static void draw_settings(int yo)
{
    dp_blit(dp_pages[3], 0, yo);
}

static void draw_splash(int frame)
{
    memcpy(dp_fb, dp_splash_bg, DP_FB_SIZE);
    dp_rle_blit(ui_demo_head_rle[frame], UI_DEMO_HEAD_X, UI_DEMO_HEAD_Y,
                UI_DEMO_HEAD_W, UI_DEMO_HEAD_H);
    dp_flush();
}

/* ---------------- 演示状态机 ---------------- */

#define ANIM_NONE           0
#define ANIM_PAGE           1
#define ANIM_SET_OPEN       2
#define ANIM_SET_CLOSE      3

static int cur_page;
static int anim_state;
static int anim_from;
static int anim_to;
static int anim_dir;
static int anim_off;
static int settings_y = -DP_H;
static u32 auto_tick;
static u32 last_render;
static int touch_down_last;
static int touch_start_x;
static int touch_start_y;
static int touch_last_x;
static int touch_last_y;

static int splash_active = 1;
static u32 splash_start;
static u8 test_active;
static u32 test_start;

static void demo_render(void)
{
    if (settings_y <= -DP_H) {
        if (anim_state == ANIM_PAGE) {
            if (anim_dir > 0) {
                draw_page(anim_from, -anim_off);
                draw_page(anim_to, DP_W - anim_off);
            } else {
                draw_page(anim_from, anim_off);
                draw_page(anim_to, -(DP_W - anim_off));
            }
        } else {
            draw_page(cur_page, 0);
        }
    } else {
        draw_page(cur_page, 0);
        draw_settings(settings_y);
    }
    dp_flush();
}

static void demo_page_goto(int page, int dir)
{
    if (page < 0 || page > 2 || page == cur_page || anim_state) {
        return;
    }
    anim_from = cur_page;
    anim_to = page;
    anim_dir = dir;
    anim_off = 0;
    anim_state = ANIM_PAGE;
}

static void demo_settings_open(void)
{
    if (settings_y > -DP_H || anim_state) {
        return;
    }
    anim_state = ANIM_SET_OPEN;
}

static void demo_settings_close(void)
{
    if (settings_y >= 0 && !anim_state) {
        anim_state = ANIM_SET_CLOSE;
    }
}

static void demo_handle_swipe(int dx, int dy)
{
    if (settings_y == 0) {
        if (dy < -24) {
            demo_settings_close();
        }
        return;
    }
    if ((dx > 0 ? dx : -dx) > (dy > 0 ? dy : -dy)) {
        if (dx < -24) {
            demo_page_goto(cur_page + 1, 1);
        } else if (dx > 24) {
            demo_page_goto(cur_page - 1, -1);
        }
    } else if (dy > 24) {
        demo_settings_open();
    }
}

static void demo_auto_step(void)
{
    u32 now = timer_get_ms();

    if (anim_state || now - auto_tick < 3500) {
        return;
    }
    auto_tick = now;

    if (settings_y == 0) {
        demo_settings_close();
        cur_page = 0;
    } else if (settings_y <= -DP_H && cur_page < 2) {
        demo_page_goto(cur_page + 1, 1);
    } else if (settings_y <= -DP_H) {
        demo_settings_open();
    }
}

static void demo_task(void *p)
{
    int rx, ry, down;
    u32 now;
    int frame_last = -1;

    dp_fb = malloc(DP_FB_SIZE);
    dp_lcd = malloc(DP_LCD_SIZE);
    dp_splash_bg = malloc(DP_FB_SIZE);
    for (int i = 0; i < 4; i++) {
        dp_pages[i] = malloc(DP_FB_SIZE);
    }
    if (!dp_fb || !dp_lcd || !dp_splash_bg ||
        !dp_pages[0] || !dp_pages[1] || !dp_pages[2] || !dp_pages[3]) {
        printf("UI_DEMO: malloc fail\n");
        return;
    }

    for (int i = 0; i < 4; i++) {
        dp_rle_decode(ui_demo_page_rle[i], dp_pages[i], DP_W * DP_H);
    }
    dp_rle_decode(ui_demo_splash_bg_rle, dp_splash_bg, DP_W * DP_H);

    printf("UI_DEMO: start, %dx%d landscape, assets ok\n", DP_W, DP_H);

    splash_start = timer_get_ms();
    draw_splash(0);
    frame_last = 0;

    while (1) {
        spt5113c_get_touch(&rx, &ry, &down);
        /* 触摸返回原生竖屏 240x296，demo 在物理横屏 296x240 上渲染，
           这里按 dp_flush 的旋转做逆变换，保证 dx/dy 与画面方向一致 */
        int tx = LCD_H - 1 - ry;
        int ty = rx;
        now = timer_get_ms();

        /* 开机画面：向日葵摇曳 5s */
        if (splash_active) {
            int frame = ((now - splash_start) / 150) % UI_DEMO_SPLASH_NUM;
            if (frame != frame_last) {
                frame_last = frame;
                draw_splash(frame);
            }
            if (now - splash_start >= 5000) {
                splash_active = 0;
                test_active = 1;
                test_start = now;
                dp_rle_decode(ui_demo_test_rle, dp_fb, DP_W * DP_H);
                dp_flush();
                printf("UI_DEMO: stripe test 6s (1px/2px/3px)\n");
            }
            touch_down_last = down;
            os_time_dly(2);
            continue;
        }

        /* 1px 条纹测试：判断屏是否 1:1 显示 */
        if (test_active) {
            if (now - test_start >= 6000) {
                test_active = 0;
                auto_tick = now;
                last_render = 0;
                demo_render();
            }
            touch_down_last = down;
            os_time_dly(2);
            continue;
        }

        if (down && !touch_down_last) {
            touch_start_x = tx;
            touch_start_y = ty;
        }
        if (!down && touch_down_last) {
            int dx = touch_last_x - touch_start_x;
            int dy = touch_last_y - touch_start_y;
            if (dx > 24 || dx < -24 || dy > 24 || dy < -24) {
                demo_handle_swipe(dx, dy);
                auto_tick = timer_get_ms();
            }
        }
        touch_last_x = tx;
        touch_last_y = ty;
        touch_down_last = down;

        if (anim_state == ANIM_PAGE) {
            if (now - last_render >= 90) {
                last_render = now;
                anim_off += 40;
                if (anim_off >= DP_W) {
                    anim_off = 0;
                    cur_page = anim_to;
                    anim_state = ANIM_NONE;
                }
                demo_render();
            }
        } else if (anim_state == ANIM_SET_OPEN) {
            if (now - last_render >= 90) {
                last_render = now;
                settings_y += 40;
                if (settings_y >= 0) {
                    settings_y = 0;
                    anim_state = ANIM_NONE;
                }
                demo_render();
            }
        } else if (anim_state == ANIM_SET_CLOSE) {
            if (now - last_render >= 90) {
                last_render = now;
                settings_y -= 40;
                if (settings_y <= -DP_H) {
                    settings_y = -DP_H;
                    anim_state = ANIM_NONE;
                }
                demo_render();
            }
        } else {
            int old_page = cur_page;
            int old_set = settings_y;
            demo_auto_step();
            if (old_page != cur_page || old_set != settings_y || anim_state) {
                demo_render();
            }
        }

        os_time_dly(2);
    }
}

void ui_demo_slide_start(void)
{
    thread_fork("ui_demo_slide", 11, 2048, 32, 0, demo_task, NULL);
}

#endif /* CONFIG_BOARD_7912AB && TCFG_LCD_JD9853_ENABLE */
