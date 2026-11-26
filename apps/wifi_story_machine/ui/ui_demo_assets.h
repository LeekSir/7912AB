#ifndef __UI_DEMO_ASSETS_H__
#define __UI_DEMO_ASSETS_H__
#include "generic/typedef.h"

#define UI_DEMO_W           296
#define UI_DEMO_H           240
#define UI_DEMO_HEAD_X      56
#define UI_DEMO_HEAD_Y      16
#define UI_DEMO_HEAD_W      184
#define UI_DEMO_HEAD_H      168
#define UI_DEMO_SPLASH_NUM  12

extern const u8 *const ui_demo_page_rle[4];
extern const u32 ui_demo_page_size[4];
extern const u8 *const ui_demo_test_rle;
extern const u32 ui_demo_test_size;
extern const u8 *const ui_demo_splash_bg_rle;
extern const u32 ui_demo_splash_bg_size;
extern const u8 *const ui_demo_head_rle[UI_DEMO_SPLASH_NUM];
extern const u32 ui_demo_head_size[UI_DEMO_SPLASH_NUM];

#endif
