/**
 * @file EPD_driver_annotated.h
 * @brief 电子墨水屏(EPD)驱动头文件 - 带详细中文注释
 * 
 * 本文件定义了EPD驱动的核心接口，支持多种驱动IC和屏幕型号。
 * 
 * 支持的驱动IC:
 * - UC81xx系列: UC8159, UC8176, UC8179
 * - SSD16xx系列: SSD1619, SSD1677
 * - JD79xxx系列: JD79668, JD79665
 * 
 * 支持的颜色模式:
 * - 黑白(COLOR_BW)
 * - 黑白红(COLOR_BWR)
 * - 黑白红黄(COLOR_BWRY)
 * 
 * @author tsl0922
 * @license GPL-3.0
 */

#ifndef __EPD_DRIVER_H__
#define __EPD_DRIVER_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "EPD_config.h"     // EPD配置
#include "nrf_delay.h"      // Nordic延时函数
#include "nrf_gpio.h"       // Nordic GPIO控制
#include "nrf_log.h"        // Nordic日志

// EPD调试日志宏
#define EPD_DEBUG(fmt, ...) NRF_LOG_DEBUG("EPD: " fmt "\r\n", ##__VA_ARGS__)

/**
 * @defgroup UC81xx系列驱动命令
 * 
 * UC81xx是UltraChip公司的电子纸驱动IC系列
 * @{
 */
enum {
    UC81xx_PSR = 0x00,    /**< Panel Setting - 面板设置 */
    UC81xx_PWR = 0x01,    /**< Power Setting - 电源设置 */
    UC81xx_POF = 0x02,    /**< Power OFF - 关闭电源 */
    UC81xx_PFS = 0x03,    /**< Power OFF Sequence Setting - 关电序列设置 */
    UC81xx_PON = 0x04,    /**< Power ON - 开启电源 */
    UC81xx_PMES = 0x05,   /**< Power ON Measure - 电源开启测量 */
    UC81xx_BTST = 0x06,   /**< Booster Soft Start - 升压器软启动 */
    UC81xx_DSLP = 0x07,   /**< Deep sleep - 深度睡眠 */
    UC81xx_DTM1 = 0x10,   /**< Display Start Transmission 1 - 显示数据传输1(黑白数据) */
    UC81xx_DSP = 0x11,    /**< Data Stop - 数据停止 */
    UC81xx_DRF = 0x12,    /**< Display Refresh - 显示刷新 */
    UC81xx_DTM2 = 0x13,   /**< Display Start transmission 2 - 显示数据传输2(红色数据) */
    UC81xx_LUTC = 0x20,   /**< VCOM LUT (LUTC) - VCOM查找表 */
    UC81xx_LUTWW = 0x21,  /**< W2W LUT (LUTWW) - 白到白查找表 */
    UC81xx_LUTBW = 0x22,  /**< B2W LUT (LUTBW / LUTR) - 黑到白查找表 */
    UC81xx_LUTWB = 0x23,  /**< W2B LUT (LUTWB / LUTW) - 白到黑查找表 */
    UC81xx_LUTBB = 0x24,  /**< B2B LUT (LUTBB / LUTB) - 黑到黑查找表 */
    UC81xx_PLL = 0x30,    /**< PLL control - 锁相环控制 */
    UC81xx_TSC = 0x40,    /**< Temperature Sensor Calibration - 温度传感器校准 */
    UC81xx_TSE = 0x41,    /**< Temperature Sensor Selection - 温度传感器选择 */
    UC81xx_TSW = 0x42,    /**< Temperature Sensor Write - 温度传感器写入 */
    UC81xx_TSR = 0x43,    /**< Temperature Sensor Read - 温度传感器读取 */
    UC81xx_CDI = 0x50,    /**< Vcom and data interval setting - VCOM和数据间隔设置 */
    UC81xx_LPD = 0x51,    /**< Lower Power Detection - 低功耗检测 */
    UC81xx_TCON = 0x60,   /**< TCON setting - TCON设置 */
    UC81xx_TRES = 0x61,   /**< Resolution setting - 分辨率设置 */
    UC81xx_GSST = 0x65,   /**< GSST Setting - GSST设置 */
    UC81xx_REV = 0x70,    /**< Revision - 读取版本 */
    UC81xx_FLG = 0x71,    /**< Get Status - 获取状态 */
    UC81xx_AMV = 0x80,    /**< Auto Measurement Vcom - 自动测量VCOM */
    UC81xx_VV = 0x81,     /**< Read Vcom Value - 读取VCOM值 */
    UC81xx_VDCS = 0x82,   /**< VCM_DC Setting - VCM_DC设置 */
    UC81xx_PTL = 0x90,    /**< Partial Window - 局部窗口 */
    UC81xx_PTIN = 0x91,   /**< Partial In - 进入局部模式 */
    UC81xx_PTOUT = 0x92,  /**< Partial Out - 退出局部模式 */
    UC81xx_PGM = 0xA0,    /**< Program Mode - 编程模式 */
    UC81xx_APG = 0xA1,    /**< Active Programming - 激活编程 */
    UC81xx_ROTP = 0xA2,   /**< Read OTP - 读取OTP */
    UC81xx_CCSET = 0xE0,  /**< Cascade Setting - 级联设置 */
    UC81xx_PWS = 0xE3,    /**< Power Saving - 省电设置 */
    UC81xx_TSSET = 0xE5,  /**< Force Temperature - 强制温度设置 */
};
/** @} */

/**
 * @defgroup SSD16xx系列驱动命令
 * 
 * SSD16xx是晶门科技(Solomon Systech)的电子纸驱动IC系列
 * @{
 */
enum {
    SSD16xx_GDO_CTR = 0x01,             /**< Driver Output control - 驱动输出控制 */
    SSD16xx_GDV_CTRL = 0x03,            /**< Gate Driving voltage Control - 栅极驱动电压控制 */
    SSD16xx_SDV_CTRL = 0x04,            /**< Source Driving voltage Control - 源极驱动电压控制 */
    SSD16xx_SOFTSTART = 0x0C,           /**< Booster Soft start Control - 升压器软启动控制 */
    SSD16xx_GSCAN_START = 0x0F,         /**< Gate scan start position - 栅极扫描起始位置 */
    SSD16xx_SLEEP_MODE = 0x10,          /**< Deep Sleep mode - 深度睡眠模式 */
    SSD16xx_ENTRY_MODE = 0x11,          /**< Data Entry mode setting - 数据输入模式设置 */
    SSD16xx_SW_RESET = 0x12,            /**< SW RESET - 软件复位 */
    SSD16xx_HV_RD_DETECT = 0x14,        /**< HV Ready Detection - 高压就绪检测 */
    SSD16xx_VCI_DETECT = 0x15,          /**< VCI Detection - VCI检测 */
    SSD16xx_TSENSOR_CTRL = 0x18,        /**< Temperature Sensor Control - 温度传感器控制 */
    SSD16xx_TSENSOR_WRITE = 0x1A,       /**< Temperature Sensor Write - 温度传感器写入 */
    SSD16xx_TSENSOR_READ = 0x1B,        /**< Temperature Sensor Read - 温度传感器读取 */
    SSD16xx_TSENSOR_WRITE_EXT = 0x1C,   /**< Temperature Sensor Write External - 外部温度传感器写入 */
    SSD16xx_MASTER_ACTIVATE = 0x20,     /**< Master Activation - 主激活 */
    SSD16xx_DISP_CTRL1 = 0x21,          /**< Display Update Control 1 - 显示更新控制1 */
    SSD16xx_DISP_CTRL2 = 0x22,          /**< Display Update Control 2 - 显示更新控制2 */
    SSD16xx_WRITE_RAM1 = 0x24,          /**< Write RAM (BW) - 写入黑白RAM */
    SSD16xx_WRITE_RAM2 = 0x26,          /**< Write RAM (RED) - 写入红色RAM */
    SSD16xx_READ_RAM = 0x27,            /**< Read RAM - 读取RAM */
    SSD16xx_VCOM_SENSE = 0x28,          /**< VCOM Sense - VCOM检测 */
    SSD16xx_VCOM_SENSE_DURATON = 0x29,  /**< VCOM Sense Duration - VCOM检测持续时间 */
    SSD16xx_PRGM_VCOM_OTP = 0x2A,       /**< Program VCOM OTP - 编程VCOM OTP */
    SSD16xx_VCOM_CTRL = 0x2B,           /**< VCOM Control - VCOM控制 */
    SSD16xx_VCOM_VOLTAGE = 0x2C,        /**< VCOM Voltage - VCOM电压 */
    SSD16xx_READ_OTP_REG = 0x2D,        /**< OTP Register Read - 读取OTP寄存器 */
    SSD16xx_READ_USER_ID = 0x2E,        /**< User ID Read - 读取用户ID */
    SSD16xx_READ_STATUS = 0x2F,         /**< Status Bit Read - 读取状态位 */
    SSD16xx_PRGM_WS_OTP = 0x30,         /**< Program WS OTP - 编程WS OTP */
    SSD16xx_LOAD_WS_OTP = 0x31,         /**< Load WS OTP - 加载WS OTP */
    SSD16xx_WRITE_LUT = 0x32,           /**< Write LUT register - 写入LUT寄存器 */
    SSD16xx_READ_LUT = 0x33,            /**< Read LUT - 读取LUT */
    SSD16xx_CRC_CALC = 0x34,            /**< CRC calculation - CRC计算 */
    SSD16xx_CRC_STATUS = 0x35,          /**< CRC Status Read - 读取CRC状态 */
    SSD16xx_PRGM_OTP_SELECTION = 0x36,  /**< Program OTP selection - 编程OTP选择 */
    SSD16xx_OTP_SELECTION_CTRL = 0x37,  /**< OTP Selection Control - OTP选择控制 */
    SSD16xx_USER_ID_CTRL = 0x38,        /**< User ID Control - 用户ID控制 */
    SSD16xx_OTP_PROG_MODE = 0x39,       /**< OTP program mode - OTP编程模式 */
    SSD16xx_DUMMY_LINE = 0x3A,          /**< Set dummy line period - 设置虚拟行周期 */
    SSD16xx_GATE_LINE_WIDTH = 0x3B,     /**< Set Gate line width - 设置栅极线宽度 */
    SSD16xx_BORDER_CTRL = 0x3C,         /**< Border Waveform Control - 边框波形控制 */
    SSD16xx_RAM_READ_CTRL = 0x41,       /**< Read RAM Option - RAM读取选项 */
    SSD16xx_RAM_XPOS = 0x44,            /**< Set RAM X address Start/End - 设置RAM X地址 */
    SSD16xx_RAM_YPOS = 0x45,            /**< Set RAM Y address Start/End - 设置RAM Y地址 */
    SSD16xx_AUTO_WRITE_RED_RAM = 0x46,  /**< Auto Write RED RAM - 自动写入红色RAM */
    SSD16xx_AUTO_WRITE_BW_RAM = 0x47,   /**< Auto Write B/W RAM - 自动写入黑白RAM */
    SSD16xx_RAM_XCOUNT = 0x4E,          /**< Set RAM X address counter - 设置RAM X地址计数器 */
    SSD16xx_RAM_YCOUNT = 0x4F,          /**< Set RAM Y address counter - 设置RAM Y地址计数器 */
    SSD16xx_ANALOG_BLOCK_CTRL = 0x74,   /**< Analog Block Control - 模拟块控制 */
    SSD16xx_DIGITAL_BLOCK_CTRL = 0x7E,  /**< Digital Block Control - 数字块控制 */
    SSD16xx_NOP = 0x7F,                 /**< NOP - 空操作 */
};
/** @} */

/**
 * @brief 颜色模式枚举
 * 
 * 定义EPD支持的颜色模式
 */
typedef enum {
    COLOR_BW = 1,     /**< 黑白双色 */
    COLOR_BWR = 2,    /**< 黑白红三色 */
    COLOR_BWRY = 3,   /**< 黑白红黄四色 */
} epd_color_t;

/**
 * @brief 驱动IC类型枚举
 * 
 * 定义支持的驱动IC型号
 */
typedef enum {
    DRV_IC_UC8159 = 0x10,   /**< UC8159驱动IC */
    DRV_IC_UC8176 = 0x11,   /**< UC8176驱动IC */
    DRV_IC_UC8179 = 0x12,   /**< UC8179驱动IC */
    DRV_IC_SSD1619 = 0x20,  /**< SSD1619驱动IC */
    DRV_IC_SSD1677 = 0x21,  /**< SSD1677驱动IC */
    DRV_IC_JD79668 = 0x30,  /**< JD79668驱动IC */
    DRV_IC_JD79665 = 0x31,  /**< JD79665驱动IC */
} epd_drv_ic_t;

/**
 * @brief EPD型号ID枚举
 * 
 * 定义支持的具体屏幕型号
 * 注意：不要修改现有的ID值，保持向后兼容
 */
typedef enum {
    UC8176_420_BW = 1,          /**< UC8176 4.2寸黑白 */
    UC8176_420_BWR = 3,         /**< UC8176 4.2寸黑白红 */
    SSD1619_420_BWR = 2,        /**< SSD1619 4.2寸黑白红 */
    SSD1619_420_BW = 4,         /**< SSD1619 4.2寸黑白 */
    JD79668_420_BWRY = 5,       /**< JD79668 4.2寸黑白红黄 */
    UC8179_750_BW = 6,          /**< UC8179 7.5寸黑白 */
    UC8179_750_BWR = 7,         /**< UC8179 7.5寸黑白红 */
    UC8159_750_LOW_BW = 8,      /**< UC8159 7.5寸低分辨率黑白 */
    UC8159_750_LOW_BWR = 9,     /**< UC8159 7.5寸低分辨率黑白红 */
    SSD1677_750_HD_BW = 10,     /**< SSD1677 7.5寸高清黑白 */
    SSD1677_750_HD_BWR = 11,    /**< SSD1677 7.5寸高清黑白红 */
    JD79665_750_BWRY = 12,      /**< JD79665 7.5寸黑白红黄 */
    JD79665_583_BWRY = 13,      /**< JD79665 5.83寸黑白红黄 */
} epd_model_id_t;

// 前向声明EPD驱动结构体
struct epd_driver;

/**
 * @brief EPD型号信息结构体
 * 
 * 包含特定型号屏幕的所有信息
 */
typedef struct {
    const epd_model_id_t id;        /**< 型号ID */
    const epd_color_t color;        /**< 颜色模式 */
    const struct epd_driver* drv;   /**< 驱动函数表 */
    const epd_drv_ic_t ic;          /**< 驱动IC类型 */
    const uint16_t width;           /**< 屏幕宽度(像素) */
    const uint16_t height;          /**< 屏幕高度(像素) */
} epd_model_t;

/**
 * @brief EPD驱动函数表结构体
 * 
 * 定义了EPD驱动的所有操作函数
 */
typedef struct epd_driver {
    /** 
     * @brief 初始化EPD寄存器
     * @param[in] epd EPD型号信息
     */
    void (*init)(epd_model_t* epd);
    
    /** 
     * @brief 清屏
     * @param[in] epd EPD型号信息
     * @param[in] refresh 是否刷新显示
     */
    void (*clear)(epd_model_t* epd, bool refresh);
    
    /** 
     * @brief 写入图像数据
     * @param[in] epd EPD型号信息
     * @param[in] black 黑白数据缓冲区
     * @param[in] color 彩色数据缓冲区
     * @param[in] x 起始X坐标
     * @param[in] y 起始Y坐标
     * @param[in] w 宽度
     * @param[in] h 高度
     */
    void (*write_image)(epd_model_t* epd, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    
    /** 
     * @brief 写入数据到EPD RAM
     * @param[in] epd EPD型号信息
     * @param[in] cfg 配置参数
     * @param[in] data 数据缓冲区
     * @param[in] len 数据长度
     */
    void (*write_ram)(epd_model_t* epd, uint8_t cfg, uint8_t* data, uint8_t len);
    
    /** 
     * @brief 刷新显示
     * @param[in] epd EPD型号信息
     */
    void (*refresh)(epd_model_t* epd);
    
    /** 
     * @brief 进入睡眠模式
     * @param[in] epd EPD型号信息
     */
    void (*sleep)(epd_model_t* epd);
    
    /** 
     * @brief 读取温度
     * @param[in] epd EPD型号信息
     * @return 温度值(摄氏度)
     */
    int8_t (*read_temp)(epd_model_t* epd);
    
    /** 
     * @brief 读取忙信号
     * @param[in] epd EPD型号信息
     * @return 忙状态
     */
    bool (*read_busy)(epd_model_t* epd);
} epd_driver_t;

/**
 * @defgroup GPIO电平定义
 * @{
 */
#define LOW (0x0)           /**< 低电平 */
#define HIGH (0x1)          /**< 高电平 */
/** @} */

/**
 * @defgroup GPIO模式定义
 * 
 * 兼容Arduino的GPIO模式定义
 * @{
 */
#define DEFAULT (0xFF)          /**< 默认模式 */
#define INPUT (0x0)             /**< 输入模式 */
#define OUTPUT (0x1)            /**< 输出模式 */
#define INPUT_PULLUP (0x2)      /**< 输入上拉模式 */
#define INPUT_PULLDOWN (0x3)    /**< 输入下拉模式 */
/** @} */

/**
 * @defgroup Arduino兼容函数
 * 
 * 提供类似Arduino的GPIO操作函数
 * @{
 */

/**
 * @brief 设置引脚模式
 * @param[in] pin 引脚号
 * @param[in] mode 模式(INPUT/OUTPUT/INPUT_PULLUP/INPUT_PULLDOWN/DEFAULT)
 */
void pinMode(uint32_t pin, uint32_t mode);

/** @brief 数字写 - 设置引脚电平 */
#define digitalWrite(pin, value) nrf_gpio_pin_write(pin, value)

/** @brief 数字读 - 读取引脚电平 */
#define digitalRead(pin) nrf_gpio_pin_read(pin)

/** @brief 延时 - 毫秒级延时 */
#define delay(ms) nrf_delay_ms(ms)
/** @} */

/**
 * @defgroup GPIO操作函数
 * @{
 */

/**
 * @brief 从配置加载GPIO引脚映射
 * 
 * 根据配置结构体设置各功能引脚
 * 
 * @param[in] cfg EPD配置结构体指针
 */
void EPD_GPIO_Load(epd_config_t* cfg);

/**
 * @brief 初始化GPIO
 * 
 * 配置所有EPD相关的GPIO引脚
 */
void EPD_GPIO_Init(void);

/**
 * @brief 反初始化GPIO
 * 
 * 释放GPIO资源，恢复默认状态
 */
void EPD_GPIO_Uninit(void);
/** @} */

/**
 * @defgroup SPI通信函数
 * @{
 */

/**
 * @brief SPI写入数据
 * @param[in] value 数据缓冲区
 * @param[in] len 数据长度
 */
void EPD_SPI_Write(uint8_t* value, uint8_t len);

/**
 * @brief SPI读取数据
 * @param[out] value 数据缓冲区
 * @param[in] len 数据长度
 */
void EPD_SPI_Read(uint8_t* value, uint8_t len);
/** @} */

/**
 * @defgroup EPD核心操作函数
 * @{
 */

/**
 * @brief 写入命令
 * @param[in] cmd 命令字节
 */
void EPD_WriteCmd(uint8_t cmd);

/**
 * @brief 写入数据
 * @param[in] value 数据缓冲区
 * @param[in] len 数据长度
 */
void EPD_WriteData(uint8_t* value, uint8_t len);

/**
 * @brief 读取数据
 * @param[out] value 数据缓冲区
 * @param[in] len 数据长度
 */
void EPD_ReadData(uint8_t* value, uint8_t len);

/**
 * @brief 写入单个字节
 * @param[in] value 数据字节
 */
void EPD_WriteByte(uint8_t value);

/**
 * @brief 读取单个字节
 * @return 读取的数据字节
 */
uint8_t EPD_ReadByte(void);

/**
 * @brief 写入命令和数据的宏
 * 
 * 使用示例: EPD_Write(UC81xx_PSR, 0x0F, 0x89)
 * 
 * @param[in] cmd 命令
 * @param[in] ... 数据字节(可变参数)
 */
#define EPD_Write(cmd, ...)                  \
    do {                                     \
        uint8_t _data[] = {__VA_ARGS__};     \
        EPD_WriteCmd(cmd);                   \
        EPD_WriteData(_data, sizeof(_data)); \
    } while (0)

/**
 * @brief 填充RAM
 * 
 * 用指定值填充EPD的RAM
 * 
 * @param[in] cmd RAM写入命令
 * @param[in] value 填充值
 * @param[in] len 填充长度
 */
void EPD_FillRAM(uint8_t cmd, uint8_t value, uint32_t len);

/**
 * @brief 复位EPD
 * 
 * 产生硬件复位信号
 * 
 * @param[in] status 复位电平
 * @param[in] duration 持续时间(ms)
 */
void EPD_Reset(bool status, uint16_t duration);

/**
 * @brief 读取忙信号
 * @return 忙引脚状态
 */
bool EPD_ReadBusy(void);

/**
 * @brief 等待忙信号
 * 
 * 阻塞等待直到忙信号变为指定状态或超时
 * 
 * @param[in] status 目标状态
 * @param[in] timeout 超时时间(ms)
 */
void EPD_WaitBusy(bool status, uint16_t timeout);
/** @} */

/**
 * @defgroup LED控制函数
 * @{
 */

/** @brief 打开LED */
void EPD_LED_ON(void);

/** @brief 关闭LED */
void EPD_LED_OFF(void);

/** @brief 翻转LED状态 */
void EPD_LED_Toggle(void);

/** @brief LED闪烁一次 */
void EPD_LED_BLINK(void);
/** @} */

/**
 * @brief 读取电池电压
 * 
 * 使用ADC读取当前电池电压
 * 
 * @return 电压值(mV)
 */
uint16_t EPD_ReadVoltage(void);

/**
 * @brief 初始化EPD
 * 
 * 根据型号ID初始化对应的EPD驱动
 * 
 * @param[in] id EPD型号ID
 * @return EPD型号信息结构体指针
 */
epd_model_t* epd_init(epd_model_id_t id);

#endif // __EPD_DRIVER_H__
