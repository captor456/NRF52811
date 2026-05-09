/*
    U8g2_for_Adafruit_GFX.cpp

    U8g2字体库为Adafruit GFX的适配层实现文件。
    本文件实现了U8g2字体系统的核心功能，包括：
    - 字体信息解析
    - 压缩字形数据解码（游程编码RLE）
    - 字形查找和绘制
    - 支持ASCII和Unicode字符集

    U8g2字体格式要点：
    - 字体数据使用游程编码(RLE)压缩，减少存储空间
    - 支持Unicode字符集（基本多语言平面，0-65535）
    - 字形数据包含宽度、高度、偏移和位图数据
    - 使用变长位编码存储字形参数

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

#include "u8g2_font.h"

#include <stddef.h>

/*============================================================================*/
/* 字体数据读取函数 */
/* 这些函数用于从字体数据中读取字节和字，支持不同平台的存储方式 */

/**
 * @brief 从字体数据中读取一个字节
 * @param font 字体数据指针
 * @param offset 相对于字体起始位置的偏移量
 * @return 读取的字节值
 * @note 使用u8x8_pgm_read宏，兼容AVR的PROGMEM和其他平台的普通存储
 */
static uint8_t u8g2_font_get_byte(const uint8_t* font, uint8_t offset) {
    font += offset;
    return u8x8_pgm_read(font);
}

/**
 * @brief 从字体数据中读取一个16位字（小端序）
 * @param font 字体数据指针
 * @param offset 相对于字体起始位置的偏移量
 * @return 读取的16位字值
 * @note 字体数据使用小端序存储16位值
 */
static uint16_t u8g2_font_get_word(const uint8_t* font, uint8_t offset) U8X8_NOINLINE;
static uint16_t u8g2_font_get_word(const uint8_t* font, uint8_t offset) {
    uint16_t pos;
    font += offset;
    pos = u8x8_pgm_read(font);  /* 读取低字节 */
    font++;
    pos <<= 8;
    pos += u8x8_pgm_read(font); /* 读取高字节 */
    return pos;
}

/*============================================================================*/
/* 字体信息解析 */

/**
 * @brief 解析字体信息头
 * @param font_info 用于存储解析结果的字体信息结构体指针
 * @param font 字体数据指针
 * @note 从字体数据头部读取23字节的元数据信息，填充font_info结构体
 *       字体数据布局：
 *       - 偏移0-3: 字符数、边界框模式、编码参数
 *       - 偏移4-8: 位宽配置
 *       - 偏移9-12: 最大尺寸和偏移
 *       - 偏移13-16: 上升/下降高度
 *       - 偏移17-20: ASCII查找表位置
 *       - 偏移21-22: Unicode查找表位置
 */
void u8g2_read_font_info(u8g2_font_info_t* font_info, const uint8_t* font) {
    /* offset 0 - 字符统计和编码模式 */
    font_info->glyph_cnt = u8g2_font_get_byte(font, 0);   /* 字形总数 */
    font_info->bbx_mode = u8g2_font_get_byte(font, 1);    /* 边界框模式 */
    font_info->bits_per_0 = u8g2_font_get_byte(font, 2);  /* 背景游程位数 */
    font_info->bits_per_1 = u8g2_font_get_byte(font, 3);  /* 前景游程位数 */

    /* offset 4 - 字形尺寸编码的位宽配置 */
    font_info->bits_per_char_width = u8g2_font_get_byte(font, 4);   /* 宽度位数 */
    font_info->bits_per_char_height = u8g2_font_get_byte(font, 5);  /* 高度位数 */
    font_info->bits_per_char_x = u8g2_font_get_byte(font, 6);       /* X偏移位数 */
    font_info->bits_per_char_y = u8g2_font_get_byte(font, 7);       /* Y偏移位数 */
    font_info->bits_per_delta_x = u8g2_font_get_byte(font, 8);      /* X增量位数 */

    /* offset 9 - 字体边界框信息 */
    font_info->max_char_width = u8g2_font_get_byte(font, 9);    /* 最大宽度 */
    font_info->max_char_height = u8g2_font_get_byte(font, 10);  /* 最大高度 */
    font_info->x_offset = u8g2_font_get_byte(font, 11);         /* X偏移 */
    font_info->y_offset = u8g2_font_get_byte(font, 12);         /* Y偏移 */

    /* offset 13 - 字体度量信息 */
    font_info->ascent_A = u8g2_font_get_byte(font, 13);     /* 'A'上升高度 */
    font_info->descent_g = u8g2_font_get_byte(font, 14);    /* 'g'下降高度 */
    font_info->ascent_para = u8g2_font_get_byte(font, 15);  /* 段落上升高度 */
    font_info->descent_para = u8g2_font_get_byte(font, 16); /* 段落下降高度 */

    /* offset 17 - ASCII字符查找表位置 */
    font_info->start_pos_upper_A = u8g2_font_get_word(font, 17);  /* 大写字母起始 */
    font_info->start_pos_lower_a = u8g2_font_get_word(font, 19);  /* 小写字母起始 */

    /* offset 21 - Unicode字符查找表位置 */
    font_info->start_pos_unicode = u8g2_font_get_word(font, 21);
}

/*============================================================================*/
/* 字体边界框查询函数 */

/**
 * @brief 获取字体边界框宽度
 * @param u8g2 字体实例指针
 * @return 字体最大字符宽度
 */
uint8_t u8g2_GetFontBBXWidth(u8g2_font_t* u8g2) { return u8g2->font_info.max_char_width; /* new font info structure */ }

/**
 * @brief 获取字体边界框高度
 * @param u8g2 字体实例指针
 * @return 字体最大字符高度
 */
uint8_t u8g2_GetFontBBXHeight(u8g2_font_t* u8g2) {
    return u8g2->font_info.max_char_height; /* new font info structure */
}

/**
 * @brief 获取字体边界框X偏移
 * @param u8g2 字体实例指针
 * @return 边界框相对于基准点的X偏移
 */
int8_t u8g2_GetFontBBXOffX(u8g2_font_t* u8g2) { return u8g2->font_info.x_offset; /* new font info structure */ }

/**
 * @brief 获取字体边界框Y偏移
 * @param u8g2 字体实例指针
 * @return 边界框相对于基准线的Y偏移
 */
int8_t u8g2_GetFontBBXOffY(u8g2_font_t* u8g2) { return u8g2->font_info.y_offset; /* new font info structure */ }

/**
 * @brief 获取大写字母'A'的高度
 * @param u8g2 字体实例指针
 * @return 大写字母'A'的上升高度（基线到顶部）
 */
uint8_t u8g2_GetFontCapitalAHeight(u8g2_font_t* u8g2) { return u8g2->font_info.ascent_A; /* new font info structure */ }

/*============================================================================*/
/* 位解码函数 */
/* 从压缩字体数据中读取指定位数的值，支持变长编码 */

/**
 * @brief 从压缩数据中读取无符号位值
 * @param f 字体解码结构体指针
 * @param cnt 要读取的位数
 * @return 读取的无符号值
 * @note 实现了跨字节边界的位读取，支持变长位编码
 *       字体数据使用位打包技术，此函数负责解包
 */
static uint8_t u8g2_font_decode_get_unsigned_bits(u8g2_font_decode_t* f, uint8_t cnt) U8X8_NOINLINE;
static uint8_t u8g2_font_decode_get_unsigned_bits(u8g2_font_decode_t* f, uint8_t cnt) {
    uint8_t val;
    uint8_t bit_pos = f->decode_bit_pos;
    uint8_t bit_pos_plus_cnt;

    // val = *(f->decode_ptr);
    val = u8x8_pgm_read(f->decode_ptr);  /* 读取当前字节 */

    val >>= bit_pos;                      /* 右移到位位置 */
    bit_pos_plus_cnt = bit_pos;
    bit_pos_plus_cnt += cnt;
    if (bit_pos_plus_cnt >= 8) {          /* 需要跨越字节边界 */
        uint8_t s = 8;
        s -= bit_pos;
        f->decode_ptr++;
        // val |= *(f->decode_ptr) << (8-bit_pos);
        val |= u8x8_pgm_read(f->decode_ptr) << (s);  /* 合并下一字节的高位部分 */
        // bit_pos -= 8;
        bit_pos_plus_cnt -= 8;
    }
    val &= (1U << cnt) - 1;               /* 掩码提取所需位数 */
    // bit_pos += cnt;

    f->decode_bit_pos = bit_pos_plus_cnt; /* 更新位位置 */
    return val;
}

/*
    有符号位解码说明：
    
    2 bit --> cnt = 2
        编码范围: -2,-1,0,1
        解码: value - 1

    3 bit --> cnt = 3
        编码范围: -4,-3,-2,-1,0,1,2,3
        解码: value - 4

    编码原理：
        if ( x < 0 )
            r = bits(x-1)+1;
        else
            r = bits(x)+1;

*/
/**
 * @brief 从压缩数据中读取有符号位值
 * @param f 字体解码结构体指针
 * @param cnt 要读取的位数
 * @return 读取的有符号值
 * @note 将无符号值转换为有符号值，使用偏移编码
 *       例如：3位编码范围是-4到3，编码为0-7
 */
/* optimized */
static int8_t u8g2_font_decode_get_signed_bits(u8g2_font_decode_t* f, uint8_t cnt) U8X8_NOINLINE;
static int8_t u8g2_font_decode_get_signed_bits(u8g2_font_decode_t* f, uint8_t cnt) {
    int8_t v, d;
    v = (int8_t)u8g2_font_decode_get_unsigned_bits(f, cnt);
    d = 1;
    cnt--;
    d <<= cnt;       /* 计算偏移量：2^(cnt-1) */
    v -= d;          /* 减去偏移量得到有符号值 */
    return v;
    // return (int8_t)u8g2_font_decode_get_unsigned_bits(f, cnt) - ((1<<cnt)>>1);
}

/*============================================================================*/
/* 向量加法函数 */
/* 支持字体旋转，根据方向计算实际绘制坐标 */

/**
 * @brief 根据方向计算Y坐标增量
 * @param dy 原始Y坐标
 * @param x 字形局部X坐标
 * @param y 字形局部Y坐标
 * @param dir 方向（0-3）
 * @return 计算后的Y坐标
 * @note 方向映射：
 *       0 (0度):   dy += y  （正常方向）
 *       1 (90度):  dy += x  （顺时针旋转90度）
 *       2 (180度): dy -= y  （旋转180度）
 *       3 (270度): dy -= x  （顺时针旋转270度）
 */
static int16_t u8g2_add_vector_y(int16_t dy, int8_t x, int8_t y, uint8_t dir) U8X8_NOINLINE;
static int16_t u8g2_add_vector_y(int16_t dy, int8_t x, int8_t y, uint8_t dir) {
    switch (dir) {
        case 0:   /* 0度：正常方向 */
            dy += y;
            break;
        case 1:   /* 90度：顺时针旋转 */
            dy += x;
            break;
        case 2:   /* 180度：倒置 */
            dy -= y;
            break;
        default:  /* 270度：逆时针旋转 */
            dy -= x;
            break;
    }
    return dy;
}

/**
 * @brief 根据方向计算X坐标增量
 * @param dx 原始X坐标
 * @param x 字形局部X坐标
 * @param y 字形局部Y坐标
 * @param dir 方向（0-3）
 * @return 计算后的X坐标
 * @note 方向映射：
 *       0 (0度):   dx += x  （正常方向）
 *       1 (90度):  dx -= y  （顺时针旋转90度）
 *       2 (180度): dx -= x  （旋转180度）
 *       3 (270度): dx += y  （顺时针旋转270度）
 */
static int16_t u8g2_add_vector_x(int16_t dx, int8_t x, int8_t y, uint8_t dir) U8X8_NOINLINE;
static int16_t u8g2_add_vector_x(int16_t dx, int8_t x, int8_t y, uint8_t dir) {
    switch (dir) {
        case 0:   /* 0度：正常方向 */
            dx += x;
            break;
        case 1:   /* 90度：顺时针旋转 */
            dx -= y;
            break;
        case 2:   /* 180度：倒置 */
            dx -= x;
            break;
        default:  /* 270度：逆时针旋转 */
            dx += y;
            break;
    }
    return dx;
}

/*============================================================================*/
/* 游程编码解码和绘制 */

/**
 * @brief 解码并绘制字形的一行（游程编码解码）
 * @param u8g2 字体实例指针
 * @param len 游程长度（像素数）
 * @param is_foreground 前景/背景标志（1=前景色，0=背景色）
 * 
 * 功能描述：
 *   绘制字形的一个游程区域。len可以是任意长度，需要在字形边界处换行。
 *   游程编码(RLE)将连续的相同像素值压缩为一个计数值，
 *   此函数负责解压并绘制这些像素。
 * 
 * 参数说明：
 *   len: 线条长度（像素数）
 *   is_foreground: 前景色(1)或背景色(0)
 *   u8g2->font_decode.target_x: 目标X位置
 *   u8g2->font_decode.target_y: 目标Y位置
 *   u8g2->font_decode.is_transparent: 透明模式标志
 * 
 * 调用关系：
 *   被u8g2_font_decode_glyph()调用
 */
/* optimized */
static void u8g2_font_decode_len(u8g2_font_t* u8g2, uint8_t len, uint8_t is_foreground) {
    uint8_t cnt;     /* 剩余需要绘制的像素总数 */
    uint8_t rem;     /* 到字形右边缘的剩余像素数 */
    uint8_t current; /* 当前需要绘制的像素数 */
    /* current要么等于cnt，要么等于rem */

    /* 字形局部坐标 */
    uint8_t lx, ly;

    /* 屏幕目标位置 */
    int16_t x, y;

    u8g2_font_decode_t* decode = &(u8g2->font_decode);

    cnt = len;

    /* 获取局部位置 */
    lx = decode->x;
    ly = decode->y;

    for (;;) {
        /* 计算到字形右边缘的像素数 */
        rem = decode->glyph_width;
        rem -= lx;

        /* 计算需要绘制的像素数，取剩余像素和右边缘像素的较小值 */
        current = rem;
        if (cnt < rem) current = cnt;

        /* 现在绘制线条，同时应用围绕字形目标位置的旋转 */
        // u8g2_font_decode_draw_pixel(u8g2, lx,ly,current, is_foreground);

        /* 获取目标位置 */
        x = decode->target_x;
        y = decode->target_y;

        /* 应用旋转变换 */
        x = u8g2_add_vector_x(x, lx, ly, decode->dir);
        y = u8g2_add_vector_y(y, lx, ly, decode->dir);

        /* 绘制前景和背景（如果需要） */
        if (current > 0) /* 避免绘制零长度线条，问题#4 */
        {
            if (is_foreground) {
                /* 绘制前景色（文字颜色） */
                u8g2->draw_hv_line(u8g2, x, y, current, decode->dir, decode->fg_color);
            } else if (decode->is_transparent == 0) {
                /* 非透明模式下绘制背景色 */
                u8g2->draw_hv_line(u8g2, x, y, current, decode->dir, decode->bg_color);
            }
        }

        /* 检查是否已到达游程编码的末尾 */
        if (cnt < rem) break;
        cnt -= rem;
        lx = 0;
        ly++;  /* 换到下一行 */
    }
    lx += cnt;

    /* 更新解码器位置 */
    decode->x = lx;
    decode->y = ly;
}

/*============================================================================*/
/* 字形解码设置 */

/**
 * @brief 设置解码器并读取字形尺寸
 * @param u8g2 字体实例指针
 * @param glyph_data 字形数据指针
 * @note 初始化解码器状态，从字形数据头部读取宽度和高度
 */
static void u8g2_font_setup_decode(u8g2_font_t* u8g2, const uint8_t* glyph_data) {
    u8g2_font_decode_t* decode = &(u8g2->font_decode);
    decode->decode_ptr = glyph_data;
    decode->decode_bit_pos = 0;

    /* 2015年11月8日，这已经在字形数据查找过程中完成了 */
    /*
    decode->decode_ptr += 1;
    decode->decode_ptr += 1;
    */

    /* 读取字形宽度和高度（使用变长位编码） */
    decode->glyph_width = u8g2_font_decode_get_unsigned_bits(decode, u8g2->font_info.bits_per_char_width);
    decode->glyph_height = u8g2_font_decode_get_unsigned_bits(decode, u8g2->font_info.bits_per_char_height);
}

/*============================================================================*/
/* 字形解码和绘制 */

/**
 * @brief 解码并绘制完整的字形
 * @param u8g2 字体实例指针
 * @param glyph_data 指向压缩字形数据的指针
 * @return 字形的前进宽度（delta x）
 * 
 * 功能描述：
 *   解码并绘制一个完整的字形。字形数据使用游程编码(RLE)压缩，
 *   包含宽度、高度、偏移和位图数据。
 * 
 * 参数说明：
 *   glyph_data: 字体中压缩字形数据的指针
 *   u8g2->font_decode.target_x: 目标X位置
 *   u8g2->font_decode.target_y: 目标Y位置
 *   u8g2->font_decode.is_transparent: 透明模式标志
 * 
 * 返回值：
 *   字形的前进宽度（delta x advance）
 * 
 * 调用关系：
 *   调用u8g2_font_decode_len()
 */
/* optimized */
static int8_t u8g2_font_decode_glyph(u8g2_font_t* u8g2, const uint8_t* glyph_data) {
    uint8_t a, b;
    int8_t x, y;
    int8_t d;
    int8_t h;
    u8g2_font_decode_t* decode = &(u8g2->font_decode);

    /* 设置解码器并获取字形尺寸 */
    u8g2_font_setup_decode(u8g2, glyph_data);
    h = u8g2->font_decode.glyph_height;

    /* 读取字形偏移和前进宽度 */
    x = u8g2_font_decode_get_signed_bits(decode, u8g2->font_info.bits_per_char_x);
    y = u8g2_font_decode_get_signed_bits(decode, u8g2->font_info.bits_per_char_y);
    d = u8g2_font_decode_get_signed_bits(decode, u8g2->font_info.bits_per_delta_x);

    if (decode->glyph_width > 0) {
        /* 应用字形偏移到目标位置 */
        decode->target_x = u8g2_add_vector_x(decode->target_x, x, -(h + y), decode->dir);
        decode->target_y = u8g2_add_vector_y(decode->target_y, x, -(h + y), decode->dir);
        // u8g2_add_vector(&(decode->target_x), &(decode->target_y), x, -(h+y), decode->dir);

        /* 重置局部x/y位置 */
        decode->x = 0;
        decode->y = 0;

        /* 解码字形位图数据 */
        /* 游程编码格式：[背景像素数][前景像素数][继续标志位]... */
        for (;;) {
            /* 读取背景游程长度 */
            a = u8g2_font_decode_get_unsigned_bits(decode, u8g2->font_info.bits_per_0);
            /* 读取前景游程长度 */
            b = u8g2_font_decode_get_unsigned_bits(decode, u8g2->font_info.bits_per_1);
            do {
                /* 绘制背景游程 */
                u8g2_font_decode_len(u8g2, a, 0);
                /* 绘制前景游程 */
                u8g2_font_decode_len(u8g2, b, 1);
            } while (u8g2_font_decode_get_unsigned_bits(decode, 1) != 0);  /* 继续标志位 */

            /* 检查是否已完成所有行的解码 */
            if (decode->y >= h) break;
        }
    }
    return d;  /* 返回前进宽度 */
}

/*============================================================================*/
/* 字形数据查找 */

/**
 * @brief 查找字形数据的起始位置
 * @param u8g2 字体实例指针
 * @param encoding 字符编码（ASCII或Unicode）
 * @return 字形数据地址，如果编码不存在于字体中则返回NULL
 * 
 * 功能描述：
 *   在字体数据中查找指定编码的字形数据起始位置。
 *   支持ASCII字符（0-255）和Unicode字符（>255）两种查找方式。
 * 
 * ASCII查找：
 *   - 使用start_pos_upper_A和start_pos_lower_a加速查找
 *   - 线性搜索字形表
 * 
 * Unicode查找：
 *   - 使用Unicode查找表进行快速定位
 *   - 查找表格式：[偏移量(2字节)][结束编码(2字节)]...
 */
const uint8_t* u8g2_font_get_glyph_data(u8g2_font_t* u8g2, uint16_t encoding) {
    const uint8_t* font = u8g2->font;
    font += 23;  /* 跳过字体信息头（23字节） */

    if (encoding <= 255) {
        /* ASCII字符查找（0-255） */
        /* 使用查找表加速：根据字符范围跳转到相应位置 */
        if (encoding >= 'a') {
            font += u8g2->font_info.start_pos_lower_a;
        } else if (encoding >= 'A') {
            font += u8g2->font_info.start_pos_upper_A;
        }

        /* 线性搜索字形表 */
        /* 表项格式：[编码(1字节)][大小(1字节)][字形数据...] */
        for (;;) {
            if (u8x8_pgm_read(font + 1) == 0) break;  /* 大小为0表示表结束 */
            if (u8x8_pgm_read(font) == encoding) {
                return font + 2; /* 跳过编码和字形大小，返回字形数据地址 */
            }
            font += u8x8_pgm_read(font + 1);  /* 移动到下一个表项 */
        }
    } else {
        /* Unicode字符查找（>255） */
        uint16_t e;
        const uint8_t* unicode_lookup_table;
        /* 支持新的Unicode查找表 */

        font += u8g2->font_info.start_pos_unicode;
        unicode_lookup_table = font;

        /* u8g2问题596：在Unicode查找表中搜索字形起始位置 */
        /* 查找表格式：[偏移量(2字节)][结束编码(2字节)]... */
        do {
            font += u8g2_font_get_word(unicode_lookup_table, 0);  /* 累加偏移量 */
            e = u8g2_font_get_word(unicode_lookup_table, 2);      /* 读取结束编码 */
            unicode_lookup_table += 4;
        } while (e < encoding);

        /* 变量"font"现已根据查找表更新 */

        /* 在Unicode字形表中搜索 */
        /* 表项格式：[编码(2字节)][大小(1字节)][字形数据...] */
        for (;;) {
            e = u8x8_pgm_read(font);
            e <<= 8;
            e |= u8x8_pgm_read(font + 1);
            if (e == 0) break;  /* 编码为0表示表结束 */
            if (e == encoding) {
                return font + 3; /* 跳过编码(2字节)和字形大小(1字节) */
            }
            font += u8x8_pgm_read(font + 2);  /* 移动到下一个表项 */
        }
    }
    return NULL;  /* 未找到字形 */
}

/*============================================================================*/
/* 内部字形绘制函数 */

/**
 * @brief 内部函数：在指定位置绘制字形
 * @param u8g2 字体实例指针
 * @param x 目标X坐标
 * @param y 目标Y坐标
 * @param encoding 字符编码
 * @return 字形的前进宽度
 */
static int16_t u8g2_font_draw_glyph(u8g2_font_t* u8g2, int16_t x, int16_t y, uint16_t encoding) {
    int16_t dx = 0;
    u8g2->font_decode.target_x = x;
    u8g2->font_decode.target_y = y;
    // u8g2->font_decode.is_transparent = is_transparent; this is already set
    // u8g2->font_decode.dir = dir;
    const uint8_t* glyph_data = u8g2_font_get_glyph_data(u8g2, encoding);
    if (glyph_data != NULL) {
        dx = u8g2_font_decode_glyph(u8g2, glyph_data);
    }
    return dx;
}

//========================================================

/**
 * @brief 检查字体是否包含指定编码的字形
 * @param u8g2 字体实例指针
 * @param requested_encoding 请求的字符编码
 * @return 1表示字形存在，0表示不存在
 */
uint8_t u8g2_IsGlyph(u8g2_font_t* u8g2, uint16_t requested_encoding) {
    /* updated to new code */
    if (u8g2_font_get_glyph_data(u8g2, requested_encoding) != NULL) return 1;
    return 0;
}

/**
 * @brief 获取字形宽度
 * @param u8g2 字体实例指针
 * @param requested_encoding 请求的字符编码
 * @return 字形的前进宽度（delta x），如果字形不存在则返回0
 * @note 副作用：更新u8g2->font_decode和u8g2->glyph_x_offset
 *       实际上u8g2_GetGlyphWidth返回的是字形delta x，
 *       字形宽度本身作为副作用被设置
 */
int8_t u8g2_GetGlyphWidth(u8g2_font_t* u8g2, uint16_t requested_encoding) {
    const uint8_t* glyph_data = u8g2_font_get_glyph_data(u8g2, requested_encoding);
    if (glyph_data == NULL) return 0;

    u8g2_font_setup_decode(u8g2, glyph_data);
    /* 读取X偏移并保存到glyph_x_offset */
    u8g2->glyph_x_offset = u8g2_font_decode_get_signed_bits(&(u8g2->font_decode), u8g2->font_info.bits_per_char_x);
    /* 跳过Y偏移 */
    u8g2_font_decode_get_signed_bits(&(u8g2->font_decode), u8g2->font_info.bits_per_char_y);

    /* 字形宽度存储在：u8g2->font_decode.glyph_width */
    /* 返回前进宽度 */
    return u8g2_font_decode_get_signed_bits(&(u8g2->font_decode), u8g2->font_info.bits_per_delta_x);
}

/**
 * @brief 设置字体绘制模式
 * @param u8g2 字体实例指针
 * @param is_transparent 模式标志：
 *                       0 = 不透明模式（mode 0/solid），绘制背景色
 *                       1 = 透明模式（mode 1/transparent），不绘制背景
 * @note 默认模式为0（不透明，字符背景色会被覆盖）
 */
void u8g2_SetFontMode(u8g2_font_t* u8g2, uint8_t is_transparent) {
    u8g2->font_decode.is_transparent = is_transparent;  // new font procedures
}

/**
 * @brief 设置字符串绘制方向
 * @param u8g2 字体实例指针
 * @param dir 方向值：
 *            0 = 0度（从左到右）
 *            1 = 90度（从上到下）
 *            2 = 180度（从右到左）
 *            3 = 270度（从下到上）
 */
void u8g2_SetFontDirection(u8g2_font_t* u8g2, uint8_t dir) { u8g2->font_decode.dir = dir; }

/**
 * @brief 绘制单个字形
 * @param u8g2 字体实例指针
 * @param x 目标X坐标
 * @param y 目标Y坐标（基线位置）
 * @param encoding 字符编码（支持0-65535的Unicode基本多语言平面）
 * @return 字形的前进宽度
 * 
 * 功能描述：
 *   在指定像素位置绘制单个字符。
 *   U8g2支持Unicode字符范围的低16位（基本多语言平面）。
 *   编码可以是0到65535之间的任意值。
 *   只有当编码存在于当前字体中时，字形才能被绘制。
 */
int16_t u8g2_DrawGlyph(u8g2_font_t* u8g2, int16_t x, int16_t y, uint16_t encoding) {
    return u8g2_font_draw_glyph(u8g2, x, y, encoding);
}

/**
 * @brief 绘制字符串
 * @param u8g2 字体实例指针
 * @param x 起始X坐标
 * @param y 起始Y坐标（基线位置）
 * @param s 要绘制的字符串（C字符串，以'\0'结尾）
 * @return 字符串的总前进宽度
 * 
 * 功能描述：
 *   第一个字符放置在位置(x, y)。
 *   绘制字符串前需要使用setFont设置字体。
 *   对于编码127-255的字符，可使用转义序列：
 *   - "\xab"：十六进制值ab
 *   - "\xyz"：八进制值xyz
 *   此函数无法绘制编码>=256的字形，请使用drawUTF8或drawGlyph。
 */
int16_t u8g2_DrawStr(u8g2_font_t* u8g2, int16_t x, int16_t y, const char* s) {
    int16_t sum, delta;
    sum = 0;

    while (*s != '\0') {
        delta = u8g2_DrawGlyph(u8g2, x, y, *s);
        /* 根据方向更新坐标 */
        switch (u8g2->font_decode.dir) {
            case 0:   /* 0度：从左到右 */
                x += delta;
                break;
            case 1:   /* 90度：从上到下 */
                y += delta;
                break;
            case 2:   /* 180度：从右到左 */
                x -= delta;
                break;
            case 3:   /* 270度：从下到上 */
                y -= delta;
                break;
        }
        sum += delta;
        s++;
    }
    return sum;
}

/**
 * @brief 设置当前字体
 * @param u8g2 字体实例指针
 * @param font 字体数据指针（指向U8g2字体数组）
 * @note 如果字体与当前字体不同，会重新解析字体信息并重置透明模式
 */
void u8g2_SetFont(u8g2_font_t* u8g2, const uint8_t* font) {
    if (u8g2->font != font) {
        u8g2->font = font;
        u8g2->font_decode.is_transparent = 0;  /* 默认不透明模式 */

        u8g2_read_font_info(&(u8g2->font_info), font);  /* 解析字体信息头 */
    }
}

/**
 * @brief 设置前景色（文字颜色）
 * @param u8g2 字体实例指针
 * @param fg 前景色值（格式取决于显示驱动，如RGB565）
 */
void u8g2_SetForegroundColor(u8g2_font_t* u8g2, uint16_t fg) { u8g2->font_decode.fg_color = fg; }

/**
 * @brief 设置背景色（用于非透明模式）
 * @param u8g2 字体实例指针
 * @param bg 背景色值（格式取决于显示驱动，如RGB565）
 */
void u8g2_SetBackgroundColor(u8g2_font_t* u8g2, uint16_t bg) { u8g2->font_decode.bg_color = bg; }
