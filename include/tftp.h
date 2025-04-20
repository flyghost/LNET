#ifndef TFTP_H
#define TFTP_H

#include <stdint.h>
#include <stddef.h>

#define TFTP_ERR_ILLEGAL_OP      4    // 非法操作

// TFTP操作码
typedef enum {
    TFTP_RRQ = 1,    // 读请求
    TFTP_WRQ = 2,    // 写请求
    TFTP_DATA = 3,   // 数据包
    TFTP_ACK = 4,    // 确认
    TFTP_ERROR = 5   // 错误
} tftp_opcode_t;

// TFTP错误码
typedef enum {
    TFTP_ERR_NOT_DEFINED = 0,
    TFTP_ERR_FILE_NOT_FOUND = 1,
    TFTP_ERR_ACCESS_VIOLATION = 2,
    // ...其他错误码
} tftp_error_t;

// TFTP会话结构
typedef struct {
    uint32_t peer_ip;
    uint16_t peer_port;
    uint16_t local_port;
    uint16_t block_num;
    uint8_t retry_count;
} tftp_session_t;

// 初始化TFTP模块
int tftp_init(void);

// 作为客户端发送文件
int tftp_put_file(const char *filename, uint32_t server_ip, const char *mode);

// 作为客户端接收文件
int tftp_get_file(const char *filename, uint32_t server_ip, const char *mode);

// 作为服务器处理请求
void tftp_server_process(void);

#endif