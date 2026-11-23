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

#ifdef CONFIG_BOARD_MUTEX
#error "board config can not enable at the same time, just select one!!!"
#else
#define CONFIG_BOARD_MUTEX
#endif

#endif

#endif
