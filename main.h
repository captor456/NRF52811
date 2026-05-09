/**
 * @file    main.h
 * @brief   主程序头文件
 * @details 包含系统级函数声明，包括睡眠管理、看门狗喂狗、时间戳获取与设置等功能。
 *          本头文件为 NRF52811 电子墨水屏项目的顶层头文件，供各模块引用。
 */

#ifndef _MAIN_H_
#define _MAIN_H_

#include <stdint.h>   /* 标准整数类型定义，提供 uint8_t、uint16_t、uint32_t 等 */

/**
 * @brief  进入深度睡眠模式
 * @note   将系统置于低功耗睡眠状态，等待外部事件唤醒（如RTC中断、GPIO中断等）。
 *         在电子墨水屏应用中，通常在刷新完成后调用以节省电量。
 */
void sleep_mode_enter(void);

/**
 * @brief  喂看门狗（复位看门狗定时器）
 * @note   必须在看门狗超时前定期调用，否则系统将被看门狗复位。
 *         建议在主循环或长耗时操作中周期性调用。
 */
void app_feed_wdt(void);

/**
 * @brief  获取当前系统时间戳
 * @return 当前时间戳值（单位：秒，从某个基准点开始计时）
 */
uint32_t timestamp(void);

/**
 * @brief  设置系统时间戳
 * @param  timestamp 要设置的时间戳值（单位：秒）
 */
void set_timestamp(uint32_t timestamp);

#endif
