#include "app_config.h"

#include "system/includes.h"
#include "device/includes.h"
#include "asm/includes.h"

// *INDENT-OFF*


UART0_PLATFORM_DATA_BEGIN(uart0_data)
	.baudrate = 115200,
	.port = PORTB_1_2,
	.tx_pin = IO_PORTB_01,
	.rx_pin = IO_PORTB_02,
	.max_continue_recv_cnt = 1024,
	.idle_sys_clk_cnt = 500000,
	.clk_src = PLL_48M,
	.flags = UART_DEBUG,
UART0_PLATFORM_DATA_END();

/**************************  POWER config ****************************/
static const struct low_power_param power_param = {
    .config         = TCFG_LOWPOWER_LOWPOWER_SEL,          //系统休眠
    .btosc_disable  = TCFG_LOWPOWER_BTOSC_DISABLE,         //进入低功耗时BTOSC是否保持
    .vddiom_lev     = TCFG_LOWPOWER_VDDIOM_LEVEL,          //强VDDIO等级,可选：2.8V  2.9V  3.0V  3.1V  3.2V  3.3V  3.4V  3.5V
    .vddiow_lev     = TCFG_LOWPOWER_VDDIOW_LEVEL,          //弱VDDIO等级,可选：2.1V  2.4V  2.8V  3.2V
	.vdc14_dcdc 	= TRUE,	   							   //打开内部1.4VDCDC，关闭则用外部
    .vdc14_lev		= VDC14_VOL_SEL_LEVEL, 				   //VDD1.4V配置
	.sysvdd_lev		= SYSVDD_VOL_SEL_LEVEL,				   //内核、sdram电压配置
	.vlvd_enable	= TRUE,                                //TRUE电压复位使能
	.vlvd_value		= VLVD_SEL_25V,                        //低电压复位电压值
};

REGISTER_DEVICES(device_table) = {
	{"uart0", &uart_dev_ops, (void *)&uart0_data },
	{"rtc", &rtc_dev_ops, NULL},
};

#ifdef CONFIG_DEBUG_ENABLE
void debug_uart_init()
{
    uart_init(&uart0_data);
}
#endif

#define PWR_KEY_IO          IO_PORTB_06   /* KEY_ADC_IN_IC */
#define PWR_KEY_ADC_CH      AD_CH_PB06    /* PB6 = ADC 通道 4 */
#define PWR_KEY_ADC_THRESHOLD 300         /* 低于该值判定按下（实测松开~1022，按下~72） */
#define PWR_KEY_LONG_MS     3000          /* 长按 3s 开机 */
#define PWR_KEY_OFF_MS      1000          /* 短按 1s 关机 */

static u8 power_key_wait_release = 1;
static u32 power_key_low_ms;

u32 power_key_adc_value(void)
{
    return adc_get_value(PWR_KEY_ADC_CH);
}

static int power_key_is_down(void)
{
    return power_key_adc_value() < PWR_KEY_ADC_THRESHOLD;
}

static void power_key_init(void)
{
    gpio_direction_input(PWR_KEY_IO);
    gpio_set_die(PWR_KEY_IO, 0);
    gpio_set_pull_up(PWR_KEY_IO, 0);
    gpio_set_pull_down(PWR_KEY_IO, 0);
    gpio_latch_en(PWR_KEY_IO, 0);
    adc_init();
    adc_add_sample_ch(PWR_KEY_ADC_CH);
}

unsigned char PWR_CTL(unsigned char on_off)
{
    /* PB7 = PW_EN，高电平保持供电，低电平断电 */
    gpio_direction_output(IO_PORTB_07, on_off ? 1 : 0);
    return 0;
}

static void power_key_scan(void *priv)
{
    (void)priv;
    if (!power_key_is_down()) {
        power_key_wait_release = 0;
        power_key_low_ms = 0;
        return;
    }
    if (power_key_wait_release) {
        return;
    }
    power_key_low_ms += 10;
    if (power_key_low_ms >= PWR_KEY_OFF_MS) {
        wdt_close();
        PWR_CTL(0);
    }
}

void board_power_hold_init(void)
{
    /* 最早阶段先锁存 PB7，保证 3s 检查期间不掉电；
       ADC/按键判定放到 board_init（UART/ADC 初始化之后），避免 setup_arch 阶段调用 ADC 卡死 */
    gpio_direction_output(IO_PORTB_07, 1);
}

unsigned char read_power_key(void)
{
    return power_key_is_down();
}

void board_early_init()
{
    devices_init();
}

static void board_power_init(void)
{

    power_init(&power_param);
}

void board_init()
{
	board_power_init();

    power_key_init();
    printf("PWR: KEY_ADC=%d\r\n", power_key_adc_value());

    if (!power_key_is_down()) {
        /* 上电时没检测到按键：不锁存 PB7（USB 供电时系统仍由 USB 维持） */
        PWR_CTL(0);
        printf("PWR: no key at boot, no latch\r\n");
        power_key_wait_release = 0;
    } else {
        printf("PWR: hold S1 for 3s...\r\n");
        u32 hold_ms = 0;
        while (hold_ms < PWR_KEY_LONG_MS) {
            wdt_clear();
            delay(10);
            hold_ms += 10;
            if (!power_key_is_down()) {
                delay(30);
                if (!power_key_is_down()) {
                    PWR_CTL(0);
                    wdt_close();
                    printf("PWR: released <3s, off\r\n");
                    while (1) {
                        delay(100);
                    }
                }
            }
        }
        wdt_clear();
        printf("PWR: 3s OK\r\n");
        power_key_wait_release = 1;
    }

    /* 运行中每 10ms 检测一次 PB6 ADC，连续按下 1s 关机 */
    sys_timer_add(NULL, power_key_scan, 10);
}
