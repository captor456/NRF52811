/**
 * @file    EPD_driver.h
 * @brief   EPD（电子纸显示屏）驱动核心头文件
 * @details 本文件定义了EPD驱动的核心数据结构和函数接口，支持以下驱动IC系列：
 *          - UC81xx 系列：UC8151、UC8159、UC8176、UC8179（Ultra Chip）
 *          - SSD16xx 系列：SSD1619、SSD1677（Solomon Systech）
 *          - JD79xx 系列：JD79665、JD79668（京东方）
 *
 *          支持的颜色模式：
 *          - 黑白（BW）
 *          - 黑白红（BWR）
 *          - 黑白红黄（BWRY）
 *
 *          支持的屏幕尺寸：4.2英寸、5.83英寸、7.5英寸等
 */
#ifndef __EPD_DRIVER_H__
#define __EPD_DRIVER_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "EPD_config.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"

/** @brief EPD调试日志宏，通过NRF_LOG输出调试信息 */
#define EPD_DEBUG(fmt, ...) NRF_LOG_DEBUG("EPD: " fmt "\r\n", ##__VA_ARGS__)

/* ========================================================================== */
/*                          UC81xx 系列命令定义                                */
/* ========================================================================== */

/**
 * @brief UC81xx系列驱动IC命令枚举
 * @details 适用于UC8151/UC8159/UC8176/UC8179等Ultra Chip电子纸驱动芯片
 */
enum {
    UC81xx_PSR   = 0x00,   /**< 面板设置（Panel Setting），配置面板扫描方向、分辨率等参数 */
    UC81xx_PWR   = 0x01,   /**< 电源设置（Power Setting），配置内部电源电路的电压参数 */
    UC81xx_POF   = 0x02,   /**< 关闭电源（Power OFF），关闭所有内部电源 */
    UC81xx_PFS   = 0x03,   /**< 关电序列设置（Power OFF Sequence Setting），配置关电时序 */
    UC81xx_PON   = 0x04,   /**< 开启电源（Power ON），开启所有内部电源 */
    UC81xx_PMES  = 0x05,   /**< 电源开启测量（Power ON Measure），启动电源测量 */
    UC81xx_BTST  = 0x06,   /**< 升压软启动（Booster Soft Start），配置升压电路的软启动参数 */
    UC81xx_DSLP  = 0x07,   /**< 深度睡眠（Deep Sleep），进入低功耗深度睡眠模式 */
    UC81xx_DTM1  = 0x10,   /**< 显示数据传输1（Display Start Transmission 1），开始传输黑白图像数据 */
    UC81xx_DSP   = 0x11,   /**< 数据停止传输（Data Stop），停止向显示缓冲区写入数据 */
    UC81xx_DRF   = 0x12,   /**< 显示刷新（Display Refresh），触发一次屏幕刷新，将RAM数据输出到屏幕 */
    UC81xx_DTM2  = 0x13,   /**< 显示数据传输2（Display Start Transmission 2），开始传输红色/彩色图像数据 */
    UC81xx_LUTC  = 0x20,   /**< VCOM查找表（VCOM LUT），配置VCOM电压的波形查找表 */
    UC81xx_LUTWW = 0x21,   /**< 白到白查找表（W2W LUT），配置白色到白色的驱动波形 */
    UC81xx_LUTBW = 0x22,   /**< 黑到白查找表（B2W LUT），配置黑色到白色的驱动波形 */
    UC81xx_LUTWB = 0x23,   /**< 白到黑查找表（W2B LUT），配置白色到黑色的驱动波形 */
    UC81xx_LUTBB = 0x24,   /**< 黑到黑查找表（B2B LUT），配置黑色到黑色的驱动波形 */
    UC81xx_PLL   = 0x30,   /**< PLL控制（PLL Control），设置内部PLL时钟频率 */
    UC81xx_TSC   = 0x40,   /**< 温度传感器校准（Temperature Sensor Calibration），启动温度校准 */
    UC81xx_TSE   = 0x41,   /**< 温度传感器选择（Temperature Sensor Selection），选择内部或外部温度传感器 */
    UC81xx_TSW   = 0x42,   /**< 温度传感器写入（Temperature Sensor Write），向温度传感器寄存器写入数据 */
    UC81xx_TSR   = 0x43,   /**< 温度传感器读取（Temperature Sensor Read），从温度传感器寄存器读取数据 */
    UC81xx_CDI   = 0x50,   /**< VCOM和数据间隔设置（VCOM and Data Interval Setting），配置刷新周期参数 */
    UC81xx_LPD   = 0x51,   /**< 低功耗检测（Lower Power Detection），配置低功耗检测阈值 */
    UC81xx_TCON  = 0x60,   /**< TCON设置（TCON Setting），配置时序控制器参数 */
    UC81xx_TRES  = 0x61,   /**< 分辨率设置（Resolution Setting），设置屏幕的水平和垂直分辨率 */
    UC81xx_GSST  = 0x65,   /**< GSST设置（GSST Setting），配置灰度级转换参数 */
    UC81xx_REV   = 0x70,   /**< 版本号（Revision），读取驱动芯片的版本信息 */
    UC81xx_FLG   = 0x71,   /**< 获取状态（Get Status），读取驱动芯片当前的状态标志 */
    UC81xx_AMV   = 0x80,   /**< 自动测量VCOM（Auto Measurement Vcom），自动测量并设置VCOM电压 */
    UC81xx_VV    = 0x81,   /**< 读取VCOM值（Read Vcom Value），读取当前VCOM电压值 */
    UC81xx_VDCS  = 0x82,   /**< VCM_DC设置（VCM_DC Setting），手动设置VCOM直流电压值 */
    UC81xx_PTL   = 0x90,   /**< 局部刷新窗口（Partial Window），设置局部刷新的区域范围 */
    UC81xx_PTIN  = 0x91,   /**< 进入局部刷新模式（Partial In），激活局部刷新模式 */
    UC81xx_PTOUT = 0x92,   /**< 退出局部刷新模式（Partial Out），退出局部刷新模式 */
    UC81xx_PGM   = 0xA0,   /**< 编程模式（Program Mode），进入OTP编程模式 */
    UC81xx_APG   = 0xA1,   /**< 激活编程（Active Programming），执行OTP编程操作 */
    UC81xx_ROTP  = 0xA2,   /**< 读取OTP（Read OTP），从OTP存储区读取数据 */
    UC81xx_CCSET = 0xE0,   /**< 级联设置（Cascade Setting），配置多芯片级联显示参数 */
    UC81xx_PWS   = 0xE3,   /**< 省电设置（Power Saving），配置电源管理相关参数 */
    UC81xx_TSSET = 0xE5,   /**< 强制温度设置（Force Temperature），手动设置温度传感器值 */
};

/* ========================================================================== */
/*                          SSD16xx 系列命令定义                               */
/* ========================================================================== */

/**
 * @brief SSD16xx系列驱动IC命令枚举
 * @details 适用于SSD1619/SSD1677等Solomon Systech电子纸驱动芯片
 */
enum {
    SSD16xx_GDO_CTR          = 0x01,  /**< 驱动输出控制（Driver Output Control），设置门极输出数量和扫描方向 */
    SSD16xx_GDV_CTRL         = 0x03,  /**< 门极驱动电压控制（Gate Driving Voltage Control），配置门极驱动电压 */
    SSD16xx_SDV_CTRL         = 0x04,  /**< 源极驱动电压控制（Source Driving Voltage Control），配置源极驱动电压 */
    SSD16xx_SOFTSTART        = 0x0C,  /**< 升压软启动控制（Booster Soft Start Control），配置升压电路软启动阶段 */
    SSD16xx_GSCAN_START      = 0x0F,  /**< 门极扫描起始位置（Gate Scan Start Position），设置门极扫描的起始行 */
    SSD16xx_SLEEP_MODE       = 0x10,  /**< 深度睡眠模式（Deep Sleep Mode），进入低功耗睡眠状态 */
    SSD16xx_ENTRY_MODE       = 0x11,  /**< 数据输入模式设置（Data Entry Mode Setting），配置数据写入方向和步进 */
    SSD16xx_SW_RESET         = 0x12,  /**< 软件复位（SW RESET），执行软件复位，恢复所有寄存器为默认值 */
    SSD16xx_HV_RD_DETECT     = 0x14,  /**< 高压就绪检测（HV Ready Detection），检测高压电路是否就绪 */
    SSD16xx_VCI_DETECT       = 0x15,  /**< VCI电压检测（VCI Detection），检测VCI电源电压是否正常 */
    SSD16xx_TSENSOR_CTRL     = 0x18,  /**< 温度传感器控制（Temperature Sensor Control），配置内部温度传感器 */
    SSD16xx_TSENSOR_WRITE    = 0x1A,  /**< 温度传感器写入（Temperature Sensor Write），向温度寄存器写入数据 */
    SSD16xx_TSENSOR_READ     = 0x1B,  /**< 温度传感器读取（Temperature Sensor Read），从温度寄存器读取温度值 */
    SSD16xx_TSENSOR_WRITE_EXT = 0x1C, /**< 外部温度传感器写入（Temperature Sensor Write External），向外部温度传感器发送命令 */
    SSD16xx_MASTER_ACTIVATE  = 0x20,  /**< 主激活（Master Activation），触发显示刷新，将RAM数据输出到屏幕 */
    SSD16xx_DISP_CTRL1       = 0x21,  /**< 显示更新控制1（Display Update Control 1），配置显示更新的时序参数 */
    SSD16xx_DISP_CTRL2       = 0x22,  /**< 显示更新控制2（Display Update Control 2），配置显示更新的时钟和模式 */
    SSD16xx_WRITE_RAM1       = 0x24,  /**< 写入RAM - 黑白数据（Write RAM BW），向黑白图像RAM写入数据 */
    SSD16xx_WRITE_RAM2       = 0x26,  /**< 写入RAM - 红色数据（Write RAM RED），向红色图像RAM写入数据 */
    SSD16xx_READ_RAM         = 0x27,  /**< 读取RAM（Read RAM），从图像RAM读取数据 */
    SSD16xx_VCOM_SENSE       = 0x28,  /**< VCOM检测（VCOM Sense），启动VCOM电压自动检测 */
    SSD16xx_VCOM_SENSE_DURATON = 0x29, /**< VCOM检测持续时间（VCOM Sense Duration），设置VCOM检测的持续时间 */
    SSD16xx_PRGM_VCOM_OTP    = 0x2A,  /**< 编程VCOM OTP（Program VCOM OTP），将VCOM值写入OTP存储 */
    SSD16xx_VCOM_CTRL        = 0x2B,  /**< VCOM控制寄存器写入（Write VCOM Control Register），配置VCOM控制参数 */
    SSD16xx_VCOM_VOLTAGE     = 0x2C,  /**< VCOM电压写入（Write VCOM Register），直接设置VCOM电压值 */
    SSD16xx_READ_OTP_REG     = 0x2D,  /**< OTP寄存器读取（OTP Register Read），读取OTP中的显示选项参数 */
    SSD16xx_READ_USER_ID     = 0x2E,  /**< 用户ID读取（User ID Read），读取芯片的用户ID */
    SSD16xx_READ_STATUS      = 0x2F,  /**< 状态读取（Status Bit Read），读取芯片当前状态位 */
    SSD16xx_PRGM_WS_OTP      = 0x30,  /**< 编程WS OTP（Program WS OTP），将波形设置写入OTP存储 */
    SSD16xx_LOAD_WS_OTP      = 0x31,  /**< 加载WS OTP（Load WS OTP），从OTP加载波形设置到寄存器 */
    SSD16xx_WRITE_LUT        = 0x32,  /**< 写入LUT（Write LUT Register），配置驱动波形查找表 */
    SSD16xx_READ_LUT         = 0x33,  /**< 读取LUT（Read LUT），读取当前LUT查找表内容 */
    SSD16xx_CRC_CALC         = 0x34,  /**< CRC计算（CRC Calculation），启动CRC校验计算 */
    SSD16xx_CRC_STATUS       = 0x35,  /**< CRC状态读取（CRC Status Read），读取CRC校验结果 */
    SSD16xx_PRGM_OTP_SELECTION = 0x36, /**< 编程OTP选择（Program OTP Selection），选择要编程的OTP区域 */
    SSD16xx_OTP_SELECTION_CTRL = 0x37, /**< OTP选择控制（Write OTP Selection），配置OTP区域选择 */
    SSD16xx_USER_ID_CTRL     = 0x38,  /**< 用户ID控制（Write User ID Register），写入用户ID寄存器 */
    SSD16xx_OTP_PROG_MODE    = 0x39,  /**< OTP编程模式（OTP Program Mode），进入OTP编程模式 */
    SSD16xx_DUMMY_LINE       = 0x3A,  /**< 设置伪线周期（Set Dummy Line Period），配置门极驱动前的伪线数量 */
    SSD16xx_GATE_LINE_WIDTH  = 0x3B,  /**< 设置门线宽度（Set Gate Line Width），配置门极驱动脉冲宽度 */
    SSD16xx_BORDER_CTRL      = 0x3C,  /**< 边框波形控制（Border Waveform Control），配置屏幕边框的驱动波形 */
    SSD16xx_RAM_READ_CTRL    = 0x41,  /**< RAM读取控制（Read RAM Option），配置RAM读取选项 */
    SSD16xx_RAM_XPOS         = 0x44,  /**< 设置RAM X地址范围（Set RAM X Address Start/End），设置水平方向的起始和结束地址 */
    SSD16xx_RAM_YPOS         = 0x45,  /**< 设置RAM Y地址范围（Set RAM Y Address Start/End），设置垂直方向的起始和结束地址 */
    SSD16xx_AUTO_WRITE_RED_RAM = 0x46, /**< 自动写入红色RAM（Auto Write RED RAM），自动向红色RAM写入规则图案 */
    SSD16xx_AUTO_WRITE_BW_RAM = 0x47,  /**< 自动写入黑白RAM（Auto Write B/W RAM），自动向黑白RAM写入规则图案 */
    SSD16xx_RAM_XCOUNT       = 0x4E,  /**< 设置RAM X地址计数器（Set RAM X Address Counter），设置当前X写入地址 */
    SSD16xx_RAM_YCOUNT       = 0x4F,  /**< 设置RAM Y地址计数器（Set RAM Y Address Counter），设置当前Y写入地址 */
    SSD16xx_ANALOG_BLOCK_CTRL = 0x74,  /**< 模拟模块控制（Set Analog Block Control），控制模拟电路模块的开关 */
    SSD16xx_DIGITAL_BLOCK_CTRL = 0x7E, /**< 数字模块控制（Set Digital Block Control），控制数字电路模块的开关 */
    SSD16xx_NOP              = 0x7F,  /**< 空操作（NOP），无操作命令，用于延时或对齐 */
};

/**
 * @brief EPD颜色模式枚举
 * @details 定义电子纸显示屏支持的颜色模式
 */
typedef enum {
    COLOR_BW   = 1,  /**< 黑白模式（Black & White），仅支持黑白两色显示 */
    COLOR_BWR  = 2,  /**< 黑白红模式（Black & White & Red），支持黑色、白色和红色三色显示 */
    COLOR_BWRY = 3,  /**< 黑白红黄模式（Black & White & Red & Yellow），支持黑色、白色、红色和黄色四色显示 */
} epd_color_t;

/**
 * @brief EPD驱动IC类型枚举
 * @details 定义所有支持的电子纸驱动芯片型号
 */
typedef enum {
    DRV_IC_UC8159  = 0x10,  /**< UC8151/UC8159驱动IC（Ultra Chip），支持7.5英寸低功耗系列 */
    DRV_IC_UC8176  = 0x11,  /**< UC8176驱动IC（Ultra Chip），支持4.2英寸系列 */
    DRV_IC_UC8179  = 0x12,  /**< UC8179驱动IC（Ultra Chip），支持7.5英寸标准系列 */
    DRV_IC_SSD1619 = 0x20,  /**< SSD1619驱动IC（Solomon Systech），支持4.2英寸系列 */
    DRV_IC_SSD1677 = 0x21,  /**< SSD1677驱动IC（Solomon Systech），支持7.5英寸高清系列 */
    DRV_IC_JD79668 = 0x30,  /**< JD79668驱动IC（京东方），支持4.2英寸四色系列 */
    DRV_IC_JD79665 = 0x31,  /**< JD79665驱动IC（京东方），支持7.5英寸和5.83英寸四色系列 */
} epd_drv_ic_t;

/**
 * @brief EPD型号ID枚举
 * @details 定义所有支持的电子纸屏幕型号，命名规则：驱动IC_尺寸_颜色
 *          注意：请勿修改已有的ID值，以免影响已存储的配置兼容性
 */
typedef enum {
    UC8176_420_BW        = 1,   /**< UC8176驱动，4.2英寸，黑白（BW） */
    SSD1619_420_BWR      = 2,   /**< SSD1619驱动，4.2英寸，黑白红（BWR） */
    UC8176_420_BWR       = 3,   /**< UC8176驱动，4.2英寸，黑白红（BWR） */
    SSD1619_420_BW       = 4,   /**< SSD1619驱动，4.2英寸，黑白（BW） */
    JD79668_420_BWRY     = 5,   /**< JD79668驱动，4.2英寸，黑白红黄（BWRY） */
    UC8179_750_BW        = 6,   /**< UC8179驱动，7.5英寸，黑白（BW） */
    UC8179_750_BWR       = 7,   /**< UC8179驱动，7.5英寸，黑白红（BWR） */
    UC8159_750_LOW_BW    = 8,   /**< UC8159驱动，7.5英寸低功耗版，黑白（BW） */
    UC8159_750_LOW_BWR   = 9,   /**< UC8159驱动，7.5英寸低功耗版，黑白红（BWR） */
    SSD1677_750_HD_BW    = 10,  /**< SSD1677驱动，7.5英寸高清版，黑白（BW） */
    SSD1677_750_HD_BWR   = 11,  /**< SSD1677驱动，7.5英寸高清版，黑白红（BWR） */
    JD79665_750_BWRY     = 12,  /**< JD79665驱动，7.5英寸，黑白红黄（BWRY） */
    JD79665_583_BWRY     = 13,  /**< JD79665驱动，5.83英寸，黑白红黄（BWRY） */
} epd_model_id_t;

struct epd_driver;

/**
 * @brief EPD型号描述结构体
 * @details 将型号ID、颜色模式、驱动函数表、驱动IC类型和屏幕分辨率绑定在一起，
 *          构成一个完整的EPD设备描述
 */
typedef struct {
    const epd_model_id_t id;           /**< 型号ID，对应 epd_model_id_t 枚举值 */
    const epd_color_t color;           /**< 颜色模式，对应 epd_color_t 枚举值 */
    const struct epd_driver* drv;      /**< 驱动函数表指针，指向对应IC的驱动操作函数集 */
    const epd_drv_ic_t ic;             /**< 驱动IC类型，对应 epd_drv_ic_t 枚举值 */
    const uint16_t width;              /**< 屏幕有效显示宽度（像素） */
    const uint16_t height;             /**< 屏幕有效显示高度（像素） */
} epd_model_t;

/**
 * @brief EPD驱动函数表结构体
 * @details 定义了EPD驱动的所有操作接口，不同驱动IC通过实现各自的函数表来提供统一的上层调用接口
 */
typedef struct epd_driver {
    void (*init)(epd_model_t* epd);                /**< 初始化电子纸寄存器，配置显示参数和LUT波形 */
    void (*clear)(epd_model_t* epd, bool refresh);  /**< 清屏操作，将屏幕清为白色；refresh=true时立即刷新显示 */
    void (*write_image)(epd_model_t* epd, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w,
                        uint16_t h);                                               /**< 写入图像数据到指定区域；black为黑白数据，color为彩色数据（红色/黄色），(x,y)为起始坐标，(w,h)为图像尺寸 */
    void (*write_ram)(epd_model_t* epd, uint8_t cfg, uint8_t* data, uint8_t len); /**< 写入原始数据到EPD RAM；cfg指定目标RAM（黑白或彩色），data为数据缓冲区，len为数据长度 */
    void (*refresh)(epd_model_t* epd);     /**< 触发显示刷新，将RAM中的图像缓冲数据输出到屏幕进行显示 */
    void (*sleep)(epd_model_t* epd);       /**< 进入睡眠模式，关闭电源以降低功耗；下次使用前需重新初始化 */
    int8_t (*read_temp)(epd_model_t* epd); /**< 读取驱动芯片内置温度传感器的温度值，返回摄氏温度 */
    bool (*read_busy)(epd_model_t* epd);   /**< 读取忙引脚电平状态；返回true表示EPD正在处理中，不可发送新命令 */
} epd_driver_t;

/* ========================================================================== */
/*                              GPIO模式宏定义                                 */
/* ========================================================================== */

#define LOW  (0x0)   /**< 低电平 */
#define HIGH (0x1)   /**< 高电平 */

#define DEFAULT       (0xFF)  /**< 默认模式（未配置） */
#define INPUT         (0x0)   /**< 输入模式 */
#define OUTPUT        (0x1)   /**< 输出模式 */
#define INPUT_PULLUP  (0x2)   /**< 输入模式，内部上拉电阻使能 */
#define INPUT_PULLDOWN (0x3)  /**< 输入模式，内部下拉电阻使能 */

/* ========================================================================== */
/*                      Arduino风格GPIO/SPI封装函数                            */
/* ========================================================================== */

/**
 * @brief  配置GPIO引脚模式（Arduino风格封装）
 * @param  pin  引脚编号
 * @param  mode 引脚模式（INPUT/OUTPUT/INPUT_PULLUP/INPUT_PULLDOWN）
 */
void pinMode(uint32_t pin, uint32_t mode);

/** @brief 写入GPIO引脚电平值（宏，映射到nrf_gpio_pin_write） */
#define digitalWrite(pin, value) nrf_gpio_pin_write(pin, value)

/** @brief 读取GPIO引脚电平值（宏，映射到nrf_gpio_pin_read） */
#define digitalRead(pin) nrf_gpio_pin_read(pin)

/** @brief 毫秒级延时（宏，映射到nrf_delay_ms） */
#define delay(ms) nrf_delay_ms(ms)

/* ========================================================================== */
/*                              GPIO操作函数                                   */
/* ========================================================================== */

/**
 * @brief  从配置结构体加载GPIO引脚映射关系
 * @param  cfg 指向EPD配置结构体的指针，包含各引脚编号
 */
void EPD_GPIO_Load(epd_config_t* cfg);

/**
 * @brief  初始化EPD相关的所有GPIO引脚，配置为正确的输入/输出模式和初始电平
 */
void EPD_GPIO_Init(void);

/**
 * @brief  反初始化EPD相关的GPIO引脚，将引脚恢复为默认状态以降低功耗
 */
void EPD_GPIO_Uninit(void);

/* ========================================================================== */
/*                              SPI操作函数                                    */
/* ========================================================================== */

/**
 * @brief  通过SPI总线写入数据
 * @param  value 待写入的数据缓冲区指针
 * @param  len   待写入的数据长度（字节）
 */
void EPD_SPI_Write(uint8_t* value, uint8_t len);

/**
 * @brief  通过SPI总线读取数据
 * @param  value 接收数据的缓冲区指针
 * @param  len   要读取的数据长度（字节）
 */
void EPD_SPI_Read(uint8_t* value, uint8_t len);

/* ========================================================================== */
/*                            EPD底层操作函数                                   */
/* ========================================================================== */

/**
 * @brief  向EPD发送命令字节（DC引脚拉低）
 * @param  cmd 命令字节
 */
void EPD_WriteCmd(uint8_t cmd);

/**
 * @brief  向EPD发送数据字节序列（DC引脚拉高）
 * @param  value 数据缓冲区指针
 * @param  len   数据长度（字节）
 */
void EPD_WriteData(uint8_t* value, uint8_t len);

/**
 * @brief  从EPD读取数据字节序列（DC引脚拉高）
 * @param  value 接收数据的缓冲区指针
 * @param  len   要读取的数据长度（字节）
 */
void EPD_ReadData(uint8_t* value, uint8_t len);

/**
 * @brief  向EPD写入单个数据字节
 * @param  value 要写入的字节值
 */
void EPD_WriteByte(uint8_t value);

/**
 * @brief  从EPD读取单个数据字节
 * @return 读取到的字节值
 */
uint8_t EPD_ReadByte(void);

/**
 * @brief  向EPD发送命令并附带数据（组合宏）
 * @param  cmd 命令字节
 * @param  ... 紧跟命令后的数据字节（可变参数）
 * @note   用法示例：EPD_Write(0x61, 0x03, 0x20, 0x01); 发送命令0x61及3字节数据
 */
#define EPD_Write(cmd, ...)                  \
    do {                                     \
        uint8_t _data[] = {__VA_ARGS__};     \
        EPD_WriteCmd(cmd);                   \
        EPD_WriteData(_data, sizeof(_data)); \
    } while (0)

/**
 * @brief  用指定值填充EPD RAM区域
 * @param  cmd    目标RAM命令（如UC81xx_DTM1或UC81xx_DTM2）
 * @param  value  填充的字节值
 * @param  len    填充的长度（字节）
 */
void EPD_FillRAM(uint8_t cmd, uint8_t value, uint32_t len);

/**
 * @brief  执行EPD硬件复位操作
 * @param  status   复位结束后rst引脚保持的电平（true=高电平，false=低电平）
 * @param  duration 复位脉冲持续时间（毫秒）
 */
void EPD_Reset(bool status, uint16_t duration);

/**
 * @brief  读取EPD忙引脚当前电平状态
 * @return true  忙状态（EPD正在处理）
 * @return false 空闲状态（EPD可以接收新命令）
 */
bool EPD_ReadBusy(void);

/**
 * @brief  等待EPD忙状态结束
 * @param  status   期望的忙引脚电平（true=等待高电平结束，false=等待低电平结束）
 * @param  timeout  超时时间（毫秒），超时后强制返回
 */
void EPD_WaitBusy(bool status, uint16_t timeout);

/* ========================================================================== */
/*                              LED控制函数                                    */
/* ========================================================================== */

/** @brief 点亮LED */
void EPD_LED_ON(void);

/** @brief 熄灭LED */
void EPD_LED_OFF(void);

/** @brief 翻转LED状态（亮变灭，灭变亮） */
void EPD_LED_Toggle(void);

/** @brief LED闪烁一次（快速亮灭） */
void EPD_LED_BLINK(void);

/* ========================================================================== */
/*                              电压检测函数                                   */
/* ========================================================================== */

/**
 * @brief  读取VDD电源电压值
 * @return 电压值（单位由具体实现决定，通常为毫伏）
 */
uint16_t EPD_ReadVoltage(void);

/* ========================================================================== */
/*                              EPD初始化函数                                   */
/* ========================================================================== */

/**
 * @brief  根据型号ID初始化EPD设备
 * @param  id  EPD型号ID，对应 epd_model_id_t 枚举值
 * @return 指向已初始化的 epd_model_t 结构体的指针，失败时返回NULL
 */
epd_model_t* epd_init(epd_model_id_t id);

#endif
