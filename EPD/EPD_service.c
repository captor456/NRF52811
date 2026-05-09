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
 * @file EPD_service.c
 * @brief EPD蓝牙BLE服务实现文件
 *
 * 本文件实现了EPD（电子墨水屏）BLE GATT服务的全部功能逻辑。
 * 主要功能包括：
 * - BLE连接/断开事件处理
 * - EPD命令解析与执行（初始化、清屏、刷新、睡眠、图片写入等）
 * - 时间同步与定时更新（日历模式每天更新，时钟模式每分钟更新）
 * - 配置管理（读取、写入、擦除Flash配置）
 * - GUI更新调度（通过app_scheduler在主循环中执行EPD刷新）
 * - 系统控制（复位、睡眠、唤醒引脚配置）
 */

#include "EPD_service.h"

#include <string.h>

#include "app_scheduler.h"
#include "ble_srv_common.h"
#include "main.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_pwr_mgmt.h"
#include "sdk_macros.h"

/**
 * @brief 默认引脚配置宏定义
 *
 * 根据不同的芯片型号定义不同的默认GPIO引脚映射配置。
 * 配置格式为字节数组，依次对应epd_config_t结构体中的各字段：
 * mosi_pin, sclk_pin, cs_pin, dc_pin, rst_pin, busy_pin, bs_pin, [model_id], [display_mode], [week_start], [en_pin]
 *
 * - EPD_CFG_52811: nRF52811芯片的默认引脚配置
 *   引脚分配：MOSI=P20, SCLK=P19, CS=P6, DC=P5, RST=P4, BUSY=P3, BS=P2, model=0x02, display_mode=日历(0xFF自动修正), week_start=周一(0x12), en_pin=P7
 * - EPD_CFG_52810: nRF52810芯片的默认引脚配置
 *   引脚分配：MOSI=P20, SCLK=P19, CS=P18, DC=P17, RST=P16, BUSY=P15, BS=P14, model=0x02, display_mode=日历(0xFF自动修正), week_start=周二(0x0D), en_pin=P2
 * - EPD_CFG_DEFAULT: nRF51等其他芯片的默认引脚配置
 *   引脚分配：MOSI=P10, SCLK=P11, CS=P12, DC=P13, RST=P14, BUSY=P15, BS=P16, model=0x03, display_mode=时钟(0x09), week_start=周三(0x03)
 */
#if defined(S112)
#define EPD_CFG_52811 {0x14, 0x13, 0x06, 0x05, 0x04, 0x03, 0x02, 0x02, 0xFF, 0x12, 0x07}
#define EPD_CFG_52810 {0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x02, 0xFF, 0x0D, 0x02}
#else
#define EPD_CFG_DEFAULT {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x03, 0x09, 0x03}
// #define EPD_CFG_DEFAULT {0x05, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x01, 0x07}
#endif

/**
 * @brief GUI更新调度函数
 *
 * 此函数由app_scheduler在主循环上下文中调用，执行完整的EPD显示更新流程：
 * 1. 初始化EPD GPIO引脚
 * 2. 根据配置的型号ID初始化EPD驱动
 * 3. 构建GUI数据结构（显示模式、颜色、尺寸、时间戳、星期起始、温度、电压、设备名称）
 * 4. 获取蓝牙设备名称作为SSID显示
 * 5. 调用DrawGUI绘制GUI界面，通过EPD驱动的write_image回调写入图像数据
 * 6. 刷新EPD显示（将RAM数据输出到屏幕）
 * 7. 使EPD进入睡眠模式以降低功耗
 * 8. 延时200ms确保EPD睡眠完成
 * 9. 反初始化EPD GPIO引脚以降低功耗
 * 10. 喂看门狗，防止长时间操作导致看门狗复位
 *
 * @param[in] p_event_data  指向epd_gui_update_event_t事件数据的指针
 * @param[in] event_size    事件数据大小
 */
static void epd_gui_update(void* p_event_data, uint16_t event_size) {
    /* 从事件数据中提取EPD服务指针 */
    epd_gui_update_event_t* event = (epd_gui_update_event_t*)p_event_data;
    ble_epd_t* p_epd = event->p_epd;

    /* 初始化EPD的GPIO引脚（SPI、控制引脚等） */
    EPD_GPIO_Init();

    /* 根据配置中的型号ID初始化EPD驱动，返回EPD型号指针 */
    epd_model_t* epd = epd_init((epd_model_id_t)p_epd->config.model_id);

    /* 构建GUI绘制所需的数据结构 */
    gui_data_t data = {
        .mode = (display_mode_t)p_epd->config.display_mode,  /* 显示模式：日历/时钟/图片等 */
        .color = epd->color,                                  /* EPD支持的色彩类型（黑白/三色等） */
        .width = epd->width,                                  /* EPD屏幕宽度（像素） */
        .height = epd->height,                                /* EPD屏幕高度（像素） */
        .timestamp = event->timestamp,                        /* 当前Unix时间戳，用于绘制时间和日期 */
        .week_start = p_epd->config.week_start,              /* 每周起始日（0=周日, 1=周一, ...） */
        .temperature = epd->drv->read_temp(epd),             /* 读取EPD内部温度传感器的温度值 */
        .voltage = EPD_ReadVoltage(),                         /* 读取当前电池电压 */
    };

    /* 获取蓝牙广播中的设备名称，作为GUI中显示的SSID */
    uint16_t dev_name_len = sizeof(data.ssid);
    uint32_t err_code = sd_ble_gap_device_name_get((uint8_t*)data.ssid, &dev_name_len);
    if (err_code == NRF_SUCCESS && dev_name_len > 0) data.ssid[dev_name_len] = '\0';

    /* 绘制GUI界面：通过EPD驱动的write_image回调函数将图像数据写入EPD RAM */
    DrawGUI(&data, (buffer_callback)epd->drv->write_image, epd);

    /* 将EPD RAM中的图像数据刷新到屏幕上显示 */
    epd->drv->refresh(epd);

    /* 使EPD进入深度睡眠模式，关闭显示电源以降低功耗 */
    epd->drv->sleep(epd);

    /* 延时200ms，等待EPD完成睡眠序列（部分EPD芯片需要一定的睡眠时间） */
    nrf_delay_ms(200);  // for sleep

    /* 反初始化EPD GPIO引脚，将引脚设为低功耗状态 */
    EPD_GPIO_Uninit();

    /* 喂看门狗定时器，防止EPD刷新耗时过长导致看门狗复位 */
    app_feed_wdt();
}

/**@brief Function for handling the @ref BLE_GAP_EVT_CONNECTED event from the S110 SoftDevice.
 *
 * @param[in] p_epd     EPD Service structure.
 * @param[in] p_ble_evt Pointer to the event received from BLE stack.
 */
/**
 * @brief BLE连接事件处理函数
 *
 * 当客户端通过BLE连接到设备时调用：
 * - 保存连接句柄，用于后续的数据发送和通知
 * - 初始化EPD GPIO引脚，为后续的EPD操作做准备
 *
 * @param[in] p_epd      EPD服务结构体指针
 * @param[in] p_ble_evt  BLE事件指针，包含连接参数和连接句柄
 */
static void on_connect(ble_epd_t* p_epd, ble_evt_t* p_ble_evt) {
    /* 保存BLE连接句柄，后续发送通知时需要使用 */
    p_epd->conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
    /* 初始化EPD GPIO引脚，使EPD处于可用状态 */
    EPD_GPIO_Init();
}

/**@brief Function for handling the @ref BLE_GAP_EVT_DISCONNECTED event from the S110 SoftDevice.
 *
 * @param[in] p_epd     EPD Service structure.
 * @param[in] p_ble_evt Pointer to the event received from BLE stack.
 */
/**
 * @brief BLE断开连接事件处理函数
 *
 * 当客户端断开BLE连接时调用：
 * - 将连接句柄置为无效值
 * - 如果EPD已初始化，使EPD进入睡眠模式
 * - 反初始化EPD GPIO引脚以降低功耗
 *
 * @param[in] p_epd      EPD服务结构体指针
 * @param[in] p_ble_evt  BLE事件指针
 */
static void on_disconnect(ble_epd_t* p_epd, ble_evt_t* p_ble_evt) {
    UNUSED_PARAMETER(p_ble_evt);
    /* 将连接句柄标记为无效，表示当前无BLE连接 */
    p_epd->conn_handle = BLE_CONN_HANDLE_INVALID;
    /* 如果EPD已初始化，使EPD进入睡眠模式以降低功耗 */
    if (p_epd->epd) {
        p_epd->epd->drv->sleep(p_epd->epd);
        nrf_delay_ms(200);  // for sleep  /* 延时200ms等待EPD完成睡眠 */
    }
    /* 反初始化EPD GPIO引脚，释放引脚资源以降低功耗 */
    EPD_GPIO_Uninit();
}

/**
 * @brief 更新显示模式并保存到Flash
 *
 * 如果新的显示模式与当前模式不同，则更新配置中的显示模式
 * 并将新配置写入Flash持久化存储。
 *
 * @param[in] p_epd  EPD服务结构体指针
 * @param[in] mode   新的显示模式（日历/时钟/图片等）
 */
static void epd_update_display_mode(ble_epd_t* p_epd, display_mode_t mode) {
    /* 仅当新模式与当前模式不同时才更新，避免不必要的Flash写入 */
    if (p_epd->config.display_mode != mode) {
        p_epd->config.display_mode = mode;
        /* 将更新后的配置写入Flash持久化存储 */
        epd_config_write(&p_epd->config);
    }
}

/**
 * @brief 发送当前时间戳给客户端
 *
 * 将当前系统时间戳格式化为 "t=xxxxxxxx" 字符串，
 * 通过BLE通知发送给已连接的客户端。
 *
 * @param[in] p_epd  EPD服务结构体指针
 */
static void epd_send_time(ble_epd_t* p_epd) {
    char buf[20] = {0};
    /* 格式化时间戳为字符串，例如 "t=1700000000" */
    snprintf(buf, 20, "t=%" PRIu32, timestamp());
    /* 通过BLE通知发送给客户端 */
    ble_epd_string_send(p_epd, (uint8_t*)buf, strlen(buf));
}

/**
 * @brief 发送MTU信息给客户端
 *
 * 将当前BLE连接的最大可传输数据长度格式化为 "mtu=xxx" 字符串，
 * 通过BLE通知发送给已连接的客户端，使客户端了解当前可发送的最大数据包大小。
 *
 * @param[in] p_epd  EPD服务结构体指针
 */
static void epd_send_mtu(ble_epd_t* p_epd) {
    char buf[10] = {0};
    /* 格式化MTU大小为字符串，例如 "mtu=244" */
    snprintf(buf, sizeof(buf), "mtu=%d", p_epd->max_data_len);
    /* 通过BLE通知发送给客户端 */
    ble_epd_string_send(p_epd, (uint8_t*)buf, strlen(buf));
}

/**
 * @brief 核心命令处理函数 - 处理客户端通过BLE发送的所有EPD控制命令
 *
 * 解析客户端写入的数据，根据第一个字节（命令码）分发到对应的处理逻辑。
 * 这是EPD BLE服务的核心函数，实现了所有设备控制功能。
 *
 * @param[in] p_epd   EPD服务结构体指针
 * @param[in] p_data  接收到的数据缓冲区，p_data[0]为命令码，后续字节为参数
 * @param[in] length  数据长度（字节）
 */
static void epd_service_on_write(ble_epd_t* p_epd, uint8_t* p_data, uint16_t length) {
    NRF_LOG_DEBUG("[EPD]: on_write LEN=%d\n", length);
    NRF_LOG_HEXDUMP_DEBUG(p_data, length);
    /* 参数有效性检查：数据指针不能为空，长度必须大于0 */
    if (p_data == NULL || length <= 0) return;

    switch (p_data[0]) {
        /**
         * @case EPD_CMD_SET_PINS (0x00) - 设置EPD引脚映射
         *
         * 参数格式：[0x00, mosi, sclk, cs, dc, rst, busy, bs, [en]]
         * 需要至少8字节数据。设置完成后保存配置到Flash，
         * 重新加载GPIO配置并初始化引脚。
         */
        case EPD_CMD_SET_PINS:
            if (length < 8) return;

            /* 从数据中解析各引脚编号 */
            p_epd->config.mosi_pin = p_data[1];  /* SPI MOSI数据引脚 */
            p_epd->config.sclk_pin = p_data[2];  /* SPI 时钟引脚 */
            p_epd->config.cs_pin = p_data[3];    /* SPI 片选引脚 */
            p_epd->config.dc_pin = p_data[4];    /* 数据/命令选择引脚 */
            p_epd->config.rst_pin = p_data[5];   /* 复位引脚 */
            p_epd->config.busy_pin = p_data[6];  /* 忙碌状态检测引脚 */
            p_epd->config.bs_pin = p_data[7];    /* 总线宽度选择引脚 */
            /* 可选的第9字节：使能引脚 */
            if (length > 8) p_epd->config.en_pin = p_data[8];
            /* 将新引脚配置写入Flash持久化 */
            epd_config_write(&p_epd->config);

            /* 重新加载GPIO配置：先反初始化旧引脚，加载新配置，再初始化新引脚 */
            EPD_GPIO_Uninit();
            EPD_GPIO_Load(&p_epd->config);
            EPD_GPIO_Init();
            break;

        /**
         * @case EPD_CMD_INIT (0x01) - 初始化EPD显示驱动
         *
         * 参数格式：[0x01, model_id(可选)]
         * 如果未提供model_id，则使用配置中保存的型号。
         * 初始化成功后自动发送MTU大小和当前时间给客户端。
         * 如果检测到型号变化，会更新配置并保存到Flash。
         */
        case EPD_CMD_INIT:
            /* 使用传入的型号ID或配置中的默认型号初始化EPD驱动 */
            p_epd->epd = epd_init((epd_model_id_t)(length > 1 ? p_data[1] : p_epd->config.model_id));
            /* 如果初始化后的实际型号与配置不同，更新配置并保存 */
            if (p_epd->epd->id != p_epd->config.model_id) {
                p_epd->config.model_id = p_epd->epd->id;
                epd_config_write(&p_epd->config);
            }
            /* 初始化完成后，通知客户端当前的MTU大小和时间戳 */
            epd_send_mtu(p_epd);
            epd_send_time(p_epd);
            break;

        /**
         * @case EPD_CMD_CLEAR (0x02) - 清除EPD屏幕
         *
         * 参数格式：[0x02, color(可选, true=白色/false=黑色)]
         * 同时将显示模式切换为图片模式（MODE_PICTURE）。
         * 如果EPD已初始化，先初始化驱动再执行清屏操作。
         */
        case EPD_CMD_CLEAR:
            /* 清屏操作自动切换到图片模式 */
            epd_update_display_mode(p_epd, MODE_PICTURE);
            if (p_epd->epd) {
                p_epd->epd->drv->init(p_epd->epd);
                /* 清屏颜色：默认白色(true)，可通过参数指定 */
                p_epd->epd->drv->clear(p_epd->epd, length > 1 ? p_data[1] : true);
            }
            break;

        /**
         * @case EPD_CMD_SEND_COMMAND (0x03) - 向EPD发送原始命令字节
         *
         * 参数格式：[0x03, command_byte]
         * 直接向EPD发送一个命令字节，用于底层调试或发送特殊命令。
         * 需要至少2字节数据。
         */
        case EPD_CMD_SEND_COMMAND:
            if (length < 2) return;
            EPD_WriteCmd(p_data[1]);
            break;

        /**
         * @case EPD_CMD_SEND_DATA (0x04) - 向EPD发送原始数据
         *
         * 参数格式：[0x04, data_byte1, data_byte2, ...]
         * 直接向EPD发送数据字节序列，用于底层调试或发送特殊数据。
         */
        case EPD_CMD_SEND_DATA:
            EPD_WriteData(&p_data[1], length - 1);
            break;

        /**
         * @case EPD_CMD_REFRESH (0x05) - 刷新EPD显示
         *
         * 参数格式：[0x05]
         * 将EPD RAM中的图像数据刷新到屏幕上显示。
         * 同时将显示模式切换为图片模式（MODE_PICTURE）。
         */
        case EPD_CMD_REFRESH:
            /* 刷新操作自动切换到图片模式 */
            epd_update_display_mode(p_epd, MODE_PICTURE);
            if (p_epd->epd) p_epd->epd->drv->refresh(p_epd->epd);
            break;

        /**
         * @case EPD_CMD_SLEEP (0x06) - 使EPD进入睡眠模式
         *
         * 参数格式：[0x06]
         * 使EPD芯片进入深度睡眠模式，关闭显示电源以降低功耗。
         */
        case EPD_CMD_SLEEP:
            if (p_epd->epd) p_epd->epd->drv->sleep(p_epd->epd);
            break;

        /**
         * @case EPD_CMD_SET_TIME (0x20) - 设置系统时间
         *
         * 参数格式：[0x20, ts[0], ts[1], ts[2], ts[3], [timezone], [display_mode]]
         * - ts[0..3]：4字节大端序Unix时间戳
         * - timezone（可选）：时区偏移量（小时），默认为+8（东八区/北京时间）
         * - display_mode（可选）：显示模式，默认为日历模式（MODE_CALENDAR）
         *
         * 设置时间后会自动触发一次强制GUI更新。
         */
        case EPD_CMD_SET_TIME: {
            if (length < 5) return;

            NRF_LOG_DEBUG("time: %02x %02x %02x %02x\n", p_data[1], p_data[2], p_data[3], p_data[4]);
            if (length > 5) NRF_LOG_DEBUG("timezone: %d\n", (int8_t)p_data[5]);

            /* 将4字节大端序时间戳合并为32位Unix时间戳 */
            uint32_t timestamp = (p_data[1] << 24) | (p_data[2] << 16) | (p_data[3] << 8) | p_data[4];
            /* 加上时区偏移（小时转秒），默认+8小时（东八区） */
            timestamp += (length > 5 ? (int8_t)p_data[5] : 8) * 60 * 60;  // timezone
            /* 设置系统时间戳 */
            set_timestamp(timestamp);
            /* 更新显示模式：如果指定了显示模式则使用，否则默认日历模式 */
            epd_update_display_mode(p_epd, length > 6 ? (display_mode_t)p_data[6] : MODE_CALENDAR);
            /* 强制立即触发一次GUI更新，使时间变更立即反映在屏幕上 */
            ble_epd_on_timer(p_epd, timestamp, true);
        } break;

        /**
         * @case EPD_CMD_SET_WEEK_START (0x21) - 设置每周起始日
         *
         * 参数格式：[0x21, day]
         * - day：0=周日, 1=周一, 2=周二, ..., 6=周六
         * 仅当值有效（0-6）且与当前配置不同时才更新并保存到Flash。
         */
        case EPD_CMD_SET_WEEK_START:
            if (length < 2) return;
            /* 验证星期起始日有效（0-6），且与当前配置不同才更新 */
            if (p_data[1] < 7 && p_data[1] != p_epd->config.week_start) {
                p_epd->config.week_start = p_data[1];
                epd_config_write(&p_epd->config);
            }
            break;

        /**
         * @case EPD_CMD_WRITE_IMAGE (0x30) - 写入图像数据到EPD RAM
         *
         * 参数格式：[0x30, x_start, data_byte1, data_byte2, ...]
         * - x_start：高4位为X坐标，低4位为起始位偏移
         * - data：图像数据字节，MSB=0000表示RAM起始，LSB=1111表示黑色像素
         * 需要至少3字节数据。
         */
        case EPD_CMD_WRITE_IMAGE:  // MSB=0000: ram begin, LSB=1111: black
            if (length < 3) return;
            if (p_epd->epd) p_epd->epd->drv->write_ram(p_epd->epd, p_data[1], &p_data[2], length - 2);
            break;

        /**
         * @case EPD_CMD_SET_CONFIG (0x90) - 设置完整的EPD配置
         *
         * 参数格式：[0x90, config_byte1, config_byte2, ...]
         * 直接将数据覆盖为完整的epd_config_t配置结构体。
         * 数据长度受EPD_CONFIG_SIZE限制，防止缓冲区溢出。
         * 写入后立即保存到Flash持久化。
         */
        case EPD_CMD_SET_CONFIG:
            if (length < 2) return;
            /* 将数据复制到配置结构体，长度不超过配置结构体大小 */
            memcpy(&p_epd->config, &p_data[1], (length - 1 > EPD_CONFIG_SIZE) ? EPD_CONFIG_SIZE : length - 1);
            /* 将新配置写入Flash持久化 */
            epd_config_write(&p_epd->config);
            break;

        /**
         * @case EPD_CMD_SYS_SLEEP (0x92) - MCU进入系统睡眠模式
         *
         * 参数格式：[0x92]
         * 使MCU进入深度睡眠模式，等待外部唤醒。
         * 调用sleep_mode_enter()执行系统级睡眠。
         */
        case EPD_CMD_SYS_SLEEP:
            sleep_mode_enter();
            break;

        /**
         * @case EPD_CMD_SYS_RESET (0x91) - MCU系统复位
         *
         * 参数格式：[0x91]
         * 立即重启MCU设备。
         * nRF52系列使用nrf_pwr_mgmt_shutdown进行电源管理复位；
         * nRF51系列使用NVIC_SystemReset直接复位。
         */
        case EPD_CMD_SYS_RESET:
#if defined(S112)
            /* nRF52系列：通过电源管理模块执行复位 */
            nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_RESET);
#else
            /* nRF51系列：直接触发NVIC系统复位 */
            NVIC_SystemReset();
#endif
            break;

        /**
         * @case EPD_CMD_CFG_ERASE (0x99) - 擦除配置并复位
         *
         * 参数格式：[0x99]
         * 擦除Flash中保存的所有EPD配置数据（恢复出厂设置），
         * 延时100ms确保Flash擦除完成，然后执行系统复位。
         * 复位后设备将使用默认配置重新初始化。
         */
        case EPD_CMD_CFG_ERASE:
            /* 清除Flash中的配置数据 */
            epd_config_clear(&p_epd->config);
            nrf_delay_ms(100);  // required  /* 延时100ms，等待Flash擦除操作完成 */
            /* 执行系统复位，设备将以默认配置重新启动 */
            NVIC_SystemReset();
            break;

        default:
            /* 未知命令，忽略不处理 */
            break;
    }
}

/**@brief Function for handling the @ref BLE_GATTS_EVT_WRITE event from the S110 SoftDevice.
 *
 * @param[in] p_epd     EPD Service structure.
 * @param[in] p_ble_evt Pointer to the event received from BLE stack.
 */
/**
 * @brief BLE写事件处理函数
 *
 * 处理客户端的GATT写操作，支持两种写目标：
 * 1. CCCD（Client Characteristic Configuration Descriptor）写入：
 *    - 当客户端写入0x0001（启用通知）时，将is_notification_enabled设为true，
 *      并立即将当前EPD配置通过通知发送给客户端
 *    - 当客户端写入0x0000（禁用通知）时，将is_notification_enabled设为false
 * 2. 特征值写入：
 *    - 当客户端写入EPD数据特征值时，将数据转发给epd_service_on_write()处理
 *
 * @param[in] p_epd      EPD服务结构体指针
 * @param[in] p_ble_evt  BLE事件指针，包含写操作的具体数据
 */
static void on_write(ble_epd_t* p_epd, ble_evt_t* p_ble_evt) {
    /* 获取写事件的详细参数 */
    ble_gatts_evt_write_t* p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;

    /* 处理CCCD（通知开关）写入 */
    if ((p_evt_write->handle == p_epd->char_handles.cccd_handle) && (p_evt_write->len == 2)) {
        if (ble_srv_is_notification_enabled(p_evt_write->data)) {
            /* 客户端启用了通知功能 */
            NRF_LOG_DEBUG("notification enabled\n");
            p_epd->is_notification_enabled = true;
            /* 通知启用后，立即将当前EPD配置发送给客户端，使其了解设备状态 */
            static uint16_t length = sizeof(epd_config_t);
            NRF_LOG_DEBUG("send epd config\n");
            uint32_t err_code = ble_epd_string_send(p_epd, (uint8_t*)&p_epd->config, length);
            /* NRF_ERROR_INVALID_STATE表示未连接，可以忽略；其他错误需要检查 */
            if (err_code != NRF_ERROR_INVALID_STATE) APP_ERROR_CHECK(err_code);
        } else {
            /* 客户端禁用了通知功能 */
            p_epd->is_notification_enabled = false;
        }
    } else if (p_evt_write->handle == p_epd->char_handles.value_handle) {
        /* 客户端写入了EPD数据特征值，转发给命令处理函数 */
        epd_service_on_write(p_epd, p_evt_write->data, p_evt_write->len);
    } else {
        // Do Nothing. This event is not relevant for this service.
        /* 写入的不是本服务的句柄，忽略 */
    }
}

#if defined(S112)
/**
 * @brief BLE事件观察者回调函数（仅S112/nRF52）
 *
 * 这是SDH（SoftDevice Handler）观察者的回调入口。
 * 当BLE事件发生时，SoftDevice Handler自动调用此函数，
 * 然后转发给 ble_epd_on_ble_evt() 进行实际的事件分发处理。
 *
 * @param[in] p_ble_evt  BLE事件指针
 * @param[in] p_context  观察者注册时传入的上下文（指向ble_epd_t实例）
 */
void ble_epd_evt_handler(ble_evt_t const* p_ble_evt, void* p_context) {
    if (p_context == NULL || p_ble_evt == NULL) return;

    ble_epd_t* p_epd = (ble_epd_t*)p_context;
    ble_epd_on_ble_evt(p_epd, (ble_evt_t*)p_ble_evt);
}
#endif

/**
 * @brief BLE事件分发函数
 *
 * 根据BLE事件类型将事件分发到对应的处理函数：
 * - BLE_GAP_EVT_CONNECTED：BLE连接建立 -> on_connect()
 * - BLE_GAP_EVT_DISCONNECTED：BLE连接断开 -> on_disconnect()
 * - BLE_GATTS_EVT_WRITE：GATT写操作 -> on_write()
 * - 其他事件：忽略不处理
 *
 * @param[in] p_epd      EPD服务结构体指针
 * @param[in] p_ble_evt  BLE事件指针
 */
void ble_epd_on_ble_evt(ble_epd_t* p_epd, ble_evt_t* p_ble_evt) {
    /* 参数有效性检查 */
    if ((p_epd == NULL) || (p_ble_evt == NULL)) {
        return;
    }

    /* 根据事件ID分发到对应的处理函数 */
    switch (p_ble_evt->header.evt_id) {
        case BLE_GAP_EVT_CONNECTED:
            /* BLE连接建立事件 */
            on_connect(p_epd, p_ble_evt);
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            /* BLE连接断开事件 */
            on_disconnect(p_epd, p_ble_evt);
            break;

        case BLE_GATTS_EVT_WRITE:
            /* GATT特征值写事件 */
            on_write(p_epd, p_ble_evt);
            break;

        default:
            // No implementation needed.
            /* 其他事件不处理 */
            break;
    }
}

/**
 * @brief GATT服务初始化函数（内部函数）
 *
 * 向SoftDevice注册EPD BLE GATT服务，添加以下特征值：
 * 1. EPD数据特征值（BLE_UUID_EPD_CHAR）：
 *    - 支持通知（Notify）、写入（Write）和无响应写入（Write Without Response）
 *    - 可变长度，最大长度为BLE_EPD_MAX_DATA_LEN
 *    - 读写权限为开放（SEC_OPEN），无需配对即可访问
 * 2. APP版本号特征值（BLE_UUID_APP_VER）：
 *    - 只读，固定1字节，初始值为APP_VERSION（0x19 = v1.9）
 *    - 读权限为开放（SEC_OPEN）
 *
 * @param[in] p_epd  EPD服务结构体指针
 *
 * @retval NRF_SUCCESS  服务和特征值添加成功
 * @retval 其他         SoftDevice返回的错误码
 */
static uint32_t epd_service_init(ble_epd_t* p_epd) {
    ble_uuid_t ble_uuid = {0};
    ble_uuid128_t base_uuid = BLE_UUID_EPD_SVC_BASE;
    ble_add_char_params_t add_char_params;
    uint8_t app_version = APP_VERSION;

    /* 向SoftDevice注册128位厂商自定义UUID基础值 */
    VERIFY_SUCCESS(sd_ble_uuid_vs_add(&base_uuid, &ble_uuid.type));

    /* 设置服务UUID并添加主服务（Primary Service） */
    ble_uuid.type = ble_uuid.type;
    ble_uuid.uuid = BLE_UUID_EPD_SVC;
    VERIFY_SUCCESS(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &ble_uuid, &p_epd->service_handle));

    /* ===== 添加EPD数据特征值（支持读写和通知） ===== */
    memset(&add_char_params, 0, sizeof(add_char_params));
    add_char_params.uuid = BLE_UUID_EPD_CHAR;           /* 特征值UUID */
    add_char_params.uuid_type = ble_uuid.type;           /* UUID类型（厂商自定义） */
    add_char_params.max_len = BLE_EPD_MAX_DATA_LEN;      /* 最大数据长度 */
    add_char_params.init_len = sizeof(uint8_t);          /* 初始数据长度 */
    add_char_params.is_var_len = true;                   /* 可变长度特征值 */
    add_char_params.char_props.notify = 1;               /* 支持通知（服务器主动推送数据给客户端） */
    add_char_params.char_props.write = 1;                /* 支持写入（需要响应） */
    add_char_params.char_props.write_wo_resp = 1;        /* 支持无响应写入（更快，不等待ACK） */
    add_char_params.read_access = SEC_OPEN;              /* 读权限：开放，无需加密 */
    add_char_params.write_access = SEC_OPEN;             /* 写权限：开放，无需加密 */
    add_char_params.cccd_write_access = SEC_OPEN;        /* CCCD写权限：开放 */

    /* 添加特征值到服务，保存句柄到char_handles */
    VERIFY_SUCCESS(characteristic_add(p_epd->service_handle, &add_char_params, &p_epd->char_handles));

    /* ===== 添加APP版本号特征值（只读） ===== */
    memset(&add_char_params, 0, sizeof(add_char_params));
    add_char_params.uuid = BLE_UUID_APP_VER;            /* 特征值UUID */
    add_char_params.uuid_type = ble_uuid.type;           /* UUID类型（厂商自定义） */
    add_char_params.max_len = sizeof(uint8_t);           /* 最大1字节 */
    add_char_params.init_len = sizeof(uint8_t);          /* 初始1字节 */
    add_char_params.p_init_value = &app_version;         /* 初始值：APP_VERSION（0x19 = v1.9） */
    add_char_params.char_props.read = 1;                 /* 只读特征值 */
    add_char_params.read_access = SEC_OPEN;              /* 读权限：开放 */

    /* 添加特征值到服务，保存句柄到app_ver_handles */
    return characteristic_add(p_epd->service_handle, &add_char_params, &p_epd->app_ver_handles);
}

/**
 * @brief 睡眠准备函数
 *
 * 在MCU进入系统睡眠前调用，执行以下操作：
 * 1. 关闭LED指示灯，避免睡眠时LED持续消耗电池电量
 * 2. 如果配置中wakeup_pin有效（非0xFF），将该引脚配置为高电平检测输入，
 *    当该引脚检测到高电平时会唤醒MCU（例如连接按钮到该引脚实现按键唤醒）
 *
 * @param[in] p_epd  EPD服务结构体指针
 */
void ble_epd_sleep_prepare(ble_epd_t* p_epd) {
    // Turn off led
    /* 关闭LED指示灯，降低睡眠功耗 */
    EPD_LED_OFF();
    // Prepare wakeup pin
    /* 配置唤醒引脚：如果配置中指定了有效的唤醒引脚（非0xFF），
     * 将其设置为无上拉/下拉输入，并启用高电平检测唤醒功能。
     * 当该引脚被拉高时（例如按下按钮），MCU将从睡眠中唤醒。 */
    if (p_epd->config.wakeup_pin != 0xFF) {
        nrf_gpio_cfg_sense_input(p_epd->config.wakeup_pin, NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_HIGH);
    }
}

/**
 * @brief EPD BLE服务初始化函数
 *
 * 完整的服务初始化流程：
 * 1. 参数有效性检查（p_epd不能为NULL）
 * 2. 初始化服务结构体成员（最大数据长度、连接句柄、通知标志）
 * 3. 初始化并从Flash读取持久化配置
 * 4. 如果配置为空（首次使用/出厂状态），根据芯片型号写入默认引脚配置：
 *    - nRF52810：使用EPD_CFG_52810默认配置
 *    - nRF52811：使用EPD_CFG_52811默认配置
 *    - 其他：使用EPD_CFG_DEFAULT默认配置
 *    同时修正无效的显示模式和星期起始日为默认值
 * 5. 加载GPIO配置到引脚
 * 6. 闪烁LED指示设备启动
 * 7. 注册BLE GATT服务
 *
 * @param[in] p_epd  EPD服务结构体指针
 *
 * @retval NRF_SUCCESS     初始化成功
 * @retval NRF_ERROR_NULL  p_epd为NULL
 */
uint32_t ble_epd_init(ble_epd_t* p_epd) {
    if (p_epd == NULL) return NRF_ERROR_NULL;

    // Initialize the service structure.
    /* 初始化服务结构体成员变量 */
    p_epd->max_data_len = BLE_EPD_MAX_DATA_LEN;  /* 设置最大BLE传输数据长度 */
    p_epd->conn_handle = BLE_CONN_HANDLE_INVALID; /* 初始状态无BLE连接 */
    p_epd->is_notification_enabled = false;       /* 初始状态通知未启用 */

    /* 初始化配置模块并从Flash读取已保存的配置 */
    epd_config_init(&p_epd->config);
    epd_config_read(&p_epd->config);

    // write default config
    /* 检查配置是否为空（首次使用或配置被擦除），如果是则写入默认配置 */
    if (epd_config_empty(&p_epd->config)) {
#if defined(S112)
        /* nRF52系列：根据芯片型号（FICR寄存器中的PART号）选择不同的默认引脚配置 */
        if (NRF_FICR->INFO.PART == 0x52810) {
            /* nRF52810芯片：使用52810专用引脚配置 */
            uint8_t cfg[] = EPD_CFG_52810;
            memcpy(&p_epd->config, cfg, sizeof(cfg));
        } else {
            /* nRF52811芯片：使用52811专用引脚配置 */
            uint8_t cfg[] = EPD_CFG_52811;
            memcpy(&p_epd->config, cfg, sizeof(cfg));
        }
#else
        /* nRF51等其他芯片：使用通用默认引脚配置 */
        uint8_t cfg[] = EPD_CFG_DEFAULT;
        memcpy(&p_epd->config, cfg, sizeof(cfg));
#endif
        /* 修正可能无效的配置值为合理的默认值 */
        if (p_epd->config.display_mode == 0xFF) p_epd->config.display_mode = MODE_CALENDAR;  /* 显示模式默认：日历 */
        if (p_epd->config.week_start == 0xFF) p_epd->config.week_start = 0;                  /* 星期起始默认：周日 */
        /* 将默认配置写入Flash持久化 */
        epd_config_write(&p_epd->config);
    }

    // load config
    /* 根据配置加载GPIO引脚映射 */
    EPD_GPIO_Load(&p_epd->config);

    // blink LED on start
    /* 闪烁LED指示设备已启动（便于用户确认设备正常运行） */
    EPD_LED_BLINK();

    // Add the service.
    /* 向SoftDevice注册BLE GATT服务和特征值 */
    return epd_service_init(p_epd);
}

/**
 * @brief 通过BLE通知发送数据到客户端
 *
 * 将数据作为EPD特征值的GATT通知（HVX）发送给已连接的客户端。
 * 发送前会进行以下检查：
 * 1. 连接状态检查：必须已建立BLE连接
 * 2. 通知使能检查：客户端必须已启用通知（通过CCCD）
 * 3. 数据长度检查：数据长度不能超过max_data_len
 *
 * @param[in] p_epd     EPD服务结构体指针
 * @param[in] p_string  要发送的数据缓冲区指针
 * @param[in] length    要发送的数据长度（字节）
 *
 * @retval NRF_SUCCESS              数据发送成功
 * @retval NRF_ERROR_INVALID_STATE  未连接或通知未启用
 * @retval NRF_ERROR_INVALID_PARAM  数据长度超过最大传输长度
 */
uint32_t ble_epd_string_send(ble_epd_t* p_epd, uint8_t* p_string, uint16_t length) {
    /* 检查是否已建立BLE连接且通知已启用 */
    if ((p_epd->conn_handle == BLE_CONN_HANDLE_INVALID) || (!p_epd->is_notification_enabled))
        return NRF_ERROR_INVALID_STATE;
    /* 检查数据长度是否超过最大传输限制 */
    if (length > p_epd->max_data_len) return NRF_ERROR_INVALID_PARAM;

    /* 配置GATT通知参数 */
    ble_gatts_hvx_params_t hvx_params;

    memset(&hvx_params, 0, sizeof(hvx_params));

    hvx_params.handle = p_epd->char_handles.value_handle;  /* 特征值句柄 */
    hvx_params.p_data = p_string;                            /* 要发送的数据指针 */
    hvx_params.p_len = &length;                              /* 数据长度指针（发送后会被更新为实际发送长度） */
    hvx_params.type = BLE_GATT_HVX_NOTIFICATION;             /* 通知类型（非指示/Indication） */

    /* 调用SoftDevice API发送通知 */
    return sd_ble_gatts_hvx(p_epd->conn_handle, &hvx_params);
}

/**
 * @brief 定时器回调函数，用于定时更新EPD显示
 *
 * 根据当前显示模式和时间条件决定是否触发GUI更新：
 * - 日历模式（MODE_CALENDAR）：每天午夜00:00:00更新一次（时间戳 % 86400 == 0）
 * - 时钟模式（MODE_CLOCK）：每分钟整更新一次（时间戳 % 60 == 0）
 * - force_update为true时：无条件立即更新
 *
 * 满足更新条件时，构造GUI更新事件并通过app_scheduler调度执行，
 * 实际的EPD刷新操作在epd_gui_update()中完成（在主循环上下文中执行）。
 *
 * @param[in] p_epd         EPD服务结构体指针
 * @param[in] timestamp     当前Unix时间戳
 * @param[in] force_update  是否强制更新（true=无条件触发，false=按模式和时间条件判断）
 */
void ble_epd_on_timer(ble_epd_t* p_epd, uint32_t timestamp, bool force_update) {
    // Update calendar on 00:00:00, clock on every minute
    /* 判断是否满足更新条件：
     * 1. force_update为true：强制更新
     * 2. 日历模式且时间戳为一天的起始（timestamp % 86400 == 0）：每天更新一次
     * 3. 时钟模式且时间戳为整分钟（timestamp % 60 == 0）：每分钟更新一次
     */
    if (force_update || (p_epd->config.display_mode == MODE_CALENDAR && timestamp % 86400 == 0) ||
        (p_epd->config.display_mode == MODE_CLOCK && timestamp % 60 == 0)) {
        /* 构造GUI更新事件，包含EPD服务指针和时间戳 */
        epd_gui_update_event_t event = {p_epd, timestamp};
        /* 将事件投入app_scheduler调度队列，在主循环中异步执行epd_gui_update */
        app_sched_event_put(&event, sizeof(epd_gui_update_event_t), epd_gui_update);
    }
}
