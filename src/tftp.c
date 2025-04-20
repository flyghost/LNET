#include "tftp.h"
#include "net_wrapper.h"
#include <string.h>

// 全局状态
typedef struct {
    uint16_t next_local_port;
} tftp_global_state_t;

static tftp_global_state_t tftp_state = {
    .next_local_port = 49152  // 从动态端口范围开始
};

void tftp_init_default_options(tftp_options_t* options) {
    if (options) {
        options->block_size = TFTP_DEFAULT_BLOCK_SIZE;
        options->timeout_ms = TFTP_DEFAULT_TIMEOUT_MS;
        options->transfer_size = 0;  // 未知
        options->wait_oack = false;
        options->retries = TFTP_DEFAULT_RETRIES;
    }
}

int tftp_send_packet(tftp_session_t* session, tftp_opcode_t opcode, 
                    const void* data, size_t data_len) {
    uint8_t packet[TFTP_PACKET_MAX_SIZE];
    uint16_t* p = (uint16_t*)packet;
    
    *p++ = htons(opcode);
    
    if (opcode == TFTP_DATA || opcode == TFTP_ACK) {
        *p++ = htons(session->block_num);
    }
    
    if (data && data_len > 0) {
        memcpy(p, data, data_len);
    }
    
    size_t packet_len = (opcode == TFTP_DATA || opcode == TFTP_ACK) ? 
                       data_len + 4 : data_len + 2;
    
    return udp_send(session->peer_ip, session->local_port, session->peer_port,
                   packet, packet_len);
}

int tftp_receive_packet(tftp_session_t* session, tftp_opcode_t* opcode,
                       void* data, size_t* data_len, int timeout_ms) {
    uint8_t packet[TFTP_PACKET_MAX_SIZE];
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t dst_port = session->local_port;
    int ret;
    
    ret = udp_receive(&src_ip, &src_port, &dst_port, 
                     packet, sizeof(packet), timeout_ms);
    if (ret <= 0) return ret;
    
    // 验证源IP和端口
    if (src_ip != session->peer_ip || src_port != session->peer_port) {
        NET_LOGE("Invalid source IP or port");
        // 打印内容
        NET_LOGE("src_ip: %u.%u.%u.%u, src_port: %u, peer_ip: %u.%u.%u.%u, peer_port: %u",
                 (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                 (src_ip >> 8) & 0xFF, src_ip & 0xFF,
                 ntohs(src_port),
                 (session->peer_ip >> 24) & 0xFF, (session->peer_ip >> 16) & 0xFF,
                 (session->peer_ip >> 8) & 0xFF, session->peer_ip & 0xFF,
                 ntohs(session->peer_port));

        return -1; // 不是我们要的包
    }

    NET_LOGD("Received packet from %u.%u.%u.%u:%u",
             (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
             (src_ip >> 8) & 0xFF, src_ip & 0xFF,
             ntohs(src_port));
    
    uint16_t* p = (uint16_t*)packet;
    *opcode = ntohs(*p++);

    NET_LOGD("Received opcode: %u", *opcode);
    
    switch (*opcode) {
    case TFTP_DATA:
    case TFTP_ACK:
    case TFTP_OACK:
        if (data && data_len) {
            *data_len = ret - 2;
            memcpy(data, p, *data_len);
        }
        break;
    case TFTP_ERROR:
        if (data && data_len) {
            *data_len = ret - 2;
            memcpy(data, p, *data_len);
        }
        break;
    default:
        return -1; // 不支持的包类型
    }
    
    return 0;
}

int tftp_send_error(uint32_t ip, uint16_t port, tftp_error_t code, const char* message) {
    uint8_t packet[4 + 128]; // 错误消息最大长度128
    uint16_t* p = (uint16_t*)packet;
    
    *p++ = htons(TFTP_ERROR);
    *p++ = htons(code);
    strncpy((char*)p, message, 128);
    
    size_t len = 4 + strlen(message) + 1;
    return udp_send(ip, 0, port, packet, len);
}

int tftp_parse_options(const uint8_t* data, size_t len, tftp_options_t* options) {
    const char* p = (const char*)data;
    const char* end = p + len;
    
    while (p < end) {
        const char* opt = p;
        p += strlen(opt) + 1;
        if (p >= end) break;
        
        const char* val = p;
        p += strlen(val) + 1;
        
        if (strcasecmp(opt, "blksize") == 0) {
            uint16_t size = (uint16_t)atoi(val);
            if (size >= TFTP_MIN_BLOCK_SIZE && size <= TFTP_MAX_BLOCK_SIZE) {
                options->block_size = size;
            }
        } else if (strcasecmp(opt, "timeout") == 0) {
            uint16_t timeout = (uint16_t)atoi(val);
            if (timeout >= 1 && timeout <= 255) {
                options->timeout_ms = timeout * 1000;
            }
        } else if (strcasecmp(opt, "tsize") == 0) {
            options->transfer_size = (uint32_t)atol(val);
        }
    }
    
    return 0;
}

int tftp_build_options(const tftp_options_t* options, uint8_t* buffer, size_t max_len) {
    char* p = (char*)buffer;
    char* end = p + max_len;
    int count = 0;
    
    if (options->block_size != TFTP_DEFAULT_BLOCK_SIZE) {
        int n = snprintf(p, end - p, "blksize%c%d%c", 0, options->block_size, 0);
        if (n < 0 || p + n >= end) return -1;
        p += n;
        count++;
    }
    
    if (options->timeout_ms != TFTP_DEFAULT_TIMEOUT_MS) {
        int n = snprintf(p, end - p, "timeout%c%d%c", 0, options->timeout_ms / 1000, 0);
        if (n < 0 || p + n >= end) return -1;
        p += n;
        count++;
    }
    
    if (options->transfer_size > 0) {
        int n = snprintf(p, end - p, "tsize%c%u%c", 0, options->transfer_size, 0);
        if (n < 0 || p + n >= end) return -1;
        p += n;
        count++;
    }
    
    return count > 0 ? (p - (char*)buffer) : 0;
}