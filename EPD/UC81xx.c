/**
 * @file UC81xx.c
 * @brief UC81xx系列电子墨水屏驱动实现（UltraChip / 京东方 JD系列）
 *
 * 本文件实现了多款UC81xx系列驱动IC的完整驱动函数，涵盖：
 *   - UC8159：支持黑白红三色，使用4bit像素格式（每像素4位）
 *   - UC8176：支持黑白和黑白红，使用1bpp标准位图格式，支持局部刷新
 *   - UC8179：支持黑白和黑白红，使用1bpp标准位图格式
 *   - JD79668：京东方四色屏（黑白红黄），使用2bpp像素格式
 *   - JD79665：京东方四色屏（黑白红黄），使用2bpp像素格式
 *
 * 支持的型号实例（共9个）：
 *   - UC8176 400x300 黑白/黑白红（4.2英寸）
 *   - UC8159 640x384 黑白/黑白红（7.5英寸低分辨率版）
 *   - UC8179 800x480 黑白/黑白红（7.5英寸）
 *   - JD79668 400x300 黑白红黄四色（4.2英寸）
 *   - JD79665 800x480 / 648x480 黑白红黄四色（7.5英寸 / 5.83英寸）
 *
 * 驱动特性：
 *   - 忙信号为低电平有效（与SSD16xx系列的高电平有效相反）
 *   - 内置温度传感器，用于波形温度补偿
 *   - 支持局部窗口刷新（UC8176支持局部刷新模式）
 *   - 不同IC的像素格式不同（1bpp / 2bpp / 4bit），通过IC类型自动分发
 */

#include "EPD_driver.h"

/**
 * @brief 读取UC81xx忙信号状态
 * @param epd 电子墨水屏型号指针
 * @return true=忙（低电平），false=空闲（高电平）
 * @note UC81xx系列的忙信号为低电平有效，与SSD16xx系列（高电平有效）相反。
 *       此处将EPD_ReadBusy()的返回值取反，统一忙信号的逻辑含义。
 */
bool UC81xx_ReadBusy(epd_model_t* epd) { return EPD_ReadBusy() == false; }

/**
 * @brief 等待UC81xx忙信号清除
 * @param timeout 超时时间（毫秒），超时后强制返回
 * @note 忙信号为低电平有效，等待期间持续检测直到信号变高或超时。
 *       第一个参数false表示忙信号为低电平有效。
 */
static void UC81xx_WaitBusy(uint16_t timeout) { EPD_WaitBusy(false, timeout); }

/**
 * @brief 开启UC81xx电源
 * @param epd 电子墨水屏型号指针
 *
 * 发送电源开启命令（PON），然后等待芯片内部电源稳定（最长200ms）。
 * 电源开启后才能进行后续的显示操作。
 */
static void UC81xx_PowerOn(epd_model_t* epd) {
    EPD_WriteCmd(UC81xx_PON);
    UC81xx_WaitBusy(200);
}

/**
 * @brief 关闭UC81xx电源
 * @param epd 电子墨水屏型号指针
 *
 * 发送电源关闭命令（POF），然后等待电源完全关闭（最长200ms）。
 * 对于四色屏（COLOR_BWRY模式），需要额外发送一个0x00参数字节，
 * 这是JD系列四色屏的特殊要求。
 */
static void UC81xx_PowerOff(epd_model_t* epd) {
    EPD_WriteCmd(UC81xx_POF);
    if (epd->color == COLOR_BWRY) EPD_WriteByte(0x00);
    UC81xx_WaitBusy(200);
}

/**
 * @brief 读取UC81xx驱动芯片内部温度
 * @param epd 电子墨水屏型号指针
 * @return 温度值（有符号8位，单位：摄氏度）
 *
 * 工作流程：
 * 1. 发送温度传感器命令（TSC）激活温度读取
 * 2. 等待忙信号清除（最长100ms，温度转换需要时间）
 * 3. 读取一个字节的有符号温度值
 *
 * @note 温度值用于LUT波形查找表的温度补偿
 */
int8_t UC81xx_ReadTemp(epd_model_t* epd) {
    EPD_WriteCmd(UC81xx_TSC);
    UC81xx_WaitBusy(100);
    return (int8_t)EPD_ReadByte();
}

/**
 * @brief 设置UC81xx显示窗口区域
 * @param epd 电子墨水屏型号指针
 * @param x  窗口起始X坐标（像素）
 * @param y  窗口起始Y坐标（像素）
 * @param w  窗口宽度（像素）
 * @param h  窗口高度（像素）
 *
 * 不同IC系列的窗口设置方式不同：
 *
 * JD79668/JD79665（京东方系列）：
 *   - 使用0x83命令（部分窗口命令）
 *   - X/Y坐标直接以像素为单位，拆分为高8位和低8位
 *   - 最后一个参数0x01表示启用部分窗口模式
 *
 * UC8176/UC8179/UC8159（UltraChip标准系列，默认）：
 *   - 使用UC81xx_PTL命令（部分窗口命令）
 *   - X坐标需要对齐到8像素（字节）边界：
 *     * 起始X：x &= 0xFFF8（低3位清零，向下对齐到字节）
 *     * 结束X：(x+w-1) | 0x0007（低3位置1，向上对齐到字节末尾）
 *   - Y坐标直接使用像素值
 *   - 最后一个参数0x00表示标准模式
 */
static void UC81xx_SetWindow(epd_model_t* epd, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    switch (epd->ic) {
        case DRV_IC_JD79668:
        case DRV_IC_JD79665:
            /* JD系列：使用0x83命令，坐标以像素为单位，无需字节对齐 */
            EPD_Write(0x83,  // partial window
                      x / 256, x % 256, (x + w - 1) / 256, (x + w - 1) % 256, y / 256, y % 256, (y + h - 1) / 256,
                      (y + h - 1) % 256, 0x01);
            break;
        default: {
            /* UC标准系列：使用PTL命令，X坐标需字节对齐 */
            uint16_t xe = (x + w - 1) | 0x0007;  // byte boundary inclusive (last byte)
            uint16_t ye = y + h - 1;
            x &= 0xFFF8;           // byte boundary
            EPD_Write(UC81xx_PTL,  // partial window
                      x / 256, x % 256, xe / 256, xe % 256, y / 256, y % 256, ye / 256, ye % 256, 0x00);
        } break;
    }
}

/**
 * @brief UC81xx刷新显示
 * @param epd 电子墨水屏型号指针
 *
 * 刷新流程：
 * 1. 设置全屏显示窗口
 * 2. 发送显示刷新命令（DRF - Display Refresh）
 * 3. 对于四色屏（COLOR_BWRY），需要额外发送0x00参数
 * 4. 延时100ms后等待忙信号清除（使用最大超时时间）
 *
 * @note 刷新过程中屏幕会闪烁，这是电子墨水屏的正常现象
 */
void UC81xx_Refresh(epd_model_t* epd) {
    EPD_DEBUG("refresh begin");

    UC81xx_SetWindow(epd, 0, 0, epd->width, epd->height);

    EPD_WriteCmd(UC81xx_DRF);
    if (epd->color == COLOR_BWRY) EPD_WriteByte(0x00);
    delay(100);
    UC81xx_WaitBusy(UINT16_MAX);

    EPD_DEBUG("refresh end");
}

/**
 * @brief UC81xx初始化
 * @param epd 电子墨水屏型号指针
 *
 * 初始化流程：
 * 1. 硬件复位（拉低复位引脚50ms后释放）
 * 2. 根据不同的驱动IC设置对应的寄存器参数：
 *
 *    UC8159（黑白红三色，4bit像素）：
 *      - PWR: 电源设置 0x37, 0x00
 *      - PSR: 面板设置 0xCF, 0x08（LUT从寄存器加载）
 *      - PLL: PLL频率设置 0x3A
 *      - VDCS: VCOM和DC电压设置 0x28
 *      - BTST: Booster软启动 0xC7, 0xCC, 0x15
 *      - CDI: 颜色深度 0x77（4bit模式）
 *      - TCON: 时序控制 0x22
 *      - 0x65: Flash控制 0x00
 *      - 0xE5: Flash模式 0x03
 *      - TRES: 分辨率设置（从epd模型获取）
 *
 *    JD79668（四色屏，2bpp）：
 *      - 0x4D: 面板设置 0x78
 *      - PSR: 面板设置 0x0F, 0x29
 *      - BTST: Booster软启动（7个参数）
 *      - PLL: PLL频率 0x08
 *      - CDI: 颜色深度 0x37
 *      - TRES: 分辨率设置
 *      - 0xAE: TCON设置 0xCF
 *      - 0xB0: VCOM设置 0x13
 *      - 0xBD: 电源优化 0x07
 *      - 0xBE: 温度传感器 0xFE
 *      - 0xE9: 其他设置 0x01
 *
 *    JD79665（四色屏，2bpp）：
 *      - 0x4D: 面板设置 0x78
 *      - PSR: 面板设置 0x2F, 0x29
 *      - BTST: Booster软启动（4个参数）
 *      - TSE: 温度传感器 0x00
 *      - CDI: 颜色深度 0x37
 *      - TCON: 时序控制 0x02, 0x02
 *      - TRES: 分辨率设置
 *      - 0x62: 波形设置（9个参数）
 *      - GSST: 灰度设置（根据型号ID不同使用不同参数）
 *      - 0xE7: 频率设置 0x1C
 *      - PWS: 电源节省 0x00
 *      - 0xE9: 其他设置 0x01
 *      - PLL: PLL频率 0x08
 *
 *    默认（UC8176/UC8179）：
 *      - PSR: 面板设置（BWR模式0x0F，BW模式0x1F）
 *      - CDI: 颜色深度（BWR模式0x77，BW模式0x97）
 *
 * 3. 开启电源
 * 4. 设置全屏显示窗口
 */
void UC81xx_Init(epd_model_t* epd) {
    EPD_Reset(true, 50);
    switch (epd->ic) {
        case DRV_IC_UC8159:
            /* UC8159初始化：黑白红三色屏，使用4bit像素格式 */
            EPD_Write(UC81xx_PWR, 0x37, 0x00);
            EPD_Write(UC81xx_PSR, 0xCF, 0x08);
            EPD_Write(UC81xx_PLL, 0x3A);
            EPD_Write(UC81xx_VDCS, 0x28);
            EPD_Write(UC81xx_BTST, 0xc7, 0xcc, 0x15);
            EPD_Write(UC81xx_CDI, 0x77);
            EPD_Write(UC81xx_TCON, 0x22);
            EPD_Write(0x65, 0x00);  // FLASH CONTROL
            EPD_Write(0xe5, 0x03);  // FLASH MODE
            EPD_Write(UC81xx_TRES, epd->width >> 8, epd->width & 0xff, epd->height >> 8, epd->height & 0xff);
            break;
        case DRV_IC_JD79668:
            /* JD79668初始化：京东方四色屏（黑白红黄），使用2bpp像素格式 */
            EPD_Write(0x4D, 0x78);
            EPD_Write(UC81xx_PSR, 0x0F, 0x29);
            EPD_Write(UC81xx_BTST, 0x0D, 0x12, 0x24, 0x25, 0x12, 0x29, 0x10);
            EPD_Write(UC81xx_PLL, 0x08);
            EPD_Write(UC81xx_CDI, 0x37);
            EPD_Write(UC81xx_TRES, epd->width / 256, epd->width % 256, epd->height / 256, epd->height % 256);
            EPD_Write(0xAE, 0xCF);
            EPD_Write(0xB0, 0x13);
            EPD_Write(0xBD, 0x07);
            EPD_Write(0xBE, 0xFE);
            EPD_Write(0xE9, 0x01);
            break;
        case DRV_IC_JD79665:
            /* JD79665初始化：京东方四色屏（黑白红黄），使用2bpp像素格式 */
            EPD_Write(0x4D, 0x78);
            EPD_Write(UC81xx_PSR, 0x2F, 0x29);
            EPD_Write(UC81xx_BTST, 0x0F, 0x8B, 0x93, 0xA1);
            EPD_Write(UC81xx_TSE, 0x00);
            EPD_Write(UC81xx_CDI, 0x37);
            EPD_Write(UC81xx_TCON, 0x02, 0x02);
            EPD_Write(UC81xx_TRES, epd->width / 256, epd->width % 256, epd->height / 256, epd->height % 256);
            EPD_Write(0x62, 0x98, 0x98, 0x98, 0x75, 0xCA, 0xB2, 0x98, 0x7E);
            if (epd->id == JD79665_750_BWRY)
                EPD_Write(UC81xx_GSST, 0x00, 0x00, 0x00, 0x00);
            else
                EPD_Write(UC81xx_GSST, 0x00, 0x10, 0x00, 0x00);
            EPD_Write(0xE7, 0x1C);
            EPD_Write(UC81xx_PWS, 0x00);
            EPD_Write(0xE9, 0x01);
            EPD_Write(UC81xx_PLL, 0x08);
            break;
        default:
            /* 默认初始化（UC8176/UC8179）：根据颜色模式设置不同参数 */
            EPD_Write(UC81xx_PSR, epd->color == COLOR_BWR ? 0x0F : 0x1F);
            EPD_Write(UC81xx_CDI, epd->color == COLOR_BWR ? 0x77 : 0x97);
            break;
    }
    UC81xx_PowerOn(epd);
    UC81xx_SetWindow(epd, 0, 0, epd->width, epd->height);
}

/**
 * @brief UC81xx清屏
 * @param epd 电子墨水屏型号指针
 * @param refresh true=清屏后立即刷新显示，false=仅写入RAM不刷新
 *
 * 不同IC使用不同的清屏填充值和像素格式：
 *
 * UC8159（4bit像素格式）：
 *   - 每个像素4位，一个字节包含2个像素
 *   - 填充值0x33 = 二进制00110011，表示两个白色像素
 *   - 每个字节位置需要写4次0x33（因为4bit模式下每个像素占4位，
 *     而原始1bpp的每个字节对应8个像素，所以需要 8/2*1 = 4倍数据量）
 *
 * JD79668/JD79665（2bpp像素格式）：
 *   - 每个像素2位，一个字节包含4个像素
 *   - 宽度按4像素对齐：wb = (width+3)/4
 *   - 填充值0x55 = 二进制01010101，表示4个白色像素（01=白色）
 *
 * 默认/UC8176/UC8179（1bpp标准格式）：
 *   - 每个像素1位，一个字节包含8个像素
 *   - 填充值0xFF = 全白
 *   - 同时填充DTM1和DTM2两个RAM
 */
void UC81xx_Clear(epd_model_t* epd, bool refresh) {
    uint32_t wb = (epd->width + 7) / 8;
    switch (epd->ic) {
        case DRV_IC_UC8159:
            /* UC8159: 4bit像素格式，用0x33填充白色 */
            EPD_WriteCmd(UC81xx_DTM1);
            for (uint32_t j = 0; j < epd->height; j++) {
                for (uint32_t i = 0; i < wb; i++) {
                    for (uint8_t k = 0; k < 4; k++) {
                        EPD_WriteByte(0x33);
                    }
                }
            }
            break;
        case DRV_IC_JD79668:
        case DRV_IC_JD79665:
            /* JD系列: 2bpp像素格式，用0x55填充白色（01=白） */
            wb = (epd->width + 3) / 4;  // 2bpp
            EPD_WriteCmd(UC81xx_DTM1);
            for (uint16_t i = 0; i < epd->height; i++) {
                for (uint16_t j = 0; j < wb; j++) {
                    EPD_WriteByte(0x55);
                }
            }
            break;
        default:
            /* UC8176/UC8179: 1bpp标准格式，用0xFF填充白色 */
            EPD_FillRAM(UC81xx_DTM1, 0xFF, wb * epd->height);
            EPD_FillRAM(UC81xx_DTM2, 0xFF, wb * epd->height);
            break;
    }
    if (refresh) UC81xx_Refresh(epd);
}

/**
 * @brief UC8176图像写入（1bpp格式，支持局部刷新）
 * @param epd   电子墨水屏型号指针
 * @param black 黑白数据缓冲区指针（1bpp位图格式，可为NULL表示全白）
 * @param color 彩色数据缓冲区指针（1bpp位图格式，可为NULL表示全白）
 * @param x     起始X坐标（像素）
 * @param y     起始Y坐标（像素）
 * @param w     图像宽度（像素）
 * @param h     图像高度（像素）
 *
 * 写入流程：
 * 1. 计算字节对齐的宽度，将坐标对齐到8像素边界
 * 2. 发送PTIN命令进入局部刷新模式
 * 3. 设置显示窗口
 * 4. COLOR_BWR模式下写入DTM1（黑白RAM）
 * 5. 写入DTM2（彩色RAM）：
 *    - COLOR_BWR模式：使用color数据（红色层）
 *    - COLOR_BW模式：复制black数据
 * 6. 发送PTOUT命令退出局部刷新模式
 *
 * @note 局部刷新模式（PTIN/PTOUT）可以只更新屏幕的部分区域，
 *       避免全屏刷新的闪烁，适合频繁更新的应用场景
 */
void UC8176_WriteImage(epd_model_t* epd, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w,
                       uint16_t h) {
    uint16_t wb = (w + 7) / 8;  // width bytes, bitmaps are padded
    x -= x % 8;                 // byte boundary
    w = wb * 8;                 // byte boundary
    if (x + w > epd->width || y + h > epd->height) return;

    EPD_WriteCmd(UC81xx_PTIN);  // partial in
    UC81xx_SetWindow(epd, x, y, w, h);
    if (epd->color == COLOR_BWR) {
        EPD_WriteCmd(UC81xx_DTM1);
        for (uint16_t i = 0; i < h; i++) {
            for (uint16_t j = 0; j < w / 8; j++) EPD_WriteByte(black ? black[j + i * wb] : 0xFF);
        }
    }
    EPD_WriteCmd(UC81xx_DTM2);
    for (uint16_t i = 0; i < h; i++) {
        for (uint16_t j = 0; j < w / 8; j++) {
            if (epd->color == COLOR_BWR)
                EPD_WriteByte(color ? color[j + i * wb] : 0xFF);
            else
                EPD_WriteByte(black[j + i * wb]);
        }
    }
    EPD_WriteCmd(UC81xx_PTOUT);  // partial out
}

/**
 * @brief UC8159像素转换：将黑白红三色数据打包为4bit像素格式
 * @param black_data 一个字节的黑白数据（8个像素，MSB在前）
 * @param color_data 一个字节的彩色数据（8个像素，MSB在前）
 *
 * UC8159使用4bit像素格式，每个像素4位：
 *   - 0x00 = 黑色（black_data位为0）
 *   - 0x03 = 白色（black_data和color_data位都为1）
 *   - 0x04 = 红色（color_data位为0，black_data位为1）
 *
 * 每次处理2个像素，打包为一个字节：
 *   高4位 = 第1个像素，低4位 = 第2个像素
 *
 * 处理流程（每次调用处理8个像素，输出4个字节）：
 *   外层循环j从0到6，步长2（每次处理2个像素）：
 *     1. 检查color_data最高位：为0→红色(0x04)，否则检查black_data最高位
 *     2. 检查black_data最高位：为0→黑色(0x00)，否则→白色(0x03)
 *     3. 将结果左移4位放入高半字节
 *     4. 左移black_data和color_data，处理下一个像素
 *     5. 同样方式处理第2个像素，结果放入低半字节
 *     6. 输出一个完整的字节
 */
static void UC8159_SendPixel(uint8_t black_data, uint8_t color_data) {
    uint8_t data;
    for (uint8_t j = 0; j < 8; j++) {
        /* 处理第1个像素：检查color_data最高位 */
        if ((color_data & 0x80) == 0x00)
            data = 0x04;  // red
        else if ((black_data & 0x80) == 0x00)
            data = 0x00;  // black
        else
            data = 0x03;  // white
        data = (data << 4) & 0xFF;  // 移到高半字节
        black_data = (black_data << 1) & 0xFF;
        color_data = (color_data << 1) & 0xFF;

        /* 处理第2个像素 */
        j++;
        if ((color_data & 0x80) == 0x00)
            data |= 0x04;  // red
        else if ((black_data & 0x80) == 0x00)
            data |= 0x00;  // black
        else
            data |= 0x03;  // white
        black_data = (black_data << 1) & 0xFF;
        color_data = (color_data << 1) & 0xFF;

        EPD_WriteByte(data);  // 输出一个字节（包含2个4bit像素）
    }
}

/**
 * @brief UC8159图像写入（使用4bit像素格式）
 * @param epd   电子墨水屏型号指针
 * @param black 黑白数据缓冲区指针（1bpp位图格式，可为NULL表示全白）
 * @param color 彩色数据缓冲区指针（1bpp位图格式，可为NULL表示全白）
 * @param x     起始X坐标（像素）
 * @param y     起始Y坐标（像素）
 * @param w     图像宽度（像素）
 * @param h     图像高度（像素）
 *
 * 写入流程：
 * 1. 计算字节对齐的宽度，将坐标对齐到8像素边界
 * 2. 进入局部刷新模式（PTIN）
 * 3. 设置显示窗口
 * 4. 发送DTM1命令开始写入
 * 5. 逐行逐字节处理：
 *    - 每个字节的black和color数据通过UC8159_SendPixel()转换为4bit像素格式
 *    - 每8个1bpp像素转换为4个字节（每字节2个4bit像素）
 * 6. 退出局部刷新模式（PTOUT）
 *
 * @note UC8159只有一个RAM（DTM1），黑白红三色数据打包在一起写入
 */
void UC8159_WriteImage(epd_model_t* epd, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w,
                       uint16_t h) {
    uint16_t wb = (w + 7) / 8;  // width bytes, bitmaps are padded
    x -= x % 8;                 // byte boundary
    w = wb * 8;                 // byte boundary
    if (x + w > epd->width || y + h > epd->height) return;

    EPD_WriteCmd(UC81xx_PTIN);  // partial in
    UC81xx_SetWindow(epd, x, y, w, h);
    EPD_WriteCmd(UC81xx_DTM1);
    for (uint16_t i = 0; i < h; i++) {
        for (uint16_t j = 0; j < w / 8; j++) {
            uint8_t black_data = 0xFF;
            uint8_t color_data = 0xFF;
            if (black) black_data = black[j + i * wb];
            if (color) color_data = color[j + i * wb];
            UC8159_SendPixel(black_data, color_data);
        }
    }
    EPD_WriteCmd(UC81xx_PTOUT);  // partial out
}

/**
 * @brief JD79668/JD79665图像写入（2bpp四色数据格式）
 * @param epd   电子墨水屏型号指针
 * @param black 黑白缓冲区指针（2bpp打包数据，每字节4个像素，可为NULL）
 * @param color 彩色缓冲区指针（未使用，JD系列四色数据已打包在black中）
 * @param x     起始X坐标（像素）
 * @param y     起始Y坐标（像素）
 * @param w     图像宽度（像素）
 * @param h     图像高度（像素）
 *
 * JD系列四色屏使用2bpp像素格式：
 *   - 每个像素2位，一个字节包含4个像素
 *   - 像素值编码：00=黑, 01=白, 10=红, 11=黄
 *   - 坐标按4像素（半字节）对齐
 *
 * 写入流程：
 * 1. 计算半字节对齐的宽度 wb = (w+3)/4
 * 2. 将X坐标和宽度对齐到4像素边界
 * 3. 设置显示窗口
 * 4. 向DTM1写入2bpp打包数据
 * 5. 如果black为NULL，填充0x55（全白色：01 01 01 01）
 *
 * @note black缓冲区中的数据已经是2bpp打包格式，无需额外转换
 */
void JD79668_WriteImage(epd_model_t* epd, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w,
                        uint16_t h) {
    uint16_t wb = (w + 3) / 4;  // width bytes, bitmaps are padded
    x -= x % 4;                 // byte boundary
    w = wb * 4;                 // byte boundary
    if (x + w > epd->width || y + h > epd->height) return;

    UC81xx_SetWindow(epd, x, y, w, h);
    EPD_WriteCmd(UC81xx_DTM1);
    for (uint16_t i = 0; i < h; i++) {
        for (uint16_t j = 0; j < wb; j++) {
            // black buffer contains the packed 2bpp data
            // If black is NULL, write 0x55 (White: 01 01 01 01)
            EPD_WriteByte(black ? black[j + i * wb] : 0x55);
        }
    }
}

/**
 * @brief UC81xx统一的图像写入接口（根据IC类型自动分发）
 * @param epd   电子墨水屏型号指针
 * @param black 黑白数据缓冲区指针
 * @param color 彩色数据缓冲区指针
 * @param x     起始X坐标（像素）
 * @param y     起始Y坐标（像素）
 * @param w     图像宽度（像素）
 * @param h     图像高度（像素）
 *
 * 根据驱动IC类型自动选择对应的图像写入函数：
 *   - DRV_IC_UC8159  → UC8159_WriteImage（4bit像素格式）
 *   - DRV_IC_JD79668/JD79665 → JD79668_WriteImage（2bpp四色格式）
 *   - 默认（UC8176/UC8179）→ UC8176_WriteImage（1bpp标准格式）
 */
void UC81xx_WriteImage(epd_model_t* epd, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w,
                       uint16_t h) {
    switch (epd->ic) {
        case DRV_IC_UC8159:
            UC8159_WriteImage(epd, black, color, x, y, w, h);
            break;
        case DRV_IC_JD79668:
        case DRV_IC_JD79665:
            JD79668_WriteImage(epd, black, color, x, y, w, h);
            break;
        default:
            UC8176_WriteImage(epd, black, color, x, y, w, h);
            break;
    }
}

/**
 * @brief 通过BLE接口写入UC81xx RAM数据
 * @param epd  电子墨水屏型号指针
 * @param cfg  配置字节，编码格式：
 *             高4位：是否为起始包（0x00=起始包，其他=续传包）
 *             低4位：RAM选择（0x0F=黑白RAM，其他=彩色RAM）
 * @param data 待写入的数据缓冲区
 * @param len  数据长度
 *
 * 工作流程：
 * 1. 解析cfg：高4位判断是否起始包，低4位判断写入哪个RAM
 * 2. 如果是起始包且写入黑白RAM，则重新设置全屏窗口
 * 3. 根据IC类型选择写入方式：
 *
 *    UC8159/JD79665/JD79668：
 *      - 只有一个RAM（DTM1），统一写入
 *
 *    默认（UC8176/UC8179）：
 *      - COLOR_BWR模式：0x0F→DTM1(黑白)，其他→DTM2(红色)
 *      - COLOR_BW模式：统一写入DTM2
 *
 * @note 此函数用于BLE分包传输场景，允许数据分多次写入
 */
void UC81xx_WriteRam(epd_model_t* epd, uint8_t cfg, uint8_t* data, uint8_t len) {
    bool begin = (cfg >> 4) == 0x00;
    bool black = (cfg & 0x0F) == 0x0F;
    if (begin && black) UC81xx_SetWindow(epd, 0, 0, epd->width, epd->height);
    switch (epd->ic) {
        case DRV_IC_UC8159:
        case DRV_IC_JD79665:
        case DRV_IC_JD79668:
            /* UC8159和JD系列：只有一个RAM，统一写入DTM1 */
            if (begin) EPD_WriteCmd(UC81xx_DTM1);
            EPD_WriteData(data, len);
            break;
        default:
            /* UC8176/UC8179：根据颜色模式选择写入DTM1或DTM2 */
            if (begin) {
                if (epd->color == COLOR_BWR)
                    EPD_WriteCmd(black ? UC81xx_DTM1 : UC81xx_DTM2);
                else
                    EPD_WriteCmd(UC81xx_DTM2);
            }
            EPD_WriteData(data, len);
            break;
    }
}

/**
 * @brief UC81xx进入深度睡眠模式
 * @param epd 电子墨水屏型号指针
 *
 * 睡眠流程：
 * 1. 先关闭电源（UC81xx_PowerOff），确保所有电荷释放
 * 2. 等待100ms让电源电路稳定
 * 3. 向深度睡眠寄存器（DSLP）写入0xA5确认码进入睡眠
 *
 * @note 0xA5是睡眠确认码，写入其他值不会进入睡眠模式。
 *       进入深度睡眠后，需要硬件复位才能唤醒。
 *       睡眠模式下功耗极低，适合长时间待机的应用场景。
 */
void UC81xx_Sleep(epd_model_t* epd) {
    UC81xx_PowerOff(epd);
    delay(100);
    EPD_Write(UC81xx_DSLP, 0xA5);
}

/**
 * @brief UC81xx系列驱动函数表
 *
 * 将所有UC81xx驱动函数注册到统一的驱动接口结构体中，
 * 供上层EPD框架通过函数指针调用。
 * 适用于UC8159/UC8176/UC8179/JD79668/JD79665等所有UC81xx系列IC。
 */
static const epd_driver_t epd_drv_uc81xx = {
    .init = UC81xx_Init,
    .clear = UC81xx_Clear,
    .write_image = UC81xx_WriteImage,
    .write_ram = UC81xx_WriteRam,
    .refresh = UC81xx_Refresh,
    .sleep = UC81xx_Sleep,
    .read_temp = UC81xx_ReadTemp,
    .read_busy = UC81xx_ReadBusy,
};

/* ======================== 型号实例定义 ======================== */

/** UC8176 4.2英寸 400x300 黑白电子墨水屏 */
const epd_model_t epd_uc8176_420_bw = {UC8176_420_BW, COLOR_BW, &epd_drv_uc81xx, DRV_IC_UC8176, 400, 300};

/** UC8176 4.2英寸 400x300 黑白红三色电子墨水屏 */
const epd_model_t epd_uc8176_420_bwr = {UC8176_420_BWR, COLOR_BWR, &epd_drv_uc81xx, DRV_IC_UC8176, 400, 300};

/** UC8159 7.5英寸 640x384 黑白电子墨水屏（低分辨率版） */
const epd_model_t epd_uc8159_750_bw = {UC8159_750_LOW_BW, COLOR_BW, &epd_drv_uc81xx, DRV_IC_UC8159, 640, 384};

/** UC8159 7.5英寸 640x384 黑白红三色电子墨水屏（低分辨率版） */
const epd_model_t epd_uc8159_750_bwr = {UC8159_750_LOW_BWR, COLOR_BWR, &epd_drv_uc81xx, DRV_IC_UC8159, 640, 384};

/** UC8179 7.5英寸 800x480 黑白电子墨水屏 */
const epd_model_t epd_uc8179_750_bw = {UC8179_750_BW, COLOR_BW, &epd_drv_uc81xx, DRV_IC_UC8179, 800, 480};

/** UC8179 7.5英寸 800x480 黑白红三色电子墨水屏 */
const epd_model_t epd_uc8179_750_bwr = {UC8179_750_BWR, COLOR_BWR, &epd_drv_uc81xx, DRV_IC_UC8179, 800, 480};

/** JD79668 4.2英寸 400x300 黑白红黄四色电子墨水屏 */
const epd_model_t epd_jd79668_420_bwry = {JD79668_420_BWRY, COLOR_BWRY, &epd_drv_uc81xx, DRV_IC_JD79668, 400, 300};

/** JD79665 7.5英寸 800x480 黑白红黄四色电子墨水屏 */
const epd_model_t epd_jd79665_750_bwry = {JD79665_750_BWRY, COLOR_BWRY, &epd_drv_uc81xx, DRV_IC_JD79665, 800, 480};

/** JD79665 5.83英寸 648x480 黑白红黄四色电子墨水屏 */
const epd_model_t epd_jd79665_583_bwry = {JD79665_583_BWRY, COLOR_BWRY, &epd_drv_uc81xx, DRV_IC_JD79665, 648, 480};
