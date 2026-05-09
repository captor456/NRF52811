/**
 * @file EPD_config_annotated.c
 * @brief EPD配置管理模块 - 带详细中文注释版本
 * 
 * 本模块负责EPD(电子墨水屏)配置的持久化存储，使用Nordic的FDS(Flash Data Storage)库
 * 将配置数据保存到Flash中，实现掉电不丢失。
 * 
 * 主要功能：
 * - 初始化FDS存储系统
 * - 读取EPD配置
 * - 写入/更新EPD配置
 * - 删除EPD配置
 * - 检查配置是否为空
 * 
 * @note FDS是Nordic提供的Flash数据存储库，基于Flash页面管理，
 *       提供文件-记录式的数据存储接口
 * 
 * @author tsl0922
 * @license GPL-3.0
 */

#include "EPD_config.h"

#include <string.h>

#include "app_scheduler.h"     // 应用调度器，用于延迟执行任务
#include "fds.h"               // Flash Data Storage - Nordic的Flash存储库
#include "nordic_common.h"     // Nordic通用定义
#include "nrf_log.h"           // 日志系统

/**
 * @defgroup FDS配置常量
 * @{
 */
#define CONFIG_FILE_ID 0x0000  /**< 配置文件ID - FDS使用文件ID来组织记录 */
#define CONFIG_REC_KEY 0x0001  /**< 配置记录键 - 每条记录有唯一的键值 */
/** @} */

/**
 * @brief FDS事件处理函数
 * 
 * 当FDS操作完成（写入、更新、删除等）时会触发此回调
 * 
 * @param[in] p_fds_evt FDS事件指针，包含事件类型和结果
 */
static void fds_evt_handler(fds_evt_t const* const p_fds_evt) {
    // 打印事件ID和操作结果
    // 常见事件ID: FDS_EVT_INIT, FDS_EVT_WRITE, FDS_EVT_UPDATE, FDS_EVT_DEL_RECORD等
    NRF_LOG_DEBUG("fds evt: id=%d result=%d\n", p_fds_evt->id, p_fds_evt->result);
}

/**
 * @brief 执行FDS垃圾回收
 * 
 * FDS使用追加写入方式，删除记录只是标记删除，实际空间需要通过GC回收。
 * 此函数通过调度器异步执行，避免在主上下文中阻塞。
 * 
 * @param[in] p_event_data 事件数据（未使用）
 * @param[in] event_size   事件大小（未使用）
 */
static void run_fds_gc(void* p_event_data, uint16_t event_size) {
    NRF_LOG_DEBUG("run garbage collection (fds_gc)\n");
    fds_gc();  // 执行垃圾回收，释放已删除记录占用的Flash空间
}

/**
 * @brief 初始化EPD配置模块
 * 
 * 注册FDS事件处理器并初始化FDS库。
 * 初始化后执行一次垃圾回收，确保Flash空间可用。
 * 
 * @param[in] cfg 配置结构体指针（此函数中未使用，保留参数一致性）
 * 
 * @note 必须在使用其他配置函数之前调用此函数
 */
void epd_config_init(epd_config_t* cfg) {
    ret_code_t ret;

    // 1. 注册FDS事件回调函数
    ret = fds_register(fds_evt_handler);
    if (ret != NRF_SUCCESS) {
        NRF_LOG_ERROR("fds_register failed, code=%d\n", ret);
        return;
    }

    // 2. 初始化FDS库
    // 这会扫描Flash中的FDS页面，恢复已存储的数据
    ret = fds_init();
    if (ret != NRF_SUCCESS) {
        NRF_LOG_ERROR("fds_init failed, code=%d\n", ret);
        return;
    }

    // 3. 执行垃圾回收
    // 清理Flash中的无效记录，释放空间
    run_fds_gc(NULL, 0);
}

/**
 * @brief 从Flash读取EPD配置
 * 
 * 在Flash中查找配置记录并读取到配置结构体中。
 * 如果找不到记录，配置结构体将被填充为0xFF。
 * 
 * @param[out] cfg 配置结构体指针，用于存储读取的配置
 * 
 * @note 如果配置不存在，cfg将被填充为0xFF（未编程Flash的默认值）
 */
void epd_config_read(epd_config_t* cfg) {
    fds_flash_record_t flash_record;   // Flash记录结构体
    fds_record_desc_t record_desc;     // 记录描述符
    fds_find_token_t ftok;             // 查找令牌，用于迭代查找

    // 1. 将配置结构体初始化为0xFF（表示未找到配置）
    // 0xFF是未编程Flash的默认值，用于检测配置是否有效
    memset(cfg, 0xFF, sizeof(epd_config_t));
    memset(&ftok, 0x00, sizeof(fds_find_token_t));

    // 2. 在Flash中查找配置记录
    // 根据文件ID和记录键查找
    if (fds_record_find(CONFIG_FILE_ID, CONFIG_REC_KEY, &record_desc, &ftok) != NRF_SUCCESS) {
        NRF_LOG_DEBUG("epd_config_load: record not found\n");
        return;  // 记录不存在，返回（cfg保持0xFF填充状态）
    }

    // 3. 打开找到的记录
    // 这会获取记录在Flash中的指针
    if (fds_record_open(&record_desc, &flash_record) != NRF_SUCCESS) {
        NRF_LOG_ERROR("epd_config_load: record open failed!");
        return;
    }

    // 4. 计算记录长度
    // nRF52(S112)和nRF51的FDS版本API略有不同
#ifdef S112
    // nRF52 SDK: 直接使用length_words字段
    uint32_t record_len = flash_record.p_header->length_words * sizeof(uint32_t);
#else
    // nRF51 SDK: 使用tl.length_words字段
    uint32_t record_len = flash_record.p_header->tl.length_words * sizeof(uint32_t);
#endif

    // 5. 复制数据到配置结构体
    // 使用MIN确保不会越界复制
    memcpy(cfg, flash_record.p_data, MIN(sizeof(epd_config_t), record_len));

    // 6. 关闭记录
    // 释放记录访问权限
    fds_record_close(&record_desc);
}

/**
 * @brief 将EPD配置写入Flash
 * 
 * 如果配置记录已存在则更新，否则创建新记录。
 * 写入操作是异步的，完成时会触发fds_evt_handler回调。
 * 
 * @param[in] cfg 配置结构体指针，包含要保存的配置数据
 * 
 * @note Flash写入有次数限制（约10,000次），请避免频繁写入
 * @note 如果Flash空间不足，会调度垃圾回收任务
 */
void epd_config_write(epd_config_t* cfg) {
    ret_code_t ret;
    fds_record_t record;           // 记录结构体
    fds_record_desc_t record_desc; // 记录描述符
    fds_find_token_t ftok;         // 查找令牌

    // 1. 设置记录的文件ID和键值
    record.file_id = CONFIG_FILE_ID;
    record.key = CONFIG_REC_KEY;

    // 2. 设置记录数据
    // nRF52和nRF51的FDS API有所不同
#ifdef S112
    // nRF52 SDK: 直接设置数据指针和长度
    record.data.p_data = (void*)cfg;
    record.data.length_words = BYTES_TO_WORDS(sizeof(epd_config_t));  // 字节转换为字(4字节)
#else
    // nRF51 SDK: 使用chunk（数据块）方式
    // nRF51的FDS支持将数据分成多个chunk存储
    fds_record_chunk_t record_chunk;
    record_chunk.p_data = cfg;
    record_chunk.length_words = BYTES_TO_WORDS(sizeof(epd_config_t));
    record.data.p_chunks = &record_chunk;
    record.data.num_chunks = 1;  // 使用单个chunk
#endif

    // 3. 查找是否已存在配置记录
    memset(&ftok, 0x00, sizeof(fds_find_token_t));
    ret = fds_record_find(CONFIG_FILE_ID, CONFIG_REC_KEY, &record_desc, &ftok);

    // 4. 更新现有记录或创建新记录
    if (ret == NRF_SUCCESS)
        ret = fds_record_update(&record_desc, &record);  // 更新已存在的记录
    else
        ret = fds_record_write(&record_desc, &record);   // 写入新记录

    // 5. 处理写入结果
    if (ret != NRF_SUCCESS) {
        NRF_LOG_ERROR("epd_config_save: record write/update failed, code=%d\n", ret);
        
        // 如果Flash空间不足，调度垃圾回收任务
        // 垃圾回收会在主循环中异步执行，释放已删除记录的空间
        if (ret == FDS_ERR_NO_SPACE_IN_FLASH) 
            app_sched_event_put(NULL, 0, run_fds_gc);
    }
}

/**
 * @brief 清除Flash中的EPD配置
 * 
 * 删除Flash中存储的配置记录。
 * 删除后，下次读取配置将返回空配置（0xFF填充）。
 * 
 * @param[in] cfg 配置结构体指针（此函数中未使用）
 * 
 * @note 删除操作只是标记记录为无效，实际空间需要通过GC回收
 */
void epd_config_clear(epd_config_t* cfg) {
    ret_code_t ret;
    fds_record_desc_t record_desc;
    fds_find_token_t ftok;

    // 1. 查找配置记录
    memset(&ftok, 0x00, sizeof(fds_find_token_t));
    if (fds_record_find(CONFIG_FILE_ID, CONFIG_REC_KEY, &record_desc, &ftok) != NRF_SUCCESS) {
        NRF_LOG_DEBUG("epd_config_clear: record not found\n");
        return;  // 记录不存在，无需删除
    }

    // 2. 删除记录
    // 这会将记录标记为无效，但不会立即释放Flash空间
    ret = fds_record_delete(&record_desc);
    if (ret != NRF_SUCCESS) {
        NRF_LOG_ERROR("fds_record_delete failed, code=%d\n", ret);
    }
}

/**
 * @brief 检查配置是否为空
 * 
 * 检查配置结构体是否全部为0xFF（未初始化状态）。
 * 用于判断是否需要使用默认配置。
 * 
 * @param[in] cfg 配置结构体指针
 * 
 * @return true  配置为空（未初始化）
 * @return false 配置有效（包含数据）
 * 
 * @note 0xFF是未编程Flash的默认值，用于检测配置是否已存储
 */
bool epd_config_empty(epd_config_t* cfg) {
    // 遍历配置结构体的每个字节
    for (uint8_t i = 0; i < EPD_CONFIG_SIZE; i++) {
        if (((uint8_t*)cfg)[i] != 0xFF) 
            return false;  // 发现非0xFF字节，配置有效
    }
    return true;  // 所有字节都是0xFF，配置为空
}

/**
 * @brief 模块使用说明
 * 
 * 典型使用流程：
 * 
 * 1. 初始化阶段：
 *    epd_config_t config;
 *    epd_config_init(&config);
 * 
 * 2. 读取配置：
 *    epd_config_read(&config);
 *    if (epd_config_empty(&config)) {
 *        // 使用默认配置
 *        config = default_config;
 *    }
 * 
 * 3. 保存配置（如用户修改设置后）：
 *    epd_config_write(&config);
 * 
 * 4. 清除配置（如恢复出厂设置）：
 *    epd_config_clear(&config);
 * 
 * FDS存储原理：
 * - FDS使用Flash的两个页面作为存储区域
 * - 数据以"文件-记录"方式组织
 * - 写入是追加式的，更新会创建新记录并标记旧记录无效
 * - 垃圾回收(GC)会清理无效记录并释放空间
 * 
 * 注意事项：
 * - Flash写入次数有限（约10,000次），避免频繁写入
 * - 写入操作是异步的，完成时会有回调通知
 * - 大数据写入可能需要多次操作
 */
