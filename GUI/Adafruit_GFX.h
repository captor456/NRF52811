/**
 * @file Adafruit_GFX.h
 * @brief Adafruit图形库头文件（精简移植版）
 * 
 * 本文件是Adafruit GFX图形库的精简移植版本，专为电子墨水屏(EPD)显示设计。
 * 提供了一套通用的图形绘制API，包括点、线、矩形、圆形等基本图形原语，
 * 以及基于U8G2字体的文字显示功能。
 * 
 * 主要特性：
 * - 支持单色、三色和四色显示模式
 * - 支持显示旋转（0°/90°/180°/270°）
 * - 支持分页绘制，适用于大尺寸屏幕
 * - 集成U8G2字体引擎，支持UTF-8中文显示
 * 
 * @note 原始代码来自Adafruit Industries，本项目进行了移植和精简
 * @copyright Copyright (c) 2013 Adafruit Industries. All rights reserved.
 */

#ifndef _ADAFRUIT_GFX_H
#define _ADAFRUIT_GFX_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "u8g2_font.h"

/*============================================================================
 * 颜色定义
 * 使用RGB565格式（16位：R5-G6-B5）
 *============================================================================*/

#define GFX_BLACK 0x0000    /* 黑色：R=0, G=0, B=0 */
#define GFX_WHITE 0xFFFF    /* 白色：R=31, G=63, B=31 */
#define GFX_RED 0xF800      /* 红色：R=31, G=0, B=0 (RGB: 255, 0, 0) */
#define GFX_YELLOW 0xFFE0   /* 黄色：R=31, G=63, B=0 (RGB: 255, 255, 0) */
#define GFX_BLUE 0x001F     /* 蓝色：R=0, G=0, B=31 (RGB: 0, 0, 255) */
#define GFX_GREEN 0x07E0    /* 绿色：R=0, G=63, B=0 (RGB: 0, 255, 0) */
#define GFX_ORANGE 0xFC00   /* 橙色：R=31, G=16, B=0 (RGB: 255, 128, 0) */

/*============================================================================
 * 类型定义
 *============================================================================*/

/**
 * @brief 缓冲区回调函数类型
 * 
 * 当分页绘制完成时调用此回调，用于将缓冲区数据发送到显示设备。
 * 
 * @param user_data 用户自定义数据指针
 * @param black     黑色像素缓冲区（单色/三色模式）
 * @param color     颜色像素缓冲区（三色/四色模式，单色模式下为NULL）
 * @param x         绘制区域起始X坐标
 * @param y         绘制区域起始Y坐标
 * @param w         绘制区域宽度
 * @param h         绘制区域高度
 */
typedef void (*buffer_callback)(void* user_data, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w,
                                uint16_t h);

/**
 * @brief 显示旋转枚举类型
 * 
 * 定义四种显示旋转方向，用于调整显示内容的方向。
 */
typedef enum {
    GFX_ROTATE_0 = 0,   /* 不旋转，原始方向 */
    GFX_ROTATE_90 = 1,  /* 顺时针旋转90度 */
    GFX_ROTATE_180 = 2, /* 旋转180度 */
    GFX_ROTATE_270 = 3, /* 顺时针旋转270度（逆时针90度） */
} GFX_Rotate;

/*============================================================================
 * 图形上下文结构体
 *============================================================================*/

/**
 * @brief Adafruit GFX图形上下文结构体
 * 
 * 该结构体包含了图形库运行所需的所有状态信息，包括：
 * - 显示尺寸和旋转状态
 * - 字体和文本绘制参数
 * - 像素缓冲区
 * - 分页绘制控制参数
 */
typedef struct {
    /*--------------------------------------------------------------------------
     * 显示尺寸相关字段
     *--------------------------------------------------------------------------*/
    int16_t WIDTH;        /* 原始显示宽度（像素），初始化后不再改变 */
    int16_t HEIGHT;       /* 原始显示高度（像素），初始化后不再改变 */
    int16_t _width;       /* 当前旋转后的显示宽度，随旋转设置变化 */
    int16_t _height;      /* 当前旋转后的显示高度，随旋转设置变化 */
    GFX_Rotate rotation;  /* 当前显示旋转设置（0-3对应四个方向） */

    /*--------------------------------------------------------------------------
     * U8G2字体引擎相关字段
     *--------------------------------------------------------------------------*/
    u8g2_font_t u8g2;     /* U8G2字体上下文，用于文字渲染 */
    int16_t tx, ty;       /* 文本光标位置（print命令使用的当前位置） */
    uint16_t encoding;    /* UTF-8解码器检测到的Unicode编码 */
    uint8_t utf8_state;   /* UTF-8解码器状态，记录剩余待解码字节数 */

    /*--------------------------------------------------------------------------
     * 像素缓冲区相关字段
     *--------------------------------------------------------------------------*/
    uint8_t* buffer;      /* 黑色像素缓冲区指针 */
    uint8_t* color;       /* 颜色像素缓冲区指针（仅三色/四色模式使用） */
    
    /*--------------------------------------------------------------------------
     * 分页绘制相关字段
     *--------------------------------------------------------------------------*/
    uint16_t px, py, pw, ph;  /* 局部窗口偏移和尺寸 */
    int16_t page_height;      /* 单页缓冲区高度（像素） */
    int16_t current_page;     /* 当前正在绘制的页面索引 */
    int16_t total_pages;      /* 总页面数量 */
} Adafruit_GFX;

/*============================================================================
 * 控制API函数声明
 *============================================================================*/

/**
 * @brief 初始化图形上下文（单色模式）
 * 
 * 分配显示缓冲区并初始化图形上下文结构体。
 * 
 * @param gfx           图形上下文指针
 * @param w             显示宽度（像素）
 * @param h             显示高度（像素）
 * @param buffer_height 页缓冲区高度（像素），影响内存占用
 */
void GFX_begin(Adafruit_GFX* gfx, int16_t w, int16_t h, int16_t buffer_height);

/**
 * @brief 初始化图形上下文（三色模式）
 * 
 * 为三色电子墨水屏（如黑白红屏）初始化图形上下文。
 * 额外分配颜色缓冲区用于存储彩色像素数据。
 * 
 * @param gfx           图形上下文指针
 * @param w             显示宽度（像素）
 * @param h             显示高度（像素）
 * @param buffer_height 页缓冲区高度（像素），应为偶数
 */
void GFX_begin_3c(Adafruit_GFX* gfx, int16_t w, int16_t h, int16_t buffer_height);

/**
 * @brief 初始化图形上下文（四色模式）
 * 
 * 为四色电子墨水屏初始化图形上下文。
 * 
 * @param gfx           图形上下文指针
 * @param w             显示宽度（像素）
 * @param h             显示高度（像素）
 * @param buffer_height 页缓冲区高度（像素），应为偶数
 */
void GFX_begin_4c(Adafruit_GFX* gfx, int16_t w, int16_t h, int16_t buffer_height);

/**
 * @brief 设置显示旋转方向
 * 
 * @param gfx 图形上下文指针
 * @param r   旋转方向（0-3对应0°/90°/180°/270°）
 */
void GFX_setRotation(Adafruit_GFX* gfx, GFX_Rotate r);

/**
 * @brief 设置局部绘制窗口
 * 
 * 设置一个矩形区域作为绘制目标，可以减少刷新面积，提高刷新速度。
 * 注意：x和w（旋转0°/180°）或y和h（旋转90°/270°）应为8的倍数，
 * 这是电子墨水屏控制器的寻址限制。
 * 
 * @param gfx 图形上下文指针
 * @param x   窗口起始X坐标
 * @param y   窗口起始Y坐标
 * @param w   窗口宽度
 * @param h   窗口高度
 */
void GFX_setWindow(Adafruit_GFX* gfx, uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/**
 * @brief 开始分页绘制
 * 
 * 清空缓冲区并重置页计数器，准备开始分页绘制。
 * 
 * @param gfx 图形上下文指针
 */
void GFX_firstPage(Adafruit_GFX* gfx);

/**
 * @brief 完成当前页绘制并获取下一页状态
 * 
 * 调用回调函数将当前页缓冲区发送到显示设备，
 * 然后清空缓冲区准备绘制下一页。
 * 
 * @param gfx      图形上下文指针
 * @param callback 缓冲区回调函数，用于发送数据到显示设备
 * @param user_data 传递给回调函数的用户数据
 * @return true    还有更多页面需要绘制
 * @return false   所有页面绘制完成
 */
bool GFX_nextPage(Adafruit_GFX* gfx, buffer_callback callback, void* user_data);

/**
 * @brief 释放图形上下文资源
 * 
 * 释放图形上下文分配的缓冲区内存。
 * 
 * @param gfx 图形上下文指针
 */
void GFX_end(Adafruit_GFX* gfx);

/*============================================================================
 * 图形绘制API函数声明
 *============================================================================*/

/**
 * @brief 画单个像素点
 * 
 * 在指定坐标绘制一个像素点，这是所有其他图形函数的基础。
 * 
 * @param gfx  图形上下文指针
 * @param x    X坐标
 * @param y    Y坐标
 * @param color 像素颜色（RGB565格式）
 */
void GFX_drawPixel(Adafruit_GFX* gfx, int16_t x, int16_t y, uint16_t color);

/**
 * @brief 画直线
 * 
 * 使用Bresenham算法在两点之间画一条直线。
 * 
 * @param gfx  图形上下文指针
 * @param x0   起点X坐标
 * @param y0   起点Y坐标
 * @param x1   终点X坐标
 * @param y1   终点Y坐标
 * @param color 线条颜色（RGB565格式）
 */
void GFX_drawLine(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

/**
 * @brief 画虚线
 * 
 * 使用Bresenham算法绘制虚线，可自定义实线和间隔长度。
 * 
 * @param gfx       图形上下文指针
 * @param x0        起点X坐标
 * @param y0        起点Y坐标
 * @param x1        终点X坐标
 * @param y1        终点Y坐标
 * @param color     线条颜色（RGB565格式）
 * @param dot_len   实线段长度（像素）
 * @param space_len 间隔长度（像素）
 */
void GFX_drawDottedLine(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color,
                        uint8_t dot_len, uint8_t space_len);

/**
 * @brief 画垂直线
 * 
 * 快速绘制一条垂直线，比通用drawLine函数效率更高。
 * 
 * @param gfx   图形上下文指针
 * @param x     顶点X坐标
 * @param y     顶点Y坐标
 * @param h     线条高度（像素）
 * @param color 线条颜色（RGB565格式）
 */
void GFX_drawFastVLine(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t h, uint16_t color);

/**
 * @brief 画水平线
 * 
 * 快速绘制一条水平线，比通用drawLine函数效率更高。
 * 
 * @param gfx   图形上下文指针
 * @param x     左端点X坐标
 * @param y     左端点Y坐标
 * @param w     线条宽度（像素）
 * @param color 线条颜色（RGB565格式）
 */
void GFX_drawFastHLine(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t w, uint16_t color);

/**
 * @brief 填充矩形
 * 
 * 用指定颜色填充一个矩形区域。
 * 
 * @param gfx   图形上下文指针
 * @param x     左上角X坐标
 * @param y     左上角Y坐标
 * @param w     矩形宽度（像素）
 * @param h     矩形高度（像素）
 * @param color 填充颜色（RGB565格式）
 */
void GFX_fillRect(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/**
 * @brief 填充整个屏幕
 * 
 * 用指定颜色清空整个屏幕缓冲区。
 * 
 * @param gfx   图形上下文指针
 * @param color 填充颜色（RGB565格式）
 */
void GFX_fillScreen(Adafruit_GFX* gfx, uint16_t color);

/**
 * @brief 画矩形边框
 * 
 * 绘制一个矩形的边框（不填充内部）。
 * 
 * @param gfx   图形上下文指针
 * @param x     左上角X坐标
 * @param y     左上角Y坐标
 * @param w     矩形宽度（像素）
 * @param h     矩形高度（像素）
 * @param color 边框颜色（RGB565格式）
 */
void GFX_drawRect(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/**
 * @brief 画圆边框
 * 
 * 使用中点圆算法绘制圆形边框。
 * 
 * @param gfx   图形上下文指针
 * @param x0    圆心X坐标
 * @param y0    圆心Y坐标
 * @param r     圆的半径（像素）
 * @param color 边框颜色（RGB565格式）
 */
void GFX_drawCircle(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t r, uint16_t color);

/**
 * @brief 画圆弧辅助函数
 * 
 * 绘制圆的一部分（四分之一圆弧），用于绘制圆角矩形等复合图形。
 * 
 * @param gfx        图形上下文指针
 * @param x0         圆心X坐标
 * @param y0         圆心Y坐标
 * @param r          圆的半径（像素）
 * @param cornername 角落选择掩码（位1-4分别对应四个象限）
 * @param color      边框颜色（RGB565格式）
 */
void GFX_drawCircleHelper(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t r, uint8_t cornername, uint16_t color);

/**
 * @brief 填充圆形
 * 
 * 绘制一个实心圆。
 * 
 * @param gfx   图形上下文指针
 * @param x0    圆心X坐标
 * @param y0    圆心Y坐标
 * @param r     圆的半径（像素）
 * @param color 填充颜色（RGB565格式）
 */
void GFX_fillCircle(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t r, uint16_t color);

/**
 * @brief 填充圆弧辅助函数
 * 
 * 填充圆的一部分，用于绘制圆角矩形等复合图形。
 * 
 * @param gfx     图形上下文指针
 * @param x0      圆心X坐标
 * @param y0      圆心Y坐标
 * @param r       圆的半径（像素）
 * @param corners 角落选择掩码（位1=左半圆，位2=右半圆）
 * @param delta   垂直偏移量，用于圆角矩形绘制
 * @param color   填充颜色（RGB565格式）
 */
void GFX_fillCircleHelper(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t r, uint8_t corners, int16_t delta,
                          uint16_t color);

/**
 * @brief 画椭圆边框
 * 
 * 使用Bresenham椭圆算法绘制椭圆边框。
 * 
 * @param gfx   图形上下文指针
 * @param x0    椭圆中心X坐标
 * @param y0    椭圆中心Y坐标
 * @param rw    水平半径（像素）
 * @param rh    垂直半径（像素）
 * @param color 边框颜色（RGB565格式）
 */
void GFX_drawEllipse(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t rw, int16_t rh, uint16_t color);

/**
 * @brief 填充椭圆
 * 
 * 绘制一个实心椭圆。
 * 
 * @param gfx   图形上下文指针
 * @param x0    椭圆中心X坐标
 * @param y0    椭圆中心Y坐标
 * @param rw    水平半径（像素）
 * @param rh    垂直半径（像素）
 * @param color 填充颜色（RGB565格式）
 */
void GFX_fillEllipse(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t rw, int16_t rh, uint16_t color);

/**
 * @brief 画三角形边框
 * 
 * 绘制三角形的三条边。
 * 
 * @param gfx   图形上下文指针
 * @param x0    第一个顶点X坐标
 * @param y0    第一个顶点Y坐标
 * @param x1    第二个顶点X坐标
 * @param y1    第二个顶点Y坐标
 * @param x2    第三个顶点X坐标
 * @param y2    第三个顶点Y坐标
 * @param color 边框颜色（RGB565格式）
 */
void GFX_drawTriangle(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                      uint16_t color);

/**
 * @brief 填充三角形
 * 
 * 绘制一个实心三角形。
 * 
 * @param gfx   图形上下文指针
 * @param x0    第一个顶点X坐标
 * @param y0    第一个顶点Y坐标
 * @param x1    第二个顶点X坐标
 * @param y1    第二个顶点Y坐标
 * @param x2    第三个顶点X坐标
 * @param y2    第三个顶点Y坐标
 * @param color 填充颜色（RGB565格式）
 */
void GFX_fillTriangle(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                      uint16_t color);

/**
 * @brief 画圆角矩形边框
 * 
 * 绘制一个带圆角的矩形边框。
 * 
 * @param gfx    图形上下文指针
 * @param x0     左上角X坐标
 * @param y0     左上角Y坐标
 * @param w      矩形宽度（像素）
 * @param h      矩形高度（像素）
 * @param radius 圆角半径（像素）
 * @param color  边框颜色（RGB565格式）
 */
void GFX_drawRoundRect(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t w, int16_t h, int16_t radius, uint16_t color);

/**
 * @brief 填充圆角矩形
 * 
 * 绘制一个实心的圆角矩形。
 * 
 * @param gfx    图形上下文指针
 * @param x0     左上角X坐标
 * @param y0     左上角Y坐标
 * @param w      矩形宽度（像素）
 * @param h      矩形高度（像素）
 * @param radius 圆角半径（像素）
 * @param color  填充颜色（RGB565格式）
 */
void GFX_fillRoundRect(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t w, int16_t h, int16_t radius, uint16_t color);

/**
 * @brief 绘制位图
 * 
 * 在指定位置绘制一个单色位图。
 * 
 * @param gfx    图形上下文指针
 * @param x      左上角X坐标
 * @param y      左上角Y坐标
 * @param bitmap 位图数据数组
 * @param w      位图宽度（像素）
 * @param h      位图高度（像素）
 * @param color  前景色（RGB565格式）
 * @param invert 是否反转位图（true时0为前景色）
 */
void GFX_drawBitmap(Adafruit_GFX* gfx, int16_t x, int16_t y, const uint8_t bitmap[], int16_t w, int16_t h,
                    uint16_t color, bool invert);

/*============================================================================
 * U8G2字体API函数声明
 *============================================================================*/

/**
 * @brief 设置文本光标位置
 * 
 * 设置print函数使用的文本起始位置。
 * 
 * @param gfx 图形上下文指针
 * @param x   光标X坐标
 * @param y   光标Y坐标
 */
void GFX_setCursor(Adafruit_GFX* gfx, int16_t x, int16_t y);

/**
 * @brief 设置当前字体
 * 
 * 设置用于文本显示的U8G2字体。
 * 
 * @param gfx 图形上下文指针
 * @param font 字体数据指针
 */
void GFX_setFont(Adafruit_GFX* gfx, const uint8_t* font);

/**
 * @brief 设置字体渲染模式
 * 
 * @param gfx            图形上下文指针
 * @param is_transparent 是否透明背景（0=不透明，1=透明）
 */
void GFX_setFontMode(Adafruit_GFX* gfx, uint8_t is_transparent);

/**
 * @brief 设置字体绘制方向
 * 
 * @param gfx 图形上下文指针
 * @param d   绘制方向（0=从左到右，1=从上到下，2=从右到左，3=从下到上）
 */
void GFX_setFontDirection(Adafruit_GFX* gfx, GFX_Rotate d);

/**
 * @brief 设置文本颜色
 * 
 * @param gfx 图形上下文指针
 * @param fg  前景色（文字颜色，RGB565格式）
 * @param bg  背景色（RGB565格式）
 */
void GFX_setTextColor(Adafruit_GFX* gfx, uint16_t fg, uint16_t bg);

/**
 * @brief 获取字体基线高度
 * 
 * 获取当前字体从基线到字体顶部的距离。
 * 
 * @param gfx 图形上下文指针
 * @return 字体上升高度（像素）
 */
int8_t GFX_getFontAscent(Adafruit_GFX* gfx);

/**
 * @brief 获取字体下降高度
 * 
 * 获取当前字体从基线到字体底部的距离（通常为负值或零）。
 * 
 * @param gfx 图形上下文指针
 * @return 字体下降高度（像素）
 */
int8_t GFX_getFontDescent(Adafruit_GFX* gfx);

/**
 * @brief 获取字体总高度
 * 
 * 获取当前字体的总高度（上升高度减去下降高度）。
 * 
 * @param gfx 图形上下文指针
 * @return 字体高度（像素）
 */
int8_t GFX_getFontHeight(Adafruit_GFX* gfx);

/**
 * @brief 绘制单个字形
 * 
 * 在指定位置绘制一个Unicode字形。
 * 
 * @param gfx 图形上下文指针
 * @param x   绘制位置X坐标
 * @param y   绘制位置Y坐标（基线位置）
 * @param e   Unicode编码
 * @return    字形宽度（像素）
 */
int16_t GFX_drawGlyph(Adafruit_GFX* gfx, int16_t x, int16_t y, uint16_t e);

/**
 * @brief 绘制ASCII字符串
 * 
 * 在指定位置绘制一个ASCII字符串（不支持中文）。
 * 
 * @param gfx 图形上下文指针
 * @param x   绘制位置X坐标
 * @param y   绘制位置Y坐标（基线位置）
 * @param s   要绘制的字符串
 * @return    字符串总宽度（像素）
 */
int16_t GFX_drawStr(Adafruit_GFX* gfx, int16_t x, int16_t y, const char* s);

/**
 * @brief 绘制UTF-8字符串
 * 
 * 在指定位置绘制一个UTF-8编码的字符串（支持中文）。
 * 
 * @param gfx 图形上下文指针
 * @param x   绘制位置X坐标
 * @param y   绘制位置Y坐标（基线位置）
 * @param str 要绘制的UTF-8字符串
 * @return    字符串总宽度（像素）
 */
int16_t GFX_drawUTF8(Adafruit_GFX* gfx, int16_t x, int16_t y, const char* str);

/**
 * @brief 计算UTF-8字符串宽度
 * 
 * 计算指定UTF-8字符串在当前字体下的显示宽度。
 * 
 * @param gfx 图形上下文指针
 * @param str UTF-8字符串
 * @return    字符串宽度（像素）
 */
int16_t GFX_getUTF8Width(Adafruit_GFX* gfx, const char* str);

/**
 * @brief 打印单个字符
 * 
 * 在当前光标位置打印一个字符，并自动移动光标。
 * 支持UTF-8编码，可以逐字节输入中文字符。
 * 
 * @param gfx 图形上下文指针
 * @param c   要打印的字符
 * @return    打印的字符数（总是返回1）
 */
size_t GFX_print(Adafruit_GFX* gfx, const char c);

/**
 * @brief 写入字符缓冲区
 * 
 * 将指定长度的字符缓冲区写入显示。
 * 
 * @param gfx    图形上下文指针
 * @param buffer 字符缓冲区指针
 * @param size   要写入的字符数
 * @return       实际写入的字符数
 */
size_t GFX_write(Adafruit_GFX* gfx, const char* buffer, size_t size);

/**
 * @brief 格式化打印
 * 
 * 类似printf的格式化输出函数，支持各种格式说明符。
 * 
 * @param gfx   图形上下文指针
 * @param format 格式化字符串
 * @param ...    可变参数列表
 * @return       写入的字符数
 */
size_t GFX_printf(Adafruit_GFX* gfx, const char* format, ...);

#endif  /* _ADAFRUIT_GFX_H */
