/**
 * @file main.c
 * @brief EPD-nRF5 主程序入口
 * 
 * 这是一个基于 Nordic nRF5系列芯片的电子墨水屏(EPD)驱动固件。
 * 主要功能包括：
 * - BLE蓝牙低功耗通信
 * - 电子墨水屏驱动控制
 * - DFU无线固件升级
 * - 日历/图片显示模式
 * - 低功耗睡眠管理
 * 
 * 支持的芯片: nrf51822 / nrf51802 / nrf52811 / nrf52810
 * 支持的屏幕: UC8176 / UC8276 / SSD1619 / SSD1683 / JD79668
 * 
 * @author tsl0922
 * @license GPL-3.0
 */

/* Copyright (c) 2014 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

#include <stdint.h>
#include <string.h>

// Nordic BLE协议栈头文件
#include "ble.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "ble_dfu.h"          // DFU(设备固件升级)支持
#include "ble_hci.h"
#include "ble_srv_common.h"
#include "nordic_common.h"
#include "nrf.h"

// 根据芯片类型选择不同的SDK版本
#if defined(S112)
// nRF52系列使用SDH(SoftDevice Handler)架构
#include "nrf_ble_gatt.h"
#include "nrf_bootloader_info.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#else
// nRF51系列使用旧版架构
#include "fstorage.h"         // Flash存储
#include "softdevice_handler.h"
#endif

// 项目自定义头文件
#include "EPD_service.h"      // EPD蓝牙服务
#include "app_error.h"        // 错误处理
#include "app_scheduler.h"    // 事件调度器
#include "app_timer.h"        // 定时器
#include "main.h"
#include "nrf_drv_gpiote.h"   // GPIO中断
#include "nrf_drv_wdt.h"      // 看门狗
#include "nrf_log.h"          // 日志系统
#include "nrf_log_ctrl.h"
#include "nrf_power.h"
#include "nrf_pwr_mgmt.h"     // 电源管理
#if defined(S112)
#include "nrf_log_default_backends.h"
#endif

// clang-format off

/**
 * @defgroup 连接参数配置
 * @{
 */
#define CENTRAL_LINK_COUNT              0           /**< 中央设备链接数，本设备作为外设 */
#define PERIPHERAL_LINK_COUNT           1           /**< 外设链接数 */

#define DEVICE_NAME                      "NRF_EPD"  /**< 蓝牙设备名称 */
#define APP_ADV_INTERVAL                 1600        /**< 广播间隔(单位0.625ms)，1600 = 1秒 */
#define APP_ADV_TIMEOUT_IN_SECONDS       120         /**< 广播超时时间(秒) */
#define APP_TIMER_PRESCALER              0           /**< RTC1预分频器值 */
#define APP_TIMER_OP_QUEUE_SIZE          4           /**< 定时器操作队列大小 */

// 根据芯片类型定义不同的定时器tick计算方式
#if defined(S112)
#define APP_BLE_CONN_CFG_TAG            1           /**< BLE配置标签 */
#define APP_BLE_OBSERVER_PRIO           3           /**< BLE观察者优先级 */
#define TIMER_TICKS(MS) APP_TIMER_TICKS(MS)
#else
#define TIMER_TICKS(MS) APP_TIMER_TICKS(MS, APP_TIMER_PRESCALER)
// 低频时钟源配置 - 使用内部RC振荡器
#define NRF_CLOCK_LFCLKSRC      {.source        = NRF_CLOCK_LF_SRC_RC,               \
                                 .rc_ctiv       = 16,                                \
                                 .rc_temp_ctiv  = 2,                                 \
                                 .xtal_accuracy = 0}
#endif
/** @} */

/**
 * @defgroup BLE连接参数
 * @{
 */
#define MIN_CONN_INTERVAL                MSEC_TO_UNITS(7.5, UNIT_1_25_MS)  /**< 最小连接间隔7.5ms */
#define MAX_CONN_INTERVAL                MSEC_TO_UNITS(30, UNIT_1_25_MS)   /**< 最大连接间隔30ms */
#define SLAVE_LATENCY                    6                                 /**< 从设备延迟 */
#define CONN_SUP_TIMEOUT                 MSEC_TO_UNITS(430, UNIT_10_MS)    /**< 连接监督超时430ms */
#define FIRST_CONN_PARAMS_UPDATE_DELAY   TIMER_TICKS(5000)                 /**< 首次连接参数更新延迟5秒 */
#define NEXT_CONN_PARAMS_UPDATE_DELAY    TIMER_TICKS(30000)                /**< 后续更新间隔30秒 */
#define MAX_CONN_PARAMS_UPDATE_COUNT     3                                 /**< 最大更新尝试次数 */
/** @} */

/**
 * @defgroup 调度器和定时器配置
 * @{
 */
#define SCHED_MAX_EVENT_DATA_SIZE       EPD_GUI_SCHD_EVENT_DATA_SIZE       /**< 调度器事件最大数据大小 */
#define SCHED_QUEUE_SIZE                10                                 /**< 调度器队列大小 */

#define CLOCK_TIMER_INTERVAL             TIMER_TICKS(1000)                 /**< 时钟定时器间隔1秒 */

#define DEAD_BEEF                        0xDEADBEEF                        /**< 错误代码，用于栈转储识别 */
/** @} */

/**
 * @brief 全局变量定义
 */
#if defined(S112)
NRF_BLE_GATT_DEF(m_gatt);                                                       /**< GATT模块实例 */
BLE_ADVERTISING_DEF(m_advertising);                                             /**< 广播模块实例 */
#else
static ble_dfu_t                         m_dfus;                               /**< DFU服务结构体 */
#endif
static uint16_t                          m_conn_handle = BLE_CONN_HANDLE_INVALID;  /**< 当前连接句柄 */
static ble_uuid_t                        m_adv_uuids[] = {{BLE_UUID_EPD_SVC, \
                                                           EPD_SVC_UUID_TYPE}};  /**< 广播UUID */

BLE_EPD_DEF(m_epd);                                                             /**< EPD服务实例 */
static uint32_t                          m_timestamp = 1735689600;              /**< 当前时间戳(Unix时间) */
APP_TIMER_DEF(m_clock_timer_id);                                                /**< 时钟定时器ID */
static nrf_drv_wdt_channel_id            m_wdt_channel_id;                      /**< 看门狗通道ID */
static uint32_t                          m_wdt_last_feed_time = 0;              /**< 上次喂狗时间 */
static uint32_t                          m_resetreas;                           /**< 复位原因 */
// clang-format on

/**
 * @brief SoftDevice断言回调函数
 * 
 * 当SoftDevice内部发生断言错误时调用此函数。
 * 这通常表示严重的协议栈错误。
 * 
 * @param[in] line_num   发生断言的代码行号
 * @param[in] p_file_name 发生断言的文件名
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t* p_file_name) {
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

/**
 * @brief 获取当前时间戳
 * @return 当前Unix时间戳(秒)
 */
uint32_t timestamp(void) { return m_timestamp; }

/**
 * @brief 设置时间戳
 * 
 * 设置系统当前时间，并重新启动时钟定时器。
 * 用于从蓝牙同步时间或手动设置时间。
 * 
 * @param[in] timestamp 新的Unix时间戳
 */
void set_timestamp(uint32_t timestamp) {
    app_timer_stop(m_clock_timer_id);
    m_timestamp = timestamp;
    app_timer_start(m_clock_timer_id, CLOCK_TIMER_INTERVAL, NULL);
}

/**
 * @brief 喂看门狗
 * 
 * 定期喂看门狗防止系统复位。
 * 每30秒喂一次，避免过于频繁。
 */
void app_feed_wdt(void) {
    if (m_timestamp - m_wdt_last_feed_time >= 30) {
        NRF_LOG_DEBUG("Feed WDT\n");
        nrf_drv_wdt_channel_feed(m_wdt_channel_id);
        m_wdt_last_feed_time = m_timestamp;
    }
}

/**
 * @defgroup DFU(设备固件升级)相关函数
 * @{
 */

#if defined(S112)
/**
 * @brief nRF52 DFU状态观察者
 * 
 * 监听SoftDevice状态变化，在禁用SoftDevice时准备进入bootloader
 */
static void buttonless_dfu_sdh_state_observer(nrf_sdh_state_evt_t state, void* p_context) {
    if (state == NRF_SDH_EVT_STATE_DISABLED) {
        // SoftDevice已被禁用，通知bootloader跳过CRC检查
        nrf_power_gpregret2_set(BOOTLOADER_DFU_SKIP_CRC);
        // 进入系统关闭模式
        nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_SYSOFF);
    }
}

/* 注册SDH状态观察者 */
NRF_SDH_STATE_OBSERVER(m_buttonless_dfu_state_obs, 0) = {
    .handler = buttonless_dfu_sdh_state_observer,
};

/**
 * @brief 获取广播配置
 * @param[out] p_config 广播配置结构体
 */
static void advertising_config_get(ble_adv_modes_config_t* p_config) {
    memset(p_config, 0, sizeof(ble_adv_modes_config_t));
    p_config->ble_adv_fast_enabled = true;
    p_config->ble_adv_fast_interval = APP_ADV_INTERVAL;
    p_config->ble_adv_fast_timeout = APP_ADV_TIMEOUT_IN_SECONDS * 100;
}

/**
 * @brief nRF52 DFU事件处理
 * 
 * 处理DFU相关事件，如准备进入bootloader、进入失败等
 * 
 * @param[in] event DFU事件类型
 */
static void ble_dfu_evt_handler(ble_dfu_buttonless_evt_type_t event) {
    switch (event) {
        case BLE_DFU_EVT_BOOTLOADER_ENTER_PREPARE: {
            NRF_LOG_INFO("Device is preparing to enter bootloader mode.");
            // 禁用断开连接后的广播
            ble_adv_modes_config_t config;
            advertising_config_get(&config);
            config.ble_adv_on_disconnect_disabled = true;
            ble_advertising_modes_config_set(&m_advertising, &config);
            // 断开当前连接
            APP_ERROR_CHECK(sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION));
            break;
        }
        case BLE_DFU_EVT_BOOTLOADER_ENTER:
            NRF_LOG_INFO("Device will enter bootloader mode.");
            break;
        case BLE_DFU_EVT_BOOTLOADER_ENTER_FAILED:
            NRF_LOG_ERROR("Request to enter bootloader mode failed asynchroneously.");
            APP_ERROR_CHECK(false);
            break;
        case BLE_DFU_EVT_RESPONSE_SEND_ERROR:
            NRF_LOG_ERROR("Request to send a response to client failed.");
            APP_ERROR_CHECK(false);
            break;
        default:
            NRF_LOG_ERROR("Unknown event from ble_dfu_buttonless.");
            break;
    }
}
#else
/**
 * @brief nRF51 DFU事件处理
 * 
 * nRF51使用不同的DFU库，事件处理方式略有不同
 * 
 * @param[in] p_dfu  DFU服务指针
 * @param[in] p_evt  DFU事件
 */
static void ble_dfu_evt_handler(ble_dfu_t* p_dfu, ble_dfu_evt_t* p_evt) {
    switch (p_evt->type) {
        case BLE_DFU_EVT_INDICATION_DISABLED:
            NRF_LOG_INFO("Indication for BLE_DFU is disabled\r\n");
            break;
        case BLE_DFU_EVT_INDICATION_ENABLED:
            NRF_LOG_INFO("Indication for BLE_DFU is enabled\r\n");
            break;
        case BLE_DFU_EVT_ENTERING_BOOTLOADER:
            NRF_LOG_INFO("Device is entering bootloader mode!\r\n");
            break;
        default:
            NRF_LOG_INFO("Unknown event from ble_dfu\r\n");
            break;
    }
}
#endif
/** @} */

/**
 * @brief 时钟定时器超时处理函数
 * 
 * 每秒执行一次，更新时间戳并触发EPD显示更新
 * 
 * @param[in] p_context 上下文参数(未使用)
 */
static void clock_timer_timeout_handler(void* p_context) {
    UNUSED_PARAMETER(p_context);
    m_timestamp++;
    // 通知EPD服务进行定时更新
    ble_epd_on_timer(&m_epd, m_timestamp, false);
}

/**
 * @brief 初始化事件调度器
 * 
 * 调度器用于在主循环中异步处理事件
 */
static void scheduler_init(void) { 
    APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE); 
}

/**
 * @brief 初始化定时器模块
 * 
 * 创建并配置应用定时器
 */
static void timers_init(void) {
#if defined(S112)
    APP_ERROR_CHECK(app_timer_init());
#else
    APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, false);
#endif
    // 创建时钟定时器
    APP_ERROR_CHECK(app_timer_create(&m_clock_timer_id, APP_TIMER_MODE_REPEATED, clock_timer_timeout_handler));
}

/**
 * @brief 启动应用定时器
 */
static void application_timers_start(void) {
    APP_ERROR_CHECK(app_timer_start(m_clock_timer_id, CLOCK_TIMER_INTERVAL, NULL));
}

/**
 * @brief 进入深度睡眠模式
 * 
 * 此函数不会返回。系统将进入System OFF模式，
 * 只能通过GPIO唤醒或复位唤醒。
 */
void sleep_mode_enter(void) {
    NRF_LOG_DEBUG("Entering deep sleep mode\n");
    NRF_LOG_FINAL_FLUSH();
    nrf_delay_ms(100);
    // 准备EPD进入睡眠
    ble_epd_sleep_prepare(&m_epd);
    // 关闭系统
    nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_SYSOFF);
}

/**
 * @brief 初始化BLE服务
 * 
 * 初始化EPD服务和DFU服务
 */
static void services_init(void) {
    // 初始化EPD服务
    memset(&m_epd, 0, sizeof(ble_epd_t));
    APP_ERROR_CHECK(ble_epd_init(&m_epd));

#if defined(S112)
    // nRF52使用无按钮DFU
    ble_dfu_buttonless_init_t dfus_init = {0};
    dfus_init.evt_handler = ble_dfu_evt_handler;
    APP_ERROR_CHECK(ble_dfu_buttonless_init(&dfus_init));
#else
    // nRF51使用传统DFU
    ble_dfu_init_t dfus_init;
    memset(&dfus_init, 0, sizeof(dfus_init));
    dfus_init.evt_handler = ble_dfu_evt_handler;
    dfus_init.ctrl_point_security_req_write_perm = SEC_SIGNED;
    dfus_init.ctrl_point_security_req_cccd_write_perm = SEC_SIGNED;
    APP_ERROR_CHECK(ble_dfu_init(&m_dfus, &dfus_init));
#endif
}

/**
 * @brief 初始化GAP(通用访问规范)
 * 
 * 设置设备名称、连接参数等GAP参数
 */
static void gap_params_init(void) {
    char device_name[20];
    ble_gap_addr_t addr;
    ble_gap_conn_params_t gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    // 设置安全模式为开放
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    // 获取并打印蓝牙MAC地址
#if defined(S112)
    APP_ERROR_CHECK(sd_ble_gap_addr_get(&addr));
#else
    APP_ERROR_CHECK(sd_ble_gap_address_get(&addr));
#endif
    NRF_LOG_INFO("Bluetooth MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                 addr.addr[5], addr.addr[4], addr.addr[3],
                 addr.addr[2], addr.addr[1], addr.addr[0]);

    // 设置设备名称，包含MAC地址后两位
    snprintf(device_name, 20, "%s_%02X%02X", DEVICE_NAME, addr.addr[1], addr.addr[0]);
    APP_ERROR_CHECK(sd_ble_gap_device_name_set(&sec_mode, (const uint8_t*)device_name, strlen(device_name)));

    // 设置连接参数
    memset(&gap_conn_params, 0, sizeof(gap_conn_params));
    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout = CONN_SUP_TIMEOUT;
    APP_ERROR_CHECK(sd_ble_gap_ppcp_set(&gap_conn_params));
}

/**
 * @brief 连接参数事件处理
 * 
 * 当连接参数协商失败时断开连接
 * 
 * @param[in] p_evt 连接参数事件
 */
static void on_conn_params_evt(ble_conn_params_evt_t* p_evt) {
    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED) {
        APP_ERROR_CHECK(sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE));
    }
}

/**
 * @brief 连接参数错误处理
 * @param[in] nrf_error 错误代码
 */
static void conn_params_error_handler(uint32_t nrf_error) { 
    APP_ERROR_HANDLER(nrf_error); 
}

/**
 * @brief 初始化连接参数模块
 */
static void conn_params_init(void) {
    ble_conn_params_init_t cp_init;
    memset(&cp_init, 0, sizeof(cp_init));
    cp_init.p_conn_params = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail = false;
    cp_init.evt_handler = on_conn_params_evt;
    cp_init.error_handler = conn_params_error_handler;
    APP_ERROR_CHECK(ble_conn_params_init(&cp_init));
}

/**
 * @brief 开始广播
 */
static void advertising_start(void) {
    NRF_LOG_INFO("advertising start\n");
#if defined(S112)
    APP_ERROR_CHECK(ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST));
#else
    APP_ERROR_CHECK(ble_advertising_start(BLE_ADV_MODE_FAST));
#endif
}

/**
 * @brief GPIO中断事件处理
 * 
 * 唤醒引脚触发的中断处理函数
 * 
 * @param[in] pin    触发中断的引脚
 * @param[in] action 触发动作
 */
void gpiote_evt_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
    NRF_LOG_DEBUG("pin: %d, event: %d\n", pin, action);
    // 禁用中断并清理
    nrf_drv_gpiote_in_event_disable(pin);
    nrf_drv_gpiote_in_uninit(pin);
    nrf_drv_gpiote_uninit();
    // 唤醒时闪烁LED
    EPD_LED_BLINK();
    advertising_start();
}

/**
 * @brief 配置唤醒引脚
 * 
 * 设置GPIO中断，用于从深度睡眠中唤醒
 * 
 * @param[in] pin 唤醒引脚号
 */
static void setup_wakeup_pin(nrf_drv_gpiote_pin_t pin) {
    NRF_LOG_DEBUG("Setting up wakeup pin\n");
    APP_ERROR_CHECK(nrf_drv_gpiote_init());
    // 配置为低电平到高电平触发
    nrf_drv_gpiote_in_config_t config = GPIOTE_CONFIG_IN_SENSE_LOTOHI(false);
    APP_ERROR_CHECK(nrf_drv_gpiote_in_init(pin, &config, gpiote_evt_handler));
    nrf_drv_gpiote_in_event_enable(pin, true);
}

/**
 * @brief 广播事件处理
 * 
 * 处理广播状态变化事件
 * 
 * @param[in] ble_adv_evt 广播事件类型
 */
static void on_adv_evt(ble_adv_evt_t ble_adv_evt) {
    switch (ble_adv_evt) {
        case BLE_ADV_EVT_FAST:
            // 快速广播开始
            break;
        case BLE_ADV_EVT_IDLE:
            // 广播超时
            NRF_LOG_INFO("advertising timeout\n");
            if (m_epd.config.wakeup_pin != 0xFF) {
                // 配置了唤醒引脚
                if (m_epd.config.display_mode == MODE_PICTURE)
                    sleep_mode_enter();  // 图片模式进入睡眠
                else
                    setup_wakeup_pin(m_epd.config.wakeup_pin);  // 日历模式等待唤醒
            } else {
                advertising_start();  // 继续广播
            }
            break;
        default:
            break;
    }
}

/**
 * @brief BLE事件处理
 * 
 * 处理各种BLE协议栈事件
 * 
 * @param[in] p_ble_evt BLE事件
 */
static void on_ble_evt(ble_evt_t* p_ble_evt) {
    switch (p_ble_evt->header.evt_id) {
        case BLE_GAP_EVT_CONNECTED:
            NRF_LOG_INFO("CONNECTED\n");
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            break;
        case BLE_GAP_EVT_DISCONNECTED:
            NRF_LOG_INFO("DISCONNECTED\n");
            m_conn_handle = BLE_CONN_HANDLE_INVALID;
#if !defined(S112)
            advertising_start();
#endif
            break;
#if defined(S112)
        case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
            // nRF52支持PHY更新
            NRF_LOG_DEBUG("PHY update request.");
            ble_gap_phys_t const phys = {
                .rx_phys = BLE_GAP_PHY_AUTO,
                .tx_phys = BLE_GAP_PHY_AUTO,
            };
            APP_ERROR_CHECK(sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys));
        } break;
#endif
        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            // 不支持配对
            APP_ERROR_CHECK(sd_ble_gap_sec_params_reply(m_conn_handle, 
                BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL));
            break;
        case BLE_GATTS_EVT_SYS_ATTR_MISSING:
            // 没有存储系统属性
            APP_ERROR_CHECK(sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0));
            break;
        case BLE_GATTC_EVT_TIMEOUT:
            // GATT客户端超时
            APP_ERROR_CHECK(sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle, 
                BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION));
            break;
        case BLE_GATTS_EVT_TIMEOUT:
            // GATT服务器超时
            APP_ERROR_CHECK(sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle, 
                BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION));
            break;
        default:
            break;
    }
}

#if defined(S112)
/**
 * @brief nRF52 BLE事件处理
 * 
 * @param[in] p_ble_evt BLE事件
 * @param[in] p_context 上下文
 */
static void ble_evt_handler(ble_evt_t const* p_ble_evt, void* p_context) {
    UNUSED_PARAMETER(p_context);
    on_ble_evt((ble_evt_t*)p_ble_evt);
}
#else
/**
 * @brief nRF51 BLE事件分发
 * 
 * 将BLE事件分发到各个模块
 * 
 * @param[in] p_ble_evt BLE事件
 */
static void ble_evt_dispatch(ble_evt_t* p_ble_evt) {
    ble_conn_params_on_ble_evt(p_ble_evt);
    ble_epd_on_ble_evt(&m_epd, p_ble_evt);
    on_ble_evt(p_ble_evt);
    ble_advertising_on_ble_evt(p_ble_evt);
    ble_dfu_on_ble_evt(&m_dfus, p_ble_evt);
}

/**
 * @brief nRF51系统事件分发
 * 
 * @param[in] sys_evt 系统事件
 */
static void sys_evt_dispatch(uint32_t sys_evt) {
    fs_sys_event_handler(sys_evt);
    ble_advertising_on_sys_evt(sys_evt);
}
#endif

/**
 * @brief 初始化BLE协议栈
 * 
 * 初始化SoftDevice并配置BLE
 */
static void ble_stack_init(void) {
#if defined(S112)
    // nRF52初始化流程
    APP_ERROR_CHECK(nrf_sdh_enable_request());
    uint32_t ram_start = 0;
    APP_ERROR_CHECK(nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start));
    APP_ERROR_CHECK(nrf_sdh_ble_enable(&ram_start));
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
#else
    // nRF51初始化流程
    nrf_clock_lf_cfg_t clock_lf_cfg = NRF_CLOCK_LFCLKSRC;
    SOFTDEVICE_HANDLER_INIT(&clock_lf_cfg, NULL);
    ble_enable_params_t ble_enable_params;
    APP_ERROR_CHECK(softdevice_enable_get_default_config(CENTRAL_LINK_COUNT, PERIPHERAL_LINK_COUNT, &ble_enable_params));
    ble_enable_params.common_enable_params.vs_uuid_count = 2;
    CHECK_RAM_START_ADDR(CENTRAL_LINK_COUNT, PERIPHERAL_LINK_COUNT);
    APP_ERROR_CHECK(softdevice_enable(&ble_enable_params));
    APP_ERROR_CHECK(softdevice_ble_evt_handler_set(ble_evt_dispatch));
    APP_ERROR_CHECK(softdevice_sys_evt_handler_set(sys_evt_dispatch));
#endif
}

#if defined(S112)
/**
 * @brief GATT事件处理(nRF52)
 * 
 * @param[in] p_gatt  GATT实例
 * @param[in] p_evt   GATT事件
 */
void gatt_evt_handler(nrf_ble_gatt_t* p_gatt, nrf_ble_gatt_evt_t const* p_evt) {
    if ((m_conn_handle == p_evt->conn_handle) && (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED)) {
        // MTU更新，计算最大数据长度
        m_epd.max_data_len = p_evt->params.att_mtu_effective - 3;
        NRF_LOG_INFO("Data len is set to 0x%X(%d)", m_epd.max_data_len, m_epd.max_data_len);
    }
    NRF_LOG_DEBUG("ATT MTU exchange completed. central 0x%x peripheral 0x%x", 
                  p_gatt->att_mtu_desired_central, p_gatt->att_mtu_desired_periph);
}

/**
 * @brief 初始化GATT库(nRF52)
 */
void gatt_init(void) {
    APP_ERROR_CHECK(nrf_ble_gatt_init(&m_gatt, gatt_evt_handler));
    APP_ERROR_CHECK(nrf_ble_gatt_att_mtu_periph_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE));
}
#else
/**
 * @brief 设置BLE选项(nRF51)
 * 
 * 设置连接带宽为高
 */
static void ble_options_set(void) {
    ble_opt_t ble_opt;
    memset(&ble_opt, 0, sizeof(ble_opt));
    ble_opt.common_opt.conn_bw.role = BLE_GAP_ROLE_PERIPH;
    ble_opt.common_opt.conn_bw.conn_bw.conn_bw_rx = BLE_CONN_BW_HIGH;
    ble_opt.common_opt.conn_bw.conn_bw.conn_bw_tx = BLE_CONN_BW_HIGH;
    APP_ERROR_CHECK(sd_ble_opt_set(BLE_COMMON_OPT_CONN_BW, &ble_opt));
}
#endif

/**
 * @brief 初始化广播功能
 */
static void advertising_init(void) {
#if defined(S112)
    // nRF52广播初始化
    ble_advertising_init_t init;
    memset(&init, 0, sizeof(init));
    init.advdata.name_type = BLE_ADVDATA_FULL_NAME;
    init.advdata.include_appearance = false;
    init.advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;
    init.srdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    init.srdata.uuids_complete.p_uuids = m_adv_uuids;
    init.config.ble_adv_fast_enabled = true;
    init.config.ble_adv_fast_interval = APP_ADV_INTERVAL;
    init.config.ble_adv_fast_timeout = APP_ADV_TIMEOUT_IN_SECONDS * 100;
    init.evt_handler = on_adv_evt;
    APP_ERROR_CHECK(ble_advertising_init(&m_advertising, &init));
    ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
#else
    // nRF51广播初始化
    ble_advdata_t advdata;
    ble_advdata_t scanrsp;
    ble_adv_modes_config_t options;
    memset(&advdata, 0, sizeof(advdata));
    advdata.name_type = BLE_ADVDATA_FULL_NAME;
    advdata.include_appearance = false;
    advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;
    memset(&scanrsp, 0, sizeof(scanrsp));
    scanrsp.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    scanrsp.uuids_complete.p_uuids = m_adv_uuids;
    memset(&options, 0, sizeof(options));
    options.ble_adv_fast_enabled = true;
    options.ble_adv_fast_interval = APP_ADV_INTERVAL;
    options.ble_adv_fast_timeout = APP_ADV_TIMEOUT_IN_SECONDS;
    APP_ERROR_CHECK(ble_advertising_init(&advdata, &scanrsp, &options, on_adv_evt, NULL));
#endif
}

/**
 * @brief 初始化日志模块
 */
static void log_init(void) {
    APP_ERROR_CHECK(NRF_LOG_INIT(timestamp));
#if defined(S112)
    NRF_LOG_DEFAULT_BACKENDS_INIT();
#endif
}

/**
 * @brief 初始化电源管理
 */
static void power_management_init(void) {
#if defined(S112)
    APP_ERROR_CHECK(nrf_pwr_mgmt_init());
#else
    APP_ERROR_CHECK(nrf_pwr_mgmt_init(APP_TIMER_TICKS(1000, APP_TIMER_PRESCALER)));
#endif
}

/**
 * @brief 空闲状态处理
 * 
 * 主循环中的空闲处理，喂狗并进入低功耗模式
 */
static void idle_state_handle(void) {
    app_feed_wdt();
    if (NRF_LOG_PROCESS() == false) nrf_pwr_mgmt_run();
}

/**
 * @brief 看门狗事件处理
 * 
 * 看门狗超时前的中断处理
 */
void wdt_event_handler(void) {
    // 注意：在看门狗中断中最多只能停留两个32768Hz时钟周期
    NRF_LOG_ERROR("WDT Rest!\r\n");
    NRF_LOG_FINAL_FLUSH();
}

/**
 * @brief 应用程序主入口
 * 
 * 系统启动后首先执行的函数
 */
int main(void) {
    // 初始化日志
    log_init();
    
    // 保存并清除复位原因
    m_resetreas = NRF_POWER->RESETREAS;
    NRF_POWER->RESETREAS |= NRF_POWER->RESETREAS;
    NRF_LOG_DEBUG("== RESET REASON: %d ===\n", m_resetreas);
    NRF_LOG_DEBUG("init..\n");

    // 配置看门狗
    nrf_drv_wdt_config_t config = NRF_DRV_WDT_DEAFULT_CONFIG;
    APP_ERROR_CHECK(nrf_drv_wdt_init(&config, wdt_event_handler));
    APP_ERROR_CHECK(nrf_drv_wdt_channel_alloc(&m_wdt_channel_id));
    nrf_drv_wdt_enable();

    // 初始化各模块
    timers_init();
    power_management_init();
    ble_stack_init();
    scheduler_init();
    gap_params_init();
#if defined(S112)
    gatt_init();
    ble_dfu_buttonless_async_svci_init();
#else
    ble_options_set();
#endif
    services_init();
    advertising_init();
    conn_params_init();

    NRF_LOG_DEBUG("start..\n");

    // 启动执行
    application_timers_start();
    advertising_start();

    NRF_LOG_DEBUG("done.\n");

    // 根据复位原因决定显示模式
    if (m_resetreas & NRF_POWER_RESETREAS_DOG_MASK) {
        // 看门狗复位，切换到日历模式
        m_epd.config.display_mode = MODE_CALENDAR;
        ble_epd_on_timer(&m_epd, 0, true);
    } else {
        // 正常启动
        ble_epd_on_timer(&m_epd, m_timestamp, true);
    }

    // 主循环
    for (;;) {
        app_sched_execute();      // 执行调度器中的事件
        idle_state_handle();       // 空闲处理
    }
}
