#include "system/includes.h"
#include "action.h"
#include "app_config.h"
#include "storage_device.h"
#include "generic/log.h"
#include "os/os_api.h"
#include "event/key_event.h"
#include "event/bt_event.h"
#include "event/device_event.h"
#include "event/net_event.h"
#include "wifi/wifi_connect.h"


/*中断列表 */
const struct irq_info irq_info_table[] = {
    //中断号   //优先级0-7   //注册的cpu(0或1)
#ifdef CONFIG_IPMASK_ENABLE
    //不可屏蔽中断方法：支持写flash，但中断函数和调用函数和const要全部放在内部ram
    { IRQ_SOFT5_IDX,      6,   0    }, //此中断强制注册到cpu0
    { IRQ_SOFT4_IDX,      6,   1    }, //此中断强制注册到cpu1
#if 0 //如下，SPI1使用不可屏蔽中断设置
    { IRQ_SPI1_IDX,      7,   1    },//中断强制注册到cpu0/1
#endif
#endif
#if CPU_CORE_NUM == 1
    { IRQ_SOFT5_IDX,      7,   0    }, //此中断强制注册到cpu0
    { IRQ_SOFT4_IDX,      7,   1    }, //此中断强制注册到cpu1
    { -2,     			-2,   -2   },//如果加入了该行, 那么只有该行之前的中断注册到对应核, 其他所有中断强制注册到CPU0
#endif

    { -1,     -1,   -1    },
};


/*创建使用 os_task_create_static 或者task_create 接口的 静态任务堆栈*/
#define SYS_TIMER_STK_SIZE 512
#define SYS_TIMER_Q_SIZE 128
static u8 sys_timer_tcb_stk_q[sizeof(StaticTask_t) + SYS_TIMER_STK_SIZE * 4 + sizeof(struct task_queue) + SYS_TIMER_Q_SIZE] ALIGNE(4);

#define SYSTIMER_STK_SIZE 256
static u8 systimer_tcb_stk_q[sizeof(StaticTask_t) + SYSTIMER_STK_SIZE * 4] ALIGNE(4);

#define SYS_EVENT_STK_SIZE 512
static u8 sys_event_tcb_stk_q[sizeof(StaticTask_t) + SYS_EVENT_STK_SIZE * 4] ALIGNE(4);

#define APP_CORE_STK_SIZE 2048
#define APP_CORE_Q_SIZE 1024
static u8 app_core_tcb_stk_q[sizeof(StaticTask_t) + APP_CORE_STK_SIZE * 4 + sizeof(struct task_queue) + APP_CORE_Q_SIZE] ALIGNE(4);

/*创建使用  thread_fork 接口的 静态任务堆栈*/
#define WIFI_TASKLET_STK_SIZE 1400
static u8 wifi_tasklet_tcb_stk_q[sizeof(struct thread_parm) + WIFI_TASKLET_STK_SIZE * 4] ALIGNE(4);

#define WIFI_CMDQ_STK_SIZE 300
static u8 wifi_cmdq_tcb_stk_q[sizeof(struct thread_parm) + WIFI_CMDQ_STK_SIZE * 4] ALIGNE(4);

#define WIFI_MLME_STK_SIZE 700
static u8 wifi_mlme_tcb_stk_q[sizeof(struct thread_parm) + WIFI_MLME_STK_SIZE * 4] ALIGNE(4);

#define WIFI_RX_STK_SIZE 256
static u8 wifi_rx_tcb_stk_q[sizeof(struct thread_parm) + WIFI_RX_STK_SIZE * 4] ALIGNE(4);


/*任务列表 */
const struct task_info task_info_table[] = {
    {"thread_fork_kill",    25,      256,   0     },
#ifndef CONFIG_STORY_BRINGUP
    {"led_ui_server",       30,      256,   64    },
#endif
    {"app_core",            15,     APP_CORE_STK_SIZE,	  APP_CORE_Q_SIZE,		 app_core_tcb_stk_q },
    {"sys_event",           29,     SYS_EVENT_STK_SIZE,	   0, 					 sys_event_tcb_stk_q },
    {"systimer",            14,     SYSTIMER_STK_SIZE, 	   0,					 systimer_tcb_stk_q },
    {"sys_timer",            9,     SYS_TIMER_STK_SIZE,	  SYS_TIMER_Q_SIZE,		 sys_timer_tcb_stk_q },
    {"audio_server",        16,      512,   64    },
    {"audio_mix",           28,      512,   0     },
    {"audio_encoder",       12,      384,   64    },
    {"speex_encoder",       13,      512,   0     },
    {"mp3_encoder",         13,      768,   0     },
    {"opus_encoder",        13,     1536,   0     },
    {"vir_dev_task",        14,      256,   0     },
    {"amr_encoder",         13,     1024,   0     },
    {"cvsd_encoder",        13,      512,   0     },
    {"vad_encoder",         14,      768,   0     },
    {"aec_encoder",         13,     1024,   0     },
    {"dns_encoder",         13,      512,   0     },
    {"msbc_encoder",        13,      256,   0     },
    {"sbc_encoder",         13,      512,   0     },
    {"adpcm_encoder",       13,      512,   0     },
    {"echo_deal",           11,     1024,   32    },
    {"uac_sync",            20,      512,   0     },
    {"uac_play0",           26,      512,   32    },
    {"uac_play1",           26,      512,   32    },
    {"uac_record0",         26,      512,   0     },
    {"uac_record1",         26,      512,   0     },
#if (RCSP_MODE)
    {"rcsp",            	4,       768,   128  },
    {"dev_mg",              3,       512,   512  },
#endif//RCSP_MODE
#if (TCFG_DEV_MANAGER_ENABLE)
    {"file_bs",              1,       768,   0  },
    {"ftran_back",           1,       512,   0  },
#endif
#if CPU_CORE_NUM > 1
    {"#C0usb_msd0",          1,      512,   128   },
#else
    {"usb_msd0",             1,      512,   128   },
#endif
    {"usb_msd1",             1,      512,   128   },
    {"uda_main",             2,     7000,   0     },

    {"update",              21,      512,   32    },
    {"dw_update",           21,      512,   32    },
    {"iperf_test",          15,     1024,   0     },

    {"tcpip_thread",        16,      800,    0    },
#ifdef CONFIG_WIFI_ENABLE
    {"tcpip_thread",        16,      800,   0     },
    {"tasklet",             10,     WIFI_TASKLET_STK_SIZE,   0,		 wifi_tasklet_tcb_stk_q	 },//通过调节任务优先级平衡WIFI收发占据总CPU的比重
    {"RtmpMlmeTask",        17,     WIFI_MLME_STK_SIZE,  	 0, 	 wifi_mlme_tcb_stk_q	 },
    {"RtmpCmdQTask",        17,     WIFI_CMDQ_STK_SIZE,   	 0,  	 wifi_cmdq_tcb_stk_q	 },
    {"wl_rx_irq_thread",     7,     WIFI_RX_STK_SIZE,    	 0,  	 wifi_rx_tcb_stk_q  	 },
#endif
#ifdef CONFIG_BT_ENABLE
#if CPU_CORE_NUM > 1
    {"#C0btencry",          14,      512,   128   },
#else
    {"btencry",             14,      512,   128   },
#endif
#if CPU_CORE_NUM > 1
    {"#C0btctrler",         19,      512,   384   },
    {"#C0btstack",          18,      1024,  384   },
#else
    {"btctrler",            19,      512,   384   },
    {"btstack",             18,      768,   384   },
#endif
#endif
    {"wl80_test",            1,     2048,   64    },

    {"video_server",        16,      768,   128   },
    {"vpkg_server",         16,      512,   128   },
    {"video0_rec0",         20,      256,   128   },
    {"video0_rec1",         20,      256,   128   },
    {"video2_rec0",         20,      512,   128   },
    {"video2_rec1",         20,      512,   128   },
    {"net_video_server",    16,      256,   64    },

    {"net_avi0",            18,      512,   0     },
    {"net_avi1",            18,      512,   0     },

    {"avi0",                11,      320,   64    },
    {"jpg_dec",             10,     1024,   32    },
    {"video_dec_server",    16,     1024,   256   },
    {"vunpkg_server",       16,     1024,   128   },

#ifdef CONFIG_UI_ENABLE
    {"ui",                  21,      768,   256   },
#endif
#if defined(CONFIG_UI_ENABLE) || defined(CONFIG_STORY_LCD_TEST)
    {"lcd_task_0",			8,	  1024,	  32   },
    {"lcd_task_1",			8,	  1024,	  32   },
    {"te_task",		    	9,	  1024,	  32   },
#endif

    {0, 0},
};

#if 0
char task_designated_cpu(const char *task_name)//返回0指定CPU0运行,返回1指定CPU1运行,返回-1 CPU01都可以运行
{
    if (!strcmp(task_name, "init")) {
        return -1;//操作系统第一个任务init任务必须安装在核0,运行完会自动删除
    }
    if (!strcmp(task_name, "thread_fork_kill")) {
        return -1;
    }

    //用户在此增加指定任务

    return -1;
}
#endif


/*
 * 默认的系统事件处理函数
 * 当所有活动的app的事件处理函数都返回false时此函数会被调用
 */
void app_default_event_handler(struct sys_event *event)
{
    switch (event->type) {
    case SYS_KEY_EVENT:
        break;
    case SYS_TOUCH_EVENT:
        break;
    case SYS_DEVICE_EVENT:
        break;
    case SYS_NET_EVENT:
        break;
    case SYS_BT_EVENT:
#if (RCSP_MODE)
        if (event->from == BT_EVENT_FROM_BLE_RCSP_UPDATE) {
            struct bt_event *e = (struct device_event *)event->payload;
            JL_rcsp_update_msg_deal(NULL, e->event, e->args);
        }
#endif
        break;
    default:
        ASSERT(0, "unknow event type: %s\n", __func__);
        break;
    }
}


#ifdef CONFIG_STORY_LCD_TEST
extern void story_lcd_bb_test(void);
extern void user_lcd_init(void);
extern void lcd_test_show_color(u32 color);
extern void lcd_bl_pinstate(u8 state);

#define LCD_TEST_USE_BITBANG 0  /* 1: GPIO bit-bang（已验证能亮）；0: 硬件 SPI2 */

/* 阶段3 LCD 点亮：NT1137G0020-V2 240x296 JD9853 */
static void story_lcd_test_task(void *p)
{
    os_time_dly(100);

#if LCD_TEST_USE_BITBANG
    story_lcd_bb_test();    /* 不返回：bit-bang 初始化 + 纯色循环 */
#else
    user_lcd_init();
    lcd_bl_pinstate(1);
    printf("STORY_LCD_TEST: SPI framework init done, first RED 2s\r\n");

    /* 先整屏红停 2s，确认满屏纯色 */
    lcd_test_show_color(0xF800);
    os_time_dly(200);

    while (1) {
        lcd_test_show_color(0x07E0); os_time_dly(100);
        lcd_test_show_color(0x001F); os_time_dly(100);
        lcd_test_show_color(0xFFFF); os_time_dly(100);
        lcd_test_show_color(0xF800); os_time_dly(100);
    }
#endif
}
#endif

#ifdef CONFIG_STORY_BRINGUP
extern u32 power_key_adc_value(void);

static void story_bringup_hb(void *p)
{
    while (1) {
        printf("STORY_BRINGUP_HB KEY_ADC=%d\r\n", power_key_adc_value());
        os_time_dly(100);
    }
}
#endif

#ifdef CONFIG_STORY_AUDIO_TEST
#include "server/audio_server.h"
#include "server/server_core.h"

#define MIC_TEST_SR             16000
#define MIC_TEST_ADC_CH_BIT     0       /* MIC0 -> ADC 逻辑通道 0（PA2/PA1 差分） */
#define MIC_TEST_SEC            3
#define MIC_TEST_BUF_SIZE       (MIC_TEST_SR * 2 * MIC_TEST_SEC * 2)   /* 留 2x 余量 */

extern void dac_set_gain(u8 gain, u8 fade_opt);
extern void dac_set_volume(s16 volume);

static u8 *mic_test_buf;
static volatile u32 mic_test_len;
static u32 mic_test_play_pos;
static volatile u32 mic_test_peak;
static volatile u32 mic_test_frames;

static int mic_test_fwrite(void *file, void *data, u32 len)
{
    s16 *pcm = (s16 *)data;
    u32 peak = 0;

    (void)file;

    for (u32 i = 0; i < len / 2; i++) {
        s32 v = pcm[i];     /* s32：避免 -32768 取负溢出 */
        if (v < 0) {
            v = -v;
        }
        if (v > peak) {
            peak = v;
        }
    }
    if (peak > mic_test_peak) {
        mic_test_peak = peak;
    }

    if (mic_test_buf && mic_test_len + len <= MIC_TEST_BUF_SIZE) {
        memcpy(mic_test_buf + mic_test_len, data, len);
        mic_test_len += len;
    }
    mic_test_frames++;
    return len;
}

static int mic_test_fread(void *file, void *data, u32 len)
{
    u32 remain = mic_test_len - mic_test_play_pos;

    (void)file;

    if (remain == 0) {
        return 0;   /* EOF */
    }
    if (len > remain) {
        len = remain;
    }
    memcpy(data, mic_test_buf + mic_test_play_pos, len);
    mic_test_play_pos += len;
    return len;
}

static const struct audio_vfs_ops mic_test_vfs_ops = {
    .fwrite = mic_test_fwrite,
    .fread  = mic_test_fread,
};

/* 阶段2 mic 测试：MIC0 差分（CP=PA2 / CN=PA1）录 3s，再通过喇叭回放 */
static void story_mic_test_task(void *p)
{
    union audio_req req;

    os_time_dly(300);   /* 等系统/音频服务稳定 */

    if (!mic_test_buf) {
        mic_test_buf = malloc(MIC_TEST_BUF_SIZE);
        if (!mic_test_buf) {
            printf("STORY_MIC_TEST: no mem\r\n");
            return;
        }
    }

    while (1) {
        /******** 录音 ********/
        void *enc = server_open("audio_server", "enc");
        if (!enc) {
            printf("STORY_MIC_TEST: open enc fail\r\n");
            os_time_dly(300);
            continue;
        }

        mic_test_peak = 0;
        mic_test_frames = 0;
        mic_test_len = 0;

        memset(&req, 0, sizeof(req));
        req.enc.cmd = AUDIO_ENC_OPEN;
        req.enc.channel = 1;
        req.enc.channel_bit_map = BIT(MIC_TEST_ADC_CH_BIT);
        req.enc.volume = 100;               /* mic 增益 */
        req.enc.sample_rate = MIC_TEST_SR;
        req.enc.frame_size = MIC_TEST_SR / 100 * 4;
        req.enc.output_buf_len = req.enc.frame_size * 3;
        req.enc.format = "pcm";
        req.enc.sample_source = "mic";
        req.enc.vfs_ops = &mic_test_vfs_ops;
        req.enc.file = (FILE *)mic_test_buf;

        if (server_request(enc, AUDIO_REQ_ENC, &req)) {
            printf("STORY_MIC_TEST: enc open fail\r\n");
            server_close(enc);
            os_time_dly(300);
            continue;
        }

        printf("STORY_MIC_TEST: MIC0 (CP=PA2/CN=PA1) recording %ds...\r\n", MIC_TEST_SEC);
        for (int i = 0; i < MIC_TEST_SEC * 2; i++) {
            os_time_dly(50);    /* 500ms */
            printf("STORY_MIC_TEST: peak=%d frames=%d\r\n", mic_test_peak, mic_test_frames);
            mic_test_peak = 0;
        }

        req.enc.cmd = AUDIO_ENC_CLOSE;
        server_request(enc, AUDIO_REQ_ENC, &req);
        server_close(enc);

        printf("STORY_MIC_TEST: recorded %d bytes, play back\r\n", mic_test_len);

        /******** 回放（先强制最大增益/音量，排除音量链路问题） ********/
        if (mic_test_len) {
            void *dec = server_open("audio_server", "dec");
            if (!dec) {
                printf("STORY_MIC_TEST: open dec fail\r\n");
            } else {
                memset(&req, 0, sizeof(req));
                req.dec.cmd = AUDIO_DEC_OPEN;
                req.dec.volume = 100;
                req.dec.output_buf_len = 4 * 1024;
                req.dec.channel = 1;
                req.dec.sample_rate = MIC_TEST_SR;
                req.dec.dec_type = "pcm";
                req.dec.sample_source = "dac";
                req.dec.vfs_ops = &mic_test_vfs_ops;
                req.dec.file = (FILE *)mic_test_buf;

                if (server_request(dec, AUDIO_REQ_DEC, &req)) {
                    printf("STORY_MIC_TEST: dec open fail\r\n");
                } else {
                    mic_test_play_pos = 0;
                    req.dec.cmd = AUDIO_DEC_START;
                    server_request(dec, AUDIO_REQ_DEC, &req);

                    for (int i = 0; i < MIC_TEST_SEC * 10 + 20; i++) {
                        os_time_dly(1);
                        dac_set_gain(31, 0);
                        dac_set_volume(16384);
                    }

                    req.dec.cmd = AUDIO_DEC_STOP;
                    server_request(dec, AUDIO_REQ_DEC, &req);
                }
                server_close(dec);
            }
        }

        os_time_dly(100);   /* 停 1s 再来一轮 */
    }
}
#endif

/*
 * 应用程序主函数
 */
void app_main()
{
#ifdef CONFIG_STORY_BRINGUP
    puts("------------- wifi_story_machine app main (STAGE1 BRINGUP) -------------\n");
    os_task_create(story_bringup_hb, NULL, 10, 1024, 0, "story_bringup_hb");
#ifdef CONFIG_STORY_AUDIO_TEST
    os_task_create(story_mic_test_task, NULL, 11, 2048, 0, "story_mic_test");
#endif
#if defined(CONFIG_STORY_LCD_TEST) && !defined(CONFIG_STORY_UI)
    os_task_create(story_lcd_test_task, NULL, 12, 2048, 0, "story_lcd_test");
#endif
#ifdef CONFIG_STORY_UI
    extern int ui_demo_task_init(void);
    ui_demo_task_init();    /* DUI UI：lcd_ui_init + UIPACKRES + music_ui */
#endif
    return;
#else
    struct intent it;

    puts("------------- wifi_story_machine app main-------------\n");

    init_intent(&it);
    it.name = "app_music";
    it.action = ACTION_MUSIC_PLAY_MAIN;
    start_app(&it);

#if defined CONFIG_BT_ENABLE && !defined CONFIG_WIFI_ENABLE
    extern void bt_ble_module_init(void);
    bt_ble_module_init();
#endif
#endif
}

