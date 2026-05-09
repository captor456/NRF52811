/**
 * @file EPD_service_annotated.h
 * @brief EPD蓝牙服务头文件 - 带详细中文注释
 * 
 * 本文件定义了EPD设备的BLE GATT服务。
 * 通过蓝牙提供以下功能：
 * - 屏幕引脚配置
 * - 显示控制(初始化/清屏/刷新)
 * - 图像数据传输
 * - 时间和日历设置
 * - 系统控制(复位/睡眠)
 * 
 * @author tsl0922
 * @license GPL-3.0
 */

/* Copyright (c) 2012 Nordic Semiconductor. All Rights Reserved.
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

#ifndef __EPD_SERVICE_H__
#define __EPD_SERVICE_H__

#include <inttypes.h>
#include <stdbool.h>

#include "ble.h"
#include "ble_srv_common.h"
#if defined(S112)
#include "nrf_sdh_ble.h"
#endif
#include "EPD_config.h"
#include "EPD_driver.h"
#include "GUI.h"
#include "sdk_config.h"

/**
 * @brief 定义ble_epd实例的宏
 * 
 * 根据芯片类型使用不同的定义方式
 * 
 * @param[in] _name 实例名称
 */
#if defined(S112)
// nRF52系列BLE事件处理函数声明
void ble_epd_evt_handler(ble_evt_t const* p_ble_evt, void* p_context);

#define BLE_EPD_BLE_OBSERVER_PRIO 2
#define BLE_EPD_DEF(_name)  \
    static ble_epd_t _name; \
    NRF_SDH_BLE_OBSERVER(_name##_obs, BLE_EPD_BLE_OBSERVER_PRIO, ble_epd_evt_handler, &_name)
#else
// nRF51系列简化定义
#define BLE_EPD_DEF(_name) static ble_epd_t _name;
#endif

/**
 * @brief 应用程序版本号
 * 
 * 版本号格式：0xAB，A为主版本，B为次版本
 * 例如：0x19 表示 v1.9
 */
#define APP_VERSION 0x19

/**
 * @brief EPD服务UUID定义
 * 
 * 使用128位UUID，基础UUID为：62753622-D828-918D-FB46-B6C11C675AEC
 */
#define BLE_UUID_EPD_SVC_BASE \
    {{0XEC, 0X5A, 0X67, 0X1C, 0XC1, 0XB6, 0X46, 0XFB, 0X8D, 0X91, 0X28, 0XD8, 0X22, 0X36, 0X75, 0X62}}
#define BLE_UUID_EPD_SVC 0x0001    /**< EPD服务UUID (16位) */
#define BLE_UUID_EPD_CHAR 0x0002   /**< EPD特征值UUID */
#define BLE_UUID_APP_VER 0x0003    /**< 应用版本特征值UUID */

#define EPD_SVC_UUID_TYPE BLE_UUID_TYPE_VENDOR_BEGIN  /**< UUID类型 */

/**
 * @brief 最大数据传输长度
 * 
 * MTU默认大小为23字节，减去3字节ATT头，有效数据为20字节
 * nRF52支持更大的MTU
 */
#if defined(S112)
#define BLE_EPD_MAX_DATA_LEN (NRF_SDH_BLE_GATT_MAX_MTU_SIZE - 3)
#else
#define BLE_EPD_MAX_DATA_LEN \
    (GATT_MTU_SIZE_DEFAULT - 3)
#endif

/**
 * @defgroup EPD服务命令ID
 * 
 * 定义了通过BLE发送的控制命令
 * @{
 */
enum EPD_CMDS {
    // 屏幕控制命令 (0x00-0x0F)
    EPD_CMD_SET_PINS = 0x00,     /**< 设置EPD引脚映射 */
    EPD_CMD_INIT = 0x01,         /**< 初始化EPD显示驱动 */
    EPD_CMD_CLEAR = 0x02,        /**< 清屏 */
    EPD_CMD_SEND_COMMAND = 0x03, /**< 发送原始命令到EPD */
    EPD_CMD_SEND_DATA = 0x04,    /**< 发送原始数据到EPD */
    EPD_CMD_REFRESH = 0x05,      /**< 刷新显示 */
    EPD_CMD_SLEEP = 0x06,        /**< EPD进入睡眠模式 */

    // 时间和日历命令 (0x20-0x2F)
    EPD_CMD_SET_TIME = 0x20,       /**< 设置Unix时间戳 */
    EPD_CMD_SET_WEEK_START = 0x21, /**< 设置星期起始日 (0:周日, 1:周一, ...) */

    // 图像传输命令 (0x30-0x3F)
    EPD_CMD_WRITE_IMAGE = 0x30, /**< 写入图像数据到EPD RAM */

    // 系统控制命令 (0x90-0x9F)
    EPD_CMD_SET_CONFIG = 0x90, /**< 设置完整EPD配置 */
    EPD_CMD_SYS_RESET = 0x91,  /**< MCU复位 */
    EPD_CMD_SYS_SLEEP = 0x92,  /**< MCU进入睡眠模式 */
    EPD_CMD_CFG_ERASE = 0x99,  /**< 擦除配置并复位 */
};
/** @} */

/**
 * @brief EPD服务结构体
 * 
 * 包含EPD服务的所有状态信息
 */
typedef struct {
    uint16_t service_handle;      /**< EPD服务句柄(由SoftDevice提供) */
    ble_gatts_char_handles_t
        char_handles;             /**< EPD特征值句柄 */
    ble_gatts_char_handles_t
        app_ver_handles;          /**< 应用版本特征值句柄 */
    uint16_t conn_handle;         /**< 当前连接句柄，未连接时为BLE_CONN_HANDLE_INVALID */
    uint16_t max_data_len;        /**< 可向对端传输的最大数据长度 */
    bool is_notification_enabled; /**< 对端是否启用了通知 */
    epd_model_t* epd;             /**< 当前EPD型号指针 */
    epd_config_t config;          /**< EPD配置 */
} ble_epd_t;

/**
 * @brief GUI更新事件结构体
 * 
 * 用于调度器传递GUI更新事件
 */
typedef struct {
    ble_epd_t* p_epd;      /**< EPD服务指针 */
    uint32_t timestamp;    /**< 时间戳 */
} epd_gui_update_event_t;

/**
 * @brief GUI调度器事件数据大小
 */
#define EPD_GUI_SCHD_EVENT_DATA_SIZE sizeof(epd_gui_update_event_t)

/**
 * @brief 准备进入睡眠模式
 * 
 * 在系统进入深度睡眠前调用，保存EPD状态
 * 
 * @param[in] p_epd EPD服务结构体指针
 */
void ble_epd_sleep_prepare(ble_epd_t* p_epd);

/**
 * @brief 初始化EPD服务
 * 
 * 初始化BLE GATT服务和特征值
 * 
 * @param[out] p_epd EPD服务结构体指针，由应用程序提供
 * 
 * @retval NRF_SUCCESS 服务初始化成功
 * @retval NRF_ERROR_NULL 指针参数为空
 */
uint32_t ble_epd_init(ble_epd_t* p_epd);

/**
 * @brief 处理EPD服务的BLE事件
 * 
 * 应用程序需要在收到SoftDevice事件时调用此函数
 * 
 * @param[in] p_epd     EPD服务结构体指针
 * @param[in] p_ble_evt 从SoftDevice收到的事件
 */
void ble_epd_on_ble_evt(ble_epd_t* p_epd, ble_evt_t* p_ble_evt);

/**
 * @brief 发送字符串到对端
 * 
 * 将字符串作为RX特征值通知发送
 * 
 * @param[in] p_epd    EPD服务结构体指针
 * @param[in] p_string 要发送的字符串
 * @param[in] length   字符串长度
 * 
 * @retval NRF_SUCCESS 发送成功
 * @retval 其他        错误码
 */
uint32_t ble_epd_string_send(ble_epd_t* p_epd, uint8_t* p_string, uint16_t length);

/**
 * @brief 定时器回调处理
 * 
 * 每秒由时钟定时器调用，更新显示
 * 
 * @param[in] p_epd        EPD服务结构体指针
 * @param[in] timestamp    当前时间戳
 * @param[in] force_update 是否强制更新
 */
void ble_epd_on_timer(ble_epd_t* p_epd, uint32_t timestamp, bool force_update);

#endif  // EPD_BLE_H__

/** @} */
