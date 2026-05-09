/**
 * @file SSD16xx.c
 * @brief SSD16xx系列电子墨水屏驱动实现（晶门科技 Solomon Systech）
 *
 * 本文件实现了SSD1619和SSD1677两款驱动IC的完整驱动函数。
 * SSD16xx系列采用SRAM显示缓冲区架构，支持黑白(BW)和黑白红(BWR)两种显示模式。
 *
 * 支持的型号实例：
 *   - SSD1619 400x300 黑白/黑白红（4.2英寸）
 *   - SSD1677 880x528 黑白/黑白红（7.5英寸高清）
 *
 * 驱动特性：
 *   - 忙信号为高电平有效（与UC81xx系列相反）
 *   - 内置温度传感器，用于波形温度补偿
 *   - 支持局部窗口刷新
 *   - 通过BLE接口支持远程RAM写入
 */

#include "EPD_driver.h"

/**
 * @brief 读取SSD16xx忙信号状态
 * @param epd 电子墨水屏型号指针
 * @return true=忙（高电平），false=空闲（低电平）
 * @note SSD16xx系列的忙信号为高电平有效，与UC81xx系列（低电平有效）相反
 */
bool SSD16xx_ReadBusy(epd_model_t* epd) { return EPD_ReadBusy(); }

/**
 * @brief 等待SSD16xx忙信号清除
 * @param timeout 超时时间（毫秒），超时后强制返回
 * @note 忙信号为高电平有效，等待期间持续检测直到信号变低或超时
 */
static void SSD16xx_WaitBusy(uint16_t timeout) { EPD_WaitBusy(true, timeout); }

/**
 * @brief 发送显示更新序列号并激活显示刷新
 * @param seq 显示更新序列号，控制刷新模式
 *            0xB1 - 用于温度读取的内部刷新
 *            0xF7 - 正常全屏刷新序列
 *
 * 工作流程：
 * 1. 向显示控制寄存器2（DISP_CTRL2）写入序列号
 * 2. 发送主激活命令（MASTER_ACTIVATE）触发刷新
 */
static void SSD16xx_Update(uint8_t seq) {
    EPD_Write(SSD16xx_DISP_CTRL2, seq);
    EPD_WriteCmd(SSD16xx_MASTER_ACTIVATE);
}

/**
 * @brief 读取SSD16xx驱动芯片内部温度
 * @param epd 电子墨水屏型号指针
 * @return 温度值（有符号8位，单位：摄氏度）
 *
 * 工作流程：
 * 1. 发送0xB1序列号触发一次内部刷新（用于激活温度传感器）
 * 2. 等待忙信号清除（最长500ms）
 * 3. 发送温度传感器读取命令
 * 4. 读取一个字节的有符号温度值
 *
 * @note 温度值用于LUT波形查找表的温度补偿，确保在不同环境温度下
 *       都能获得最佳的显示效果（防止高温残影或低温闪烁）
 */
int8_t SSD16xx_ReadTemp(epd_model_t* epd) {
    SSD16xx_Update(0xB1);
    SSD16xx_WaitBusy(500);
    EPD_WriteCmd(SSD16xx_TSENSOR_READ);
    return (int8_t)EPD_ReadByte();
}

/**
 * @brief 设置SSD16xx显示窗口区域
 * @param epd 电子墨水屏型号指针
 * @param x  窗口起始X坐标（像素）
 * @param y  窗口起始Y坐标（像素）
 * @param w  窗口宽度（像素）
 * @param h  窗口高度（像素）
 *
 * SSD1619和SSD1677的坐标计算方式不同：
 * - SSD1677：X坐标以像素为单位，直接使用模256和高8位拆分
 *            只设置X位置寄存器（RAM_XPOS）和X计数器（RAM_XCOUNT）
 * - SSD1619（默认）：X坐标以字节（8像素）为单位，需要除以8
 *                   同时设置X和Y的位置寄存器及计数器
 *
 * 两种IC都需要设置Y位置和Y计数器，Y坐标统一使用模256和高8位拆分方式
 */
static void SSD16xx_SetWindow(epd_model_t* epd, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    EPD_Write(SSD16xx_ENTRY_MODE, 0x03);  // set ram entry mode: x increase, y increase
    switch (epd->ic) {
        case DRV_IC_SSD1677:
            /* SSD1677: X坐标以像素为单位 */
            EPD_Write(SSD16xx_RAM_XPOS, x % 256, x / 256, (x + w - 1) % 256, (x + w - 1) / 256);
            EPD_Write(SSD16xx_RAM_XCOUNT, x % 256, x / 256);
            break;
        default:
            /* SSD1619: X坐标以字节(8像素)为单位 */
            EPD_Write(SSD16xx_RAM_XPOS, x / 8, (x + w - 1) / 8);
            EPD_Write(SSD16xx_RAM_YPOS, y % 256, y / 256, (y + h - 1) % 256, (y + h - 1) / 256);
            EPD_Write(SSD16xx_RAM_XCOUNT, x / 8);
            break;
    }
    /* Y坐标设置（所有IC通用） */
    EPD_Write(SSD16xx_RAM_YPOS, y % 256, y / 256, (y + h - 1) % 256, (y + h - 1) / 256);
    EPD_Write(SSD16xx_RAM_YCOUNT, y % 256, y / 256);
}

/**
 * @brief SSD16xx初始化
 * @param epd 电子墨水屏型号指针
 *
 * 初始化流程：
 * 1. 硬件复位（拉低复位引脚10ms后释放）
 * 2. 发送软件复位命令并等待完成（最长200ms）
 * 3. 设置边框控制寄存器为0x01（边框浮动放电）
 * 4. 启用内部温度传感器（0x80 = 开启温度传感器）
 * 5. 设置全屏显示窗口
 */
void SSD16xx_Init(epd_model_t* epd) {
    EPD_Reset(true, 10);

    EPD_WriteCmd(SSD16xx_SW_RESET);
    SSD16xx_WaitBusy(200);

    EPD_Write(SSD16xx_BORDER_CTRL, 0x01);
    EPD_Write(SSD16xx_TSENSOR_CTRL, 0x80);

    SSD16xx_SetWindow(epd, 0, 0, epd->width, epd->height);
}

/**
 * @brief SSD16xx刷新显示
 * @param epd 电子墨水屏型号指针
 *
 * 刷新流程：
 * 1. 设置显示控制寄存器1（DISP_CTRL1）：
 *    - COLOR_BWR模式(黑白红)：0x80 = 启用双色RAM模式
 *    - COLOR_BW模式(黑白)：0x40 = 启用单色RAM模式
 * 2. 读取当前芯片温度（用于波形补偿）
 * 3. 发送0xF7序列号激活全屏刷新
 * 4. 等待刷新完成（使用最大超时时间，因为全屏刷新耗时较长）
 * 5. 重新设置全屏窗口（必须保留！刷新后窗口寄存器可能被重置）
 */
static void SSD16xx_Refresh(epd_model_t* epd) {
    EPD_Write(SSD16xx_DISP_CTRL1, epd->color == COLOR_BWR ? 0x80 : 0x40, 0x00);

    EPD_DEBUG("refresh begin");
    EPD_DEBUG("temperature: %d", SSD16xx_ReadTemp(epd));
    SSD16xx_Update(0xF7);
    SSD16xx_WaitBusy(UINT16_MAX);
    EPD_DEBUG("refresh end");
    SSD16xx_SetWindow(epd, 0, 0, epd->width, epd->height);  // DO NOT REMOVE!
}

/**
 * @brief SSD16xx清屏
 * @param epd 电子墨水屏型号指针
 * @param refresh true=清屏后立即刷新显示，false=仅写入RAM不刷新
 *
 * 清屏逻辑：
 * 1. 计算RAM所需字节数 = (宽度+7)/8 * 高度（按字节对齐）
 * 2. 设置全屏显示窗口
 * 3. 向黑白RAM（RAM1）填充0xFF（全白）
 * 4. 向彩色RAM（RAM2）填充0xFF（全白/无红色）
 * 5. 如果refresh为true，则执行显示刷新
 *
 * @note 0xFF表示所有像素位为1，在电子墨水屏中对应白色
 */
void SSD16xx_Clear(epd_model_t* epd, bool refresh) {
    uint32_t ram_bytes = ((epd->width + 7) / 8) * epd->height;

    SSD16xx_SetWindow(epd, 0, 0, epd->width, epd->height);

    EPD_FillRAM(SSD16xx_WRITE_RAM1, 0xFF, ram_bytes);
    EPD_FillRAM(SSD16xx_WRITE_RAM2, 0xFF, ram_bytes);

    if (refresh) SSD16xx_Refresh(epd);
}

/**
 * @brief SSD16xx写入图像数据
 * @param epd   电子墨水屏型号指针
 * @param black 黑白数据缓冲区指针（1bpp，位图格式，可为NULL表示全白）
 * @param color 彩色数据缓冲区指针（1bpp，位图格式，可为NULL表示全白）
 * @param x     起始X坐标（像素）
 * @param y     起始Y坐标（像素）
 * @param w     图像宽度（像素）
 * @param h     图像高度（像素）
 *
 * 写入流程：
 * 1. 计算字节对齐的宽度 wb = (w+7)/8
 * 2. 将X坐标和宽度对齐到8像素（字节）边界
 * 3. 边界检查：确保图像不超出屏幕范围
 * 4. 设置显示窗口
 * 5. 写入黑白RAM（RAM1）：
 *    - 逐行逐字节写入，如果black为NULL则写入0xFF（白色）
 * 6. 写入彩色RAM（RAM2）：
 *    - COLOR_BWR模式：使用color数据（红色层），NULL时填0xFF
 *    - COLOR_BW模式：复制black数据（单色模式下RAM2也使用黑白数据）
 *
 * @note 位图数据按行优先存储，每行字节数为wb，不足8位的行末用0填充
 */
void SSD16xx_WriteImage(epd_model_t* epd, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w,
                        uint16_t h) {
    uint16_t wb = (w + 7) / 8;  // width bytes, bitmaps are padded
    x -= x % 8;                 // byte boundary
    w = wb * 8;                 // byte boundary
    if (x + w > epd->width || y + h > epd->height) return;

    SSD16xx_SetWindow(epd, x, y, w, h);
    EPD_WriteCmd(SSD16xx_WRITE_RAM1);
    for (uint16_t i = 0; i < h; i++) {
        for (uint16_t j = 0; j < w / 8; j++) EPD_WriteByte(black ? black[j + i * wb] : 0xFF);
    }
    EPD_WriteCmd(SSD16xx_WRITE_RAM2);
    for (uint16_t i = 0; i < h; i++) {
        for (uint16_t j = 0; j < w / 8; j++) {
            if (epd->color == COLOR_BWR)
                EPD_WriteByte(color ? color[j + i * wb] : 0xFF);
            else
                EPD_WriteByte(black[j + i * wb]);
        }
    }
}

/**
 * @brief 通过BLE接口写入SSD16xx RAM数据
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
 * 3. 如果是起始包，发送对应的RAM写入命令：
 *    - COLOR_BWR模式：0x0F→RAM1(黑白)，其他→RAM2(红色)
 *    - COLOR_BW模式：统一写入RAM1
 * 4. 写入数据
 *
 * @note 此函数用于BLE分包传输场景，允许数据分多次写入
 */
void SSD16xx_WriteRam(epd_model_t* epd, uint8_t cfg, uint8_t* data, uint8_t len) {
    bool begin = (cfg >> 4) == 0x00;
    bool black = (cfg & 0x0F) == 0x0F;
    if (begin && black) SSD16xx_SetWindow(epd, 0, 0, epd->width, epd->height);
    if (begin) {
        if (epd->color == COLOR_BWR)
            EPD_WriteCmd(black ? SSD16xx_WRITE_RAM1 : SSD16xx_WRITE_RAM2);
        else
            EPD_WriteCmd(SSD16xx_WRITE_RAM1);
    }
    EPD_WriteData(data, len);
}

/**
 * @brief SSD16xx进入睡眠模式
 * @param epd 电子墨水屏型号指针
 *
 * 向睡眠模式寄存器写入0x01，使驱动IC进入低功耗睡眠状态。
 * 进入睡眠后需要等待100ms确保芯片完成状态切换。
 * 唤醒时需要重新执行硬件复位和初始化流程。
 */
void SSD16xx_Sleep(epd_model_t* epd) {
    EPD_Write(SSD16xx_SLEEP_MODE, 0x01);
    delay(100);
}

/**
 * @brief SSD16xx系列驱动函数表
 *
 * 将所有SSD16xx驱动函数注册到统一的驱动接口结构体中，
 * 供上层EPD框架通过函数指针调用。
 */
static const epd_driver_t epd_drv_ssd16xx = {
    .init = SSD16xx_Init,
    .clear = SSD16xx_Clear,
    .write_image = SSD16xx_WriteImage,
    .write_ram = SSD16xx_WriteRam,
    .refresh = SSD16xx_Refresh,
    .sleep = SSD16xx_Sleep,
    .read_temp = SSD16xx_ReadTemp,
    .read_busy = SSD16xx_ReadBusy,
};

/* ======================== 型号实例定义 ======================== */

/** SSD1619 4.2英寸 400x300 黑白红三色电子墨水屏 */
const epd_model_t epd_ssd1619_420_bwr = {SSD1619_420_BWR, COLOR_BWR, &epd_drv_ssd16xx, DRV_IC_SSD1619, 400, 300};

/** SSD1619 4.2英寸 400x300 黑白电子墨水屏 */
const epd_model_t epd_ssd1619_420_bw = {SSD1619_420_BW, COLOR_BW, &epd_drv_ssd16xx, DRV_IC_SSD1619, 400, 300};

/** SSD1677 7.5英寸 880x528 黑白红三色电子墨水屏（高清版） */
const epd_model_t epd_ssd1677_750_bwr = {SSD1677_750_HD_BWR, COLOR_BWR, &epd_drv_ssd16xx, DRV_IC_SSD1677, 880, 528};

/** SSD1677 7.5英寸 880x528 黑白电子墨水屏（高清版） */
const epd_model_t epd_ssd1677_750_bw = {SSD1677_750_HD_BW, COLOR_BW, &epd_drv_ssd16xx, DRV_IC_SSD1677, 880, 528};
