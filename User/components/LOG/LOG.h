//
// Created by Pluviophile on 2026/1/16.
// Thanks Espressif.
//


#ifndef G474_1_LOG_H
#define G474_1_LOG_H
#include <stdbool.h>
#include <stdint.h>

#define LOG_RAW_MAX_LEN 253

typedef enum {
    LOG_NONE, /* 不输出 */
    LOG_ERROR, /* 错误 (Red) */
    LOG_WARN, /* 警告 (Yellow) */
    LOG_INFO, /* 信息 (Green) */
    LOG_DEBUG, /* 调试 (Default/White) */
    LOG_VERBOSE /* 详细 (Gray) */
} log_level_t;

typedef struct {
    uint16_t len;
    char data[LOG_RAW_MAX_LEN + 1];
} log_data_t;

void log_init(log_level_t level);

void log_set_level(log_level_t level);

/**
 * @brief 从日志内存池获取一个发送缓冲区。
 * @return 成功时返回缓冲区指针，内存池为空或日志服务未初始化时返回NULL。
 * @note 仅可在任务上下文调用，接口不会阻塞。
 */
log_data_t *log_alloc_buffer(void);

/**
 * @brief 归还一个尚未提交的发送缓冲区。
 * @param buffer 由log_alloc_buffer()获取且尚未提交的缓冲区。
 */
void log_free_buffer(log_data_t *buffer);

/**
 * @brief 将缓冲区提交给日志发送任务。
 * @param buffer 由log_alloc_buffer()获取的缓冲区，提交前需填写data和len。
 * @return 成功入队返回true，参数无效或发送队列不可用时返回false。
 * @note 调用后缓冲区的所有权均由日志模块接管，调用方不得继续访问。
 */
bool log_send_buffer(log_data_t *buffer);

void log_write(log_level_t level, const char *tag, const char *format, ...);

#define LOG_COLOR_E "31"  /* Red */
#define LOG_COLOR_W "33"  /* Yellow */
#define LOG_COLOR_I "32"  /* Green */
#define LOG_COLOR_D "39"  /* Default */
#define LOG_COLOR_V "90"  /* Gray */

/*
 *  设置编译时的日志过滤级别。
 */
#ifndef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL LOG_VERBOSE
#endif

#define LOG_FORMAT(letter, format)  LOG_COLOR_ ## letter, format

#define LOGE(tag, format, ...) do { if (LOG_LOCAL_LEVEL >= LOG_ERROR) log_write(LOG_ERROR, tag, format, ##__VA_ARGS__); } while(0)
#define LOGW(tag, format, ...) do { if (LOG_LOCAL_LEVEL >= LOG_WARN)  log_write(LOG_WARN,  tag, format, ##__VA_ARGS__); } while(0)
#define LOGI(tag, format, ...) do { if (LOG_LOCAL_LEVEL >= LOG_INFO)  log_write(LOG_INFO,  tag, format, ##__VA_ARGS__); } while(0)
#define LOGD(tag, format, ...) do { if (LOG_LOCAL_LEVEL >= LOG_DEBUG) log_write(LOG_DEBUG, tag, format, ##__VA_ARGS__); } while(0)
#define LOGV(tag, format, ...) do { if (LOG_LOCAL_LEVEL >= LOG_VERBOSE) log_write(LOG_VERBOSE, tag, format, ##__VA_ARGS__); } while(0)


#endif //G474_1_LOG_H
