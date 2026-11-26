#ifndef CONFIG_BOARD_7912AB_CFG_H
#define CONFIG_BOARD_7912AB_CFG_H

#ifdef CONFIG_BOARD_7912AB

#define __FLASH_SIZE__    (8 * 1024 * 1024)
#define __SDRAM_SIZE__    (8 * 1024 * 1024)

#define CONFIG_ALL_USB_ENABLE

//7911B
#include "board_7911B_develop_cfg.h"
#include "board_7911B_dui_cfg.h"

/* WB-AC79-01 原理图（7912AB 故事机）修正：
   - 功放 mute = AMP_MUTE_OUT = SoC pin18 = PB5（AC7912A datasheet：
     pin18 PB5 = PLNK0_DAT1/PLNK1_DAT1/SPI1_DIA/FPIN2/CAP0；PB3 是 pin19 CHR_PG）
   - 功放 SD 极性：低 = 输出使能，高 = 切断（mute 电平 = 1）
   - 3V3_M 由 4V2_SUB 经 RD36 10K 上拉常开，没有 MCU 控制脚；
     PA9 实际是 SD0_DAT0，不能当 VCC33 控制脚 */
#undef TCFG_DAC_MUTE_PORT
#define TCFG_DAC_MUTE_PORT      IO_PORTB_05
#undef TCFG_DAC_MUTE_VALUE
#define TCFG_DAC_MUTE_VALUE     1
#undef PORT_VCC33_CTRL_IO

/* 板上 MIC：MIC0 差分（PA1/PA2）为主 mic；MIC1 差分（PH8/PH9）是功放回采 AEC，
   没有 MIC2。阶段2 mic 调试先只开 MIC0。 */
#undef TCFG_MIC_CHANNEL_MAP
#undef TCFG_MIC_CHANNEL_NUM
#define TCFG_MIC_CHANNEL_MAP    LADC_CH_MIC0_P_N
#define TCFG_MIC_CHANNEL_NUM    1

/* 阶段3：NT1137G0020-V2 屏，240x296，JD9853（ST7789 兼容），4L1P SPI
   - 触摸靶测试：面板原生竖屏 240x296，5 个靶环全部可见，触摸 1:1 便于标定
   - DUI 故事机 UI：横屏 320x240（MADCTL 0x60，面板实际可见 296x240，右侧 24 列不可见）
   注：本文件在 app_config.h 之前展开，不能判断 CONFIG_STORY_UI_TOUCH_TARGET，
       故单独用 CONFIG_STORY_TOUCH_TEST_NATIVE_PORTRAIT 控制。 */
#define CONFIG_STORY_TOUCH_TEST_NATIVE_PORTRAIT  1

#if CONFIG_STORY_TOUCH_TEST_NATIVE_PORTRAIT
#define LCD_W                   240
#define LCD_H                   296
#else
#define LCD_W                   320
#define LCD_H                   240
#endif

/* SPT5113C 触摸坐标映射：默认按原厂屏幕坐标。
   实测 raw 为 12bit 时把 RAW_MAX 改成 4096；方向不对时改 SWAP/INVERT。 */
#define SPT_RAW_MAX_X           LCD_W
#define SPT_RAW_MAX_Y           LCD_H
#define SPT_TOUCH_SWAP_XY       0
#define SPT_TOUCH_INVERT_X      0
#define SPT_TOUCH_INVERT_Y      0

#ifdef CONFIG_BOARD_MUTEX
#error "board config can not enable at the same time, just select one!!!"
#else
#define CONFIG_BOARD_MUTEX
#endif

#endif

#endif
