/**
 * @file    emulator.c
 * @brief   Windows 平台 GUI 模拟器
 * @details 本文件实现了一个简单的 Windows GUI 应用程序，用于在 PC 上模拟电子墨水屏（EPD）
 *          的显示效果。通过调用与嵌入式端相同的 GUI 绘制逻辑（DrawGUI），将 EPD 显示缓冲区
 *          的内容渲染为 Windows 位图，方便在开发阶段预览和调试界面布局，无需实际硬件。
 *
 * 键盘操作说明：
 *   - 空格键    ：切换显示模式（日历模式 <-> 时钟模式）
 *   - R 键      ：切换颜色模式（黑白模式 <-> 黑白红三色模式）
 *   - W 键      ：切换星期起始日（在周日~周六之间循环切换）
 *   - 上/下方向键：调整月份（上=下一个月，下=上一个月）
 *   - 左/右方向键：调整日期（右=下一天，左=前一天）
 */

#include <stdint.h>   /* 标准整数类型定义，提供 uint8_t、uint16_t、uint32_t 等 */
#include <string.h>   /* 字符串操作函数，提供 memset、memcpy 等 */
#include <time.h>     /* 时间日期函数，提供 time、localtime、mktime 等 */
#include <wchar.h>    /* 宽字符处理函数，提供 wcslen 等 */
#include <windows.h>  /* Windows API 头文件，提供窗口、绘图、消息等接口 */

#include "GUI.h"      /* GUI 绘制模块头文件，提供 DrawGUI、display_mode_t、gui_data_t 等定义 */

/* ======================== 宏定义 ======================== */

#define BITMAP_WIDTH  400   /* EPD 位图宽度（像素），与实际电子墨水屏分辨率一致 */
#define BITMAP_HEIGHT 300   /* EPD 位图高度（像素），与实际电子墨水屏分辨率一致 */
#define WINDOW_WIDTH  450   /* 模拟器窗口宽度（像素），略大于位图以留出边距 */
#define WINDOW_HEIGHT 380   /* 模拟器窗口高度（像素），略大于位图以留出边距 */

/* ======================== 全局变量 ======================== */

HINSTANCE g_hInstance;                                      /* 程序实例句柄，由 WinMain 传入，用于注册窗口类和创建窗口 */
HWND g_hwnd;                                                /* 主窗口句柄，窗口创建后保存，供各函数使用 */
HDC g_paintHDC = NULL;                                     /* 绘图设备上下文（HDC），在 WM_PAINT 时设置，供 DrawBitmap 回调使用 */
display_mode_t g_display_mode = MODE_CALENDAR;              /* 当前显示模式，默认为日历模式（MODE_CALENDAR），可切换为时钟模式（MODE_CLOCK） */
BOOL g_bwr_mode = TRUE;                                    /* 三色模式标志，TRUE=黑白红三色模式，FALSE=黑白两色模式 */
uint8_t g_week_start = 0;                                  /* 星期起始日，0=周日，1=周一，...，6=周六，默认从周日开始 */
time_t g_display_time;                                     /* 当前显示的时间戳（秒），用于控制日历/时钟显示的日期时间 */
struct tm g_tm_time;                                       /* 时间结构体，用于方向键调整日期时的中间计算 */

/**
 * @brief  DrawBitmap 回调函数 —— 将 EPD 显示缓冲区渲染为 Windows 位图
 * @param  user_data  用户数据指针（本实现中未使用，传 NULL）
 * @param  black      黑色通道缓冲区，每个 bit 代表一个像素（1=黑，0=白）
 * @param  color      红色通道缓冲区，每个 bit 代表一个像素（1=红，0=无色），可为 NULL（两色模式）
 * @param  x          绘制区域在位图中的起始 X 坐标（像素）
 * @param  y          绘制区域在位图中的起始 Y 坐标（像素）
 * @param  w          绘制区域的宽度（像素）
 * @param  h          绘制区域的高度（像素）
 * @note   本函数由 DrawGUI 内部回调，将嵌入式端的 EPD 帧缓冲数据转换为 Windows 可显示的位图。
 *         使用 4-bit 位图格式（每像素 4 位），调色板仅使用 3 种颜色：
 *           - 颜色 0：白色 (RGB 255,255,255)
 *           - 颜色 1：黑色 (RGB 0,0,0)
 *           - 颜色 2：红色 (RGB 255,0,0)
 *         EPD 缓冲区中每个字节表示 8 个像素（MSB 在左），需要逐位解析后打包为 4-bit 格式。
 */
void DrawBitmap(void* user_data, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    /* 获取全局绘图 HDC，若无效则直接返回 */
    HDC hdc = g_paintHDC;
    if (!hdc) return;

    RECT clientRect;
    int scale = 1;  /* 缩放比例，1 表示原始尺寸 */

    /* 获取窗口客户区大小，用于计算位图居中位置 */
    GetClientRect(g_hwnd, &clientRect);

    /* 计算位图在窗口中的居中绘制坐标 */
    int drawX = (clientRect.right - BITMAP_WIDTH * scale) / 2;
    int drawY = (clientRect.bottom - BITMAP_HEIGHT * scale) / 2;

    /* ---- 配置 4-bit 位图信息头 ---- */
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(BITMAPINFO));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);  /* 位图信息头大小 */
    bmi.bmiHeader.biWidth = w;                         /* 位图宽度 */
    bmi.bmiHeader.biHeight = -h;                       /* 负值表示从上到下（top-down）的位图 */
    bmi.bmiHeader.biPlanes = 1;                        /* 颜色平面数 */
    bmi.bmiHeader.biBitCount = 4;                      /* 每像素 4 位（16 色） */
    bmi.bmiHeader.biCompression = BI_RGB;              /* 无压缩 */
    bmi.bmiHeader.biClrUsed = 16;                      /* 调色板使用 16 种颜色（2^4） */

    /* 初始化调色板：先将全部 16 个颜色设为白色 */
    for (int i = 0; i < 16; i++) {
        bmi.bmiColors[i].rgbBlue = 255;
        bmi.bmiColors[i].rgbGreen = 255;
        bmi.bmiColors[i].rgbRed = 255;
        bmi.bmiColors[i].rgbReserved = 0;
    }

    /* 设置实际使用的 3 种颜色 */
    /* 颜色 0：白色 */
    bmi.bmiColors[0].rgbBlue = 255;
    bmi.bmiColors[0].rgbGreen = 255;
    bmi.bmiColors[0].rgbRed = 255;

    /* 颜色 1：黑色 */
    bmi.bmiColors[1].rgbBlue = 0;
    bmi.bmiColors[1].rgbGreen = 0;
    bmi.bmiColors[1].rgbRed = 0;

    /* 颜色 2：红色 */
    bmi.bmiColors[2].rgbBlue = 0;
    bmi.bmiColors[2].rgbGreen = 0;
    bmi.bmiColors[2].rgbRed = 255;

    /* ---- 创建 4-bit 位图数据缓冲区 ---- */
    /* 每个字节包含 2 个像素（每个像素 4 位） */
    int pixelsPerByte = 2;
    /* 计算每行所需的字节数，并按 DWORD（4 字节）边界对齐 */
    int bytesPerRow = ((w + pixelsPerByte - 1) / pixelsPerByte);
    bytesPerRow = ((bytesPerRow + 3) / 4) * 4;  /* 向上对齐到 4 字节边界 */
    int totalSize = bytesPerRow * h;             /* 位图数据总大小 */

    /* 分配位图缓冲区并初始化为全 0（对应白色） */
    uint8_t* bitmap4bit = (uint8_t*)malloc(totalSize);
    if (!bitmap4bit) {
        return;
    }
    memset(bitmap4bit, 0, totalSize);  /* 全部初始化为白色（像素值 0） */

    /* ---- 逐像素转换 EPD 缓冲区数据到 4-bit 位图 ---- */
    int ePaperBytesPerRow = (w + 7) / 8;  /* EPD 缓冲区每行字节数（每字节 8 像素） */
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            /* 计算当前像素在 EPD 缓冲区中的字节位置和位偏移 */
            int bytePos = row * ePaperBytesPerRow + col / 8;
            int bitPos = 7 - (col % 8);  /* MSB 在左，所以从高位开始取 */

            /* 从黑色通道读取像素值（EPD 中 1=有墨水，需取反：1->黑，0->白） */
            int blackBit = !((black[bytePos] >> bitPos) & 0x01);
            /* 从红色通道读取像素值（若 color 为 NULL 则为两色模式，无红色） */
            int colorBit = color ? !((color[bytePos] >> bitPos) & 0x01) : 0;

            /* 确定最终像素颜色值：红色优先级最高，其次黑色，最后白色 */
            uint8_t pixelValue = colorBit ? 2 : (blackBit ? 1 : 0);

            /* 将像素值打包到 4-bit 位图缓冲区中 */
            /* 每个字节存储 2 个像素：高 4 位 = 第一个像素，低 4 位 = 第二个像素 */
            int bitmap4bitBytePos = row * bytesPerRow + col / pixelsPerByte;
            int isHighNibble = (col % pixelsPerByte) == 0;  /* 偶数列像素放入高 4 位 */

            if (isHighNibble) {
                /* 清除高 4 位并设置新的像素值 */
                bitmap4bit[bitmap4bitBytePos] &= 0x0F;
                bitmap4bit[bitmap4bitBytePos] |= (pixelValue << 4);
            } else {
                /* 清除低 4 位并设置新的像素值 */
                bitmap4bit[bitmap4bitBytePos] &= 0xF0;
                bitmap4bit[bitmap4bitBytePos] |= pixelValue;
            }
        }
    }

    /* ---- 将 4-bit 位图绘制到窗口设备上下文 ---- */
    StretchDIBits(hdc, drawX + x * scale, drawY + y * scale, w * scale, h * scale, 0, 0, w, h, bitmap4bit, &bmi,
                  DIB_RGB_COLORS, SRCCOPY);

    /* 释放位图缓冲区 */
    free(bitmap4bit);
}

/**
 * @brief  窗口过程函数（Window Procedure）
 * @param  hwnd    窗口句柄
 * @param  message 消息类型
 * @param  wParam  消息附加参数（含义取决于消息类型）
 * @param  lParam  消息附加参数（含义取决于消息类型）
 * @return 消息处理结果
 * @note   处理以下 Windows 消息：
 *         - WM_CREATE  ：窗口创建时初始化显示时间和定时器
 *         - WM_TIMER   ：定时器触发，时钟模式下每秒更新时间
 *         - WM_PAINT   ：窗口重绘，清除背景、绘制边框、模式文字、帮助文字，并调用 DrawGUI 渲染界面
 *         - WM_KEYDOWN ：键盘按键处理（空格/R/W/方向键）
 *         - WM_DESTROY ：窗口销毁时清理定时器并退出消息循环
 */
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            /* 窗口创建时的初始化 */
            /* 设置显示时间为当前时间（UTC+8 东八区，即北京时间） */
            g_display_time = time(NULL) + 8 * 3600;
            /* 设置定时器，每 1000 毫秒（1 秒）触发一次，用于时钟模式的时间更新 */
            SetTimer(hwnd, 1, 1000, NULL);
            return 0;

        case WM_TIMER:
            /* 定时器消息处理 */
            if (g_display_mode == MODE_CLOCK) {
                /* 时钟模式下，每秒更新显示时间 */
                g_display_time = time(NULL) + 8 * 3600;
                /* 每分钟整（秒数为 0）时触发窗口重绘，减少不必要的刷新 */
                if (g_display_time % 60 == 0) {
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;

        case WM_PAINT: {
            /* 窗口重绘消息处理 */
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            /* 将绘图 HDC 保存到全局变量，供 DrawBitmap 回调使用 */
            g_paintHDC = hdc;

            /* 获取客户区大小 */
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);

            /* 用浅灰色填充整个客户区背景 */
            HBRUSH bgBrush = CreateSolidBrush(RGB(240, 240, 240));
            FillRect(hdc, &clientRect, bgBrush);
            DeleteObject(bgBrush);

            /* 计算位图绘制位置（居中） */
            int scale = 1;
            int drawX = (clientRect.right - BITMAP_WIDTH * scale) / 2;
            int drawY = (clientRect.bottom - BITMAP_HEIGHT * scale) / 2;

            /* 在位图区域周围绘制蓝色虚线边框，模拟电子墨水屏的显示边界 */
            HPEN borderPen = CreatePen(PS_DOT, 1, RGB(0, 0, 255));
            HPEN oldPen = SelectObject(hdc, borderPen);
            HBRUSH oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));  /* 不填充 */

            Rectangle(hdc, drawX - 1, drawY - 1, drawX + BITMAP_WIDTH * scale + 1, drawY + BITMAP_HEIGHT * scale + 1);

            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(borderPen);

            /* 在位图上方显示当前模式名称（"时钟模式" 或 "日历模式"） */
            const wchar_t* modeText = (g_display_mode == MODE_CLOCK) ? L"时钟模式" : L"日历模式";
            int modeTextY = drawY - 20;  /* 位于位图上方 20 像素处 */
            SetTextColor(hdc, RGB(50, 50, 50));
            SetBkMode(hdc, TRANSPARENT);  /* 文字背景透明 */

            /* 创建模式文字的字体（16 号粗体 Arial） */
            HFONT modeFont = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
            HFONT oldFont = SelectObject(hdc, modeFont);

            /* 计算模式文字宽度，使其在位图区域上方水平居中 */
            SIZE modeTextSize;
            GetTextExtentPoint32W(hdc, modeText, wcslen(modeText), &modeTextSize);
            int modeCenteredX = drawX + (BITMAP_WIDTH - modeTextSize.cx) / 2;

            TextOutW(hdc, modeCenteredX, modeTextY, modeText, wcslen(modeText));

            /* 在位图下方显示键盘操作帮助文字 */
            const wchar_t helpText[] = L"空格 - 切换模式 | R - 切换颜色 | W - 星期起点 | 方向键 - 调整日期/月份";
            int helpTextY = drawY + BITMAP_HEIGHT * scale + 5;  /* 位于位图下方 5 像素处 */
            SetTextColor(hdc, RGB(80, 80, 80));

            /* 创建帮助文字的字体（14 号常规 Arial） */
            HFONT helpFont =
                CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
            SelectObject(hdc, modeFont);
            DeleteObject(modeFont);
            SelectObject(hdc, helpFont);

            /* 计算帮助文字宽度，使其在位图区域下方水平居中 */
            SIZE textSize;
            GetTextExtentPoint32W(hdc, helpText, wcslen(helpText), &textSize);
            int centeredX = drawX + (BITMAP_WIDTH - textSize.cx) / 2;

            TextOutW(hdc, centeredX, helpTextY, helpText, wcslen(helpText));

            /* 恢复旧字体并释放帮助字体 */
            SelectObject(hdc, oldFont);
            DeleteObject(helpFont);

            /* 构造 GUI 绘制数据结构，传入当前显示参数 */
            gui_data_t data = {
                .mode = g_display_mode,       /* 显示模式：日历或时钟 */
                .color = g_bwr_mode ? 2 : 1,  /* 颜色模式：2=三色（黑白红），1=两色（黑白） */
                .width = BITMAP_WIDTH,         /* 位图宽度 */
                .height = BITMAP_HEIGHT,       /* 位图高度 */
                .timestamp = g_display_time,   /* 显示的时间戳 */
                .week_start = g_week_start,    /* 星期起始日 */
                .temperature = 25,             /* 模拟温度值（25 摄氏度） */
                .voltage = 2920,               /* 模拟电池电压值（2920mV） */
                .ssid = "NRF_EPD_84AC",        /* 模拟 WiFi 名称 */
            };

            /* 调用 DrawGUI 渲染界面，DrawBitmap 作为回调函数将缓冲区内容绘制到窗口 */
            DrawGUI(&data, DrawBitmap, NULL);

            /* 清除全局绘图 HDC，防止悬挂引用 */
            g_paintHDC = NULL;

            /* 结束绘制 */
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_KEYDOWN:
            /* 键盘按键消息处理 */

            /* 空格键：切换显示模式（日历 <-> 时钟） */
            if (wParam == VK_SPACE) {
                if (g_display_mode == MODE_CLOCK)
                    g_display_mode = MODE_CALENDAR;
                else
                    g_display_mode = MODE_CLOCK;

                InvalidateRect(hwnd, NULL, TRUE);  /* 触发窗口重绘 */
            }
            /* R 键：切换颜色模式（黑白红三色 <-> 黑白两色） */
            else if (wParam == 'R') {
                g_bwr_mode = !g_bwr_mode;
                InvalidateRect(hwnd, NULL, TRUE);  /* 触发窗口重绘 */
            }
            /* W 键：切换星期起始日（0=周日，1=周一，...，6=周六，循环切换） */
            else if (wParam == 'W') {
                g_week_start++;
                if (g_week_start > 6) g_week_start = 0;  /* 超过周六则回到周日 */
                InvalidateRect(hwnd, NULL, TRUE);  /* 触发窗口重绘 */
            }
            /* 方向键：调整日期和月份 */
            else if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_LEFT || wParam == VK_RIGHT) {
                /* 将当前显示时间转换为 tm 结构体，便于修改年月日 */
                g_tm_time = *localtime(&g_display_time);

                /* 上方向键：月份 +1（下一个月） */
                if (wParam == VK_UP) {
                    g_tm_time.tm_mon++;
                    if (g_tm_time.tm_mon > 11) {  /* 超过 12 月则回到 1 月，年份 +1 */
                        g_tm_time.tm_mon = 0;
                        g_tm_time.tm_year++;
                    }
                }
                /* 下方向键：月份 -1（上一个月） */
                else if (wParam == VK_DOWN) {
                    g_tm_time.tm_mon--;
                    if (g_tm_time.tm_mon < 0) {  /* 小于 1 月则回到 12 月，年份 -1 */
                        g_tm_time.tm_mon = 11;
                        g_tm_time.tm_year--;
                    }
                }
                /* 右方向键：日期 +1（下一天） */
                else if (wParam == VK_RIGHT) {
                    g_tm_time.tm_mday++;
                }
                /* 左方向键：日期 -1（前一天） */
                else if (wParam == VK_LEFT) {
                    g_tm_time.tm_mday--;
                }

                /* 将修改后的 tm 结构体转换回 time_t 时间戳 */
                g_display_time = mktime(&g_tm_time);

                /* 触发窗口重绘以显示更新后的日期 */
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;

        case WM_DESTROY:
            /* 窗口销毁消息处理 */
            KillTimer(hwnd, 1);       /* 销毁定时器 */
            PostQuitMessage(0);       /* 发送退出消息，结束消息循环 */
            return 0;

        default:
            /* 其他未处理的消息交给系统默认处理 */
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

/**
 * @brief  程序入口函数（WinMain）
 * @param  hInstance      当前程序实例句柄
 * @param  hPrevInstance  前一个实例句柄（Win32 中始终为 NULL）
 * @param  lpCmdLine      命令行参数字符串
 * @param  nCmdShow       窗口初始显示方式（最小化、最大化等）
 * @return 程序退出码
 * @note   执行流程：
 *         1. 保存程序实例句柄到全局变量
 *         2. 注册窗口类（类名 "Emurator"）
 *         3. 创建主窗口（标题 "模拟器"，带标题栏和系统菜单，可最小化）
 *         4. 显示并更新窗口
 *         5. 进入消息循环，分发消息到窗口过程函数，直到收到 WM_QUIT 退出
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    /* 保存程序实例句柄 */
    g_hInstance = hInstance;

    /* ---- 注册窗口类 ---- */
    WNDCLASSW wc = {0};
    wc.style = CS_HREDRAW | CS_VREDRAW;     /* 窗口水平或垂直尺寸变化时重绘 */
    wc.lpfnWndProc = WndProc;                /* 窗口过程函数 */
    wc.hInstance = hInstance;                /* 窗口类所属的程序实例 */
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);/* 使用标准箭头光标 */
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);  /* 默认窗口背景色 */
    wc.lpszClassName = L"Emurator";          /* 窗口类名称 */

    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"Window Registration Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    /* ---- 创建主窗口 ---- */
    /* 窗口样式：WS_POPUPWINDOW（弹出窗口）| WS_CAPTION（标题栏）| WS_SYSMENU（系统菜单）| WS_MINIMIZEBOX（最小化按钮） */
    g_hwnd = CreateWindowW(L"Emurator", L"模拟器", WS_POPUPWINDOW | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, hInstance, NULL);

    if (!g_hwnd) {
        MessageBoxW(NULL, L"Window Creation Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    /* ---- 显示窗口 ---- */
    ShowWindow(g_hwnd, nCmdShow);   /* 按系统指定的方式显示窗口 */
    UpdateWindow(g_hwnd);           /* 立即发送 WM_PAINT 消息，绘制窗口初始内容 */

    /* ---- 主消息循环 ---- */
    /* 从消息队列中获取消息并分发到对应的窗口过程函数，直到收到 WM_QUIT 退出 */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);     /* 将虚拟键消息转换为字符消息 */
        DispatchMessage(&msg);      /* 将消息分发到窗口过程函数 */
    }

    return (int)msg.wParam;  /* 返回 PostQuitMessage 中指定的退出码 */
}
