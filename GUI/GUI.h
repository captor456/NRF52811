/**
 * @file GUI.h
 * @brief GUI图形界面模块头文件
 * 
 * 本模块提供电子墨水屏的图形界面显示功能，支持以下三种显示模式：
 * - 图片模式(MODE_PICTURE)：显示用户上传的图片
 * - 日历模式(MODE_CALENDAR)：显示月历视图，包含农历、节气、节假日信息
 * - 时钟模式(MODE_CLOCK)：显示大字体时钟，包含日期和农历信息
 * 
 * @author NRF52811 EPD Project
 * @version 1.0
 */

#ifndef __GUI_H
#define __GUI_H

#include "Adafruit_GFX.h"

/**
 * @brief 显示模式枚举类型
 * 
 * 定义GUI支持的三种显示模式
 */
typedef enum {
    MODE_PICTURE = 0,   /**< 图片模式：显示用户上传的图片 */
    MODE_CALENDAR = 1,  /**< 日历模式：显示月历视图，包含农历、节气、节假日 */
    MODE_CLOCK = 2,     /**< 时钟模式：显示大字体时钟界面 */
} display_mode_t;

/**
 * @brief GUI数据结构体
 * 
 * 包含GUI绘制所需的所有数据，包括显示参数、时间信息和设备状态
 */
typedef struct {
    display_mode_t mode;    /**< 当前显示模式（图片/日历/时钟） */
    uint16_t color;         /**< 颜色深度：1=黑白, 2=三色, 3=四色 */
    uint16_t width;         /**< 显示区域宽度（像素） */
    uint16_t height;        /**< 显示区域高度（像素） */
    uint32_t timestamp;     /**< Unix时间戳（秒） */
    uint8_t week_start;     /**< 一周起始日：0=周日开始, 1=周一开始 */
    int8_t temperature;     /**< 当前温度值（摄氏度） */
    uint16_t voltage;       /**< 电池电压（毫伏） */
    char ssid[20];          /**< WiFi SSID名称 */
} gui_data_t;

/**
 * @brief 绘制GUI界面
 * 
 * 根据gui_data_t中的数据绘制对应的GUI界面，支持日历模式和时钟模式。
 * 绘制完成后通过回调函数将显示缓冲区数据传递给EPD驱动进行刷新显示。
 * 
 * @param data          GUI数据指针，包含显示模式、时间、设备状态等信息
 * @param callback      缓冲区回调函数，用于将显示数据传递给EPD驱动
 * @param callback_data 回调函数的用户数据指针
 */
void DrawGUI(gui_data_t* data, buffer_callback callback, void* callback_data);

#endif
