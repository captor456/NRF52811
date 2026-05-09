/**
 * @file EPD_driver.c
 * @brief EPD（电子纸显示屏）硬件驱动核心实现文件
 *
 * 本文件实现了EPD的底层硬件驱动层，包含以下功能模块：
 * - GPIO引脚配置与管理（引脚映射加载、初始化、反初始化）
 * - SPI通信接口（半双工读写，支持MOSI引脚方向自动切换）
 * - EPD命令与数据传输（DC引脚区分命令/数据模式）
 * - LED状态控制（点亮、熄灭、翻转、闪烁）
 * - ADC电压检测（nRF52使用SAADC，nRF51使用ADC，测量VDD电源电压）
 * - EPD型号初始化（根据型号ID查找并初始化对应的EPD驱动）
 *
 * 适用于nRF52811（S112 SoftDevice）和nRF51系列芯片。
 */

#include "EPD_driver.h"

#include "app_error.h"
#include "nrf_drv_spi.h"

/* ========================================================================== */
/* 宏定义                                                                     */
/* ========================================================================== */

/** 计算数组长度的宏，返回数组元素个数 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/** EPD RAM填充操作的SPI发送缓冲区大小（128字节），
 *  用于分块传输数据，避免单次发送过多数据 */
#define BUFFER_SIZE 128

/** 喂看门狗函数声明（定义在main.c中），
 *  在长时间等待忙信号时需要定期喂狗，防止系统复位 */
extern void app_feed_wdt(void);

/* ========================================================================== */
/* GPIO引脚变量                                                               */
/* ========================================================================== */

/**
 * EPD相关GPIO引脚编号（静态变量，可通过EPD_GPIO_Load从配置结构体加载）
 *
 * 默认引脚分配：
 *   MOSI  = P0.05  SPI数据输出引脚（主出从入）
 *   SCLK  = P0.08  SPI时钟引脚
 *   CS    = P0.09  SPI片选引脚（低电平有效）
 *   DC    = P0.10  数据/命令选择引脚（低电平=命令，高电平=数据）
 *   RST   = P0.11  硬件复位引脚（低电平复位）
 *   BUSY  = P0.12  忙状态检测引脚（读取EPD芯片忙闲状态）
 *   BS    = P0.13  板选/总线选择引脚（用于多片EPD选通，可选）
 *   EN    = 0xFF   使能引脚（0xFF表示未使用/无效引脚）
 *   LED   = 0xFF   状态指示LED引脚（0xFF表示未使用/无效引脚，低电平点亮）
 */
static uint32_t EPD_MOSI_PIN = 5;   /**< SPI MOSI引脚 - 数据输出 */
static uint32_t EPD_SCLK_PIN = 8;   /**< SPI SCLK引脚 - 时钟 */
static uint32_t EPD_CS_PIN = 9;     /**< SPI CS引脚   - 片选（低电平有效） */
static uint32_t EPD_DC_PIN = 10;    /**< DC引脚 - 命令/数据选择 */
static uint32_t EPD_RST_PIN = 11;   /**< RST引脚 - 硬件复位 */
static uint32_t EPD_BUSY_PIN = 12;  /**< BUSY引脚 - 忙状态检测 */
static uint32_t EPD_BS_PIN = 13;    /**< BS引脚 - 板选/总线选择（可选） */
static uint32_t EPD_EN_PIN = 0xFF;  /**< EN引脚 - 使能控制（0xFF=未使用） */
static uint32_t EPD_LED_PIN = 0xFF; /**< LED引脚 - 状态指示灯（0xFF=未使用） */

/* ========================================================================== */
/* SPI实例与HAL宏                                                             */
/* ========================================================================== */

/** SPI实例索引号，使用SPI0外设 */
#define SPI_INSTANCE 0                                               /**< SPI实例索引 */

/** SPI驱动实例，使用Nordic SDK提供的宏创建SPI0实例 */
static const nrf_drv_spi_t spi = NRF_DRV_SPI_INSTANCE(SPI_INSTANCE); /**< SPI实例 */

/**
 * HAL_SPI_INSTANCE宏 - 获取SPI底层寄存器指针
 *
 * 由于nRF52811使用S112 SoftDevice和较新版本的SDK，
 * 其SPI寄存器访问方式与nRF51不同，需要通过条件编译区分：
 * - S112（nRF52系列）：通过 spi.u.spi.p_reg 访问寄存器
 * - 其他（nRF51系列）：通过 spi.p_registers 访问寄存器
 */
#if defined(S112)
#define HAL_SPI_INSTANCE spi.u.spi.p_reg  /**< nRF52系列SPI寄存器指针 */
#else
#define HAL_SPI_INSTANCE spi.p_registers  /**< nRF51系列SPI寄存器指针 */

/**
 * nrf_gpio_pin_dir_get - 获取指定GPIO引脚的当前方向（输入或输出）
 *
 * 注意：nRF51 SDK中缺少此函数，需要手动实现。
 * nRF52 SDK已内置此函数，无需额外定义。
 *
 * @param pin GPIO引脚编号
 * @return nrf_gpio_pin_dir_t 引脚方向（NRF_GPIO_PIN_DIR_INPUT 或 NRF_GPIO_PIN_DIR_OUTPUT）
 *
 * 实现原理：
 * 1. 通过 nrf_gpio_pin_port_decode 获取引脚所在GPIO端口的寄存器基址
 * 2. 读取该引脚的 PIN_CNF 配置寄存器
 * 3. 提取 DIR 字段（bit0）判断引脚方向
 */
nrf_gpio_pin_dir_t nrf_gpio_pin_dir_get(uint32_t pin) {
    NRF_GPIO_Type* reg = nrf_gpio_pin_port_decode(&pin);
    return (nrf_gpio_pin_dir_t)((reg->PIN_CNF[pin] & GPIO_PIN_CNF_DIR_Msk) >> GPIO_PIN_CNF_DIR_Pos);
}
#endif

/* ========================================================================== */
/* Arduino兼容引脚模式设置函数                                                 */
/* ========================================================================== */

/**
 * pinMode - Arduino兼容的GPIO引脚模式设置函数
 *
 * 将Arduino风格的引脚模式映射到nRF5 SDK的GPIO配置函数。
 * 支持以下模式：
 *   INPUT          - 浮空输入（无上下拉）
 *   INPUT_PULLUP   - 上拉输入（内部上拉电阻使能）
 *   INPUT_PULLDOWN - 下拉输入（内部下拉电阻使能）
 *   OUTPUT         - 推挽输出
 *   DEFAULT        - 恢复默认状态（输入，无上下拉，断开缓冲器）
 *
 * @param pin  GPIO引脚编号
 * @param mode 引脚模式（INPUT / INPUT_PULLUP / INPUT_PULLDOWN / OUTPUT / DEFAULT）
 */
void pinMode(uint32_t pin, uint32_t mode) {
    switch (mode) {
        case INPUT:
            /* 配置为浮空输入模式，无内部上下拉电阻 */
            nrf_gpio_cfg_input(pin, NRF_GPIO_PIN_NOPULL);
            break;
        case INPUT_PULLUP:
            /* 配置为上拉输入模式，内部上拉电阻连接到VDD */
            nrf_gpio_cfg_input(pin, NRF_GPIO_PIN_PULLUP);
            break;
        case INPUT_PULLDOWN:
            /* 配置为下拉输入模式，内部下拉电阻连接到GND */
            nrf_gpio_cfg_input(pin, NRF_GPIO_PIN_PULLDOWN);
            break;
        case OUTPUT:
            /* 配置为推挽输出模式，默认输出低电平 */
            nrf_gpio_cfg_output(pin);
            break;
        case DEFAULT:
        default:
            /* 恢复引脚为默认状态：输入模式，断开输入缓冲器，无上下拉 */
            nrf_gpio_cfg_default(pin);
            break;
    }
}

/* ========================================================================== */
/* GPIO初始化与反初始化                                                       */
/* ========================================================================== */

/**
 * 驱动引用计数器，用于支持多个EPD实例共享同一套GPIO/SPI资源。
 * 当计数器大于0时，重复调用EPD_GPIO_Init不会重新初始化硬件，
 * 避免资源冲突和重复配置。
 */
static uint16_t m_driver_refs = 0;

/**
 * EPD_GPIO_Load - 从配置结构体加载EPD引脚映射
 *
 * 将用户传入的epd_config_t结构体中的引脚编号赋值给内部静态变量，
 * 后续的GPIO初始化和SPI通信将使用这些引脚配置。
 * 支持在运行时动态更改引脚分配。
 *
 * @param cfg 指向EPD配置结构体的指针，包含各引脚编号；
 *            若为NULL则直接返回，不执行任何操作
 */
void EPD_GPIO_Load(epd_config_t* cfg) {
    if (cfg == NULL) return;
    EPD_MOSI_PIN = cfg->mosi_pin;   /* 加载SPI MOSI引脚 */
    EPD_SCLK_PIN = cfg->sclk_pin;   /* 加载SPI SCLK引脚 */
    EPD_CS_PIN = cfg->cs_pin;       /* 加载SPI CS引脚 */
    EPD_DC_PIN = cfg->dc_pin;       /* 加载DC引脚 */
    EPD_RST_PIN = cfg->rst_pin;     /* 加载RST引脚 */
    EPD_BUSY_PIN = cfg->busy_pin;   /* 加载BUSY引脚 */
    EPD_BS_PIN = cfg->bs_pin;       /* 加载BS引脚 */
    EPD_EN_PIN = cfg->en_pin;       /* 加载EN引脚 */
    EPD_LED_PIN = cfg->led_pin;     /* 加载LED引脚 */
}

/**
 * EPD_GPIO_Init - 初始化EPD相关的GPIO引脚和SPI外设
 *
 * 执行以下初始化操作：
 * 1. 引用计数检查：若已初始化（m_driver_refs > 0）则直接返回，避免重复初始化
 * 2. 配置DC引脚为输出（初始低电平，表示命令模式）
 * 3. 配置RST引脚为输出（初始高电平，表示非复位状态）
 * 4. 配置BUSY引脚为输入（用于检测EPD忙状态）
 * 5. 初始化SPI外设（使用已配置的SCLK/MOSI/CS引脚）
 * 6. 若BS引脚有效（非0xFF），配置为输出并拉低（选择总线/板选）
 * 7. 若EN引脚有效（非0xFF），配置为输出并拉高（使能EPD电源）
 * 8. 若LED引脚有效（非0xFF），配置为输出并点亮LED
 *
 * 注意：SPI初始化时未注册回调函数（event_handler为NULL），
 *       采用同步阻塞方式进行SPI传输。
 */
void EPD_GPIO_Init(void) {
    /* 引用计数递增，若已有其他实例初始化过则直接返回 */
    if (m_driver_refs++ > 0) return;

    /* 配置EPD核心控制引脚 */
    pinMode(EPD_DC_PIN, OUTPUT);    /* DC引脚 - 输出模式 */
    pinMode(EPD_RST_PIN, OUTPUT);   /* RST引脚 - 输出模式 */
    pinMode(EPD_BUSY_PIN, INPUT);   /* BUSY引脚 - 输入模式 */

    /* 配置并初始化SPI外设 */
    nrf_drv_spi_config_t spi_config = NRF_DRV_SPI_DEFAULT_CONFIG;
    spi_config.sck_pin = EPD_SCLK_PIN;   /* 设置SPI时钟引脚 */
    spi_config.mosi_pin = EPD_MOSI_PIN;  /* 设置SPI数据输出引脚 */
    spi_config.ss_pin = EPD_CS_PIN;      /* 设置SPI片选引脚 */
#if defined(S112)
    /* nRF52系列（S112 SoftDevice）：SPI初始化需要4个参数（含event_handler和p_context） */
    APP_ERROR_CHECK(nrf_drv_spi_init(&spi, &spi_config, NULL, NULL));
#else
    /* nRF51系列：SPI初始化需要3个参数 */
    APP_ERROR_CHECK(nrf_drv_spi_init(&spi, &spi_config, NULL));
#endif

    /* 配置可选的板选（BS）引脚 */
    if (EPD_BS_PIN != 0xFF) {
        pinMode(EPD_BS_PIN, OUTPUT);      /* BS引脚 - 输出模式 */
        digitalWrite(EPD_BS_PIN, LOW);    /* 拉低，选中当前EPD总线 */
    }

    /* 配置可选的使能（EN）引脚 */
    if (EPD_EN_PIN != 0xFF) {
        pinMode(EPD_EN_PIN, OUTPUT);      /* EN引脚 - 输出模式 */
        digitalWrite(EPD_EN_PIN, HIGH);   /* 拉高，使能EPD电源 */
    }

    /* 设置DC和RST引脚初始电平 */
    digitalWrite(EPD_DC_PIN, LOW);    /* DC低电平 = 命令模式 */
    digitalWrite(EPD_RST_PIN, HIGH); /* RST高电平 = 正常工作状态 */

    /* 配置可选的LED引脚并点亮 */
    if (EPD_LED_PIN != 0xFF) pinMode(EPD_LED_PIN, OUTPUT);

    EPD_LED_ON(); /* 点亮状态指示LED */
}

/**
 * EPD_GPIO_Uninit - 反初始化EPD相关的GPIO引脚和SPI外设
 *
 * 执行以下清理操作：
 * 1. 引用计数递减，若仍有其他实例在使用（m_driver_refs > 0）则直接返回
 * 2. 关闭LED指示灯
 * 3. 反初始化SPI外设（释放硬件资源）
 * 4. 将DC、CS、RST、EN引脚拉低（安全状态）
 * 5. 将所有EPD相关引脚恢复为默认状态（断开连接，降低功耗）
 *
 * 注意：引用计数机制确保多个EPD实例共享GPIO/SPI时，
 *       只有最后一个实例反初始化时才会真正释放硬件资源。
 */
void EPD_GPIO_Uninit(void) {
    /* 引用计数递减，若仍有其他实例在使用则直接返回 */
    if (--m_driver_refs > 0) return;

    EPD_LED_OFF(); /* 关闭LED */

    /* 反初始化SPI外设，释放硬件资源 */
    nrf_drv_spi_uninit(&spi);

    /* 将关键控制引脚拉低到安全状态 */
    digitalWrite(EPD_DC_PIN, LOW);    /* DC拉低 */
    digitalWrite(EPD_CS_PIN, LOW);    /* CS拉低（释放片选） */
    digitalWrite(EPD_RST_PIN, LOW);   /* RST拉低 */
    if (EPD_EN_PIN != 0xFF) digitalWrite(EPD_EN_PIN, LOW); /* EN拉低（禁用电源） */

    /* 将所有EPD相关引脚恢复为默认状态（断开连接，降低功耗） */
    pinMode(EPD_MOSI_PIN, DEFAULT);   /* MOSI恢复默认 */
    pinMode(EPD_SCLK_PIN, DEFAULT);   /* SCLK恢复默认 */
    pinMode(EPD_CS_PIN, DEFAULT);     /* CS恢复默认 */
    pinMode(EPD_DC_PIN, DEFAULT);     /* DC恢复默认 */
    pinMode(EPD_RST_PIN, DEFAULT);    /* RST恢复默认 */
    pinMode(EPD_BUSY_PIN, DEFAULT);   /* BUSY恢复默认 */
    pinMode(EPD_BS_PIN, DEFAULT);     /* BS恢复默认 */
    pinMode(EPD_EN_PIN, DEFAULT);     /* EN恢复默认 */
    pinMode(EPD_LED_PIN, DEFAULT);    /* LED恢复默认 */
}

/* ========================================================================== */
/* SPI通信函数                                                                */
/* ========================================================================== */

/**
 * EPD_SPI_Write - 通过SPI总线发送数据
 *
 * 在发送前自动检查MOSI引脚的当前方向：
 * - 若MOSI引脚当前为输入模式（之前可能被配置为SPI读取），
 *   则先切换为输出模式，并重新配置SPI引脚映射（MOSI连接，MISO断开）
 * - 若MOSI引脚已经是输出模式，则直接发送数据
 *
 * 这种设计支持在半双工模式下复用MOSI引脚进行双向通信
 * （MOSI引脚在写操作时作为输出，在读操作时切换为输入/MISO）。
 *
 * @param value 指向待发送数据缓冲区的指针
 * @param len   待发送数据的字节长度
 */
void EPD_SPI_Write(uint8_t* value, uint8_t len) {
    /* 检查MOSI引脚当前方向 */
    nrf_gpio_pin_dir_t dir = nrf_gpio_pin_dir_get(EPD_MOSI_PIN);
    if (dir != NRF_GPIO_PIN_DIR_OUTPUT) {
        /* MOSI引脚当前为输入模式，需要切换为输出模式 */
        pinMode(EPD_MOSI_PIN, OUTPUT);
        /* 重新配置SPI引脚：SCLK保持，MOSI连接，MISO断开 */
        nrf_spi_pins_set(HAL_SPI_INSTANCE, EPD_SCLK_PIN, EPD_MOSI_PIN, NRF_SPI_PIN_NOT_CONNECTED);
    }
    /* 执行SPI同步传输（发送数据，不接收） */
    APP_ERROR_CHECK(nrf_drv_spi_transfer(&spi, value, len, NULL, 0));
}

/**
 * EPD_SPI_Read - 通过SPI总线接收数据
 *
 * 在接收前自动检查MOSI引脚的当前方向：
 * - 若MOSI引脚当前为输出模式（之前可能被配置为SPI写入），
 *   则先切换为输入模式，并重新配置SPI引脚映射（MOSI断开，MISO连接到原MOSI引脚）
 * - 若MOSI引脚已经是输入模式，则直接接收数据
 *
 * 这种设计利用了3线SPI的半双工特性，通过复用MOSI引脚实现数据读取，
 * 节省了一个专用的MISO引脚。
 *
 * @param value 指向接收数据缓冲区的指针，读取的数据将存入此处
 * @param len   待接收数据的字节长度
 */
void EPD_SPI_Read(uint8_t* value, uint8_t len) {
    /* 检查MOSI引脚当前方向 */
    nrf_gpio_pin_dir_t dir = nrf_gpio_pin_dir_get(EPD_MOSI_PIN);
    if (dir != NRF_GPIO_PIN_DIR_INPUT) {
        /* MOSI引脚当前为输出模式，需要切换为输入模式（复用为MISO） */
        pinMode(EPD_MOSI_PIN, INPUT);
        /* 重新配置SPI引脚：SCLK保持，MOSI断开，MISO连接到原MOSI引脚 */
        nrf_spi_pins_set(HAL_SPI_INSTANCE, EPD_SCLK_PIN, NRF_SPI_PIN_NOT_CONNECTED, EPD_MOSI_PIN);
    }
    /* 执行SPI同步传输（不发送，仅接收） */
    APP_ERROR_CHECK(nrf_drv_spi_transfer(&spi, NULL, 0, value, len));
}

/* ========================================================================== */
/* EPD命令与数据传输函数                                                       */
/* ========================================================================== */

/**
 * EPD_WriteCmd - 向EPD发送命令字节
 *
 * 发送流程：
 * 1. 将DC引脚拉低（告诉EPD接下来传输的是命令）
 * 2. 通过SPI发送1字节命令
 *
 * @param cmd 要发送的命令字节（如0x04=读忙状态，0x10=深度刷新等）
 */
void EPD_WriteCmd(uint8_t cmd) {
    digitalWrite(EPD_DC_PIN, LOW);   /* DC拉低 = 命令模式 */
    EPD_SPI_Write(&cmd, 1);         /* 通过SPI发送命令字节 */
}

/**
 * EPD_WriteData - 向EPD发送数据（支持多字节）
 *
 * 发送流程：
 * 1. 将DC引脚拉高（告诉EPD接下来传输的是数据）
 * 2. 通过SPI发送指定长度的数据
 *
 * @param value 指向待发送数据缓冲区的指针
 * @param len   待发送数据的字节长度
 */
void EPD_WriteData(uint8_t* value, uint8_t len) {
    digitalWrite(EPD_DC_PIN, HIGH);  /* DC拉高 = 数据模式 */
    EPD_SPI_Write(value, len);       /* 通过SPI发送数据 */
}

/**
 * EPD_ReadData - 从EPD读取数据（支持多字节）
 *
 * 读取流程：
 * 1. 将DC引脚拉高（告诉EPD接下来是数据传输）
 * 2. 通过SPI接收指定长度的数据
 *
 * @param value 指向接收数据缓冲区的指针，读取的数据将存入此处
 * @param len   待接收数据的字节长度
 */
void EPD_ReadData(uint8_t* value, uint8_t len) {
    digitalWrite(EPD_DC_PIN, HIGH);  /* DC拉高 = 数据模式 */
    EPD_SPI_Read(value, len);        /* 通过SPI接收数据 */
}

/**
 * EPD_WriteByte - 向EPD发送单个数据字节
 *
 * EPD_WriteData的单字节简化版本，用于发送单个参数字节。
 *
 * @param value 要发送的数据字节
 */
void EPD_WriteByte(uint8_t value) {
    digitalWrite(EPD_DC_PIN, HIGH);  /* DC拉高 = 数据模式 */
    EPD_SPI_Write(&value, 1);        /* 通过SPI发送1字节数据 */
}

/**
 * EPD_ReadByte - 从EPD读取单个数据字节
 *
 * EPD_ReadData的单字节简化版本，用于读取单个返回字节。
 *
 * @return 读取到的数据字节
 */
uint8_t EPD_ReadByte(void) {
    uint8_t value;
    digitalWrite(EPD_DC_PIN, HIGH);  /* DC拉高 = 数据模式 */
    EPD_SPI_Read(&value, 1);         /* 通过SPI接收1字节数据 */
    return value;
}

/**
 * EPD_FillRAM - 向EPD的指定RAM区域填充固定值
 *
 * 使用128字节的内部缓冲区分块传输数据，避免一次性发送过大数据。
 * 典型应用场景：
 * - 清屏：向所有RAM区域填充0x00或0xFF
 * - 初始化显示缓冲区
 *
 * @param cmd   要发送的RAM写入命令（如DTM1/DTM2等）
 * @param value 填充的数据值（每个字节都填充此值）
 * @param len   需要填充的总字节数
 */
void EPD_FillRAM(uint8_t cmd, uint8_t value, uint32_t len) {
    /* 创建128字节的填充缓冲区，全部填充为指定值 */
    uint8_t buffer[BUFFER_SIZE];
    for (uint8_t i = 0; i < BUFFER_SIZE; i++) buffer[i] = value;

    /* 先发送RAM写入命令 */
    EPD_WriteCmd(cmd);

    /* 分块发送填充数据，每次最多发送BUFFER_SIZE字节 */
    uint16_t remaining = len;
    while (remaining > 0) {
        /* 计算本次发送的块大小：取剩余长度和缓冲区大小的较小值 */
        uint16_t chunk_size = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : remaining;
        EPD_WriteData(buffer, chunk_size);
        remaining -= chunk_size;
    }
}

/**
 * EPD_Reset - 执行EPD硬件复位
 *
 * 通过RST引脚产生一个复位脉冲序列，使EPD芯片复位到初始状态。
 * 复位时序：status -> delay -> !status -> delay -> status -> delay
 * 通常status=true表示正常工作电平（高），复位时拉低再恢复。
 *
 * @param status   RST引脚的正常工作电平（true=高电平有效，false=低电平有效）
 * @param duration 每个电平状态的保持时间（毫秒），决定了复位脉冲的宽度
 */
void EPD_Reset(bool status, uint16_t duration) {
    digitalWrite(EPD_RST_PIN, status);           /* 设置为正常电平 */
    delay(duration);                             /* 等待稳定 */
    digitalWrite(EPD_RST_PIN, status ? LOW : HIGH); /* 翻转到复位电平 */
    delay(duration);                             /* 保持复位脉冲宽度 */
    digitalWrite(EPD_RST_PIN, status);           /* 恢复到正常电平 */
    delay(duration);                             /* 等待EPD芯片完成复位 */
}

/**
 * EPD_ReadBusy - 读取EPD的当前忙状态
 *
 * @return true=EPD正处于忙状态（正在处理上一条命令）
 *         false=EPD空闲，可以接收新命令
 */
bool EPD_ReadBusy(void) { return digitalRead(EPD_BUSY_PIN); }

/**
 * EPD_WaitBusy - 等待EPD忙信号释放（或变为忙状态）
 *
 * 在等待过程中执行以下操作：
 * 1. 记录LED当前状态
 * 2. 循环检测BUSY引脚电平，直到状态翻转或超时
 * 3. 每隔100次循环喂一次看门狗（防止系统复位）
 * 4. 每隔100次循环翻转LED状态（提供视觉反馈，表示正在等待）
 * 5. 等待结束后恢复LED到之前的状态
 *
 * @param status   等待的忙状态（true=等待忙信号变为低电平，false=等待变为高电平）
 * @param timeout  最大等待时间（单位：毫秒），超时后强制退出
 */
void EPD_WaitBusy(bool status, uint16_t timeout) {
    /* 记录LED当前状态，等待结束后需要恢复 */
    uint32_t led_status = digitalRead(EPD_LED_PIN);

    EPD_DEBUG("check busy");
    /* 循环检测BUSY引脚，直到状态与期望不符或超时 */
    while (EPD_ReadBusy() == status) {
        if (timeout % 100 == 0) {
            app_feed_wdt();      /* 定期喂看门狗，防止系统复位 */
            EPD_LED_Toggle();    /* 翻转LED，提供等待中的视觉反馈 */
        }
        delay(1);                /* 延时1ms，避免CPU空转 */
        timeout--;
        if (timeout == 0) {
            EPD_DEBUG("busy timeout!"); /* 超时，强制退出等待 */
            break;
        }
    }
    EPD_DEBUG("busy release");

    /* 恢复LED到等待前的状态 */
    if (led_status == LOW)
        EPD_LED_ON();    /* 之前LED是亮的，恢复点亮 */
    else
        EPD_LED_OFF();   /* 之前LED是灭的，恢复熄灭 */
}

/* ========================================================================== */
/* LED控制函数                                                                */
/* ========================================================================== */

/**
 * EPD_LED_ON - 点亮状态指示LED
 *
 * LED采用低电平点亮方式（共阳接法）。
 * 仅在EPD_LED_PIN有效（非0xFF）时执行操作。
 */
void EPD_LED_ON(void) {
    if (EPD_LED_PIN != 0xFF) digitalWrite(EPD_LED_PIN, LOW); /* 低电平点亮 */
}

/**
 * EPD_LED_OFF - 熄灭状态指示LED
 *
 * LED采用高电平熄灭方式（共阳接法）。
 * 仅在EPD_LED_PIN有效（非0xFF）时执行操作。
 */
void EPD_LED_OFF(void) {
    if (EPD_LED_PIN != 0xFF) digitalWrite(EPD_LED_PIN, HIGH); /* 高电平熄灭 */
}

/**
 * EPD_LED_Toggle - 翻转状态指示LED
 *
 * 切换LED引脚的输出电平，实现亮灭翻转。
 * 仅在EPD_LED_PIN有效（非0xFF）时执行操作。
 */
void EPD_LED_Toggle(void) {
    if (EPD_LED_PIN != 0xFF) nrf_gpio_pin_toggle(EPD_LED_PIN); /* 翻转电平 */
}

/**
 * EPD_LED_BLINK - LED闪烁一次（亮100ms + 灭100ms）
 *
 * 执行一次完整的闪烁周期：
 * 1. 配置LED引脚为输出模式
 * 2. 拉低点亮LED，延时100ms
 * 3. 拉高熄灭LED，延时100ms
 * 4. 将LED引脚恢复为默认状态（降低功耗）
 *
 * 仅在EPD_LED_PIN有效（非0xFF）时执行操作。
 */
void EPD_LED_BLINK(void) {
    if (EPD_LED_PIN != 0xFF) {
        pinMode(EPD_LED_PIN, OUTPUT);     /* 确保LED引脚为输出模式 */
        digitalWrite(EPD_LED_PIN, LOW);   /* 拉低点亮 */
        delay(100);                        /* 保持点亮100ms */
        digitalWrite(EPD_LED_PIN, HIGH);  /* 拉高熄灭 */
        delay(100);                        /* 保持熄灭100ms */
        pinMode(EPD_LED_PIN, DEFAULT);    /* 恢复默认状态，降低功耗 */
    }
}

/* ========================================================================== */
/* ADC电压检测函数                                                             */
/* ========================================================================== */

/**
 * EPD_ReadVoltage - 读取当前VDD电源电压（单位：毫伏）
 *
 * 通过片内ADC测量VDD电源电压，用于电池电量监测等场景。
 * 根据不同芯片系列使用不同的ADC外设：
 *
 * === nRF52系列（S112 SoftDevice）- 使用SAADC ===
 * - 分辨率：10位（0~1023）
 * - 增益：1/6（测量范围扩展到0~6V）
 * - 参考电压：内部0.6V参考
 * - 输入通道：PSELP_VDD（直接测量VDD）
 * - 采集模式：单端模式
 * - 采样时间：3us
 *
 * === nRF51系列 - 使用传统ADC ===
 * - 分辨率：10位
 * - 输入选择：VDD/3（电源电压1/3分压）
 * - 参考电压：内部带隙参考电压（VBG）
 * - 采样完成后自动停止ADC
 *
 * @return 当前VDD电压值（单位：毫伏），典型值范围：1800~3600mV
 *
 * 计算公式：
 *   nRF52: voltage_mv = (adc_value * 3600) / 1024
 *     （增益1/6 * 参考电压0.6V * 10位满量程 = 0.6V * 6 / 1023 * 1024 ≈ 3600mV）
 *   nRF51: voltage_mv = (adc_value * 3600) / 1024
 *     （VDD/3分压 * VBG参考 * 10位，计算方式类似）
 */
uint16_t EPD_ReadVoltage(void) {
#if defined(S112)
    /* ===== nRF52系列：使用SAADC（Successive Approximation ADC） ===== */
    volatile int16_t value = 0;

    /* 配置SAADC分辨率：10位（结果范围0~1023） */
    NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_10bit;

    /* 使能SAADC外设 */
    NRF_SAADC->ENABLE = (SAADC_ENABLE_ENABLE_Enabled << SAADC_ENABLE_ENABLE_Pos);

    /* 配置SAADC通道0：
     * - RESP: 正端电阻旁路（缩短采样时间）
     * - RESN: 负端电阻旁路
     * - GAIN: 增益1/6（将0.6V参考扩展到3.6V满量程）
     * - REFSEL: 内部0.6V参考电压
     * - TACQ: 采集时间3us
     * - MODE: 单端模式（SE = Single Ended）
     */
    NRF_SAADC->CH[0].CONFIG =
        ((SAADC_CH_CONFIG_RESP_Bypass << SAADC_CH_CONFIG_RESP_Pos) & SAADC_CH_CONFIG_RESP_Msk) |
        ((SAADC_CH_CONFIG_RESP_Bypass << SAADC_CH_CONFIG_RESN_Pos) & SAADC_CH_CONFIG_RESN_Msk) |
        ((SAADC_CH_CONFIG_GAIN_Gain1_6 << SAADC_CH_CONFIG_GAIN_Pos) & SAADC_CH_CONFIG_GAIN_Msk) |
        ((SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos) & SAADC_CH_CONFIG_REFSEL_Msk) |
        ((SAADC_CH_CONFIG_TACQ_3us << SAADC_CH_CONFIG_TACQ_Pos) & SAADC_CH_CONFIG_TACQ_Msk) |
        ((SAADC_CH_CONFIG_MODE_SE << SAADC_CH_CONFIG_MODE_Pos) & SAADC_CH_CONFIG_MODE_Msk);

    /* 配置通道0引脚选择：
     * - PSELN: 负端不连接（单端模式）
     * - PSELP: 正端连接到VDD（直接测量电源电压）
     */
    NRF_SAADC->CH[0].PSELN = SAADC_CH_PSELN_PSELN_NC;
    NRF_SAADC->CH[0].PSELP = SAADC_CH_PSELP_PSELP_VDD;

    /* 配置DMA传输：结果存入value变量，采样1次 */
    NRF_SAADC->RESULT.PTR = (uint32_t)&value;
    NRF_SAADC->RESULT.MAXCNT = 1;

    /* 启动SAADC转换 */
    NRF_SAADC->TASKS_START = 0x01UL;
    while (!NRF_SAADC->EVENTS_STARTED);    /* 等待SAADC启动完成 */
    NRF_SAADC->EVENTS_STARTED = 0x00UL;    /* 清除启动完成事件 */

    NRF_SAADC->TASKS_SAMPLE = 0x01UL;      /* 触发采样 */
    while (!NRF_SAADC->EVENTS_END);         /* 等待采样结束 */
    NRF_SAADC->EVENTS_END = 0x00UL;         /* 清除采样结束事件 */

    NRF_SAADC->TASKS_STOP = 0x01UL;        /* 停止SAADC */
    while (!NRF_SAADC->EVENTS_STOPPED);     /* 等待SAADC完全停止 */
    NRF_SAADC->EVENTS_STOPPED = 0x00UL;     /* 清除停止事件 */

    /* 处理可能的负值（理论上不应出现，但做保护处理） */
    if (value < 0) value = 0;

    /* 禁用SAADC外设，降低功耗 */
    NRF_SAADC->ENABLE = (SAADC_ENABLE_ENABLE_Disabled << SAADC_ENABLE_ENABLE_Pos);
#else
    /* ===== nRF51系列：使用传统ADC ===== */
    NRF_ADC->ENABLE = 1;  /* 使能ADC外设 */

    /* 配置ADC：
     * - RES: 10位分辨率
     * - INPSEL: VDD/3分压输入（将VDD电压缩小到ADC可测范围）
     * - REFSEL: 内部带隙参考电压（VBG，约1.2V）
     * - PSEL: 禁用模拟输入引脚（使用内部VDD通道）
     * - EXTREFSEL: 不使用外部参考
     */
    NRF_ADC->CONFIG = (ADC_CONFIG_RES_10bit << ADC_CONFIG_RES_Pos) |
                      (ADC_CONFIG_INPSEL_SupplyOneThirdPrescaling << ADC_CONFIG_INPSEL_Pos) |
                      (ADC_CONFIG_REFSEL_VBG << ADC_CONFIG_REFSEL_Pos) |
                      (ADC_CONFIG_PSEL_Disabled << ADC_CONFIG_PSEL_Pos) |
                      (ADC_CONFIG_EXTREFSEL_None << ADC_CONFIG_EXTREFSEL_Pos);

    /* 启动ADC转换 */
    NRF_ADC->TASKS_START = 1;
    while (!NRF_ADC->EVENTS_END);    /* 等待转换结束 */
    NRF_ADC->EVENTS_END = 0;         /* 清除转换结束事件 */

    /* 读取转换结果 */
    uint16_t value = NRF_ADC->RESULT;

    /* 停止并禁用ADC，降低功耗 */
    NRF_ADC->TASKS_STOP = 1;
    NRF_ADC->ENABLE = 0;
#endif

    EPD_DEBUG("ADC value: %d", value);

    /* 将ADC原始值转换为毫伏值：
     * 10位ADC满量程对应3600mV（由增益1/6和0.6V参考电压决定）
     * voltage_mv = (adc_value * 3600) / 1024 */
    return (value * 3600) / (1 << 10);
}

/* ========================================================================== */
/* EPD型号管理与初始化                                                         */
/* ========================================================================== */

/* --- 外部声明的EPD型号驱动结构体 --- */
/* 每个epd_model_t包含型号ID、屏幕尺寸、驱动函数指针集等信息 */

extern epd_model_t epd_uc8176_420_bw;     /**< UC8176控制器 - 4.2英寸 黑白 */
extern epd_model_t epd_uc8176_420_bwr;    /**< UC8176控制器 - 4.2英寸 黑白红 */
extern epd_model_t epd_uc8159_750_bw;     /**< UC8159控制器 - 7.5英寸 黑白 */
extern epd_model_t epd_uc8159_750_bwr;    /**< UC8159控制器 - 7.5英寸 黑白红 */
extern epd_model_t epd_uc8179_750_bw;     /**< UC8179控制器 - 7.5英寸 黑白 */
extern epd_model_t epd_uc8179_750_bwr;    /**< UC8179控制器 - 7.5英寸 黑白红 */
extern epd_model_t epd_ssd1619_420_bwr;   /**< SSD1619控制器 - 4.2英寸 黑白红 */
extern epd_model_t epd_ssd1619_420_bw;    /**< SSD1619控制器 - 4.2英寸 黑白 */
extern epd_model_t epd_ssd1677_750_bwr;   /**< SSD1677控制器 - 7.5英寸 黑白红 */
extern epd_model_t epd_ssd1677_750_bw;    /**< SSD1677控制器 - 7.5英寸 黑白 */
extern epd_model_t epd_jd79668_420_bwry;  /**< JD79668控制器 - 4.2英寸 黑白红黄 */
extern epd_model_t epd_jd79665_750_bwry;  /**< JD79665控制器 - 7.5英寸 黑白红黄 */
extern epd_model_t epd_jd79665_583_bwry;  /**< JD79665控制器 - 5.83英寸 黑白红黄 */

/**
 * EPD型号驱动数组 - 包含所有支持的EPD型号
 *
 * 数组中每个元素是一个指向epd_model_t结构体的指针，
 * epd_init函数会遍历此数组，根据型号ID查找匹配的驱动。
 *
 * 支持的控制器系列：
 * - UC系列：UC8159, UC8176, UC8179
 * - SSD系列：SSD1619, SSD1677
 * - JD系列：JD79665, JD79668
 *
 * 支持的屏幕尺寸：4.2英寸、5.83英寸、7.5英寸
 * 支持的显示模式：黑白(BW)、黑白红(BWR)、黑白红黄(BWRY)
 */
static epd_model_t* epd_models[] = {
    &epd_uc8176_420_bw,    &epd_uc8176_420_bwr,   &epd_uc8159_750_bw,    &epd_uc8159_750_bwr,  &epd_uc8179_750_bw,
    &epd_uc8179_750_bwr,   &epd_ssd1619_420_bwr,  &epd_ssd1619_420_bw,   &epd_ssd1677_750_bwr, &epd_ssd1677_750_bw,
    &epd_jd79668_420_bwry, &epd_jd79665_750_bwry, &epd_jd79665_583_bwry,
};

/**
 * epd_init - 根据型号ID初始化对应的EPD驱动
 *
 * 初始化流程：
 * 1. 遍历epd_models数组，查找与指定ID匹配的EPD型号驱动
 * 2. 若未找到匹配的型号，则默认使用数组中的第一个型号（epd_uc8176_420_bw）
 * 3. 调用匹配型号的init函数，执行EPD芯片的初始化序列
 *    （包括发送初始化命令、设置显示参数、清除RAM等）
 * 4. 返回匹配的EPD型号驱动指针，供上层调用显示函数
 *
 * @param id  EPD型号ID（定义在epd_model_id_t枚举中）
 * @return    指向匹配的EPD型号驱动结构体的指针（始终非NULL）
 */
epd_model_t* epd_init(epd_model_id_t id) {
    epd_model_t* epd = NULL;

    /* 遍历所有已注册的EPD型号，查找匹配的ID */
    for (uint8_t i = 0; i < ARRAY_SIZE(epd_models); i++) {
        if (epd_models[i]->id == id) {
            epd = epd_models[i]; /* 找到匹配的型号 */
        }
    }

    /* 若未找到匹配的型号，使用默认型号（数组第一个元素） */
    if (epd == NULL) epd = epd_models[0];

    /* 调用该型号的初始化函数，执行EPD芯片初始化序列 */
    epd->drv->init(epd);

    return epd; /* 返回EPD型号驱动指针 */
}
