/**
 * @file Lunar.h
 * @brief 农历计算模块头文件
 * @details 本模块提供农历计算相关功能，包括：
 *          - 公历转农历日期转换
 *          - 二十四节气计算
 *          - 天干地支、生肖计算
 *          - 时间戳与日期结构体转换
 * 
 * @note 农历算法要点：
 *       - 农历以1901年1月1日为基准（对应农历1900年11月11日）
 *       - lunar_info数组每项16位，存储该年各月大小和闰月信息
 *       - 节气计算基于太阳黄经，每15度一个节气
 */

#ifndef __LUNAR_H
#define __LUNAR_H

#include <stdint.h>
#include <string.h>

/*============================================================================
 *                              宏定义
 *============================================================================*/

#define YEAR0 (1900)        /* 基准年份，用于年份偏移计算 */
#define EPOCH_YR (1970)     /* Unix时间戳纪元年份：1970年1月1日 00:00:00 */
#define SEC_PER_DY (86400)  /* 一天的秒数：24小时 * 60分钟 * 60秒 = 86400秒 */
#define SEC_PER_HR (3600)   /* 一小时的秒数：60分钟 * 60秒 = 3600秒 */

/*============================================================================
 *                              数据结构定义
 *============================================================================*/

/**
 * @brief 公历日期时间结构体
 * @details 用于存储公历的年、月、日、时、分、秒、星期信息
 */
typedef struct devtm {
    uint16_t tm_year;   /**< 年份（实际年份，如2024） */
    uint8_t tm_mon;     /**< 月份（1-12） */
    uint8_t tm_mday;    /**< 日（1-31） */
    uint8_t tm_hour;    /**< 小时（0-23） */
    uint8_t tm_min;     /**< 分钟（0-59） */
    uint8_t tm_sec;     /**< 秒（0-59） */
    uint8_t tm_wday;    /**< 星期（0-6，0表示周日） */
} tm_t;

/**
 * @brief 农历日期结构体
 * @details 用于存储农历的年、月、日及闰月标志
 *          农历月份名称：正月、二月、三月、四月、五月、六月、
 *                       七月、八月、九月、十月、冬月、腊月
 *          农历日期名称：初一、初二...三十
 */
struct Lunar_Date {
    uint8_t IsLeap;     /**< 闰月标志：0-非闰月，1-当前月份为闰月 */
    uint8_t Date;       /**< 农历日（1-30） */
    uint8_t Month;      /**< 农历月（1-12） */
    uint16_t Year;      /**< 农历年 */
};

/*============================================================================
 *                              外部变量声明
 *============================================================================*/

/** @brief 农历月份名称字符串数组，索引0为无效值，1-12对应正月到腊月 */
extern const char Lunar_MonthString[13][7];

/** @brief 闰月标志字符串数组，索引0为空格，索引1为"闰" */
extern const char Lunar_MonthLeapString[2][4];

/** @brief 农历日期名称字符串数组，索引0为无效值，1-30对应初一到三十 */
extern const char Lunar_DateString[31][7];

/** @brief 星期名称字符串数组，索引0-6对应日、一到六 */
extern const char Lunar_DayString[7][4];

/** @brief 生肖名称字符串数组，索引0-11对应猴、鸡、狗、猪、鼠、牛、虎、兔、龙、蛇、马、羊 */
extern const char Lunar_ZodiacString[12][4];

/** @brief 天干名称字符串数组，索引0-9对应庚、辛、壬、癸、甲、乙、丙、丁、戊、己 */
extern const char Lunar_StemStrig[10][4];

/** @brief 地支名称字符串数组，索引0-11对应申、酉、戌、亥、子、丑、寅、卯、辰、巳、午、未 */
extern const char Lunar_BranchStrig[12][4];

/** @brief 二十四节气名称字符串数组 */
extern const char JieQiStr[24][7];

/*============================================================================
 *                              函数声明
 *============================================================================*/

/**
 * @brief 公历转农历
 * @details 将公历日期转换为农历日期，核心算法流程：
 *          1. 计算公历日期距离基准日的天数
 *          2. 减去农历基准天数
 *          3. 查表计算农历年份
 *          4. 逐月减去各月天数，得到农历月日
 * @param[out] lunar       输出的农历日期结构体指针
 * @param[in]  solar_year  公历年份
 * @param[in]  solar_month 公历月份（1-12）
 * @param[in]  solar_date  公历日期（1-31）
 */
void LUNAR_SolarToLunar(struct Lunar_Date* lunar, uint16_t solar_year, uint8_t solar_month, uint8_t solar_date);

/**
 * @brief 获取生肖索引
 * @details 根据农历年份计算生肖，生肖以农历年为准
 *          生肖顺序：猴(0)、鸡(1)、狗(2)、猪(3)、鼠(4)、牛(5)、
 *                   虎(6)、兔(7)、龙(8)、蛇(9)、马(10)、羊(11)
 * @param[in] lunar 农历日期结构体指针
 * @return 生肖索引（0-11）
 */
uint8_t LUNAR_GetZodiac(const struct Lunar_Date* lunar);

/**
 * @brief 获取天干索引
 * @details 根据农历年份计算天干
 *          天干顺序：庚(0)、辛(1)、壬(2)、癸(3)、甲(4)、
 *                   乙(5)、丙(6)、丁(7)、戊(8)、己(9)
 * @param[in] lunar 农历日期结构体指针
 * @return 天干索引（0-9）
 */
uint8_t LUNAR_GetStem(const struct Lunar_Date* lunar);

/**
 * @brief 获取地支索引
 * @details 根据农历年份计算地支
 *          地支顺序：申(0)、酉(1)、戌(2)、亥(3)、子(4)、丑(5)、
 *                   寅(6)、卯(7)、辰(8)、巳(9)、午(10)、未(11)
 * @param[in] lunar 农历日期结构体指针
 * @return 地支索引（0-11）
 */
uint8_t LUNAR_GetBranch(const struct Lunar_Date* lunar);

/**
 * @brief 获取节气信息（带距离天数）
 * @details 计算指定日期距离最近节气的天数
 * @param[in]  myear  公历年份
 * @param[in]  mmonth 公历月份（1-12）
 * @param[in]  mday   公历日期（1-31）
 * @param[out] day    距离最近节气的天数（0表示当天是节气日）
 * @return 节气索引（0-23），失败返回0xFF
 */
uint8_t GetJieQiStr(uint16_t myear, uint8_t mmonth, uint8_t mday, uint8_t* day);

/**
 * @brief 获取节气日期
 * @details 计算指定月份的节气日期
 *          每月有两个节气，上半月一个，下半月一个
 *          如：GetJieQi(2007, 2, 8, &date) 返回date为当月节气日期
 * @param[in]  myear   公历年份
 * @param[in]  mmonth  公历月份（1-12）
 * @param[in]  mday    公历日期（1-31），用于判断上半月还是下半月
 * @param[out] JQdate  节气日期（1-31）
 * @return 1-成功，0-失败
 */
uint8_t GetJieQi(uint16_t myear, uint8_t mmonth, uint8_t mday, uint8_t* JQdate);

/**
 * @brief Unix时间戳转日期时间结构体
 * @details 将Unix时间戳（秒）转换为年月日时分秒星期结构体
 * @param[in]  unix_time Unix时间戳（秒）
 * @param[out] result    输出的日期时间结构体指针
 */
void transformTime(uint32_t unix_time, struct devtm* result);

/**
 * @brief 日期时间结构体转Unix时间戳
 * @details 将年月日时分秒结构体转换为Unix时间戳（秒）
 * @param[in] result 日期时间结构体指针
 * @return Unix时间戳（秒）
 */
uint32_t transformTimeStruct(struct devtm* result);

/**
 * @brief 获取指定月份第一天的星期
 * @param[in] year  年份
 * @param[in] month 月份（1-12）
 * @return 星期（0-6，0表示周日）
 */
uint8_t get_first_day_week(uint16_t year, uint8_t month);

/**
 * @brief 获取指定月份的最后一天（该月天数）
 * @param[in] year  年份
 * @param[in] month 月份（1-12）
 * @return 该月最后一天日期（28-31）
 */
uint8_t get_last_day(uint16_t year, uint8_t month);

/**
 * @brief 根据年月日计算星期几
 * @param[in] month 月份（1-12）
 * @param[in] day   日期（1-31）
 * @param[in] year  年份
 * @return 星期（0-6，0表示周日）
 */
unsigned char day_of_week_get(unsigned char month, unsigned char day, unsigned short year);

/**
 * @brief 获取指定月份的最大天数
 * @param[in] year  年份（用于判断闰年）
 * @param[in] month 月份（1-12）
 * @return 该月最大天数（28-31）
 */
uint8_t thisMonthMaxDays(uint8_t year, uint8_t month);

#endif /* __LUNAR_H */
