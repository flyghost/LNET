#include "tftp.h"
#include "net_wrapper.h"
#include <string.h>
#include <stdbool.h>

#define TFTP_PORT 69
#define TFTP_DATA_SIZE 512
#define TFTP_TIMEOUT_MS 5000
#define TFTP_MAX_RETRIES 1

// 在tftp.c文件顶部添加
typedef struct {
    uint16_t next_local_port;
} net_state_t;

static net_state_t net_state = {
    .next_local_port = 49152  // 从动态端口范围开始
};

static int tftp_receive_packet(tftp_session_t *session, tftp_opcode_t *opcode,
    void *data, size_t *data_len, int timeout_ms);
// 辅助函数：等待特定ACK
static int tftp_wait_for_ack(uint32_t ip, uint16_t port, uint16_t block_num,
                             uint16_t *ack_block, int timeout_ms)
{
    uint32_t start_time = net_get_time_ms();

    while (net_get_time_ms() - start_time < timeout_ms)
    {
        uint8_t packet[4];
        tftp_opcode_t opcode;
        size_t len;

        if (tftp_receive_packet(&(tftp_session_t){.peer_ip = ip, .peer_port = port},
                                &opcode, packet, &len, 100) >= 0)
        {
            if (opcode == TFTP_ACK && len >= 4)
            {
                *ack_block = ntohs(*(uint16_t *)packet);
                if (*ack_block == block_num)
                {
                    return 0; // 收到正确的ACK
                }
            }
        }
    }

    return -1; // 超时
}

static int tftp_send_packet(tftp_session_t *session, tftp_opcode_t opcode, 
                           const void *data, size_t data_len)
{
    uint8_t packet[4 + TFTP_DATA_SIZE];
    uint16_t *p = (uint16_t *)packet;
    
    *p++ = htons(opcode);
    
    if (opcode == TFTP_DATA || opcode == TFTP_ACK) {
        *p++ = htons(session->block_num);
    }
    
    if (data && data_len > 0) {
        memcpy(p, data, data_len);
    }
    
    return udp_send(session->peer_ip, session->local_port, session->peer_port,
                   packet, (opcode == TFTP_DATA || opcode == TFTP_ACK) ? 
                   data_len + 4 : data_len + 2);
}

static int tftp_receive_packet(tftp_session_t *session, tftp_opcode_t *opcode,
                              void *data, size_t *data_len, int timeout_ms)
{
    uint8_t packet[4 + TFTP_DATA_SIZE];
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t dst_port = session->local_port;
    int ret;
    
    ret = udp_receive(&src_ip, &src_port, &dst_port, 
                     packet, sizeof(packet), timeout_ms);
    if (ret <= 0) return ret;
    
    // 验证源IP和端口
    if (src_ip != session->peer_ip || src_port != session->peer_port) {
        NET_LOGW("Received packet from unexpected source: %u.%u.%u.%u:%u",
               (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
               (src_ip >> 8) & 0xFF, src_ip & 0xFF, ntohs(src_port));
        return -1; // 不是我们要的包
    }
    
    uint16_t *p = (uint16_t *)packet;
    *opcode = ntohs(*p++);
    
    switch (*opcode) {
    case TFTP_DATA:
    case TFTP_ACK:
        if (*opcode == TFTP_DATA) {
            NET_LOGD("Received DATA block %u", ntohs(*p));
        } else {
            NET_LOGD("Received ACK block %u", ntohs(*p));
        }
        // session->block_num = ntohs(*p++);
        if (data && data_len) {
            // 包含block号
            *data_len = ret - 2;
            memcpy(data, p, *data_len);
        }
        break;
    case TFTP_ERROR:
        NET_LOGE("Received ERROR packet");
        if (data && data_len) {
            *data_len = ret - 2;
            memcpy(data, p, *data_len);
        }
        break;
    default:
        NET_LOGE("Received unknown opcode: %u", *opcode);
        return -1; // 不支持的包类型
    }
    
    return 0;
}

int tftp_put_file(const char *filename, uint32_t server_ip, const char *mode)
{
    tftp_session_t session = {
        .peer_ip = server_ip,
        .peer_port = TFTP_PORT,
        .block_num = 0,
        .retry_count = 0
    };
    
    // 发送WRQ请求
    uint8_t wrq_packet[2 + strlen(filename) + 1 + strlen(mode) + 1];
    uint8_t *p = wrq_packet;
    
    *((uint16_t *)p) = htons(TFTP_WRQ);
    p += 2;
    strcpy((char *)p, filename);
    p += strlen(filename) + 1;
    strcpy((char *)p, mode);
    p += strlen(mode) + 1;
    
    if (udp_send(server_ip, 0, TFTP_PORT, wrq_packet, p - wrq_packet) < 0) {
        NET_LOGE("udp send fail");
        return -1;
    }
    
    // 等待ACK0
    tftp_opcode_t opcode;
    uint16_t block_num;
    int ret = tftp_receive_packet(&session, &opcode, NULL, NULL, TFTP_TIMEOUT_MS);
    
    if (ret < 0 || opcode != TFTP_ACK || session.block_num != 0) {
        return -1;
    }
    
    // 开始发送数据
    FILE *fp = fopen(filename, "rb");
    if (!fp) return -1;
    
    uint8_t data[TFTP_DATA_SIZE];
    uint8_t rx_data[4];
    size_t rx_len;
    size_t bytes_read;
    
    do {
        bytes_read = fread(data, 1, TFTP_DATA_SIZE, fp);
        if (bytes_read > 0) {
            session.block_num++;
            session.retry_count = 0;
            
            while (session.retry_count < TFTP_MAX_RETRIES) {
                if (tftp_send_packet(&session, TFTP_DATA, data, bytes_read) < 0) {
                    fclose(fp);
                    return -1;
                }
                
                ret = tftp_receive_packet(&session, &opcode, rx_data, &rx_len, TFTP_TIMEOUT_MS);
                if (ret == 0 && opcode == TFTP_ACK && session.block_num == ntohs(*(uint16_t *)rx_data)) {
                    break; // 收到正确的ACK
                }
                else {
                    NET_LOGD("Retrying block %u %u %d %d", session.block_num, ntohs(*(uint16_t *)rx_data), ret, opcode);
                }
                
                session.retry_count++;
            }
            
            if (session.retry_count >= TFTP_MAX_RETRIES) {
                fclose(fp);
                return -1; // 超时重试次数过多
            }
        }
    } while (bytes_read == TFTP_DATA_SIZE);
    
    fclose(fp);
    return 0;
}

int tftp_get_file(const char *filename, uint32_t server_ip, const char *mode) {
    tftp_session_t session = {
        .peer_ip = server_ip,
        .peer_port = TFTP_PORT,
        .block_num = 1,  // 第一个数据包是block 1
        .retry_count = 0,
        .local_port = net_state.next_local_port++
    };
    
    // 发送RRQ请求
    uint8_t rrq_packet[2 + strlen(filename) + 1 + strlen(mode) + 1];
    uint8_t *p = rrq_packet;
    
    *((uint16_t *)p) = htons(TFTP_RRQ);
    p += 2;
    strcpy((char *)p, filename);
    p += strlen(filename) + 1;
    strcpy((char *)p, mode);
    p += strlen(mode) + 1;
    
    if (udp_send(server_ip, session.local_port, TFTP_PORT, rrq_packet, p - rrq_packet) < 0) {
        return -1;
    }
    
    // 打开目标文件
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        // 发送错误包
        uint8_t error_packet[4 + strlen("Access violation") + 1];
        *((uint16_t *)error_packet) = htons(TFTP_ERROR);
        *((uint16_t *)(error_packet + 2)) = htons(TFTP_ERR_ACCESS_VIOLATION);
        strcpy((char *)(error_packet + 4), "Access violation");
        udp_send(server_ip, session.local_port, TFTP_PORT, error_packet, sizeof(error_packet));
        return -1;
    }
    
    uint8_t data[TFTP_DATA_SIZE];
    size_t total_bytes = 0;
    bool last_packet = false;
    
    while (!last_packet) {
        tftp_opcode_t opcode;
        size_t data_len;
        
        // 接收数据包
        int ret = tftp_receive_packet(&session, &opcode, data, &data_len, TFTP_TIMEOUT_MS);
        if (ret < 0) {
            if (session.retry_count++ >= TFTP_MAX_RETRIES) {
                fclose(fp);
                remove(filename); // 删除不完整的文件
                return -1;
            }
            continue;
        }
        
        // 检查是否为预期的DATA包
        if (opcode != TFTP_DATA || ntohs(*(uint16_t *)data) != session.block_num) {
            NET_LOGD("Received unexpected packet: opcode=%u, block_num=%u",
                   opcode, ntohs(*(uint16_t *)data));
            continue;
        }
        
        // 写入文件
        fwrite(data + 2, 1, data_len - 2, fp);
        total_bytes += data_len - 2;
        
        // 发送ACK
        uint16_t ack_packet[2];
        ack_packet[0] = htons(TFTP_ACK);
        ack_packet[1] = htons(session.block_num);
        
        if (udp_send(server_ip, session.local_port, session.peer_port, 
                    (uint8_t *)ack_packet, sizeof(ack_packet)) < 0) {
            fclose(fp);
            remove(filename);
            return -1;
        }
        
        // 检查是否为最后一个包
        if (data_len - 2 < TFTP_DATA_SIZE) {
            last_packet = true;
        } else {
            session.block_num++;
            session.retry_count = 0;
        }
    }
    
    fclose(fp);
    NET_LOGD("Download complete: %s (%zu bytes)", filename, total_bytes);
    return 0;
}

void tftp_server_process(void) {
    uint8_t packet[TFTP_PACKET_MAX_SIZE];
    uint32_t client_ip;
    uint16_t client_port, server_port;
    
    // 接收UDP包（非阻塞）
    int len = udp_receive(&client_ip, &client_port, &server_port, 
                         packet, sizeof(packet), 100);
    
    if (len <= 0) return;
    
    // 解析TFTP操作码
    uint16_t opcode = ntohs(*(uint16_t *)packet);
    
    switch (opcode) {
        case TFTP_RRQ: {
            // 读请求处理
            char *filename = (char *)(packet + 2);
            char *mode = filename + strlen(filename) + 1;
            
            NET_LOGD("RRQ from %d.%d.%d.%d:%d - File: %s, Mode: %s",
                  (client_ip >> 24) & 0xFF, (client_ip >> 16) & 0xFF,
                  (client_ip >> 8) & 0xFF, client_ip & 0xFF,
                  client_port, filename, mode);
            
            // 打开文件
            FILE *fp = fopen(filename, "rb");
            if (!fp) {
                // 发送错误包
                uint8_t error_packet[4 + strlen("File not found") + 1];
                *((uint16_t *)error_packet) = htons(TFTP_ERROR);
                *((uint16_t *)(error_packet + 2)) = htons(TFTP_ERR_FILE_NOT_FOUND);
                strcpy((char *)(error_packet + 4), "File not found");
                udp_send(client_ip, TFTP_PORT, client_port, error_packet, sizeof(error_packet));
                return;
            }
            
            // 发送数据包
            uint8_t data_packet[4 + TFTP_DATA_SIZE];
            uint16_t block_num = 1;
            size_t bytes_read;
            
            do {
                bytes_read = fread(data_packet + 4, 1, TFTP_DATA_SIZE, fp);
                if (bytes_read > 0) {
                    *((uint16_t *)data_packet) = htons(TFTP_DATA);
                    *((uint16_t *)(data_packet + 2)) = htons(block_num);
                    
                    // 发送数据包并等待ACK
                    int retry = 0;
                    bool ack_received = false;
                    
                    while (retry < TFTP_MAX_RETRIES && !ack_received) {
                        udp_send(client_ip, TFTP_PORT, client_port, 
                                data_packet, bytes_read + 4);
                        
                        // 等待ACK
                        uint16_t ack_block;
                        if (tftp_wait_for_ack(client_ip, client_port, block_num, 
                                            &ack_block, TFTP_TIMEOUT_MS) == 0) {
                            ack_received = true;
                        } else {
                            retry++;
                        }
                    }
                    
                    if (!ack_received) {
                        fclose(fp);
                        return; // 放弃传输
                    }
                    
                    block_num++;
                }
            } while (bytes_read == TFTP_DATA_SIZE);
            
            fclose(fp);
            break;
        }
        
        case TFTP_WRQ: {
            // 写请求处理
            char *filename = (char *)(packet + 2);
            char *mode = filename + strlen(filename) + 1;
            
            NET_LOGD("WRQ from %d.%d.%d.%d:%d - File: %s, Mode: %s",
                  (client_ip >> 24) & 0xFF, (client_ip >> 16) & 0xFF,
                  (client_ip >> 8) & 0xFF, client_ip & 0xFF,
                  client_port, filename, mode);
            
            // 发送ACK0
            uint16_t ack_packet[2];
            ack_packet[0] = htons(TFTP_ACK);
            ack_packet[1] = htons(0);
            udp_send(client_ip, TFTP_PORT, client_port, (uint8_t *)ack_packet, sizeof(ack_packet));
            
            // 接收数据并写入文件
            FILE *fp = fopen(filename, "wb");
            if (!fp) {
                // 发送错误包
                uint8_t error_packet[4 + strlen("Access violation") + 1];
                *((uint16_t *)error_packet) = htons(TFTP_ERROR);
                *((uint16_t *)(error_packet + 2)) = htons(TFTP_ERR_ACCESS_VIOLATION);
                strcpy((char *)(error_packet + 4), "Access violation");
                udp_send(client_ip, TFTP_PORT, client_port, error_packet, sizeof(error_packet));
                return;
            }
            
            uint16_t expected_block = 1;
            bool transfer_complete = false;
            
            while (!transfer_complete) {
                uint8_t data_packet[4 + TFTP_DATA_SIZE];
                tftp_opcode_t opcode;
                size_t data_len;
                
                // 接收数据包
                if (tftp_receive_packet(&(tftp_session_t){.peer_ip=client_ip, .peer_port=client_port},
                                      &opcode, data_packet, &data_len, TFTP_TIMEOUT_MS) < 0) {
                    fclose(fp);
                    remove(filename);
                    return;
                }
                
                if (opcode == TFTP_DATA && ntohs(*(uint16_t *)data_packet) == expected_block) {
                    // 写入文件
                    fwrite(data_packet + 2, 1, data_len - 2, fp);
                    
                    // 发送ACK
                    ack_packet[0] = htons(TFTP_ACK);
                    ack_packet[1] = htons(expected_block);
                    udp_send(client_ip, TFTP_PORT, client_port, 
                            (uint8_t *)ack_packet, sizeof(ack_packet));
                    
                    // 检查是否最后一个包
                    if (data_len - 2 < TFTP_DATA_SIZE) {
                        transfer_complete = true;
                    } else {
                        expected_block++;
                    }
                } else if (opcode == TFTP_ERROR) {
                    fclose(fp);
                    remove(filename);
                    return;
                }
            }
            
            fclose(fp);
            NET_LOGD("File %s received successfully", filename);
            break;
        }
        
        default:
            // 不支持的TFTP操作
            uint8_t error_packet[4 + strlen("Illegal operation") + 1];
            *((uint16_t *)error_packet) = htons(TFTP_ERROR);
            *((uint16_t *)(error_packet + 2)) = htons(TFTP_ERR_ILLEGAL_OP);
            strcpy((char *)(error_packet + 4), "Illegal operation");
            udp_send(client_ip, TFTP_PORT, client_port, error_packet, sizeof(error_packet));
            break;
    }
}



// 在tftp_init()中初始化网络
int tftp_init(void) {
    net_config_t config = {
        .ip_addr = 0xC0A80102,    // 192.168.1.2
        .netmask = 0xFFFFFF00,    // 255.255.255.0
        .gateway = 0xC0A80101,    // 192.168.1.1
        .mac_addr = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}
    };
    
    return net_wrapper_init(&config);
}