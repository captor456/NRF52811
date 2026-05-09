/**
 * @file Adafruit_GFX.c
 * @brief Adafruit图形库实现文件（精简移植版）
 * 
 * 本文件是Adafruit GFX图形库的核心实现，提供了各种图形绘制功能。
 * 主要包括：
 * - 图形上下文初始化和管理
 * - 基本图形绘制（点、线、矩形、圆等）
 * - U8G2字体集成，支持UTF-8中文显示
 * 
 * @note 原始代码来自Adafruit Industries，本项目进行了移植和精简
 * @copyright Copyright (c) 2013 Adafruit Industries. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "Adafruit_GFX.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * 宏定义
 *============================================================================*/

#ifndef ABS
#define ABS(x) ((x) > 0 ? (x) : -(x))  /* 绝对值计算 */
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))  /* 取最小值 */
#endif
#ifndef SWAP
#define SWAP(a, b, T) \
    do {              \
        T t = a;      \
        a = b;        \
        b = t;        \
    } while (0)  /* 交换两个变量的值 */
#endif
#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, member) (type*)((char*)ptr - offsetof(type, member))  /* 通过成员指针获取结构体指针 */
#endif

/*============================================================================
 * U8G2字体回调函数
 *============================================================================*/

/**
 * @brief U8G2字体绘制回调函数
 * 
 * U8G2字体引擎调用此函数来绘制水平或垂直线段。
 * 根据方向参数调用相应的GFX绘图函数。
 * 
 * @param u8g2 U8G2字体上下文指针
 * @param x    起始X坐标
 * @param y    起始Y坐标
 * @param len  线段长度
 * @param dir  方向（0=右，1=下，2=左，3=上）
 * @param color 绘制颜色
 */
static void GFX_u8g2_draw_hv_line(u8g2_font_t* u8g2, int16_t x, int16_t y, int16_t len, uint8_t dir, uint16_t color) {
    Adafruit_GFX* gfx = CONTAINER_OF(u8g2, Adafruit_GFX, u8g2);
    switch (dir) {
        case 0:  /* 向右绘制水平线 */
            GFX_drawFastHLine(gfx, x, y, len, color);
            break;
        case 1:  /* 向下绘制垂直线 */
            GFX_drawFastVLine(gfx, x, y, len, color);
            break;
        case 2:  /* 向左绘制水平线 */
            GFX_drawFastHLine(gfx, x - len + 1, y, len, color);
            break;
        case 3:  /* 向上绘制垂直线 */
            GFX_drawFastVLine(gfx, x, y - len + 1, len, color);
            break;
    }
}

/*============================================================================
 * 图形上下文初始化函数
 *============================================================================*/

/**
 * @brief 初始化图形上下文（单色模式）
 * 
 * 分配显示缓冲区并初始化图形上下文结构体。
 * 单色模式只有一个缓冲区，用于存储黑白像素数据。
 * 
 * @param gfx           图形上下文指针
 * @param w             显示宽度（像素）
 * @param h             显示高度（像素）
 * @param buffer_height 页缓冲区高度（像素），影响内存占用
 */
void GFX_begin(Adafruit_GFX* gfx, int16_t w, int16_t h, int16_t buffer_height) {
    /* 清零整个结构体 */
    memset(gfx, 0, sizeof(Adafruit_GFX));
    memset(&gfx->u8g2, 0, sizeof(gfx->u8g2));
    
    /* 设置显示尺寸 */
    gfx->WIDTH = gfx->_width = w;
    gfx->HEIGHT = gfx->_height = h;
    
    /* 设置U8G2回调函数 */
    gfx->u8g2.draw_hv_line = GFX_u8g2_draw_hv_line;
    
    /* 分配像素缓冲区，每个像素用1位表示 */
    gfx->buffer = malloc(((gfx->WIDTH + 7) / 8) * buffer_height);
    
    /* 设置分页参数 */
    gfx->page_height = buffer_height;
    gfx->total_pages = (gfx->HEIGHT / gfx->page_height) + (gfx->HEIGHT % gfx->page_height > 0);
    
    /* 设置默认绘制窗口为全屏 */
    GFX_setWindow(gfx, 0, 0, gfx->WIDTH, gfx->HEIGHT);
}

/**
 * @brief 初始化图形上下文（三色模式）
 * 
 * 为三色电子墨水屏（如黑白红屏）初始化图形上下文。
 * 三色模式需要两个缓冲区：黑色缓冲区和颜色缓冲区。
 * 
 * @param gfx           图形上下文指针
 * @param w             显示宽度（像素）
 * @param h             显示高度（像素）
 * @param buffer_height 页缓冲区高度（像素），应为偶数
 */
void GFX_begin_3c(Adafruit_GFX* gfx, int16_t w, int16_t h, int16_t buffer_height) {
    /* 先调用单色模式初始化 */
    GFX_begin(gfx, w, h, buffer_height);
    
    /* 三色模式下页高度减半，因为需要两个缓冲区 */
    gfx->page_height /= 2;
    
    /* 颜色缓冲区位于黑色缓冲区之后 */
    gfx->color = gfx->buffer + ((gfx->WIDTH + 7) / 8) * gfx->page_height;
    
    /* 重新计算总页数 */
    gfx->total_pages = (gfx->HEIGHT / gfx->page_height) + (gfx->HEIGHT % gfx->page_height > 0);
}

/**
 * @brief 初始化图形上下文（四色模式）
 * 
 * 为四色电子墨水屏初始化图形上下文。
 * 四色模式使用特殊的像素编码方式，每个像素用2位表示。
 * 
 * @param gfx           图形上下文指针
 * @param w             显示宽度（像素）
 * @param h             显示高度（像素）
 * @param buffer_height 页缓冲区高度（像素），应为偶数
 */
void GFX_begin_4c(Adafruit_GFX* gfx, int16_t w, int16_t h, int16_t buffer_height) {
    /* 先调用单色模式初始化 */
    GFX_begin(gfx, w, h, buffer_height);
    
    /* 四色模式下页高度减半 */
    gfx->page_height /= 2;
    
    /* 四色模式下，颜色缓冲区与黑色缓冲区相同（使用不同的编码方式） */
    gfx->color = gfx->buffer;
    
    /* 重新计算总页数 */
    gfx->total_pages = (gfx->HEIGHT / gfx->page_height) + (gfx->HEIGHT % gfx->page_height > 0);
}

/**
 * @brief 释放图形上下文资源
 * 
 * 释放图形上下文分配的缓冲区内存。
 * 
 * @param gfx 图形上下文指针
 */
void GFX_end(Adafruit_GFX* gfx) {
    if (gfx->buffer) free(gfx->buffer);
}

/*============================================================================
 * 分页绘制控制函数
 *============================================================================*/

/**
 * @brief 开始分页绘制
 * 
 * 清空缓冲区并重置页计数器，准备开始分页绘制。
 * 每次绘制新画面前应调用此函数。
 * 
 * @param gfx 图形上下文指针
 */
void GFX_firstPage(Adafruit_GFX* gfx) {
    /* 用白色填充整个缓冲区 */
    GFX_fillScreen(gfx, GFX_WHITE);
    /* 重置页计数器 */
    gfx->current_page = 0;
}

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
bool GFX_nextPage(Adafruit_GFX* gfx, buffer_callback callback, void* user_data) {
    if (callback) {
        /* 计算当前页的起始Y坐标 */
        int16_t page_ys = gfx->current_page * gfx->page_height;
        
        /* 检查是否设置了局部窗口 */
        if (gfx->px != 0 || gfx->py != 0 || gfx->pw != gfx->_width || gfx->ph != gfx->_height) {
            /* 局部窗口模式：只刷新指定区域 */
            int16_t page_ye = gfx->current_page < gfx->total_pages - 1 ? page_ys + gfx->page_height : gfx->HEIGHT;
            uint16_t dest_ys = gfx->py + page_ys;  /* 转换后的起始Y坐标 */
            uint16_t dest_ye = MIN(gfx->py + gfx->ph, gfx->py + page_ye);
            if (dest_ye > dest_ys)
                callback(user_data, gfx->buffer, gfx->color, gfx->px, dest_ys, gfx->pw, dest_ye - dest_ys);
        } else {
            /* 全屏刷新模式 */
            int16_t height = MIN(gfx->page_height, gfx->HEIGHT - page_ys);
            callback(user_data, gfx->buffer, gfx->color, 0, page_ys, gfx->WIDTH, height);
        }
    }

    /* 移动到下一页 */
    gfx->current_page++;
    
    /* 清空缓冲区准备绘制下一页 */
    GFX_fillScreen(gfx, GFX_WHITE);

    /* 返回是否还有更多页面 */
    return gfx->current_page < gfx->total_pages;
}

/*============================================================================
 * 显示控制函数
 *============================================================================*/

/**
 * @brief 设置显示旋转方向
 * 
 * 根据旋转方向调整显示的宽高参数。
 * 旋转90°或270°时，宽高需要互换。
 * 
 * @param gfx 图形上下文指针
 * @param r   旋转方向（0-3对应0°/90°/180°/270°）
 */
void GFX_setRotation(Adafruit_GFX* gfx, GFX_Rotate r) {
    gfx->rotation = r;
    switch (gfx->rotation) {
        case GFX_ROTATE_0:
        case GFX_ROTATE_180:
            /* 0°和180°旋转，宽高不变 */
            gfx->_width = gfx->WIDTH;
            gfx->_height = gfx->HEIGHT;
            break;
        case GFX_ROTATE_90:
        case GFX_ROTATE_270:
            /* 90°和270°旋转，宽高互换 */
            gfx->_width = gfx->HEIGHT;
            gfx->_height = gfx->WIDTH;
            break;
    }
}

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
void GFX_setWindow(Adafruit_GFX* gfx, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    /* 限制窗口范围在显示区域内 */
    gfx->px = MIN(x, gfx->_width);
    gfx->py = MIN(y, gfx->_height);
    gfx->pw = MIN(w, gfx->_width - gfx->px);
    gfx->ph = MIN(h, gfx->_height - gfx->py);

    /* 根据旋转方向转换坐标 */
    switch (gfx->rotation) {
        case GFX_ROTATE_0:
            break;
        case GFX_ROTATE_90:
            /* 90°旋转：交换x/y和w/h，并调整x坐标 */
            SWAP(gfx->px, gfx->py, uint16_t);
            SWAP(gfx->pw, gfx->ph, uint16_t);
            gfx->px = gfx->WIDTH - gfx->px - gfx->pw;
            break;
        case GFX_ROTATE_180:
            /* 180°旋转：x和y都取反 */
            gfx->px = gfx->WIDTH - gfx->px - gfx->pw;
            gfx->py = gfx->HEIGHT - gfx->py - gfx->ph;
            break;
        case GFX_ROTATE_270:
            /* 270°旋转：交换x/y和w/h，并调整y坐标 */
            SWAP(gfx->px, gfx->py, uint16_t);
            SWAP(gfx->pw, gfx->ph, uint16_t);
            gfx->py = gfx->HEIGHT - gfx->py - gfx->ph;
            break;
    }

    /* 调整窗口边界为8的倍数（电子墨水屏控制器要求） */
    gfx->pw += gfx->px % 8;
    if (gfx->pw % 8 > 0) gfx->pw += 8 - (gfx->pw % 8);
    gfx->px -= gfx->px % 8;
}

/*============================================================================
 * 颜色转换函数
 *============================================================================*/

/**
 * @brief 将RGB565颜色转换为四色模式颜色值
 * 
 * 四色电子墨水屏支持4种颜色：黑、白、红、黄。
 * 此函数将RGB565颜色映射到这4种颜色。
 * 
 * @param color RGB565颜色值
 * @return 四色模式的颜色编码（0=黑，1=白，2=黄，3=红）
 */
static uint8_t color4(uint16_t color) {
    static uint16_t _prev_color = GFX_BLACK;
    static uint8_t _prev_color4 = 0x00;  /* 缓存上一次的颜色转换结果 */
    
    /* 如果颜色相同，直接返回缓存结果 */
    if (color == _prev_color) return _prev_color4;
    
    uint8_t cv4 = 0x00;
    switch (color) {
        case GFX_BLACK:
            cv4 = 0x00;
            break;
        case GFX_WHITE:
            cv4 = 0x01;
            break;
        case GFX_GREEN:
            cv4 = 0x02;
            break;  /* 绿色映射为黄色 */
        case GFX_BLUE:
            cv4 = 0x00;
            break;  /* 蓝色映射为黑色 */
        case GFX_RED:
            cv4 = 0x03;
            break;
        case GFX_YELLOW:
            cv4 = 0x02;
            break;
        case GFX_ORANGE:
            cv4 = 0x02;
            break;  /* 橙色映射为黄色 */
        default: {
            /* 对于其他颜色，根据RGB分量判断 */
            uint16_t red = color & 0xF800;
            uint16_t green = (color & 0x07E0) << 5;
            uint16_t blue = (color & 0x001F) << 11;
            if ((red < 0x8000) && (green < 0x8000) && (blue < 0x8000))
                cv4 = 0x00;  /* 黑色 */
            else if ((red >= 0x8000) && (green >= 0x8000) && (blue >= 0x8000))
                cv4 = 0x01;  /* 白色 */
            else if ((red >= 0x8000) && (blue >= 0x8000))
                cv4 = 0x03;  /* 红色（红+蓝 > 红） */
            else if ((green >= 0x8000) && (blue >= 0x8000))
                cv4 = 0x01;  /* 白色（绿+蓝 > 白） */
            else if ((red >= 0x8000) && (green >= 0xC000))
                cv4 = 0x02;  /* 黄色 */
            else if ((red >= 0x8000) && (green >= 0x4000))
                cv4 = 0x03;  /* 红色（橙色 > 红） */
            else if (red >= 0x8000)
                cv4 = 0x03;  /* 红色 */
            else if (green >= 0x8000)
                cv4 = 0x00;  /* 黑色（绿色 > 黑） */
            else
                cv4 = 0x03;  /* 蓝色映射为红色 */
        } break;
    }
    
    /* 更新缓存 */
    _prev_color = color;
    _prev_color4 = cv4;
    return cv4;
}

/*============================================================================
 * 像素绘制函数（核心函数）
 *============================================================================*/

/**
 * @brief 画单个像素点
 * 
 * 在指定坐标绘制一个像素点，这是所有其他图形函数的基础。
 * 该函数处理：
 * 1. 坐标边界检查
 * 2. 旋转坐标转换
 * 3. 局部窗口裁剪
 * 4. 分页偏移计算
 * 5. 不同颜色模式的像素写入
 * 
 * @param gfx   图形上下文指针
 * @param x     X坐标
 * @param y     Y坐标
 * @param color 像素颜色（RGB565格式）
 */
void GFX_drawPixel(Adafruit_GFX* gfx, int16_t x, int16_t y, uint16_t color) {
    /* 边界检查 */
    if (x < 0 || x >= gfx->_width || y < 0 || y >= gfx->_height) return;

    /* 根据旋转方向转换坐标 */
    switch (gfx->rotation) {
        case GFX_ROTATE_0:
            break;
        case GFX_ROTATE_90:
            SWAP(x, y, int16_t);
            x = gfx->WIDTH - x - 1;
            break;
        case GFX_ROTATE_180:
            x = gfx->WIDTH - x - 1;
            y = gfx->HEIGHT - y - 1;
            break;
        case GFX_ROTATE_270:
            SWAP(x, y, int16_t);
            y = gfx->HEIGHT - y - 1;
            break;
    }

    /* 转换为局部窗口坐标 */
    x -= gfx->px;
    y -= gfx->py;
    
    /* 裁剪到局部窗口范围 */
    if (x < 0 || x >= gfx->pw || y < 0 || y >= gfx->ph) return;
    
    /* 计算当前页偏移 */
    y -= gfx->current_page * gfx->page_height;
    
    /* 检查是否在当前页范围内 */
    if (y < 0 || y >= gfx->page_height) return;

    /* 根据颜色模式写入像素 */
    if (gfx->color == gfx->buffer) {  
        /* 四色模式：每个像素用2位表示 */
        uint32_t i = x / 4 + ((uint32_t)y) * (gfx->pw / 4);
        uint8_t pv = color4(color);
        switch (x % 4) {
            case 0:
                gfx->buffer[i] = (gfx->buffer[i] & 0x3F) | (pv << 6);
                break;
            case 1:
                gfx->buffer[i] = (gfx->buffer[i] & 0xCF) | (pv << 4);
                break;
            case 2:
                gfx->buffer[i] = (gfx->buffer[i] & 0xF3) | (pv << 2);
                break;
            case 3:
                gfx->buffer[i] = (gfx->buffer[i] & 0xFC) | pv;
                break;
        }
    } else if (gfx->color != NULL) {  
        /* 三色模式：使用两个独立的位图缓冲区 */
        uint16_t i = x / 8 + y * (gfx->pw / 8);
        gfx->buffer[i] |= 0x80 >> (x & 7);  /* 黑色缓冲区默认为白 */
        gfx->color[i] |= 0x80 >> (x & 7);   /* 颜色缓冲区默认为白 */
        if (color == GFX_BLACK)
            gfx->buffer[i] &= ~(0x80 >> (x & 7));  /* 设置黑色 */
        else if (color != GFX_WHITE)
            gfx->color[i] &= ~(0x80 >> (x & 7));   /* 设置红色 */
    } else {
        /* 单色模式：每个像素用1位表示 */
        uint16_t i = x / 8 + y * (gfx->pw / 8);
        if (color == GFX_WHITE)
            gfx->buffer[i] |= 0x80 >> (x & 7);   /* 设置白色 */
        else
            gfx->buffer[i] &= ~(0x80 >> (x & 7)); /* 设置黑色 */
    }
}

/*============================================================================
 * 直线绘制函数
 *============================================================================*/

/**
 * @brief 画直线（Bresenham算法）
 * 
 * 使用Bresenham直线算法在两点之间画一条直线。
 * 这是一种高效的整数运算算法，避免了浮点运算。
 * 
 * 算法原理：
 * 1. 确定主方向（较长的轴）
 * 2. 沿主方向步进，根据误差累积决定是否在次方向步进
 * 
 * @param gfx   图形上下文指针
 * @param x0    起点X坐标
 * @param y0    起点Y坐标
 * @param x1    终点X坐标
 * @param y1    终点Y坐标
 * @param color 线条颜色（RGB565格式）
 */
void GFX_drawLine(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    /* 判断是否为陡峭线（斜率绝对值大于1） */
    int16_t steep = ABS(y1 - y0) > ABS(x1 - x0);
    if (steep) {
        /* 陡峭线：交换x和y，使x成为主方向 */
        SWAP(x0, y0, int16_t);
        SWAP(x1, y1, int16_t);
    }

    /* 确保从左到右绘制 */
    if (x0 > x1) {
        SWAP(x0, x1, int16_t);
        SWAP(y0, y1, int16_t);
    }

    /* 计算增量 */
    int16_t dx, dy;
    dx = x1 - x0;
    dy = ABS(y1 - y0);

    /* 初始化误差值 */
    int16_t err = dx / 2;
    int16_t ystep;

    /* 确定y方向步进值 */
    if (y0 < y1) {
        ystep = 1;
    } else {
        ystep = -1;
    }

    /* 主循环：沿x方向步进 */
    for (; x0 <= x1; x0++) {
        if (steep) {
            GFX_drawPixel(gfx, y0, x0, color);
        } else {
            GFX_drawPixel(gfx, x0, y0, color);
        }
        
        /* 更新误差值 */
        err -= dy;
        if (err < 0) {
            y0 += ystep;
            err += dx;
        }
    }
}

/**
 * @brief 画虚线（Bresenham算法）
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
                        uint8_t dot_len, uint8_t space_len) {
    /* 判断是否为陡峭线 */
    int16_t steep = ABS(y1 - y0) > ABS(x1 - x0);
    if (steep) {
        SWAP(x0, y0, int16_t);
        SWAP(x1, y1, int16_t);
    }

    /* 确保从左到右绘制 */
    if (x0 > x1) {
        SWAP(x0, x1, int16_t);
        SWAP(y0, y1, int16_t);
    }

    /* 计算增量 */
    int16_t dx, dy;
    dx = x1 - x0;
    dy = ABS(y1 - y0);

    /* 初始化误差值和步进方向 */
    int16_t err = dx / 2;
    int16_t ystep;

    if (y0 < y1) {
        ystep = 1;
    } else {
        ystep = -1;
    }

    /* 虚线控制变量 */
    uint8_t draw = 1;   /* 是否绘制当前点 */
    uint8_t len = 0;    /* 当前段长度计数 */
    
    /* 主循环 */
    for (; x0 <= x1; x0++) {
        if (draw) {
            /* 绘制实线段 */
            if (steep) {
                GFX_drawPixel(gfx, y0, x0, color);
            } else {
                GFX_drawPixel(gfx, x0, y0, color);
            }
            len++;
            if (len >= dot_len) {
                len = 0;
                draw = 0;  /* 切换到间隔段 */
            }
        } else {
            /* 跳过间隔段 */
            len++;
            if (len >= space_len) {
                len = 0;
                draw = 1;  /* 切换到实线段 */
            }
        }
        
        /* 更新误差值 */
        err -= dy;
        if (err < 0) {
            y0 += ystep;
            err += dx;
        }
    }
}

/*============================================================================
 * 快速线段绘制函数
 *============================================================================*/

/**
 * @brief 画垂直线
 * 
 * 快速绘制一条垂直线，比通用drawLine函数效率更高。
 * 通过边界检查和裁剪优化绘制过程。
 * 
 * @param gfx   图形上下文指针
 * @param x     顶点X坐标
 * @param y     顶点Y坐标
 * @param h     线条高度（像素）
 * @param color 线条颜色（RGB565格式）
 */
void GFX_drawFastVLine(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t h, uint16_t color) {
    if (h <= 0) return;

    /* 边界检查 */
    if (x < 0 || x >= gfx->_width) return;
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (y + h > gfx->_height) h = gfx->_height - y;
    if (h <= 0) return;

    /* 逐点绘制 */
    for (int16_t i = 0; i < h; i++) {
        GFX_drawPixel(gfx, x, y + i, color);
    }
}

/**
 * @brief 画水平线
 * 
 * 快速绘制一条水平线，比通用drawLine函数效率更高。
 * 通过边界检查和裁剪优化绘制过程。
 * 
 * @param gfx   图形上下文指针
 * @param x     左端点X坐标
 * @param y     左端点Y坐标
 * @param w     线条宽度（像素）
 * @param color 线条颜色（RGB565格式）
 */
void GFX_drawFastHLine(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (w <= 0) return;

    /* 边界检查 */
    if (y < 0 || y >= gfx->_height) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (x + w > gfx->_width) w = gfx->_width - x;
    if (w <= 0) return;

    /* 逐点绘制 */
    for (int16_t i = 0; i < w; i++) {
        GFX_drawPixel(gfx, x + i, y, color);
    }
}

/*============================================================================
 * 矩形绘制函数
 *============================================================================*/

/**
 * @brief 填充矩形
 * 
 * 用指定颜色填充一个矩形区域。
 * 通过绘制多条垂直线实现。
 * 
 * @param gfx   图形上下文指针
 * @param x     左上角X坐标
 * @param y     左上角Y坐标
 * @param w     矩形宽度（像素）
 * @param h     矩形高度（像素）
 * @param color 填充颜色（RGB565格式）
 */
void GFX_fillRect(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    for (int16_t i = x; i < x + w; i++) {
        GFX_drawFastVLine(gfx, i, y, h, color);
    }
}

/**
 * @brief 填充整个屏幕
 * 
 * 用指定颜色清空整个屏幕缓冲区。
 * 使用memset进行快速填充，比逐点绘制效率高得多。
 * 
 * @param gfx   图形上下文指针
 * @param color 填充颜色（RGB565格式）
 */
void GFX_fillScreen(Adafruit_GFX* gfx, uint16_t color) {
    uint32_t size = ((gfx->WIDTH + 7) / 8) * gfx->page_height;
    if (gfx->color == gfx->buffer) {        
        /* 四色模式：用特定模式填充 */
        uint8_t pv = color4(color) * 0x55;  /* 生成0b01010101模式 */
        memset(gfx->buffer, pv, size * 2);
    } else {
        /* 单色/三色模式 */
        memset(gfx->buffer, color == GFX_WHITE ? 0xFF : 0x00, size);
        if (gfx->color != NULL) memset(gfx->color, color == GFX_RED ? 0x00 : 0xFF, size);
    }
}

/*============================================================================
 * 圆形绘制函数
 *============================================================================*/

/**
 * @brief 画圆边框（中点圆算法）
 * 
 * 使用中点圆算法（Midpoint Circle Algorithm）绘制圆形边框。
 * 这是一种高效的整数运算算法，利用圆的八分对称性，
 * 只需计算1/8圆弧，其余部分通过对称复制得到。
 * 
 * 算法原理：
 * 从圆顶点开始，逐步向右下方移动，
 * 根据当前点到圆心的距离与半径的差值决定下一步方向。
 * 
 * @param gfx   图形上下文指针
 * @param x0    圆心X坐标
 * @param y0    圆心Y坐标
 * @param r     圆的半径（像素）
 * @param color 边框颜色（RGB565格式）
 */
void GFX_drawCircle(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    /* 初始化决策参数 */
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    /* 绘制圆的四个顶点 */
    GFX_drawPixel(gfx, x0, y0 + r, color);
    GFX_drawPixel(gfx, x0, y0 - r, color);
    GFX_drawPixel(gfx, x0 + r, y0, color);
    GFX_drawPixel(gfx, x0 - r, y0, color);

    /* 主循环：计算1/8圆弧 */
    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        /* 利用八分对称性绘制8个点 */
        GFX_drawPixel(gfx, x0 + x, y0 + y, color);
        GFX_drawPixel(gfx, x0 - x, y0 + y, color);
        GFX_drawPixel(gfx, x0 + x, y0 - y, color);
        GFX_drawPixel(gfx, x0 - x, y0 - y, color);
        GFX_drawPixel(gfx, x0 + y, y0 + x, color);
        GFX_drawPixel(gfx, x0 - y, y0 + x, color);
        GFX_drawPixel(gfx, x0 + y, y0 - x, color);
        GFX_drawPixel(gfx, x0 - y, y0 - x, color);
    }
}

/**
 * @brief 画圆弧辅助函数
 * 
 * 绘制圆的一部分（四分之一圆弧），用于绘制圆角矩形等复合图形。
 * 通过cornername参数选择绘制哪些象限的圆弧。
 * 
 * @param gfx        图形上下文指针
 * @param x0         圆心X坐标
 * @param y0         圆心Y坐标
 * @param r          圆的半径（像素）
 * @param cornername 角落选择掩码（位1=右上，位2=右下，位4=左下，位8=左上）
 * @param color      边框颜色（RGB565格式）
 */
void GFX_drawCircleHelper(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t r, uint8_t cornername, uint16_t color) {
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;
        
        /* 根据掩码绘制指定象限的点 */
        if (cornername & 0x4) {
            GFX_drawPixel(gfx, x0 + x, y0 + y, color);
            GFX_drawPixel(gfx, x0 + y, y0 + x, color);
        }
        if (cornername & 0x2) {
            GFX_drawPixel(gfx, x0 + x, y0 - y, color);
            GFX_drawPixel(gfx, x0 + y, y0 - x, color);
        }
        if (cornername & 0x8) {
            GFX_drawPixel(gfx, x0 - y, y0 + x, color);
            GFX_drawPixel(gfx, x0 - x, y0 + y, color);
        }
        if (cornername & 0x1) {
            GFX_drawPixel(gfx, x0 - y, y0 - x, color);
            GFX_drawPixel(gfx, x0 - x, y0 - y, color);
        }
    }
}

/**
 * @brief 填充圆形
 * 
 * 绘制一个实心圆。通过绘制垂直线段实现填充。
 * 
 * @param gfx   图形上下文指针
 * @param x0    圆心X坐标
 * @param y0    圆心Y坐标
 * @param r     圆的半径（像素）
 * @param color 填充颜色（RGB565格式）
 */
void GFX_fillCircle(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    /* 先绘制中心垂直线 */
    GFX_drawFastVLine(gfx, x0, y0 - r, 2 * r + 1, color);
    /* 使用辅助函数填充两侧 */
    GFX_fillCircleHelper(gfx, x0, y0, r, 3, 0, color);
}

/**
 * @brief 填充圆弧辅助函数
 * 
 * 填充圆的一部分，用于绘制圆角矩形等复合图形。
 * 通过绘制垂直线段实现填充。
 * 
 * @param gfx     图形上下文指针
 * @param x0      圆心X坐标
 * @param y0      圆心Y坐标
 * @param r       圆的半径（像素）
 * @param corners 角落选择掩码（位1=右侧，位2=左侧）
 * @param delta   垂直偏移量，用于圆角矩形绘制
 * @param color   填充颜色（RGB565格式）
 */
void GFX_fillCircleHelper(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t r, uint8_t corners, int16_t delta,
                          uint16_t color) {
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;
    int16_t px = x;
    int16_t py = y;

    delta++;  /* 避免循环中的+1操作 */

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;
        
        /* 这些检查避免重复绘制某些线段 */
        if (x < (y + 1)) {
            if (corners & 1) GFX_drawFastVLine(gfx, x0 + x, y0 - y, 2 * y + delta, color);
            if (corners & 2) GFX_drawFastVLine(gfx, x0 - x, y0 - y, 2 * y + delta, color);
        }
        if (y != py) {
            if (corners & 1) GFX_drawFastVLine(gfx, x0 + py, y0 - px, 2 * px + delta, color);
            if (corners & 2) GFX_drawFastVLine(gfx, x0 - py, y0 - px, 2 * px + delta, color);
            py = y;
        }
        px = x;
    }
}

/*============================================================================
 * 椭圆绘制函数
 *============================================================================*/

/**
 * @brief 画椭圆边框（Bresenham椭圆算法）
 * 
 * 使用Bresenham椭圆算法绘制椭圆边框。
 * 算法分两个区域处理，每个区域使用不同的决策函数。
 * 
 * @param gfx   图形上下文指针
 * @param x0    椭圆中心X坐标
 * @param y0    椭圆中心Y坐标
 * @param rw    水平半径（像素）
 * @param rh    垂直半径（像素）
 * @param color 边框颜色（RGB565格式）
 */
void GFX_drawEllipse(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t rw, int16_t rh, uint16_t color) {
    /* Bresenham椭圆算法 */
    int16_t x = 0, y = rh;
    int32_t rw2 = rw * rw, rh2 = rh * rh;
    int32_t twoRw2 = 2 * rw2, twoRh2 = 2 * rh2;

    /* 初始化决策参数 */
    int32_t decision = rh2 - (rw2 * rh) + (rw2 / 4);

    /* 区域1：斜率绝对值小于1的部分 */
    while ((twoRh2 * x) < (twoRw2 * y)) {
        /* 利用四分对称性绘制4个点 */
        GFX_drawPixel(gfx, x0 + x, y0 + y, color);
        GFX_drawPixel(gfx, x0 - x, y0 + y, color);
        GFX_drawPixel(gfx, x0 + x, y0 - y, color);
        GFX_drawPixel(gfx, x0 - x, y0 - y, color);
        x++;
        if (decision < 0) {
            decision += rh2 + (twoRh2 * x);
        } else {
            decision += rh2 + (twoRh2 * x) - (twoRw2 * y);
            y--;
        }
    }

    /* 区域2：斜率绝对值大于1的部分 */
    decision = ((rh2 * (2 * x + 1) * (2 * x + 1)) >> 2) + (rw2 * (y - 1) * (y - 1)) - (rw2 * rh2);
    while (y >= 0) {
        GFX_drawPixel(gfx, x0 + x, y0 + y, color);
        GFX_drawPixel(gfx, x0 - x, y0 + y, color);
        GFX_drawPixel(gfx, x0 + x, y0 - y, color);
        GFX_drawPixel(gfx, x0 - x, y0 - y, color);
        y--;
        if (decision > 0) {
            decision += rw2 - (twoRw2 * y);
        } else {
            decision += rw2 + (twoRh2 * x) - (twoRw2 * y);
            x++;
        }
    }
}

/**
 * @brief 填充椭圆（Bresenham椭圆算法）
 * 
 * 绘制一个实心椭圆。通过绘制水平线段实现填充。
 * 
 * @param gfx   图形上下文指针
 * @param x0    椭圆中心X坐标
 * @param y0    椭圆中心Y坐标
 * @param rw    水平半径（像素）
 * @param rh    垂直半径（像素）
 * @param color 填充颜色（RGB565格式）
 */
void GFX_fillEllipse(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t rw, int16_t rh, uint16_t color) {
    /* Bresenham椭圆算法 */
    int16_t x = 0, y = rh;
    int32_t rw2 = rw * rw, rh2 = rh * rh;
    int32_t twoRw2 = 2 * rw2, twoRh2 = 2 * rh2;

    int32_t decision = rh2 - (rw2 * rh) + (rw2 / 4);

    /* 区域1 */
    while ((twoRh2 * x) < (twoRw2 * y)) {
        x++;
        if (decision < 0) {
            decision += rh2 + (twoRh2 * x);
        } else {
            decision += rh2 + (twoRh2 * x) - (twoRw2 * y);
            /* 绘制水平填充线 */
            GFX_drawFastHLine(gfx, x0 - (x - 1), y0 + y, 2 * (x - 1) + 1, color);
            GFX_drawFastHLine(gfx, x0 - (x - 1), y0 - y, 2 * (x - 1) + 1, color);
            y--;
        }
    }

    /* 区域2 */
    decision = ((rh2 * (2 * x + 1) * (2 * x + 1)) >> 2) + (rw2 * (y - 1) * (y - 1)) - (rw2 * rh2);
    while (y >= 0) {
        /* 绘制水平填充线 */
        GFX_drawFastHLine(gfx, x0 - x, y0 + y, 2 * x + 1, color);
        GFX_drawFastHLine(gfx, x0 - x, y0 - y, 2 * x + 1, color);
        y--;
        if (decision > 0) {
            decision += rw2 - (twoRw2 * y);
        } else {
            decision += rw2 + (twoRh2 * x) - (twoRw2 * y);
            x++;
        }
    }
}

/*============================================================================
 * 矩形绘制函数
 *============================================================================*/

/**
 * @brief 画矩形边框
 * 
 * 绘制一个矩形的边框（不填充内部）。
 * 通过绘制四条线段实现。
 * 
 * @param gfx   图形上下文指针
 * @param x     左上角X坐标
 * @param y     左上角Y坐标
 * @param w     矩形宽度（像素）
 * @param h     矩形高度（像素）
 * @param color 边框颜色（RGB565格式）
 */
void GFX_drawRect(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    /* 上边 */
    GFX_drawFastHLine(gfx, x, y, w, color);
    /* 下边 */
    GFX_drawFastHLine(gfx, x, y + h - 1, w, color);
    /* 左边 */
    GFX_drawFastVLine(gfx, x, y, h, color);
    /* 右边 */
    GFX_drawFastVLine(gfx, x + w - 1, y, h, color);
}

/**
 * @brief 画圆角矩形边框
 * 
 * 绘制一个带圆角的矩形边框。
 * 
 * @param gfx    图形上下文指针
 * @param x      左上角X坐标
 * @param y      左上角Y坐标
 * @param w      矩形宽度（像素）
 * @param h      矩形高度（像素）
 * @param r      圆角半径（像素）
 * @param color  边框颜色（RGB565格式）
 */
void GFX_drawRoundRect(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    /* 限制圆角半径不超过短边的一半 */
    int16_t max_radius = ((w < h) ? w : h) / 2;
    if (r > max_radius) r = max_radius;
    
    /* 绘制四条直边 */
    GFX_drawFastHLine(gfx, x + r, y, w - 2 * r, color);          /* 上边 */
    GFX_drawFastHLine(gfx, x + r, y + h - 1, w - 2 * r, color);  /* 下边 */
    GFX_drawFastVLine(gfx, x, y + r, h - 2 * r, color);          /* 左边 */
    GFX_drawFastVLine(gfx, x + w - 1, y + r, h - 2 * r, color);  /* 右边 */
    
    /* 绘制四个圆角 */
    GFX_drawCircleHelper(gfx, x + r, y + r, r, 1, color);            /* 左上角 */
    GFX_drawCircleHelper(gfx, x + w - r - 1, y + r, r, 2, color);    /* 右上角 */
    GFX_drawCircleHelper(gfx, x + w - r - 1, y + h - r - 1, r, 4, color);  /* 右下角 */
    GFX_drawCircleHelper(gfx, x + r, y + h - r - 1, r, 8, color);    /* 左下角 */
}

/**
 * @brief 填充圆角矩形
 * 
 * 绘制一个实心的圆角矩形。
 * 
 * @param gfx    图形上下文指针
 * @param x      左上角X坐标
 * @param y      左上角Y坐标
 * @param w      矩形宽度（像素）
 * @param h      矩形高度（像素）
 * @param r      圆角半径（像素）
 * @param color  填充颜色（RGB565格式）
 */
void GFX_fillRoundRect(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    /* 限制圆角半径 */
    int16_t max_radius = ((w < h) ? w : h) / 2;
    if (r > max_radius) r = max_radius;
    
    /* 填充中间矩形区域 */
    GFX_fillRect(gfx, x + r, y, w - 2 * r, h, color);
    
    /* 填充两侧圆角区域 */
    GFX_fillCircleHelper(gfx, x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color);
    GFX_fillCircleHelper(gfx, x + r, y + r, r, 2, h - 2 * r - 1, color);
}

/*============================================================================
 * 三角形绘制函数
 *============================================================================*/

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
                      uint16_t color) {
    GFX_drawLine(gfx, x0, y0, x1, y1, color);
    GFX_drawLine(gfx, x1, y1, x2, y2, color);
    GFX_drawLine(gfx, x2, y2, x0, y0, color);
}

/**
 * @brief 填充三角形
 * 
 * 绘制一个实心三角形。使用扫描线填充算法。
 * 
 * 算法原理：
 * 1. 将三个顶点按Y坐标排序
 * 2. 分上下两部分分别处理
 * 3. 对每条水平扫描线，计算与三角形两边的交点
 * 4. 在两个交点之间绘制水平线
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
                      uint16_t color) {
    int16_t a, b, y, last;

    /* 按Y坐标排序三个顶点（y2 >= y1 >= y0） */
    if (y0 > y1) {
        SWAP(y0, y1, int16_t);
        SWAP(x0, x1, int16_t);
    }
    if (y1 > y2) {
        SWAP(y2, y1, int16_t);
        SWAP(x2, x1, int16_t);
    }
    if (y0 > y1) {
        SWAP(y0, y1, int16_t);
        SWAP(x0, x1, int16_t);
    }

    /* 处理三个顶点在同一水平线的特殊情况 */
    if (y0 == y2) {
        a = b = x0;
        if (x1 < a)
            a = x1;
        else if (x1 > b)
            b = x1;
        if (x2 < a)
            a = x2;
        else if (x2 > b)
            b = x2;
        GFX_drawFastHLine(gfx, a, y0, b - a + 1, color);
        return;
    }

    /* 计算增量 */
    int16_t dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0, dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t sa = 0, sb = 0;

    /* 处理上半部分三角形 */
    if (y1 == y2)
        last = y1;      /* 包含y1扫描线 */
    else
        last = y1 - 1;  /* 跳过y1扫描线 */

    for (y = y0; y <= last; y++) {
        a = x0 + sa / dy01;
        b = x0 + sb / dy02;
        sa += dx01;
        sb += dx02;
        if (a > b) SWAP(a, b, int16_t);
        GFX_drawFastHLine(gfx, a, y, b - a + 1, color);
    }

    /* 处理下半部分三角形 */
    sa = (int32_t)dx12 * (y - y1);
    sb = (int32_t)dx02 * (y - y0);
    for (; y <= y2; y++) {
        a = x1 + sa / dy12;
        b = x0 + sb / dy02;
        sa += dx12;
        sb += dx02;
        if (a > b) SWAP(a, b, int16_t);
        GFX_drawFastHLine(gfx, a, y, b - a + 1, color);
    }
}

/*============================================================================
 * 位图绘制函数
 *============================================================================*/

/**
 * @brief 绘制位图
 * 
 * 在指定位置绘制一个单色位图。
 * 位图数据按字节存储，每个字节的最高位对应最左边的像素。
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
                    uint16_t color, bool invert) {
    int16_t byteWidth = (w + 7) / 8;  /* 每行字节数 */
    uint8_t byte = 0;

    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            /* 读取位图数据 */
            if (i & 7)
                byte <<= 1;
            else
                byte = bitmap[j * byteWidth + i / 8];
            
            /* 检查像素是否需要绘制 */
            if (((byte & 0x80) == 0x80) ^ invert) GFX_drawPixel(gfx, x + i, y + j, color);
        }
    }
}

/*============================================================================
 * U8G2字体集成（支持UTF-8中文显示）
 * 
 * 以下代码来自U8G2_for_Adafruit_GFX项目，为Adafruit GFX库添加
 * Unicode支持和U8G2字体渲染功能。
 * 
 * Copyright (c) 2018, olikraus@gmail.com
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * * Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 * 
 * * Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or other
 *   materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *============================================================================*/

/**
 * @brief 设置文本光标位置
 * 
 * 设置print函数使用的文本起始位置。
 * 同时重置UTF-8解码器状态。
 * 
 * @param gfx 图形上下文指针
 * @param x   光标X坐标
 * @param y   光标Y坐标
 */
void GFX_setCursor(Adafruit_GFX* gfx, int16_t x, int16_t y) {
    gfx->tx = x;
    gfx->ty = y;
    gfx->utf8_state = 0;
}

/**
 * @brief 设置当前字体
 * 
 * 设置用于文本显示的U8G2字体。
 * 
 * @param gfx 图形上下文指针
 * @param font 字体数据指针
 */
void GFX_setFont(Adafruit_GFX* gfx, const uint8_t* font) { 
    u8g2_SetFont(&gfx->u8g2, font); 
}

/**
 * @brief 设置字体渲染模式
 * 
 * @param gfx            图形上下文指针
 * @param is_transparent 是否透明背景（0=不透明，1=透明）
 */
void GFX_setFontMode(Adafruit_GFX* gfx, uint8_t is_transparent) { 
    u8g2_SetFontMode(&gfx->u8g2, is_transparent); 
}

/**
 * @brief 设置字体绘制方向
 * 
 * @param gfx 图形上下文指针
 * @param d   绘制方向（0=从左到右，1=从上到下，2=从右到左，3=从下到上）
 */
void GFX_setFontDirection(Adafruit_GFX* gfx, GFX_Rotate d) { 
    u8g2_SetFontDirection(&gfx->u8g2, (uint8_t)d); 
}

/**
 * @brief 设置文本颜色
 * 
 * @param gfx 图形上下文指针
 * @param fg  前景色（文字颜色，RGB565格式）
 * @param bg  背景色（RGB565格式）
 */
void GFX_setTextColor(Adafruit_GFX* gfx, uint16_t fg, uint16_t bg) {
    u8g2_SetForegroundColor(&gfx->u8g2, fg);
    u8g2_SetBackgroundColor(&gfx->u8g2, bg);
}

/**
 * @brief 获取字体基线高度
 * 
 * 获取当前字体从基线到字体顶部的距离。
 * 
 * @param gfx 图形上下文指针
 * @return 字体上升高度（像素）
 */
int8_t GFX_getFontAscent(Adafruit_GFX* gfx) { 
    return gfx->u8g2.font_info.ascent_A; 
}

/**
 * @brief 获取字体下降高度
 * 
 * 获取当前字体从基线到字体底部的距离（通常为负值或零）。
 * 
 * @param gfx 图形上下文指针
 * @return 字体下降高度（像素）
 */
int8_t GFX_getFontDescent(Adafruit_GFX* gfx) { 
    return gfx->u8g2.font_info.descent_g; 
}

/**
 * @brief 获取字体总高度
 * 
 * 获取当前字体的总高度（上升高度减去下降高度）。
 * 
 * @param gfx 图形上下文指针
 * @return 字体高度（像素）
 */
int8_t GFX_getFontHeight(Adafruit_GFX* gfx) { 
    return gfx->u8g2.font_info.ascent_A - gfx->u8g2.font_info.descent_g; 
}

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
int16_t GFX_drawGlyph(Adafruit_GFX* gfx, int16_t x, int16_t y, uint16_t e) {
    return u8g2_DrawGlyph(&gfx->u8g2, x, y, e);
}

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
int16_t GFX_drawStr(Adafruit_GFX* gfx, int16_t x, int16_t y, const char* s) {
    return u8g2_DrawStr(&gfx->u8g2, x, y, s);
}

/**
 * @brief UTF-8解码器
 * 
 * 将UTF-8字节序列解码为Unicode码点。
 * UTF-8是一种变长编码，每个字符可能占用1-6个字节。
 * 
 * @param gfx 图形上下文指针
 * @param b   输入字节
 * @return    解码结果：
 *            - 0x0FFFF: 字符串结束
 *            - 0x0FFFE: 需要更多字节
 *            - 其他: 解码完成的Unicode码点
 */
static uint16_t utf8_next(Adafruit_GFX* gfx, uint8_t b) {
    if (b == 0)         /* '\0' 结束字符串 */
        return 0x0ffff; /* 检测到字符串结束，丢弃待处理的UTF-8序列 */
    
    if (gfx->utf8_state == 0) {
        /* 初始状态：判断UTF-8序列长度 */
        if (b >= 0xfc) {        /* 6字节序列 */
            gfx->utf8_state = 5;
            b &= 1;
        } else if (b >= 0xf8) { /* 5字节序列 */
            gfx->utf8_state = 4;
            b &= 3;
        } else if (b >= 0xf0) { /* 4字节序列 */
            gfx->utf8_state = 3;
            b &= 7;
        } else if (b >= 0xe0) { /* 3字节序列（常见中文） */
            gfx->utf8_state = 2;
            b &= 15;
        } else if (b >= 0xc0) { /* 2字节序列 */
            gfx->utf8_state = 1;
            b &= 0x01f;
        } else {
            /* 单字节ASCII字符 */
            return b;
        }
        gfx->encoding = b;
        return 0x0fffe;
    } else {
        /* 继续解码多字节序列 */
        gfx->utf8_state--;
        gfx->encoding <<= 6;
        b &= 0x03f;
        gfx->encoding |= b;
        if (gfx->utf8_state != 0) return 0x0fffe; /* 还需要更多字节 */
    }
    return gfx->encoding;
}

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
int16_t GFX_drawUTF8(Adafruit_GFX* gfx, int16_t x, int16_t y, const char* str) {
    uint16_t e;
    int16_t delta, sum;

    gfx->utf8_state = 0;
    sum = 0;
    for (;;) {
        e = utf8_next(gfx, (uint8_t)*str);
        if (e == 0x0ffff) break;
        str++;
        if (e != 0x0fffe) {
            delta = u8g2_DrawGlyph(&gfx->u8g2, x, y, e);

            /* 根据字体方向更新坐标 */
            switch (gfx->u8g2.font_decode.dir) {
                case 0:  /* 从左到右 */
                    x += delta;
                    break;
                case 1:  /* 从上到下 */
                    y += delta;
                    break;
                case 2:  /* 从右到左 */
                    x -= delta;
                    break;
                case 3:  /* 从下到上 */
                    y -= delta;
                    break;
            }

            sum += delta;
        }
    }
    return sum;
}

/**
 * @brief 计算UTF-8字符串宽度
 * 
 * 计算指定UTF-8字符串在当前字体下的显示宽度。
 * 
 * @param gfx 图形上下文指针
 * @param str UTF-8字符串
 * @return    字符串宽度（像素）
 */
int16_t GFX_getUTF8Width(Adafruit_GFX* gfx, const char* str) {
    uint16_t e;
    int16_t dx, w;

    gfx->u8g2.font_decode.glyph_width = 0;
    gfx->utf8_state = 0;
    w = 0;
    dx = 0;
    for (;;) {
        e = utf8_next(gfx, (uint8_t)*str);
        if (e == 0x0ffff) break;
        str++;
        if (e != 0x0fffe) {
            dx = u8g2_GetGlyphWidth(&gfx->u8g2, e);
            w += dx;
        }
    }
    /* 调整最后一个字形的宽度 */
    if (gfx->u8g2.font_decode.glyph_width != 0) {
        w -= dx;
        w += gfx->u8g2.font_decode.glyph_width;
        w += gfx->u8g2.glyph_x_offset;
    }

    return w;
}

/**
 * @brief 打印单个字符
 * 
 * 在当前光标位置打印一个字符，并自动移动光标。
 * 支持UTF-8编码，可以逐字节输入中文字符。
 * 支持换行符'\n'和回车符'\r'。
 * 
 * @param gfx 图形上下文指针
 * @param c   要打印的字符
 * @return    打印的字符数（总是返回1）
 */
size_t GFX_print(Adafruit_GFX* gfx, const char c) {
    int16_t delta;
    uint16_t e = utf8_next(gfx, (uint8_t)c);
    
    if (e == '\n') {
        /* 换行：重置X坐标，Y坐标下移一行 */
        gfx->tx = 0;
        gfx->ty += gfx->u8g2.font_info.ascent_para - gfx->u8g2.font_info.descent_para;
    } else if (e == '\r') {
        /* 回车：仅重置X坐标 */
        gfx->tx = 0;
    } else if (e < 0x0fffe) {
        /* 绘制字符并更新光标位置 */
        delta = u8g2_DrawGlyph(&gfx->u8g2, gfx->tx, gfx->ty, e);
        switch (gfx->u8g2.font_decode.dir) {
            case 0:
                gfx->tx += delta;
                break;
            case 1:
                gfx->ty += delta;
                break;
            case 2:
                gfx->tx -= delta;
                break;
            case 3:
                gfx->ty -= delta;
                break;
        }
    }
    return 1;
}

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
size_t GFX_write(Adafruit_GFX* gfx, const char* buffer, size_t size) {
    size_t cnt = 0;
    while (size > 0) {
        cnt += GFX_print(gfx, *buffer++);
        size--;
    }
    return cnt;
}

/**
 * @brief 格式化打印
 * 
 * 类似printf的格式化输出函数，支持各种格式说明符。
 * 如：%d, %s, %x, %f等。
 * 
 * @param gfx   图形上下文指针
 * @param format 格式化字符串
 * @param ...    可变参数列表
 * @return       写入的字符数
 */
size_t GFX_printf(Adafruit_GFX* gfx, const char* format, ...) {
    va_list va;
    char tmp[64] = {0};
    char* buf = tmp;
    size_t len;

    /* 格式化字符串 */
    va_start(va, format);
    len = vsnprintf(tmp, sizeof(tmp), format, va);
    va_end(va);

    /* 如果缓冲区不够，动态分配更大的缓冲区 */
    if (len > sizeof(tmp) - 1) {
        buf = malloc(len + 1);
        if (buf == NULL) return 0;
        va_start(va, format);
        vsnprintf(buf, len + 1, format, va);
        va_end(va);
    }

    /* 写入显示 */
    len = GFX_write(gfx, buf, len);
    if (buf != tmp) free(buf);

    return len;
}
