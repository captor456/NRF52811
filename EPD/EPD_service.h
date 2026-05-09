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

/**
 * @file EPD_service.h
 * @brief EPD蓝牙BLE服务头文件
 *
 * 本文件定义了EPD（电子墨水屏）的BLE GATT服务接口。
 * 通过蓝牙BLE协议，客户端（如手机APP）可以向EPD设备发送控制命令，
 * 实现EPD的初始化、清屏、刷新、睡眠、时间设置、图片写入、配置管理等功能。
 *
 * 主要包含：
 * - BLE服务宏定义（支持nRF52811/nRF52810 S112协议栈和nRF51简化协议栈）
 * - 自定义128位服务UUID和特征值UUID
 * - EPD命令枚举（EPD_CMDS）
 * - BLE服务结构体（ble_epd_t）和GUI更新事件结构体（epd_gui_update_event_t）
 * - 所有服务函数的声明
 */

#ifndef __EPD_SERVICE_H__
#define __EPD_SERVICE_H__

#include <inttypes.h>
#include <stdbool.h>

#include "ble.h"
#include "ble_srv_common.h"
#if defined(S112)
/* S112是nRF52811/nRF52810的蓝牙低功耗协议栈（SoftDevice），
 * 需要使用SDH（SoftDevice Handler）观察者模式来接收BLE事件 */
#include "nrf_sdh_ble.h"
#endif
#include "EPD_config.h"
#include "EPD_driver.h"
#include "GUI.h"
#include "sdk_config.h"

/**
 * @brief BLE_EPD_DEF宏 - 定义一个EPD BLE服务实例
 *
 * 根据不同的平台/协议栈，采用不同的实现方式：
 * - nRF52系列（S112协议栈）：使用NRF_SDH_BLE_OBSERVER宏注册BLE事件观察者，
 *   当BLE事件发生时自动调用 ble_epd_evt_handler 回调函数处理事件。
 *   观察者优先级为 BLE_EPD_BLE_OBSERVER_PRIO（值为2）。
 * - nRF51系列（简化协议栈）：仅定义一个静态的 ble_epd_t 结构体实例，
 *   BLE事件需要由应用程序手动调用 ble_epd_on_ble_evt 来分发。
 *
 * @param _name 实例名称，用于生成变量名和观察者名称
 */
#if defined(S112)
void ble_epd_evt_handler(ble_evt_t const* p_ble_evt, void* p_context);

#define BLE_EPD_BLE_OBSERVER_PRIO 2  /**< BLE事件观察者优先级，数值越小优先级越高 */
#define BLE_EPD_DEF(_name)  \
    static ble_epd_t _name; \
    NRF_SDH_BLE_OBSERVER(_name##_obs, BLE_EPD_BLE_OBSERVER_PRIO, ble_epd_evt_handler, &_name)
#else
#define BLE_EPD_DEF(_name) static ble_epd_t _name;  /**< nRF51简化定义，仅创建静态实例 */
#endif

/** @brief 应用程序版本号，0x19 表示 v1.9 */
#define APP_VERSION 0x19

/**
 * @brief EPD BLE服务的128位基础UUID（小端格式）
 *
 * 这是一个自定义的厂商特定UUID，用于唯一标识EPD BLE服务。
 * 客户端通过此UUID来发现和连接EPD服务。
 * UUID字节序列（小端）：EC 5A 67 1C C1 B6 46 FB 8D 91 28 D8 22 36 75 62
 */
#define BLE_UUID_EPD_SVC_BASE \
    {{0XEC, 0X5A, 0X67, 0X1C, 0XC1, 0XB6, 0X46, 0XFB, 0X8D, 0X91, 0X28, 0XD8, 0X22, 0X36, 0X75, 0X62}}

#define BLE_UUID_EPD_SVC 0x0001   /**< EPD服务的16位短UUID，用于在基础UUID下标识服务 */
#define BLE_UUID_EPD_CHAR 0x0002  /**< EPD数据特征值的16位短UUID，支持读写和通知，用于收发命令和数据 */
#define BLE_UUID_APP_VER 0x0003   /**< APP版本号特征值的16位短UUID，只读，客户端可读取固件版本 */

/** @brief UUID类型，使用厂商自定义UUID起始类型 */
#define EPD_SVC_UUID_TYPE BLE_UUID_TYPE_VENDOR_BEGIN

/**
 * @brief BLE EPD单次可传输的最大数据长度
 *
 * 根据不同的协议栈配置：
 * - S112（nRF52）：使用SoftDevice配置的最大MTU值减去3字节ATT协议开销
 * - 其他（nRF51）：使用默认MTU值（23字节）减去3字节ATT协议开销，即20字节
 */
#if defined(S112)
#define BLE_EPD_MAX_DATA_LEN (NRF_SDH_BLE_GATT_MAX_MTU_SIZE - 3)
#else
#define BLE_EPD_MAX_DATA_LEN \
    (GATT_MTU_SIZE_DEFAULT - 3) /**< Maximum length of data (in bytes) that can be transmitted to the peer. */
#endif

/**
 * @brief EPD服务命令ID枚举
 *
 * 定义了客户端通过BLE写入特征值时可以发送的所有命令。
 * 每个命令对应一个字节码（p_data[0]），部分命令需要附加参数。
 */
enum EPD_CMDS {
    EPD_CMD_SET_PINS = 0x00,     /**< 设置EPD引脚映射。参数：mosi, sclk, cs, dc, rst, busy, bs, [en] */
    EPD_CMD_INIT = 0x01,         /**< 初始化EPD显示驱动。可选参数：model_id（不传则使用配置中的型号） */
    EPD_CMD_CLEAR = 0x02,        /**< 清除EPD屏幕。可选参数：颜色（true=白色，false=黑色） */
    EPD_CMD_SEND_COMMAND = 0x03, /**< 向EPD发送原始命令字节。参数：command byte */
    EPD_CMD_SEND_DATA = 0x04,    /**< 向EPD发送原始数据。参数：data bytes */
    EPD_CMD_REFRESH = 0x05,      /**< 将EPD RAM中的数据刷新到屏幕显示 */
    EPD_CMD_SLEEP = 0x06,        /**< 使EPD进入深度睡眠模式，降低功耗 */

    EPD_CMD_SET_TIME = 0x20,       /**< 设置时间（Unix时间戳）。参数：4字节时间戳 + [1字节时区偏移(小时)] + [1字节显示模式] */
    EPD_CMD_SET_WEEK_START = 0x21, /**< 设置每周起始日。参数：0=周日, 1=周一, 2=周二, ..., 6=周六 */

    EPD_CMD_WRITE_IMAGE = 0x30, /**< 写入图像数据到EPD RAM。参数：X坐标(高4位)+起始位(低4位), 数据字节...
                                    MSB=0000表示RAM起始，LSB=1111表示黑色像素 */

    EPD_CMD_SET_CONFIG = 0x90, /**< 设置完整的EPD配置。参数：完整的epd_config_t结构体数据 */
    EPD_CMD_SYS_RESET = 0x91,  /**< MCU系统复位，重启设备 */
    EPD_CMD_SYS_SLEEP = 0x92,  /**< MCU进入睡眠模式，等待唤醒 */
    EPD_CMD_CFG_ERASE = 0x99,  /**< 擦除Flash中的配置数据并复位，恢复出厂设置 */
};

/**
 * @brief EPD BLE服务结构体
 *
 * 包含EPD BLE服务的所有状态信息，包括服务句柄、特征值句柄、
 * 连接状态、通知使能标志、当前EPD型号指针和配置数据。
 */
typedef struct {
    uint16_t service_handle; /**< EPD服务的GATT服务句柄，由SoftDevice在添加服务时分配 */
    ble_gatts_char_handles_t
        char_handles; /**< EPD数据特征值的句柄集合（包含value_handle和cccd_handle），由SoftDevice分配 */
    ble_gatts_char_handles_t
        app_ver_handles;  /**< APP版本号特征值的句柄集合，客户端可读取此特征值获取固件版本 */
    uint16_t conn_handle; /**< 当前BLE连接句柄，未连接时为 BLE_CONN_HANDLE_INVALID */
    uint16_t max_data_len;        /**< 单次BLE通知可发送的最大数据长度（字节），受MTU限制 */
    bool is_notification_enabled; /**< 客户端是否已启用通知（通过写入CCCD控制），true表示已启用 */
    epd_model_t* epd;             /**< 当前EPD型号指针，指向已初始化的EPD驱动模型，NULL表示未初始化 */
    epd_config_t config;          /**< EPD配置数据，包含引脚映射、显示模式、型号等持久化配置 */
} ble_epd_t;

/**
 * @brief EPD GUI更新事件结构体
 *
 * 用于通过app_scheduler调度GUI更新任务。
 * 当定时器触发或收到设置时间命令时，将此事件投入调度队列，
 * 在主循环中执行EPD的初始化、GUI绘制、刷新和睡眠操作。
 */
typedef struct {
    ble_epd_t* p_epd;     /**< 指向EPD服务实例的指针 */
    uint32_t timestamp;   /**< 当前Unix时间戳，用于GUI绘制时间/日历信息 */
} epd_gui_update_event_t;

/** @brief GUI更新调度事件的数据大小，用于app_scheduler事件分配 */
#define EPD_GUI_SCHD_EVENT_DATA_SIZE sizeof(epd_gui_update_event_t)

/**
 * @brief 睡眠准备函数
 *
 * 在MCU进入睡眠前调用，执行以下操作：
 * - 关闭LED指示灯，避免睡眠时LED持续耗电
 * - 配置唤醒引脚（如果配置中wakeup_pin有效），设置为高电平检测唤醒
 *
 * @param[in] p_epd  EPD服务结构体指针
 */
void ble_epd_sleep_prepare(ble_epd_t* p_epd);

/**
 * @brief EPD BLE服务初始化函数
 *
 * 执行以下初始化操作：
 * 1. 初始化服务结构体（连接句柄、最大数据长度、通知标志等）
 * 2. 从Flash读取持久化配置
 * 3. 如果配置为空（首次使用），写入默认引脚配置（根据芯片型号选择不同默认值）
 * 4. 加载GPIO配置
 * 5. 闪烁LED指示启动
 * 6. 注册BLE GATT服务和特征值
 *
 * @param[out] p_epd  EPD服务结构体指针，由应用程序提供，此函数会初始化并填充
 *
 * @retval NRF_SUCCESS     服务初始化成功
 * @retval NRF_ERROR_NULL  p_epd指针为NULL
 */
uint32_t ble_epd_init(ble_epd_t* p_epd);

/**
 * @brief BLE事件处理函数
 *
 * 处理从SoftDevice接收到的BLE事件，分发到对应的处理函数：
 * - BLE_GAP_EVT_CONNECTED：连接事件 -> on_connect()
 * - BLE_GAP_EVT_DISCONNECTED：断开事件 -> on_disconnect()
 * - BLE_GATTS_EVT_WRITE：写事件 -> on_write()
 *
 * 对于nRF52（S112），此函数由SDH观察者自动调用；
 * 对于nRF51，需要应用程序在BLE事件回调中手动调用。
 *
 * @param[in] p_epd      EPD服务结构体指针
 * @param[in] p_ble_evt  从SoftDevice接收到的BLE事件指针
 */
void ble_epd_on_ble_evt(ble_epd_t* p_epd, ble_evt_t* p_ble_evt);

/**
 * @brief 通过BLE通知发送数据到客户端
 *
 * 将指定的数据作为EPD特征值的通知（Notification）发送给已连接的客户端。
 * 发送前会检查连接状态和通知使能标志。
 *
 * @param[in] p_epd     EPD服务结构体指针
 * @param[in] p_string  要发送的数据缓冲区指针
 * @param[in] length    要发送的数据长度（字节）
 *
 * @retval NRF_SUCCESS              数据发送成功
 * @retval NRF_ERROR_INVALID_STATE  未连接或通知未启用
 * @retval NRF_ERROR_INVALID_PARAM  数据长度超过最大传输长度
 */
uint32_t ble_epd_string_send(ble_epd_t* p_epd, uint8_t* p_string, uint16_t length);

/**
 * @brief 定时器回调函数，用于定时更新EPD显示
 *
 * 根据当前显示模式决定更新频率：
 * - 日历模式（MODE_CALENDAR）：每天00:00:00（时间戳 % 86400 == 0）更新一次
 * - 时钟模式（MODE_CLOCK）：每分钟（时间戳 % 60 == 0）更新一次
 * - force_update为true时：强制立即更新，不受时间条件限制
 *
 * 满足条件时，通过app_scheduler调度epd_gui_update函数执行实际更新。
 *
 * @param[in] p_epd         EPD服务结构体指针
 * @param[in] timestamp     当前Unix时间戳
 * @param[in] force_update  是否强制更新（true=无条件更新，false=按模式定时更新）
 */
void ble_epd_on_timer(ble_epd_t* p_epd, uint32_t timestamp, bool force_update);

#endif  // EPD_BLE_H__

/** @} */
