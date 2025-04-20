#ifndef TFTP_H
#define TFTP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// TFTP协议常量
#define TFTP_DEFAULT_PORT        69
#define TFTP_DEFAULT_BLOCK_SIZE  512
#define TFTP_DEFAULT_TIMEOUT_MS  5000
#define TFTP_DEFAULT_RETRIES     5
#define TFTP_MAX_BLOCK_SIZE      65464
#define TFTP_MIN_BLOCK_SIZE      8
#define TFTP_PACKET_MAX_SIZE     (4 + TFTP_MAX_BLOCK_SIZE)

// TFTP操作码
typedef enum {
    TFTP_RRQ = 1,    // 读请求
    TFTP_WRQ = 2,    // 写请求
    TFTP_DATA = 3,   // 数据包
    TFTP_ACK = 4,    // 确认
    TFTP_ERROR = 5,  // 错误
    TFTP_OACK = 6    // 选项确认
} tftp_opcode_t;

// TFTP错误码
typedef enum {
    TFTP_ERR_NOT_DEFINED = 0,
    TFTP_ERR_FILE_NOT_FOUND = 1,
    TFTP_ERR_ACCESS_VIOLATION = 2,
    TFTP_ERR_DISK_FULL = 3,
    TFTP_ERR_ILLEGAL_OP = 4,
    TFTP_ERR_UNKNOWN_ID = 5,
    TFTP_ERR_FILE_EXISTS = 6,
    TFTP_ERR_NO_SUCH_USER = 7,
    TFTP_ERR_OPTION_NEGOTIATION = 8
} tftp_error_t;

// TFTP选项
typedef struct {
    uint16_t block_size;      // 块大小
    uint32_t timeout_ms;      // 超时时间(毫秒)
    uint16_t transfer_size;   // 传输大小(字节)
    bool wait_oack;           // 是否等待OACK
    uint8_t retries;         // 重试次数
} tftp_options_t;

// TFTP会话结构
typedef struct {
    uint32_t peer_ip;
    uint16_t peer_port;
    uint16_t local_port;
    uint16_t block_num;
    uint8_t retry_count;
    tftp_options_t options;
} tftp_session_t;

// TFTP数据回调函数
typedef int (*tftp_data_callback)(void* user_data, const uint8_t* data, size_t size);
typedef int (*tftp_get_data_callback)(void* user_data, uint8_t* buffer, size_t max_size);

// 初始化默认配置
void tftp_init_default_options(tftp_options_t* options);

// 核心协议函数
int tftp_send_packet(tftp_session_t* session, tftp_opcode_t opcode, const void* data, size_t data_len);
int tftp_receive_packet(tftp_session_t* session, tftp_opcode_t* opcode, void* data, size_t* data_len, int timeout_ms);
int tftp_send_error(uint32_t ip, uint16_t port, tftp_error_t code, const char* message);

// 选项协商
int tftp_parse_options(const uint8_t* data, size_t len, tftp_options_t* options);
int tftp_build_options(const tftp_options_t* options, uint8_t* buffer, size_t max_len);

#endif // TFTP_H