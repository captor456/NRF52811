/**
 * @file    EPD_config.h
 * @brief   EPD（电子纸显示屏）配置结构体定义头文件
 * @details 本文件定义了EPD外设的硬件引脚配置结构体 epd_config_t，
 *          以及对配置进行初始化、读取、写入、清除和判空等操作的函数声明。
 *          配置信息通常存储在非易失性存储器中，用于在系统启动时恢复EPD的引脚连接和显示参数。
 */
#ifndef __EPD_CONFIG_H__
#define __EPD_CONFIG_H__
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief EPD硬件配置结构体
 * @details 包含EPD模块所有相关的SPI引脚、控制引脚及显示参数配置
 */
typedef struct {
    uint8_t mosi_pin;      /**< SPI数据输出引脚（Master Out Slave In） */
    uint8_t sclk_pin;      /**< SPI时钟引脚（Serial Clock） */
    uint8_t cs_pin;        /**< 片选引脚（Chip Select），低电平有效时选中EPD */
    uint8_t dc_pin;        /**< 数据/命令选择引脚（Data/Command），用于区分发送的是命令还是数据 */
    uint8_t rst_pin;       /**< 复位引脚（Reset），用于硬件复位EPD芯片 */
    uint8_t busy_pin;      /**< 忙信号引脚（Busy），高电平时表示EPD正在处理，不可发送新命令 */
    uint8_t bs_pin;        /**< 总线选择引脚（Bus Select），用于选择SPI总线或并行总线模式 */
    uint8_t model_id;      /**< EPD型号ID，对应 epd_model_id_t 枚举值，标识具体的屏幕型号 */
    uint8_t wakeup_pin;    /**< 唤醒引脚（Wake Up），用于从低功耗模式唤醒EPD或主控 */
    uint8_t led_pin;       /**< LED引脚，用于控制背光LED或状态指示灯 */
    uint8_t en_pin;        /**< 电源使能引脚（Enable），控制EPD模块的电源通断 */
    uint8_t display_mode;  /**< 显示模式，配置屏幕的刷新模式或其他显示特性 */
    uint8_t week_start;    /**< 星期起始日，用于时钟/日历类显示界面的星期排列方式 */
} epd_config_t;

/**
 * @brief EPD配置结构体的大小（以uint8_t为单位）
 * @details 用于配置数据的序列化/反序列化操作，例如读写Flash时的长度计算
 */
#define EPD_CONFIG_SIZE (sizeof(epd_config_t) / sizeof(uint8_t))

/**
 * @brief  初始化EPD配置结构体
 * @param  cfg 指向EPD配置结构体的指针，将被填充为默认值
 */
void epd_config_init(epd_config_t* cfg);

/**
 * @brief  从非易失性存储器中读取EPD配置
 * @param  cfg 指向EPD配置结构体的指针，读取的数据将写入此结构体
 */
void epd_config_read(epd_config_t* cfg);

/**
 * @brief  将EPD配置写入非易失性存储器
 * @param  cfg 指向EPD配置结构体的指针，其数据将被持久化保存
 */
void epd_config_write(epd_config_t* cfg);

/**
 * @brief  清除EPD配置，将所有字段重置为零或默认值
 * @param  cfg 指向EPD配置结构体的指针
 */
void epd_config_clear(epd_config_t* cfg);

/**
 * @brief  检查EPD配置是否为空（所有字段均为零或未设置）
 * @param  cfg 指向EPD配置结构体的指针
 * @return true  配置为空（未设置有效值）
 * @return false 配置不为空（已包含有效配置）
 */
bool epd_config_empty(epd_config_t* cfg);

#endif
