/**
 * @file GUI.c
 * @brief GUI图形界面模块实现文件
 * 
 * 本文件实现了电子墨水屏的图形界面绘制功能，包括：
 * - 日历界面绘制（月历、农历、节气、节假日、放假调休标记）
 * - 时钟界面绘制（大字体时间显示、日期、农历信息）
 * - 电池电量显示
 * - 时间同步提示
 * 
 * @author NRF52811 EPD Project
 * @version 1.0
 */

#include "GUI.h"

#include <stdio.h>
#include <time.h>

#include "Lunar.h"
#include "fonts.h"

/*============================================================================
 * 宏定义
 *============================================================================*/

/** @brief 计算数组元素个数 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/** @brief 带样式的格式化打印宏，自动设置前景色、背景色和字体 */
#define GFX_printf_styled(gfx, fg, bg, font, ...) \
    GFX_setTextColor(gfx, fg, bg);                \
    GFX_setFont(gfx, font);                       \
    GFX_printf(gfx, __VA_ARGS__);

/** @brief 判断是否使用大布局（高度>=400像素时使用） */
#define large_layout(data) ((data)->height >= 400)

/*============================================================================
 * 数据结构定义
 *============================================================================*/

/**
 * @brief 节日数据结构
 * 
 * 用于存储公历或农历节日的信息
 */
typedef struct {
    uint8_t month;      /**< 月份（1-12） */
    uint8_t day;        /**< 日期（1-31） */
    char name[10];      /**< 节日名称（最多3个汉字+结束符） */
} Festival;

/*============================================================================
 * 静态数据表
 *============================================================================*/

/**
 * @brief 公历节日表
 * 
 * 存储固定的公历节日，包括元旦、情人节、劳动节、国庆节、圣诞节等
 */
static const Festival festivals[] = {
    {1, 1, "元旦节"},  {2, 14, "情人节"}, {3, 8, "妇女节"},  {3, 12, "植树节"},  {4, 1, "愚人节"},
    {5, 1, "劳动节"},  {5, 4, "青年节"},  {6, 1, "儿童节"},  {7, 1, "建党节"},   {8, 1, "建军节"},
    {9, 10, "教师节"}, {10, 1, "国庆节"}, {11, 1, "万圣节"}, {12, 24, "平安夜"}, {12, 25, "圣诞节"},
};

/**
 * @brief 农历节日表
 * 
 * 存储固定的农历节日，包括春节、元宵节、端午节、中秋节等
 */
static const Festival festivals_lunar[] = {
    {1, 1, "春节"},    {1, 15, "元宵节"}, {2, 2, "龙抬头"},  {5, 5, "端午节"},  {7, 7, "七夕节"}, {7, 15, "中元节"},
    {8, 15, "中秋节"}, {9, 9, "重阳节"},  {10, 1, "寒衣节"}, {12, 8, "腊八节"}, {12, 30, "除夕"},
};

/**
 * @brief 放假和调休数据
 * 
 * 存储指定年份的放假安排和调休工作日信息
 * 数据格式：高4位表示是否为调休工作日（>0为调休），中间4位为月份，低8位为日期
 * 需要每年更新此数据
 */
#define HOLIDAY_YEAR 2026
static const uint16_t holidays[] = {
    0x0101, 0x0102, 0x0103, 0x1104, 0x120E, 0x020F, 0x0210, 0x0211, 0x0212, 0x0213, 0x0214, 0x0215, 0x0216,
    0x0217, 0x121C, 0x0404, 0x0405, 0x0406, 0x0501, 0x0502, 0x0503, 0x0504, 0x0505, 0x1509, 0x0613, 0x0614,
    0x0615, 0x0919, 0x091A, 0x091B, 0x1914, 0x0A01, 0x0A02, 0x0A03, 0x0A04, 0x0A05, 0x0A06, 0x0A07, 0x1A0A,
};

/*============================================================================
 * 静态函数实现
 *============================================================================*/

/**
 * @brief 查询指定日期是否为节假日或调休工作日
 * 
 * @param mon   月份（1-12）
 * @param day   日期（1-31）
 * @param work  输出参数，true表示调休工作日，false表示节假日
 * @return true 如果是节假日或调休工作日，false 否则
 */
static bool GetHoliday(uint8_t mon, uint8_t day, bool* work) {
    for (uint8_t i = 0; i < ARRAY_SIZE(holidays); i++) {
        if (((holidays[i] >> 8) & 0xF) == mon && (holidays[i] & 0xFF) == day) {
            *work = ((holidays[i] >> 12) & 0xF) > 0;
            return true;
        }
    }
    return false;
}

/**
 * @brief 获取指定日期的节日或节气名称
 * 
 * 按优先级查询：农历节日 -> 除夕 -> 母亲节/父亲节/感恩节 -> 公历节日 -> 二十四节气
 * 
 * @param year      年份
 * @param mon       月份（1-12）
 * @param day       日期（1-31）
 * @param week      星期（0=周日, 1-6=周一至周六）
 * @param Lunar     农历日期结构体指针
 * @param festival  输出参数，节日或节气名称字符串
 * @return true     如果找到节日或节气，false 否则
 */
static bool GetFestival(uint16_t year, uint8_t mon, uint8_t day, uint8_t week, struct Lunar_Date* Lunar,
                        char* festival) {
    /* 查询农历节日 */
    for (uint8_t i = 0; i < ARRAY_SIZE(festivals_lunar); i++) {
        if (Lunar->Month == festivals_lunar[i].month && Lunar->Date == festivals_lunar[i].day) {
            strcpy(festival, festivals_lunar[i].name);
            return true;
        }
    }

    /* 除夕判断：春节前一天（农历12月29日或30日） */
    if (Lunar->Month == 12 && Lunar->Date == 29) {
        struct Lunar_Date nextLunar;
        struct devtm tm = {year, mon, day, 0, 0, 0, week};
        transformTime(transformTimeStruct(&tm) + 86400, &tm);
        LUNAR_SolarToLunar(&nextLunar, tm.tm_year + YEAR0, tm.tm_mon + 1, tm.tm_mday);
        if (nextLunar.Month == 1 && nextLunar.Date == 1) {
            strcpy(festival, "除夕");
            return true;
        }
    }
    
    /* 母亲节：五月第二个星期日 */
    if (mon == 5 && week == 0 && day >= 8 && day <= 14) {
        strcpy(festival, "母亲节");
        return true;
    }
    
    /* 父亲节：六月第三个星期日 */
    if (mon == 6 && week == 0 && day >= 15 && day <= 21) {
        strcpy(festival, "父亲节");
        return true;
    }
    
    /* 感恩节：十一月第四个星期四 */
    if (mon == 11 && week == 4 && day >= 22 && day <= 28) {
        strcpy(festival, "感恩节");
        return true;
    }

    /* 查询公历节日 */
    for (uint8_t i = 0; i < ARRAY_SIZE(festivals); i++) {
        if (mon == festivals[i].month && day == festivals[i].day) {
            strcpy(festival, festivals[i].name);
            return true;
        }
    }

    /* 查询二十四节气 */
    uint8_t JQdate;
    if (GetJieQi(year, mon, day, &JQdate) && JQdate == day) {
        uint8_t JQ = (mon - 1) * 2;
        if (day >= 15) JQ++;
        strcpy(festival, JieQiStr[JQ]);
        if (JQ == 6)  /* 清明节特殊处理 */
            strcat(festival, "节");

        return true;
    }

    return false;
}

/**
 * @brief 绘制时间同步提示框
 * 
 * 当检测到时间未同步（默认为2025年1月）时，显示同步提示信息
 * 
 * @param gfx   图形上下文指针
 * @param data  GUI数据指针
 */
static void DrawTimeSyncTip(Adafruit_GFX* gfx, gui_data_t* data) {
    const char* title = "SYNC TIME!";
    const char* url = "https://tsl0922.github.io/EPD-nRF5";

    GFX_setFont(gfx, u8g2_font_wqy9_t_lunar);

    int16_t fh = GFX_getFontHeight(gfx);
    int16_t box_w = GFX_getUTF8Width(gfx, url) + 20;
    int16_t box_h = fh * 2 + 20;
    int16_t box_x = (data->width - box_w) / 2;
    int16_t box_y = data->height / 2 - box_h / 2;

    /* 绘制提示框背景和边框 */
    GFX_fillRect(gfx, box_x, box_y, box_w, box_h, GFX_WHITE);
    GFX_drawRoundRect(gfx, box_x, box_y, box_w, box_h, 5, GFX_BLACK);
    
    /* 绘制标题文字 */
    GFX_setTextColor(gfx, GFX_RED, GFX_WHITE);
    GFX_setCursor(gfx, box_x + (box_w - GFX_getUTF8Width(gfx, title)) / 2, box_y + 5 + fh);
    GFX_printf(gfx, title);
    
    /* 绘制URL地址 */
    GFX_setTextColor(gfx, GFX_BLACK, GFX_WHITE);
    GFX_setCursor(gfx, box_x + 10, box_y + box_h - GFX_getFontAscent(gfx));
    GFX_printf(gfx, url);
}

/**
 * @brief 根据电池电压计算电量百分比
 * 
 * 使用分段线性映射将电池电压转换为电量百分比（0-100%）
 * 
 * @param voltage 电池电压（毫伏）
 * @return 电量百分比（0-100）
 */
static uint8_t batt_cal(uint16_t voltage) {
    uint16_t adc_sample = (voltage * 2047) / 3600;
    if (adc_sample > 1705)
        return 100;
    else if (adc_sample <= 1705 && adc_sample > 1584)
        return 28 + (uint8_t)(((((adc_sample - 1584) << 16) / (1705 - 1584)) * 72) >> 16);
    else if (adc_sample <= 1584 && adc_sample > 1360)
        return 4 + (uint8_t)(((((adc_sample - 1360) << 16) / (1584 - 1360)) * 24) >> 16);
    else if (adc_sample <= 1360 && adc_sample > 1136)
        return (uint8_t)(((((adc_sample - 1136) << 16) / (1360 - 1136)) * 4) >> 16);
    else
        return 0;
}

/**
 * @brief 绘制电池电量图标和电压值
 * 
 * 在指定位置绘制电池图标，并显示当前电压值
 * 
 * @param gfx     图形上下文指针
 * @param x       绘制位置的右边界X坐标
 * @param y       绘制位置的Y坐标
 * @param iw      电池图标宽度（像素）
 * @param voltage 电池电压（毫伏）
 */
static void DrawBattery(Adafruit_GFX* gfx, int16_t x, int16_t y, uint8_t iw, uint16_t voltage) {
    x -= iw;
    uint8_t level = batt_cal(voltage);
    
    /* 显示电压值 */
    GFX_setFont(gfx, u8g2_font_wqy9_t_lunar);
    GFX_setCursor(gfx, x - GFX_getUTF8Width(gfx, "3.2V") - 2, y + 9);
    GFX_printf(gfx, "%d.%dV", voltage / 1000, (voltage % 1000) / 100);
    
    /* 绘制电池外框 */
    GFX_fillRect(gfx, x, y, iw, 10, GFX_WHITE);
    GFX_drawRect(gfx, x, y, iw, 10, GFX_BLACK);
    
    /* 绘制电池正极凸起 */
    GFX_fillRect(gfx, x + iw, y + 4, 2, 2, GFX_BLACK);
    
    /* 绘制电量填充 */
    GFX_fillRect(gfx, x + 2, y + 2, 16 * level / 100, 6, GFX_BLACK);
}

/**
 * @brief 获取指定日期是当年第几周
 * 
 * 使用ISO周数标准计算
 * 
 * @param year  年份（tm_year格式，实际年份需减1900）
 * @param mon   月份（0-11）
 * @param mday  日期（1-31）
 * @param wday  星期（0-6）
 * @return 周数（1-53）
 */
static uint8_t GetWeekOfYear(uint8_t year, uint8_t mon, uint8_t mday, uint8_t wday) {
    struct tm tm = {0};
    tm.tm_year = year;
    tm.tm_mon = mon;
    tm.tm_mday = mday;
    tm.tm_wday = wday;
    tm.tm_isdst = -1;
    mktime(&tm);
    char buffer[3] = {0};
    strftime(buffer, 3, "%V", &tm);
    return atoi(buffer);
}

/**
 * @brief 绘制日历界面的日期头部
 * 
 * 显示年月信息、农历日期、干支年份、生肖、周数、电池电量和WiFi SSID
 * 
 * @param gfx   图形上下文指针
 * @param x     绘制起始X坐标
 * @param y     绘制起始Y坐标
 * @param tm    公历时间结构体指针
 * @param Lunar 农历日期结构体指针
 * @param data  GUI数据指针
 */
static void DrawDateHeader(Adafruit_GFX* gfx, int16_t x, int16_t y, tm_t* tm, struct Lunar_Date* Lunar,
                           gui_data_t* data) {
    /* 显示公历年月 */
    GFX_setCursor(gfx, x, y - 2);
    GFX_printf_styled(gfx, GFX_RED, GFX_WHITE, u8g2_font_helvB18_tn, "%d", tm->tm_year + YEAR0);
    GFX_printf_styled(gfx, GFX_BLACK, GFX_WHITE, u8g2_font_wqy12_t_lunar, "年");
    GFX_printf_styled(gfx, GFX_RED, GFX_WHITE, u8g2_font_helvB18_tn, "%d", tm->tm_mon + 1);
    GFX_printf_styled(gfx, GFX_BLACK, GFX_WHITE, u8g2_font_wqy12_t_lunar, "月");

    int16_t tx = gfx->tx;
    int16_t ty = y;

    /* 显示农历日期和周数 */
    GFX_setFont(gfx, u8g2_font_wqy9_t_lunar);
    GFX_setCursor(gfx, tx, ty);
    if (Lunar->IsLeap) GFX_printf(gfx, " ");
    GFX_printf(gfx, "%s%s%s", Lunar_MonthLeapString[Lunar->IsLeap], Lunar_MonthString[Lunar->Month],
               Lunar_DateString[Lunar->Date]);
    GFX_setTextColor(gfx, GFX_RED, GFX_WHITE);
    GFX_printf(gfx, " [%d周]", GetWeekOfYear(tm->tm_year, tm->tm_mon, tm->tm_mday, tm->tm_wday));

    /* 显示干支年份和生肖 */
    GFX_setCursor(gfx, tx, ty - 14);
    GFX_setTextColor(gfx, GFX_BLACK, GFX_WHITE);
    GFX_printf(gfx, " %s%s年", Lunar_StemStrig[LUNAR_GetStem(Lunar)], Lunar_BranchStrig[LUNAR_GetBranch(Lunar)]);
    GFX_setTextColor(gfx, GFX_RED, GFX_WHITE);
    GFX_printf(gfx, " [%s]", Lunar_ZodiacString[LUNAR_GetZodiac(Lunar)]);

    /* 显示电池电量和SSID */
    GFX_setTextColor(gfx, GFX_BLACK, GFX_WHITE);
    DrawBattery(gfx, data->width - 10 - 2, large_layout(data) ? 16 : 6, 20, data->voltage);
    GFX_setCursor(gfx, data->width - GFX_getUTF8Width(gfx, data->ssid) - 10, y);
    GFX_printf(gfx, "%s", data->ssid);
}

/**
 * @brief 绘制日历界面的星期头部
 * 
 * 显示一周七天的星期名称，周六周日用红色标识
 * 
 * @param gfx   图形上下文指针
 * @param x     绘制起始X坐标
 * @param y     绘制起始Y坐标
 * @param data  GUI数据指针
 */
static void DrawWeekHeader(Adafruit_GFX* gfx, int16_t x, int16_t y, gui_data_t* data) {
    GFX_setFont(gfx, large_layout(data) ? u8g2_font_wqy12_t_lunar : u8g2_font_wqy9_t_lunar);
    uint8_t w = (data->width - 2 * x) / 7;
    uint8_t h = large_layout(data) ? 32 : 24;
    uint8_t r = (data->width - 2 * x) % 7;
    uint8_t fh = (h - GFX_getFontHeight(gfx)) / 2 + GFX_getFontAscent(gfx) + 1;
    int16_t cw = GFX_getUTF8Width(gfx, Lunar_DayString[0]);
    
    /* 绘制七个星期标签 */
    for (int i = 0; i < 7; i++) {
        uint8_t day = (data->week_start + i) % 7;
        uint16_t bg = (day == 0 || day == 6) ? GFX_RED : GFX_BLACK;
        GFX_fillRect(gfx, x + i * w, y, i == 6 ? (w + r) : w, h, bg);
        GFX_setTextColor(gfx, GFX_WHITE, bg);
        GFX_setCursor(gfx, x + (w - cw) / 2 + i * w, y + fh);
        GFX_printf(gfx, "%s", Lunar_DayString[day]);
    }
}

/**
 * @brief 绘制日历界面的月历主体
 * 
 * 显示当月所有日期，包括农历、节日、节气、放假调休标记
 * 当天日期用红色圆圈标识
 * 
 * @param gfx   图形上下文指针
 * @param x     绘制起始X坐标
 * @param y     绘制起始Y坐标
 * @param tm    公历时间结构体指针
 * @param Lunar 农历日期结构体指针（用于临时存储）
 * @param data  GUI数据指针
 */
static void DrawMonthDays(Adafruit_GFX* gfx, int16_t x, int16_t y, tm_t* tm, struct Lunar_Date* Lunar,
                          gui_data_t* data) {
    /* 计算月份信息 */
    uint8_t firstDayWeek = get_first_day_week(tm->tm_year + YEAR0, tm->tm_mon + 1);
    int8_t adjustedFirstDay = (firstDayWeek - data->week_start + 7) % 7;
    uint8_t monthMaxDays = thisMonthMaxDays(tm->tm_year + YEAR0, tm->tm_mon + 1);
    uint8_t monthDayRows = 1 + (monthMaxDays - (7 - adjustedFirstDay) + 6) / 7;

    int16_t bw = (data->width - x - 10) / 7;
    int16_t bh = (data->height - y - 10) / monthDayRows;
    bool large = large_layout(data);

    /* 绘制网格线（大布局时） */
    if (large) {
        for (uint8_t i = 1; i < monthDayRows; i++)
            GFX_drawDottedLine(gfx, x, y + i * bh, x + 7 * bw - 1, y + i * bh, GFX_BLACK, 1, 5);
        for (uint8_t i = 1; i < 7; i++)
            GFX_drawDottedLine(gfx, x + i * bw, y, x + i * bw, y + monthDayRows * bh - 1, GFX_BLACK, 1, 5);
    }

    /* 遍历每一天进行绘制 */
    for (uint8_t i = 0; i < monthMaxDays; i++) {
        uint16_t year = tm->tm_year + YEAR0;
        uint8_t month = tm->tm_mon + 1;
        uint8_t day = i + 1;

        int16_t actualWeek = (firstDayWeek + i) % 7;
        int16_t displayWeek = (adjustedFirstDay + i) % 7;
        bool weekend = (actualWeek == 0) || (actualWeek == 6);

        /* 计算农历日期 */
        LUNAR_SolarToLunar(Lunar, year, month, day);

        /* 计算日期单元格位置 */
        int16_t cr = large ? 15 : 11;
        if (monthDayRows > 5) cr -= 1;  /* 6行时减小圆圈高度 */
        int16_t bx = x + (bw - 2 * cr) / 2 + displayWeek * bw;
        int16_t by = y + (bh - 2 * cr) / 2 + (i + adjustedFirstDay) / 7 * bh + 3;

        /* 绘制当天日期的红色圆圈背景 */
        if (day == tm->tm_mday) {
            GFX_fillCircle(gfx, bx + cr, by + cr - 3, 2 * cr, GFX_RED);
            GFX_setTextColor(gfx, GFX_WHITE, GFX_RED);
        } else {
            GFX_setTextColor(gfx, weekend ? GFX_RED : GFX_BLACK, GFX_WHITE);
        }

        /* 绘制日期数字 */
        char buf[10] = {0};
        snprintf(buf, sizeof(buf), "%d", day);
        GFX_setFont(gfx, large ? u8g2_font_helvB18_tn : u8g2_font_helvB14_tn);
        GFX_setCursor(gfx, bx + (2 * cr - GFX_getUTF8Width(gfx, buf)) / 2, by - (cr - GFX_getFontHeight(gfx)) - 1);
        GFX_printf(gfx, "%s", buf);

        /* 绘制农历/节日信息 */
        GFX_setFont(gfx, large ? u8g2_font_wqy12_t_lunar : u8g2_font_wqy9_t_lunar);
        GFX_setFontMode(gfx, 1);  /* 透明模式 */
        if (GetFestival(year, month, day, actualWeek, Lunar, buf)) {
            if (day != tm->tm_mday) GFX_setTextColor(gfx, GFX_RED, GFX_WHITE);
        } else {
            /* 非节日时显示农历日期，初一显示月份 */
            if (Lunar->Date == 1)
                snprintf(buf, sizeof(buf), "%s%s", Lunar_MonthLeapString[Lunar->IsLeap],
                         Lunar_MonthString[Lunar->Month]);
            else
                snprintf(buf, sizeof(buf), "%s", Lunar_DateString[Lunar->Date]);
        }
        GFX_setCursor(gfx, bx + (2 * cr - GFX_getUTF8Width(gfx, buf)) / 2 + 1,
                      gfx->ty + GFX_getFontHeight(gfx) + (large ? 5 : 3));
        GFX_printf(gfx, "%s", buf);

        /* 绘制放假/调休标记 */
        bool work = false;
        if (year == HOLIDAY_YEAR && GetHoliday(month, day, &work)) {
            if (day == tm->tm_mday) {
                /* 当天日期绘制白色圆圈背景 */
                uint16_t rx = bx + (large ? 36 : 27);
                uint16_t ry = by - 2;
                uint8_t cr = large ? 10 : 8;
                GFX_fillCircle(gfx, rx, ry, cr, GFX_WHITE);
                GFX_drawCircle(gfx, rx, ry, cr, GFX_RED);
            }
            /* 显示"休"或"班"标记 */
            GFX_setFont(gfx, u8g2_font_wqy9_t_lunar);
            GFX_setTextColor(gfx, work ? GFX_BLACK : GFX_RED, GFX_WHITE);
            GFX_setCursor(gfx, bx + (large ? 31 : 22), by + 3);
            GFX_printf(gfx, "%s", work ? "班" : "休");
        }
    }
}

/**
 * @brief 绘制完整的日历界面
 * 
 * 组合绘制日期头部、星期头部和月历主体
 * 
 * @param gfx   图形上下文指针
 * @param tm    公历时间结构体指针
 * @param Lunar 农历日期结构体指针
 * @param data  GUI数据指针
 */
static void DrawCalendar(Adafruit_GFX* gfx, tm_t* tm, struct Lunar_Date* Lunar, gui_data_t* data) {
    bool large = large_layout(data);
    DrawDateHeader(gfx, 10, large ? 38 : 28, tm, Lunar, data);
    DrawWeekHeader(gfx, 10, large ? 44 : 32, data);
    DrawMonthDays(gfx, 10, large ? 84 : 64, tm, Lunar, data);
}

/**
 * @brief 绘制七段数码管风格的大数字
 * 
 * 使用图形原语绘制七段数码管风格的数字，适合显示时间
 * 
 * @param gfx   图形上下文指针
 * @param n     要显示的数字
 * @param xLoc  左上角X坐标
 * @param yLoc  左上角Y坐标
 * @param cS    数字大小系数
 * @param fC    前景色（数字颜色）
 * @param bC    背景色
 * @param nD    数字位数（负数表示不显示前导零）
 * 
 * @note 宽度 = nD*(11*cS+2)-2*cS, 高度 = 20*cS+4
 * @note 来源: https://forum.arduino.cc/t/fast-7-segment-number-display-for-tft/296619/4
 */
// clang-format off
static void Draw7Number(Adafruit_GFX *gfx, int16_t n, uint16_t xLoc, uint16_t yLoc, int16_t cS, uint16_t fC, uint16_t bC, int16_t nD) {
    uint16_t num=abs(n),i,t,w,col,h,a,b,j=1,d=0,S2=5*cS,S3=2*cS,S4=7*cS,x1=cS+1,x2=S3+S2+1,y1=yLoc+x1,y3=yLoc+S3+S4+1;
    uint16_t seg[7][3]={{x1,yLoc,1},{x2,y1,0},{x2,y3+x1,0},{x1,(2*y3)-yLoc,1},{0,y3+x1,0},{0,y1,0},{x1,y3,1}};
    uint8_t nums[12]={0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F,0x00,0x40},c=(c=abs(cS))>10?10:(c<1)?1:c,cnt=(cnt=abs(nD))>10?10:(cnt<1)?1:cnt;
    for (xLoc+=cnt*(d=S2+(3*S3)+2);cnt>0;cnt--){
      for (i=(num>9)?num%10:((!cnt)&&(n<0))?11:((nD<0)&&(!num))?10:num,xLoc-=d,num/=10,j=0;j<7;++j){
        col=(nums[i]&(1<<j))?fC:bC;
        if (seg[j][2])for(w=S2,t=seg[j][1]+S3,h=seg[j][1]+cS,a=xLoc+seg[j][0]+cS,b=seg[j][1];b<h;b++,a--,w+=2)GFX_drawFastHLine(gfx,a,b,w,col);
        else for(w=S4,t=xLoc+seg[j][0]+S3,h=xLoc+seg[j][0]+cS,b=xLoc+seg[j][0],a=seg[j][1]+cS;b<h;b++,a--,w+=2)GFX_drawFastVLine(gfx,b,a,w,col);
        for (;b<t;b++,a++,w-=2)seg[j][2]?GFX_drawFastHLine(gfx,a,b,w,col):GFX_drawFastVLine(gfx,b,a,w,col);
        }
    }
}
// clang-format on

/**
 * @brief 绘制时间显示（小时:分钟）
 * 
 * 使用七段数码管风格显示时间，中间有冒号分隔
 * 
 * @param gfx   图形上下文指针
 * @param tm    公历时间结构体指针
 * @param x     绘制起始X坐标
 * @param y     绘制起始Y坐标
 * @param cS    数字大小系数
 * @param nD    数字位数
 */
static void DrawTime(Adafruit_GFX* gfx, tm_t* tm, int16_t x, int16_t y, uint16_t cS, uint16_t nD) {
    /* 绘制小时 */
    Draw7Number(gfx, tm->tm_hour, x, y, cS, GFX_BLACK, GFX_WHITE, nD);
    
    /* 绘制冒号 */
    x += (nD * (11 * cS + 2) - 2 * cS) + 2 * cS;
    GFX_fillRect(gfx, x, y + 4.5 * cS + 1, 2 * cS, 2 * cS, GFX_BLACK);
    GFX_fillRect(gfx, x, y + 13.5 * cS + 3, 2 * cS, 2 * cS, GFX_BLACK);
    x += 4 * cS;
    
    /* 绘制分钟 */
    Draw7Number(gfx, tm->tm_min, x, y, cS, GFX_BLACK, GFX_WHITE, nD);
}

/**
 * @brief 绘制时钟界面
 * 
 * 显示大字体时间、日期、星期、农历、干支年份、生肖、周数、节气信息
 * 
 * @param gfx   图形上下文指针
 * @param tm    公历时间结构体指针
 * @param Lunar 农历日期结构体指针
 * @param data  GUI数据指针
 */
static void DrawClock(Adafruit_GFX* gfx, tm_t* tm, struct Lunar_Date* Lunar, gui_data_t* data) {
    uint8_t padding = large_layout(data) ? 100 : 40;
    
    /* 显示公历日期 */
    GFX_setCursor(gfx, padding, 36);
    GFX_printf_styled(gfx, GFX_RED, GFX_WHITE, u8g2_font_helvB18_tn, "%d", tm->tm_year + YEAR0);
    GFX_printf_styled(gfx, GFX_BLACK, GFX_WHITE, u8g2_font_wqy12_t_lunar, "年");
    GFX_printf_styled(gfx, GFX_RED, GFX_WHITE, u8g2_font_helvB18_tn, "%02d", tm->tm_mon + 1);
    GFX_printf_styled(gfx, GFX_BLACK, GFX_WHITE, u8g2_font_wqy12_t_lunar, "月");
    GFX_printf_styled(gfx, GFX_RED, GFX_WHITE, u8g2_font_helvB18_tn, "%02d", tm->tm_mday);
    GFX_printf_styled(gfx, GFX_BLACK, GFX_WHITE, u8g2_font_wqy12_t_lunar, "日 ");

    /* 显示星期和农历 */
    GFX_setCursor(gfx, padding, 58);
    GFX_setFont(gfx, u8g2_font_wqy9_t_lunar);
    GFX_printf(gfx, "星期%s", Lunar_DayString[tm->tm_wday]);
    GFX_setCursor(gfx, 138, 58);
    GFX_printf(gfx, "%s%s%s", Lunar_MonthLeapString[Lunar->IsLeap], Lunar_MonthString[Lunar->Month],
               Lunar_DateString[Lunar->Date]);

    /* 显示电池电量 */
    DrawBattery(gfx, data->width - padding, 25, 20, data->voltage);

    /* 显示温度和SSID */
    char ssid[5] = {0};
    int16_t ssid_len = strlen(data->ssid);
    int16_t sw = GFX_getUTF8Width(gfx, "25℃[1234]");
    memcpy(ssid, &data->ssid[ssid_len - 4], 4);
    GFX_setCursor(gfx, data->width - padding - sw - 2, 58);
    GFX_setFont(gfx, u8g2_font_wqy9_t_lunar);
    GFX_printf(gfx, "%d℃[%s]", data->temperature, ssid);

    /* 绘制分隔线 */
    GFX_drawFastHLine(gfx, padding - 10, 68, data->width - 2 * (padding - 10), GFX_BLACK);

    /* 计算并绘制时间 */
    uint16_t cS = data->height / 45;
    uint16_t nD = 2;
    uint16_t time_width = 2 * (nD * (11 * cS + 2) - 2 * cS) + 4 * cS;
    uint16_t time_height = 20 * cS + 4;
    int16_t time_x = (data->width - time_width) / 2;
    int16_t time_y = (68 + (data->height - 68)) / 2 - time_height / 2;
    DrawTime(gfx, tm, time_x, time_y, cS, nD);

    /* 绘制底部分隔线 */
    GFX_drawFastHLine(gfx, padding - 10, data->height - 68, data->width - 2 * (padding - 10), GFX_BLACK);

    /* 显示干支年份和生肖 */
    GFX_setCursor(gfx, padding, data->height - 68 + 30);
    GFX_setFont(gfx, u8g2_font_wqy12_t_lunar);
    GFX_printf(gfx, "%s%s", Lunar_StemStrig[LUNAR_GetStem(Lunar)], Lunar_BranchStrig[LUNAR_GetBranch(Lunar)]);
    GFX_setTextColor(gfx, GFX_RED, GFX_WHITE);
    GFX_printf(gfx, "%s", Lunar_ZodiacString[LUNAR_GetZodiac(Lunar)]);
    GFX_setTextColor(gfx, GFX_BLACK, GFX_WHITE);
    GFX_printf(gfx, "年");

    /* 显示周数 */
    GFX_setCursor(gfx, padding, data->height - 68 + 30 + 20);
    GFX_printf(gfx, " %d周", GetWeekOfYear(tm->tm_year, tm->tm_mon, tm->tm_mday, tm->tm_wday));

    /* 显示节气信息 */
    uint8_t day = 0;
    uint8_t JQday = GetJieQiStr(tm->tm_year + YEAR0, tm->tm_mon + 1, tm->tm_mday, &day);
    if (day == 0) {
        /* 当天是节气日 */
        GFX_setCursor(gfx, data->width - GFX_getUTF8Width(gfx, "小暑") - padding, data->height - 68 + 30);
        GFX_setTextColor(gfx, GFX_RED, GFX_WHITE);
        GFX_printf(gfx, "%s", JieQiStr[JQday % 24]);
    } else {
        /* 显示距离下一个节气的天数 */
        GFX_setCursor(gfx, data->width - GFX_getUTF8Width(gfx, "离小暑") - padding, data->height - 68 + 30);
        GFX_printf(gfx, "离%");
        GFX_setTextColor(gfx, GFX_RED, GFX_WHITE);
        GFX_printf(gfx, "%s", JieQiStr[JQday % 24]);
        GFX_setTextColor(gfx, GFX_BLACK, GFX_WHITE);
        char buf[15] = {0};
        snprintf(buf, sizeof(buf), "还有%d天", day);
        GFX_setCursor(gfx, data->width - GFX_getUTF8Width(gfx, buf) - padding, data->height - 68 + 30 + 20);
        GFX_printf(gfx, buf);
    }
}

/*============================================================================
 * 公共函数实现
 *============================================================================*/

/**
 * @brief 绘制GUI界面（主入口函数）
 * 
 * 根据gui_data_t中的模式设置绘制对应的界面：
 * - MODE_CALENDAR: 绘制日历界面
 * - MODE_CLOCK: 绘制时钟界面
 * - MODE_PICTURE: 不绘制（由其他模块处理图片显示）
 * 
 * 绘制采用分页缓冲机制，通过回调函数将每页数据传递给EPD驱动
 * 
 * @param data          GUI数据指针，包含显示模式、时间、设备状态等信息
 * @param callback      缓冲区回调函数，用于将显示数据传递给EPD驱动
 * @param callback_data 回调函数的用户数据指针
 */
void DrawGUI(gui_data_t* data, buffer_callback callback, void* callback_data) {
    /* 验证并修正week_start参数 */
    if (data->week_start > 6) data->week_start = 0;

    tm_t tm = {0};
    struct Lunar_Date Lunar;

    /* 转换时间戳为时间结构 */
    transformTime(data->timestamp, &tm);

    /* 初始化图形上下文 */
    Adafruit_GFX gfx;
    int16_t ph = (__HEAP_SIZE - 512) / (data->width / 8);

    /* 根据颜色深度选择初始化方式 */
    if (data->color == 2)
        GFX_begin_3c(&gfx, data->width, data->height, ph);
    else if (data->color == 3)
        GFX_begin_4c(&gfx, data->width, data->height, ph);
    else
        GFX_begin(&gfx, data->width, data->height, ph);

    /* 分页绘制 */
    GFX_firstPage(&gfx);
    do {
        /* 清空屏幕为白色背景 */
        GFX_fillScreen(&gfx, GFX_WHITE);

        /* 计算农历日期 */
        LUNAR_SolarToLunar(&Lunar, tm.tm_year + YEAR0, tm.tm_mon + 1, tm.tm_mday);

        /* 根据模式绘制对应界面 */
        switch (data->mode) {
            case MODE_CALENDAR:
                DrawCalendar(&gfx, &tm, &Lunar, data);
                break;
            case MODE_CLOCK:
                DrawClock(&gfx, &tm, &Lunar, data);
                break;
            default:
                break;
        }
        
        /* 如果时间未同步（2025年1月），显示同步提示 */
        if ((data->mode == MODE_CALENDAR || data->mode == MODE_CLOCK) &&
            (tm.tm_year + YEAR0 == 2025 && tm.tm_mon + 1 == 1)) {
            DrawTimeSyncTip(&gfx, data);
        }
    } while (GFX_nextPage(&gfx, callback, callback_data));

    /* 释放图形上下文资源 */
    GFX_end(&gfx);
}
