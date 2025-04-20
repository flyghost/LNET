#include "net_wrapper.h"
#include "net_device.h"
#include <string.h>
#include <stdbool.h>

// 以太网类型 (假设使用IPv4)
#define ETH_TYPE_IPV4 0x0800

// IP协议类型 (UDP)
#define IP_PROTO_UDP 17

// ARP类型
#define ETH_TYPE_ARP 0x0806

// 网络状态
typedef struct {
    net_config_t config;
    bool initialized;
    uint16_t next_local_port;

    net_device_t net_device;
} net_wrapper_t;
static net_wrapper_t g_net_wraper = {0};

#if NET_USE_ASYNC_TASK
static void *net_task = NULL;
static void *net_sem = NULL;
#endif

static net_device_t g_net_device = {0};

// IP头结构
typedef struct {
    uint8_t ver_ihl;      // 版本和头部长度
    uint8_t tos;          // 服务类型
    uint16_t total_length; // 总长度
    uint16_t id;          // 标识
    uint16_t flags_frag;  // 标志和分片偏移
    uint8_t ttl;          // 生存时间
    uint8_t protocol;     // 协议
    uint16_t checksum;    // 校验和
    uint32_t src_ip;      // 源IP
    uint32_t dst_ip;      // 目的IP
} ip_header_t;

// 以太网帧头
typedef struct {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t eth_type;
} eth_header_t;

static void net_input(uint8_t *buffer, size_t length)
{
    NET_LOGD("net input %zu bytes", length);

#if NET_USE_ASYNC_TASK
    net_sem_post(net_sem);
#endif
}

static void net_output(uint8_t *buffer, size_t length)
{
    NET_LOGD("net output %zu bytes", length);
#if NET_USE_ASYNC_TASK
    net_sem_post(net_sem);
#endif
}

static net_device_ops_t net_device_ops = {
    .tx_callback = net_output,
    .rx_callback = net_input
};

// 计算IP校验和
static uint16_t ip_checksum(const void *data, size_t length) {
    uint32_t sum = 0;
    const uint16_t *ptr = data;
    
    for (; length > 1; length -= 2) {
        sum += *ptr++;
    }
    
    if (length > 0) {
        sum += *(uint8_t *)ptr;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

static int eth_input(uint8_t *data) {
    eth_header_t *eth = (eth_header_t *)data;
    if (ntohs(eth->eth_type) != ETH_TYPE_IPV4) {
        NET_LOGW("Not an IPv4 packet");
        return -1; // 不是IP包
    }

    return 0;
}

static int udp_input(uint8_t *data) {
    ip_header_t *ip = (ip_header_t *)(data + sizeof(eth_header_t));
    if (ip->protocol != IP_PROTO_UDP) {
        NET_LOGW("Not a UDP packet");
        return -1; // 不是UDP包
    }

    return 0;
}

static void net_dev_callback(NET_MSG_TYPE msg_type, void *userdata, uint8_t *data, size_t length)
{
    switch (msg_type)
    {
    case NET_MSG_TYPE_RX_PACKET:
        NET_LOGD("Received packet");
        break;

    case NET_MSG_TYPE_TX_PACKET:
        NET_LOGD("Transmitted packet");
        break;
    
    default:
        break;
    }
}

#if NET_USE_ASYNC_TASK
static void net_thread_entry(void *arg)
{
    while (1)
    {
        net_sem_wait(net_sem);
        NET_LOGD("new data");
    }
}

static int net_async_init(void)
{
    net_sem = net_create_sem();
    if (!net_sem) {
        NET_LOGE("Failed to create semaphore");
        return -1;
    }
    net_task = net_create_task(net_thread_entry, NULL);
    if (!net_task) {
        NET_LOGE("Failed to create task");
        net_sem_destroy(net_sem);
        return -1;
    }
    return 0;
}
#endif

// 初始化网络封装层
int net_wrapper_init(net_config_t *config) {
    if (!config) return -1;

    memset(&g_net_wraper, 0, sizeof(net_wrapper_t));

    memcpy(&g_net_wraper.config, config, sizeof(net_config_t));
    g_net_wraper.initialized = true;
    g_net_wraper.next_local_port = 49152; // 从动态端口范围开始

#if NET_USE_ASYNC_TASK
    if (net_async_init() < 0) {
        NET_LOGE("Failed to initialize async task");
        return -1;
    }
#endif

    g_net_wraper.net_device.ops = net_device_ops;
    g_net_wraper.net_device.userdata = &g_net_wraper;
    g_net_wraper.net_device.callback = net_dev_callback;
    
    return net_init(&g_net_wraper.net_device);
}

// 发送UDP数据包
int udp_send(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, 
            const uint8_t *data, size_t length) {
    if (!g_net_wraper.initialized) {
        NET_LOGE("net warper not initialized");
        return -1;
    }
    
    // 分配缓冲区: 以太网头 + IP头 + UDP头 + 数据
    uint8_t packet[sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(udp_header_t) + length];
    uint8_t *ptr = packet;
    
    // 1. 以太网头
    eth_header_t *eth = (eth_header_t *)ptr;
    memset(eth->dst_mac, 0xFF, 6); // 广播地址(简化实现)
    memcpy(eth->src_mac, g_net_wraper.config.mac_addr, 6);
    eth->eth_type = htons(ETH_TYPE_IPV4);
    ptr += sizeof(eth_header_t);
    
    // 2. IP头
    ip_header_t *ip = (ip_header_t *)ptr;
    ip->ver_ihl = 0x45; // IPv4, 5字(20字节)头部
    ip->tos = 0;
    ip->total_length = htons(sizeof(ip_header_t) + sizeof(udp_header_t) + length);
    ip->id = 0;
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_UDP;
    ip->src_ip = g_net_wraper.config.ip_addr;
    ip->dst_ip = dest_ip;
    ip->checksum = 0;
    ip->checksum = ip_checksum(ip, sizeof(ip_header_t));
    ptr += sizeof(ip_header_t);
    
    // 3. UDP头
    udp_header_t *udp = (udp_header_t *)ptr;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dest_port);
    udp->length = htons(sizeof(udp_header_t) + length);
    udp->checksum = 0; // 可选，简化实现不计算
    ptr += sizeof(udp_header_t);
    
    // 4. 数据
    memcpy(ptr, data, length);
    
    // 发送整个数据包
    return net_send(&g_net_wraper.net_device, packet, sizeof(packet));
}

// 接收UDP数据包(非阻塞)
int udp_receive(uint32_t *src_ip, uint16_t *src_port, uint16_t *dst_port,
               uint8_t *buffer, size_t buf_size, int timeout_ms) {
    if (!g_net_wraper.initialized) {
        NET_LOGE("net warper not initialized");
        return -1;
    }
    
    uint8_t packet[NET_MTU_MAX];
    uint32_t start_time = net_get_time_ms(); // 需要实现获取当前时间的函数
    
    while (1) {
        int ret = net_receive_pool(&g_net_wraper.net_device, packet, sizeof(packet));
        if (ret <= 0) {
            if (net_get_time_ms() - start_time > timeout_ms) {
                return -1; // 超时
            }
            continue;
        }
        
        // 解析以太网头
        if (eth_input(packet) < 0) {
            continue;
        }
        
        // 解析IP头
        if (udp_input(packet) < 0) {
            continue;
        }
        
        ip_header_t *ip = (ip_header_t *)(packet + sizeof(eth_header_t));
        // 检查目的IP是否匹配
        if (ip->dst_ip != g_net_wraper.config.ip_addr) {
            NET_LOGW("Not for us: %u.%u.%u.%u", 
                   (ip->dst_ip >> 24) & 0xFF, (ip->dst_ip >> 16) & 0xFF,
                   (ip->dst_ip >> 8) & 0xFF, ip->dst_ip & 0xFF);
            continue; // 不是发给我们的
        }
        
        // 解析UDP头
        udp_header_t *udp = (udp_header_t *)((uint8_t *)ip + (ip->ver_ihl & 0xF) * 4);
        
        // 检查目的端口是否匹配(简化实现，接收所有UDP包)
        if (dst_port && ntohs(udp->dst_port) != *dst_port) {
            NET_LOGW("Destination port mismatch: %u %u", ntohs(udp->dst_port), *dst_port);
            continue; // 端口不匹配
        }
        
        // 提取源信息
        if (src_ip) *src_ip = ip->src_ip;
        if (src_port) *src_port = ntohs(udp->src_port);
        if (dst_port) *dst_port = ntohs(udp->dst_port);
        
        // 提取数据
        size_t data_len = ntohs(udp->length) - sizeof(udp_header_t);
        if (data_len > buf_size) {
            data_len = buf_size; // 防止缓冲区溢出
        }
        
        uint8_t *data = (uint8_t *)udp + sizeof(udp_header_t);
        memcpy(buffer, data, data_len);
        
        return data_len;
    }
}