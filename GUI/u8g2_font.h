/*
  U8g2_for_Adafruit_GFX.h

  U8g2字体库为Adafruit GFX的适配层，提供Unicode字体支持。
  本文件是U8g2字体系统的核心头文件，定义了字体数据结构和解码接口。

  U8g2 for Adafruit GFX Lib (https://github.com/olikraus/U8g2_for_Adafruit_GFX)

  Copyright (c) 2018, olikraus@gmail.com
  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice, this list
    of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright notice, this
    list of conditions and the following disclaimer in the documentation and/or other
    materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
  CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/
#ifndef __U8G2_H
#define __U8G2_H

#include <stdint.h>

/*============================================================================*/
/* 编译器相关宏定义 */
/* 这些宏用于控制函数的编译行为，优化代码生成和内存布局 */

#ifdef __GNUC__
/* U8X8_NOINLINE: 禁止函数内联，强制函数独立调用
   用于防止编译器过度优化导致的问题，特别是对于需要独立地址的函数 */
#define U8X8_NOINLINE __attribute__((noinline))

/* U8X8_SECTION: 将函数或变量放置到指定的内存段
   用于精确控制代码/数据的内存布局 */
#define U8X8_SECTION(name) __attribute__((section(name)))

/* U8X8_UNUSED: 标记参数或变量可能未使用，避免编译器警告 */
#define U8X8_UNUSED __attribute__((unused))
#else
/* 非GCC编译器的空定义 */
#define U8X8_SECTION(name)
#define U8X8_NOINLINE
#define U8X8_UNUSED
#endif

/*============================================================================*/
/* AVR平台PROGMEM支持 */
/* AVR架构(如Arduino Uno)使用哈佛架构，程序存储器和数据存储器分离
   需要特殊指令从程序存储器读取常量数据 */

#if defined(__GNUC__) && defined(__AVR__)
/* U8X8_FONT_SECTION: 将字体数据放置到PROGMEM段的指定位置 */
#define U8X8_FONT_SECTION(name) U8X8_SECTION(".progmem." name)

/* u8x8_pgm_read: 从程序存储器读取一个字节 */
#define u8x8_pgm_read(adr) pgm_read_byte_near(adr)

/* U8X8_PROGMEM: 将常量数据放置到程序存储器 */
#define U8X8_PROGMEM PROGMEM
#endif

/*============================================================================*/
/* 默认宏定义（用于非AVR平台） */
/* ARM、ESP等平台使用统一寻址，可以直接通过指针访问常量数据 */

#ifndef U8X8_FONT_SECTION
/* 默认空定义，字体数据存储在普通常量区 */
#define U8X8_FONT_SECTION(name)
#endif

#ifndef u8x8_pgm_read
/* 默认直接指针读取，适用于统一寻址架构 */
#define u8x8_pgm_read(adr) (*(const uint8_t*)(adr))
#endif

#ifndef U8X8_PROGMEM
/* 默认空定义，常量数据使用普通存储方式 */
#define U8X8_PROGMEM
#endif

/* U8G2字体段定义，与U8X8字体段保持一致 */
#define U8G2_FONT_SECTION(name) U8X8_FONT_SECTION(name)

/*============================================================================*/
/* 大字体支持 */
/* 启用大于32KB的字体支持，适用于支持大数组的平台 */

/* the macro U8G2_USE_LARGE_FONTS enables large fonts (>32K) */
/* it can be enabled for those uC supporting larger arrays */
#if defined(unix) || defined(__arm__) || defined(__arc__) || defined(ESP8266) || defined(ESP_PLATFORM)
#ifndef U8G2_USE_LARGE_FONTS
#define U8G2_USE_LARGE_FONTS
#endif
#endif

/*============================================================================*/
/* 字体信息结构体 */
/* 存储字体的元数据信息，包括字符数量、编码模式、位宽配置、边界框等
   这些信息从字体数据头部解析而来，用于指导字形解码过程 */
typedef struct _u8g2_font_info_t {
    /* offset 0 - 字符统计和编码模式 */
    uint8_t glyph_cnt;       /* 字体包含的字形总数 */
    uint8_t bbx_mode;        /* 边界框模式：0=最大宽度模式，1=等宽模式，2=比例宽度模式 */
    uint8_t bits_per_0;      /* 游程编码中'0'(背景)的位数 */
    uint8_t bits_per_1;      /* 游程编码中'1'(前景)的位数 */

    /* offset 4 - 字形尺寸编码的位宽配置 */
    uint8_t bits_per_char_width;   /* 字形宽度字段的位数 */
    uint8_t bits_per_char_height;  /* 字形高度字段的位数 */
    uint8_t bits_per_char_x;       /* 字形X偏移字段的位数 */
    uint8_t bits_per_char_y;       /* 字形Y偏移字段的位数 */
    uint8_t bits_per_delta_x;      /* 字形X增量字段的位数 */

    /* offset 9 - 字体边界框信息 */
    int8_t max_char_width;   /* 字体中最大字符宽度 */
    int8_t max_char_height;  /* 字体整体高度，注意：不是上升高度
                                上升高度 = max_char_height + y_offset */
    int8_t x_offset;         /* 字体边界框X偏移（相对于基准点） */
    int8_t y_offset;         /* 字体边界框Y偏移（相对于基准线） */

    /* offset 13 - 字体度量信息 */
    int8_t ascent_A;         /* 大写字母'A'的上升高度（基线到顶部的距离） */
    int8_t descent_g;        /* 小写字母'g'的下降高度（通常为负值，基线到底部的距离） */
    int8_t ascent_para;      /* 段落上升高度 */
    int8_t descent_para;     /* 段落下降高度 */

    /* offset 17 - ASCII字符查找表位置 */
    uint16_t start_pos_upper_A;  /* 大写字母'A'在字体数据中的起始位置 */
    uint16_t start_pos_lower_a;  /* 小写字母'a'在字体数据中的起始位置 */

    /* offset 21 - Unicode字符查找表位置 */
    uint16_t start_pos_unicode;  /* Unicode字符查找表在字体数据中的起始位置 */
} u8g2_font_info_t;

/*============================================================================*/
/* 字体解码结构体 */
/* 用于解码压缩的字形数据，包含解码状态、目标位置、颜色设置等
   支持字形旋转和透明/不透明绘制模式 */
typedef struct _u8g2_font_decode_t {
    const uint8_t* decode_ptr;  /* 指向压缩字形数据的指针 */

    /* 绘制目标位置 */
    int16_t target_x;           /* 目标X坐标（屏幕坐标） */
    int16_t target_y;           /* 目标Y坐标（屏幕坐标） */

    /* 颜色设置 */
    uint16_t fg_color;          /* 前景色（文字颜色） */
    uint16_t bg_color;          /* 背景色（用于非透明模式） */

    /* 字形局部坐标（原点在字形左上角） */
    int8_t x;                   /* 当前解码X坐标（字形局部坐标） */
    int8_t y;                   /* 当前解码Y坐标（字形局部坐标） */
    int8_t glyph_width;         /* 当前字形宽度 */
    int8_t glyph_height;        /* 当前字形高度 */

    /* 解码状态 */
    uint8_t decode_bit_pos;     /* 当前字节中的位位置（0-7） */
    uint8_t is_transparent;     /* 透明模式标志：0=不透明（绘制背景），1=透明 */
    uint8_t dir;                /* 绘制方向：0=左到右，1=上到下，2=右到左，3=下到上 */
} u8g2_font_decode_t;

/*============================================================================*/
/* 字体主结构体 */
/* U8g2字体系统的核心结构，包含字体数据指针、解码器、信息结构和绘制回调
   每个使用U8g2字体的显示设备需要维护一个此结构体实例 */
typedef struct _u8g2_font_t {
    const uint8_t* font;        /* 当前字体数据指针 */

    u8g2_font_decode_t font_decode;  /* 字体解码器实例 */
    u8g2_font_info_t font_info;      /* 字体信息实例 */

    int8_t glyph_x_offset;      /* 字形X偏移（由u8g2_GetGlyphWidth设置的副作用） */

    /* 绘制回调函数：绘制水平或垂直线
       参数：u8g2实例、X坐标、Y坐标、长度、方向、颜色
       方向：0=水平向右，1=垂直向下，2=水平向左，3=垂直向上 */
    void (*draw_hv_line)(struct _u8g2_font_t* u8g2, int16_t x, int16_t y, int16_t len, uint8_t dir, uint16_t color);
} u8g2_font_t;

/*============================================================================*/
/* 公共函数声明 */

/**
 * @brief 检查字体是否包含指定编码的字形
 * @param u8g2 字体实例指针
 * @param requested_encoding 请求的字符编码（ASCII或Unicode）
 * @return 1表示字形存在，0表示不存在
 */
uint8_t u8g2_IsGlyph(u8g2_font_t* u8g2, uint16_t requested_encoding);

/**
 * @brief 获取指定字形的前进宽度（delta x）
 * @param u8g2 字体实例指针
 * @param requested_encoding 请求的字符编码
 * @return 字形的前进宽度，如果字形不存在则返回0
 * @note 副作用：会更新font_decode和glyph_x_offset
 */
int8_t u8g2_GetGlyphWidth(u8g2_font_t* u8g2, uint16_t requested_encoding);

/**
 * @brief 设置字体绘制模式
 * @param u8g2 字体实例指针
 * @param is_transparent 0=不透明模式（绘制背景色），1=透明模式（不绘制背景）
 */
void u8g2_SetFontMode(u8g2_font_t* u8g2, uint8_t is_transparent);

/**
 * @brief 设置字符串绘制方向
 * @param u8g2 字体实例指针
 * @param dir 方向值：
 *            0 = 0度（从左到右）
 *            1 = 90度（从上到下）
 *            2 = 180度（从右到左）
 *            3 = 270度（从下到上）
 */
void u8g2_SetFontDirection(u8g2_font_t* u8g2, uint8_t dir);

/**
 * @brief 在指定位置绘制单个字形
 * @param u8g2 字体实例指针
 * @param x 目标X坐标
 * @param y 目标Y坐标（基线位置）
 * @param encoding 字符编码（支持0-65535的Unicode基本多语言平面）
 * @return 字形的前进宽度
 */
int16_t u8g2_DrawGlyph(u8g2_font_t* u8g2, int16_t x, int16_t y, uint16_t encoding);

/**
 * @brief 在指定位置绘制字符串
 * @param u8g2 字体实例指针
 * @param x 起始X坐标
 * @param y 起始Y坐标（基线位置）
 * @param s 要绘制的字符串（C字符串，以'\0'结尾）
 * @return 字符串的总前进宽度
 * @note 对于编码127-255的字符，可使用"\xab"(十六进制)或"\xyz"(八进制)转义
 *       此函数不支持编码>=256的Unicode字符，请使用drawUTF8或drawGlyph
 */
int16_t u8g2_DrawStr(u8g2_font_t* u8g2, int16_t x, int16_t y, const char* s);

/**
 * @brief 设置当前字体
 * @param u8g2 字体实例指针
 * @param font 字体数据指针（指向U8g2字体数组）
 */
void u8g2_SetFont(u8g2_font_t* u8g2, const uint8_t* font);

/**
 * @brief 设置前景色（文字颜色）
 * @param u8g2 字体实例指针
 * @param fg 前景色值（RGB565格式或其他显示驱动支持的格式）
 */
void u8g2_SetForegroundColor(u8g2_font_t* u8g2, uint16_t fg);

/**
 * @brief 设置背景色（用于非透明模式）
 * @param u8g2 字体实例指针
 * @param bg 背景色值（RGB565格式或其他显示驱动支持的格式）
 */
void u8g2_SetBackgroundColor(u8g2_font_t* u8g2, uint16_t bg);

#endif
