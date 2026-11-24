#include "system/includes.h"
#include "asm/port_waked_up.h"
#include "typedef.h"
#include "os/os_api.h"
#include "asm/iic.h"
#include "device/iic.h"
#include "device/device.h"
#include "lcd_config.h"
#include "app_config.h"
#include "gpio.h"
#include "ui_api.h"
#include "touch_event.h"
#include "sys_common.h"

#if TCFG_TOUCH_SPT5113C_ENABLE

/* SPT5113C 内部 8051 固件（原厂 spt5113c_JL707_driver_v0.2 包） */
#include "spt5113c_firmware.h"

/* SPT5113C：
 *  - 应用寄存器：7bit 地址 0x13（写 0x26 / 读 0x27），触摸数据从 8bit 寄存器 0x00 读 7 字节
 *      buf[2]        按下标志（非 0 = 按下）
 *      buf[3] bit7   按下标志（备用）
 *      X = buf[4] | ((buf[3] & 0x0f) << 8)
 *      Y = buf[6] | ((buf[5] & 0x0f) << 8)
 *  - 启动/ASU 寄存器：7bit 地址 0x60（写 0xC0 / 读 0xC1），16bit 寄存器地址（低字节在前）
 *
 * 原厂上电流程：芯片码校验 -> 固件 CRC 校验/烧录 -> start_work。
 * 触摸坐标目前直接按原厂方式使用，坐标标定等触摸厂家调整固件后再做。
 */
#define SPT_APP_W               0x26
#define SPT_APP_R               0x27
#define SPT_ASU_W               0xC0
#define SPT_ASU_R               0xC1

#define SPT_CHIP_CODE           0x53

#define VK_X                    LCD_W
#define VK_Y                    LCD_H

#if 1
#define log_info(x, ...)        printf("\n[SPT5113C]> " x " \n", ## __VA_ARGS__)
#else
#define log_info(...)
#endif

static void *iic;
static u8 touch_en = 1;

static u8 spt_wr_asu(u16 reg, const u8 *data, u32 len)
{
    u8 ret = 0;

    dev_ioctl(iic, IIC_IOCTL_START, 0);
    if (dev_ioctl(iic, IIC_IOCTL_TX_WITH_START_BIT, SPT_ASU_W)) {
        goto exit;
    }
    if (dev_ioctl(iic, IIC_IOCTL_TX, reg & 0xff)) {
        goto exit;
    }
    if (dev_ioctl(iic, IIC_IOCTL_TX, (reg >> 8) & 0xff)) {
        goto exit;
    }
    for (u32 i = 0; i < len; i++) {
        if (dev_ioctl(iic, IIC_IOCTL_TX, data[i])) {
            goto exit;
        }
    }
    /* IIC_IOCTL_STOP 只是解锁 GPIO，不产生 I2C STOP；写事务必须用 TX_STOP_BIT 结束 */
    dev_ioctl(iic, IIC_IOCTL_TX_STOP_BIT, 0);
    ret = 1;

exit:
    dev_ioctl(iic, IIC_IOCTL_STOP, 0);
    return ret;
}

static u8 spt_rd_asu(u16 reg, u8 *buf, u32 len)
{
    u8 ret = 0;

    dev_ioctl(iic, IIC_IOCTL_START, 0);
    if (dev_ioctl(iic, IIC_IOCTL_TX_WITH_START_BIT, SPT_ASU_W)) {
        goto exit;
    }
    if (dev_ioctl(iic, IIC_IOCTL_TX, reg & 0xff)) {
        goto exit;
    }
    if (dev_ioctl(iic, IIC_IOCTL_TX, (reg >> 8) & 0xff)) {
        goto exit;
    }
    if (dev_ioctl(iic, IIC_IOCTL_TX_WITH_START_BIT, SPT_ASU_R)) {
        goto exit;
    }
    for (u32 i = 0; i < len; i++) {
        if (i == len - 1) {
            if (dev_ioctl(iic, IIC_IOCTL_RX_WITH_STOP_BIT, (u32)&buf[i])) {
                goto exit;
            }
        } else {
            if (dev_ioctl(iic, IIC_IOCTL_RX_WITH_ACK, (u32)&buf[i])) {
                goto exit;
            }
        }
    }
    ret = 1;

exit:
    dev_ioctl(iic, IIC_IOCTL_STOP, 0);
    return ret;
}

static u8 spt_rd_app(u8 reg, u8 *buf, u32 len)
{
    u8 ret = 0;

    dev_ioctl(iic, IIC_IOCTL_START, 0);
    if (dev_ioctl(iic, IIC_IOCTL_TX_WITH_START_BIT, SPT_APP_W)) {
        goto exit;
    }
    if (dev_ioctl(iic, IIC_IOCTL_TX, reg)) {
        goto exit;
    }
    if (dev_ioctl(iic, IIC_IOCTL_TX_WITH_START_BIT, SPT_APP_R)) {
        goto exit;
    }
    for (u32 i = 0; i < len; i++) {
        if (i == len - 1) {
            if (dev_ioctl(iic, IIC_IOCTL_RX_WITH_STOP_BIT, (u32)&buf[i])) {
                goto exit;
            }
        } else {
            if (dev_ioctl(iic, IIC_IOCTL_RX_WITH_ACK, (u32)&buf[i])) {
                goto exit;
            }
        }
    }
    ret = 1;

exit:
    dev_ioctl(iic, IIC_IOCTL_STOP, 0);
    return ret;
}

/* ASU 连接：确认芯片码，并进入升级保持状态 */
static u8 spt_asu_connect(void)
{
    u8 code = 0;
    u8 v = 0;

    if (!spt_rd_asu(0xC000, &code, 1)) {
        log_info("asu read chip code fail");
        return 0;
    }
    log_info("chip code = 0x%02x (expect 0x%02x)", code, SPT_CHIP_CODE);
    if (code != SPT_CHIP_CODE) {
        return 0;
    }

    if (!spt_rd_asu(0xC103, &v, 1)) {
        log_info("asu read C103 fail");
        return 0;
    }
    v &= 0xF0;      /* 低 4 bit 清零 = 保持升级状态 */
    if (!spt_wr_asu(0xC103, &v, 1)) {
        log_info("asu write C103 fail");
        return 0;
    }

    return 1;
}

/* 校验芯片内固件 CRC：0 = 一致，1 = 不一致，-1 = I2C 失败 */
static int spt_fw_crc_check(void)
{
    u8 hi = 0, lo = 0, v;
    u16 len = SPT5113C_FIRMWARE_SIZE - 1;
    u16 crc;

    v = (len >> 8) & 0xff;
    if (!spt_wr_asu(0xC130, &v, 1)) {
        return -1;
    }
    v = len & 0xff;
    if (!spt_wr_asu(0xC131, &v, 1)) {
        return -1;
    }
    v = (SPT5113C_FIRMWARE_CRC16 >> 8) & 0xff;
    if (!spt_wr_asu(0xC132, &v, 1)) {
        return -1;
    }
    v = SPT5113C_FIRMWARE_CRC16 & 0xff;
    if (!spt_wr_asu(0xC133, &v, 1)) {
        return -1;
    }
    v = 0x01;       /* 触发 CRC 校验 */
    if (!spt_wr_asu(0xC136, &v, 1)) {
        return -1;
    }
    mdelay(10);

    if (!spt_rd_asu(0xC134, &hi, 1)) {
        return -1;
    }
    if (!spt_rd_asu(0xC135, &lo, 1)) {
        return -1;
    }
    crc = ((u16)hi << 8) | lo;
    log_info("chip fw crc = 0x%04x (expect 0x%04x)", crc, SPT5113C_FIRMWARE_CRC16);

    return (crc == SPT5113C_FIRMWARE_CRC16) ? 0 : 1;
}

/* 检查固件，不一致则烧录（原厂流程） */
static u8 spt_fw_update(void)
{
    int ret;

    ret = spt_fw_crc_check();
    if (ret == 0) {
        log_info("fw already latest");
        return 1;
    }
    if (ret < 0) {
        log_info("fw crc check i2c fail");
        return 0;
    }

    log_info("fw mismatch, writing %d bytes...", SPT5113C_FIRMWARE_SIZE);
    if (!spt_wr_asu(0x0000, spt5113c_firmware, SPT5113C_FIRMWARE_SIZE)) {
        log_info("fw write fail");
        return 0;
    }
    log_info("fw write done, verify...");

    ret = spt_fw_crc_check();
    if (ret != 0) {
        log_info("fw verify fail (%d)", ret);
        return 0;
    }
    log_info("fw update ok");

    return 1;
}

/* 启动应用固件（不扫频，按固件默认频率运行） */
static void spt_start_work(void)
{
    u8 v;
    u8 hop[3] = {0, 0, 0};

    v = SPT_APP_W;
    spt_wr_asu(0xC10E, &v, 1);

    v = 0x2F;
    spt_wr_asu(0xC10F, &v, 1);

    if (spt_rd_asu(0xC10D, &v, 1)) {
        v |= 0x01;
        spt_wr_asu(0xC10D, &v, 1);
    }

    spt_wr_asu(0x2FB0, hop, 3);

    if (spt_rd_asu(0xC103, &v, 1)) {
        v &= 0xDF;
        v |= 0x03;
        spt_wr_asu(0xC103, &v, 1);
    }
}

/* 触摸坐标先按原厂方式直接使用，等触摸厂家调整固件后再做标定 */
static void spt_report(int x, int y, int down)
{
    static u8 last_state = 0;
    static int last_x = 0, last_y = 0;
    struct touch_event t = {0};

    if (x < 0) {
        x = 0;
    }
    if (x > (VK_X - 1)) {
        x = VK_X - 1;
    }
    if (y < 0) {
        y = 0;
    }
    if (y > (VK_Y - 1)) {
        y = VK_Y - 1;
    }

    if (down) {
        if (last_state && x == last_x && y == last_y) {
            return;
        }
        t.action = last_state ? ELM_EVENT_TOUCH_MOVE : ELM_EVENT_TOUCH_DOWN;
        t.x = x;
        t.y = y;
        last_x = x;
        last_y = y;
        last_state = 1;
    } else {
        if (!last_state) {
            return;
        }
        t.action = ELM_EVENT_TOUCH_UP;
        t.x = last_x;
        t.y = last_y;
        last_state = 0;
    }

    if (touch_en) {
        ui_touch_msg_post(&t);
    }
}

static void spt_touch_task(void *p)
{
    extern const struct ui_devices_cfg ui_cfg_data;
    const struct ui_lcd_platform_data *pdata = (struct ui_lcd_platform_data *)ui_cfg_data.private_data;
    u8 buf[7];
    u8 code = 0;
    int last_x = -1, last_y = -1, last_down = -1;
    u32 i2c_err = 0;

    /* 复位（原厂时序：H -> L -> H） */
    gpio_direction_output(pdata->touch_reset_pin, 1);
    mdelay(10);
    gpio_direction_output(pdata->touch_reset_pin, 0);
    mdelay(12);
    gpio_direction_output(pdata->touch_reset_pin, 1);
    mdelay(12);

    /* INT 暂时不用，配置成输入上拉 */
    gpio_direction_input(pdata->touch_int_pin);
    gpio_set_pull_up(pdata->touch_int_pin, 1);

    iic = dev_open("iic0", NULL);
    if (!iic) {
        log_info("open iic0 fail");
        return;
    }
    mdelay(10);

    /* 原厂上电流程：芯片码 + 固件校验/烧录 + start_work */
    if (spt_asu_connect()) {
        spt_fw_update();
    } else {
        log_info("asu connect fail, skip fw update");
    }

    spt_start_work();
    mdelay(300);

    if (spt_rd_asu(0xC103, &code, 1)) {
        log_info("C103 = 0x%02x", code);
    }
    if (spt_rd_asu(0xC10E, &code, 1)) {
        log_info("C10E = 0x%02x", code);
    }
    log_info("INT pin = %d", gpio_read(pdata->touch_int_pin));
    log_info("start work, poll 10ms, screen %dx%d", VK_X, VK_Y);

    while (1) {
        os_time_dly(1);     /* 10ms */

        if (!spt_rd_app(0x00, buf, 7)) {
            if ((i2c_err++ % 50) == 0) {
                log_info("i2c read touch fail (%d)", i2c_err);
            }
            continue;
        }

        int down = (buf[2] != 0) || (buf[3] & 0x80);
        int x = buf[4] | ((buf[3] & 0x0f) << 8);
        int y = buf[6] | ((buf[5] & 0x0f) << 8);

        if (x != last_x || y != last_y || down != last_down) {
            last_x = x;
            last_y = y;
            last_down = down;
            log_info("raw x=%d y=%d down=%d", x, y, down);
            spt_report(x, y, down);
        }
    }
}

int SPT5113C_task_init(void)
{
    return thread_fork("spt5113c", 29, 1024, 0, NULL, spt_touch_task, NULL);
}

#endif /* TCFG_TOUCH_SPT5113C_ENABLE */
