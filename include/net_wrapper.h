#ifndef NET_WRAPPER_H
#define NET_WRAPPER_H

#include <stdint.h>
#include <stddef.h>
#include "net_device.h"

// 模拟UDP包头
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum; // 简单实现可以忽略
} udp_header_t;

// 网络配置
typedef struct {
    uint32_t ip_addr;      // 本地IP地址
    uint32_t netmask;      // 子网掩码
    uint32_t gateway;      // 网关
    uint8_t mac_addr[6];   // MAC地址
} net_config_t;

// 初始化网络封装层
int net_wrapper_init(net_config_t *config);

// 发送UDP数据包
int udp_send(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, 
             const uint8_t *data, size_t length);

// 接收UDP数据包(非阻塞)
int udp_receive(uint32_t *src_ip, uint16_t *src_port, uint16_t *dst_port,
                uint8_t *buffer, size_t buf_size, int timeout_ms);




// 方法1：检查系统字节序（有问题）
// #define IS_BIG_ENDIAN (*(uint16_t *)"\0\x01" >= 0x0100)

// 方法2：
// static inline int is_big_endian() {
//     union {
//         uint16_t u16;
//         uint8_t  u8[2];
//     } test = { .u16 = 0x0100 };  // 如果大端，test.u8[0] == 0x01
//     return test.u8[0] == 0x01;
// }
// #define IS_BIG_ENDIAN is_big_endian()

// 方法3
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    #define IS_BIG_ENDIAN 1
#else
    #define IS_BIG_ENDIAN 0
#endif
// 16位主机字节序转网络字节序
static inline uint16_t htons(uint16_t hostshort) {
    if (IS_BIG_ENDIAN) {
        return hostshort;
    }
    return ((hostshort & 0xFF00) >> 8) | ((hostshort & 0x00FF) << 8);
}

// 16位网络字节序转主机字节序
static inline uint16_t ntohs(uint16_t netshort) {
    return htons(netshort); // 两者操作相同
}

// 32位主机字节序转网络字节序
static inline uint32_t htonl(uint32_t hostlong) {
    if (IS_BIG_ENDIAN) {
        return hostlong;
    }
    return ((hostlong & 0xFF000000) >> 24) |
           ((hostlong & 0x00FF0000) >> 8) |
           ((hostlong & 0x0000FF00) << 8) |
           ((hostlong & 0x000000FF) << 24);
}

// 32位网络字节序转主机字节序
static inline uint32_t ntohl(uint32_t netlong) {
    return htonl(netlong); // 两者操作相同
}

#endif