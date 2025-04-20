#include "tftpclient.h"
#include "net_wrapper.h"
#include <string.h>

static int tftp_send_request(tftp_session_t* session, tftp_opcode_t opcode,
                            const char* filename, const char* mode) {
    uint8_t packet[2 + 256 + 1 + 32 + 1 + 64]; // 文件名+模式+选项
    uint8_t* p = packet;
    
    // 构建基本请求
    *((uint16_t*)p) = htons(opcode);
    p += 2;
    strcpy((char*)p, filename);
    p += strlen(filename) + 1;
    strcpy((char*)p, mode ? mode : "octet");
    p += strlen((char*)p) + 1;
    
    // 添加选项
    int opt_len = tftp_build_options(&session->options, p, sizeof(packet) - (p - packet));
    if (opt_len > 0) {
        p += opt_len;
    }
    
    return udp_send(session->peer_ip, session->local_port, session->peer_port,
                   packet, p - packet);
}

int tftp_client_put(tftp_session_t* session, const char* filename, 
                   tftp_get_data_callback get_data, void* user_data) {
    // 发送WRQ请求
    if (tftp_send_request(session, TFTP_WRQ, filename, "octet") < 0) {
        return -1;
    }
    
    // 等待ACK或OACK
    tftp_opcode_t opcode;
    uint8_t data[TFTP_PACKET_MAX_SIZE];
    size_t data_len;
    int ret = tftp_receive_packet(session, &opcode, data, &data_len, session->options.timeout_ms);
    
    if (ret < 0) return -1;
    
    // 处理OACK
    if (opcode == TFTP_OACK && session->options.wait_oack) {
        tftp_options_t negotiated = session->options;
        tftp_parse_options(data, data_len, &negotiated);
        
        // 发送ACK0确认选项
        uint16_t ack_packet[2] = {htons(TFTP_ACK), htons(0)};
        if (udp_send(session->peer_ip, session->local_port, session->peer_port,
                    (uint8_t*)ack_packet, sizeof(ack_packet)) < 0) {
            return -1;
        }
        
        session->options = negotiated;
    } else if (opcode != TFTP_ACK) {
        return -1;
    }
    
    // 开始发送数据
    uint8_t buffer[session->options.block_size];
    session->block_num = 1;
    
    while (1) {
        size_t bytes_read = get_data(user_data, buffer, session->options.block_size);
        if (bytes_read == 0) break;
        
        session->retry_count = 0;
        bool ack_received = false;
        
        while (session->retry_count < session->options.retries && !ack_received) {
            if (tftp_send_packet(session, TFTP_DATA, buffer, bytes_read) < 0) {
                return -1;
            }
            
            ret = tftp_receive_packet(session, &opcode, data, &data_len, 
                                     session->options.timeout_ms);
            if (ret == 0 && opcode == TFTP_ACK && 
                session->block_num == ntohs(*(uint16_t*)data)) {
                ack_received = true;
            } else {
                session->retry_count++;
            }
        }
        
        if (!ack_received) {
            return -1;
        }
        
        session->block_num++;
        if (bytes_read < session->options.block_size) {
            break; // 最后一个包
        }
    }
    
    return 0;
}

int tftp_client_get(tftp_session_t* session, const char* filename,
                   tftp_data_callback data_cb, void* user_data) {
    // 发送RRQ请求
    if (tftp_send_request(session, TFTP_RRQ, filename, "octet") < 0) {
        return -1;
    }
    
    // 等待DATA或OACK
    tftp_opcode_t opcode;
    uint8_t data[TFTP_PACKET_MAX_SIZE];
    size_t data_len;
    int ret = tftp_receive_packet(session, &opcode, data, &data_len, 
                                 session->options.timeout_ms);
    
    if (ret < 0) {
        NET_LOGE("Failed to receive packet");
        return -1;
    }

    NET_LOGD("Received opcode: %u, wait oack: %d", opcode, session->options.wait_oack);
    
    // 处理OACK
    if (opcode == TFTP_OACK && session->options.wait_oack) {
        tftp_options_t negotiated = session->options;
        tftp_parse_options(data, data_len, &negotiated);

        NET_LOGD("Negotiated options: block_size=%u, timeout_ms=%u",
                 negotiated.block_size, negotiated.timeout_ms);
        
        // 发送ACK0确认选项
        uint16_t ack_packet[2] = {htons(TFTP_ACK), htons(0)};
        if (udp_send(session->peer_ip, session->local_port, session->peer_port,
                    (uint8_t*)ack_packet, sizeof(ack_packet)) < 0) {
            NET_LOGE("Failed to send ACK0");
            return -1;
        }
        
        session->options = negotiated;
    } else if (opcode != TFTP_DATA || ntohs(*(uint16_t*)data) != 1) {
        NET_HEX_DUMP(data, data_len);
        NET_LOGE("Invalid first packet");
        return -1;
    }
    
    // 开始接收数据
    session->block_num = 1;
    bool last_packet = false;
    
    while (!last_packet) {
        NET_LOGD("Waiting for DATA or ACK");
        // 处理数据包
        if (opcode == TFTP_DATA) {
            NET_LOGD("Received DATA block %u", session->block_num);
            uint16_t block_num = ntohs(*(uint16_t*)data);
            if (block_num == session->block_num) {
                // 调用回调处理数据
                if (data_cb(user_data, data + 2, data_len - 2) != 0) {
                    NET_LOGE("Data callback failed");
                    return -1;
                }
                
                // 发送ACK
                uint16_t ack_packet[2] = {htons(TFTP_ACK), htons(session->block_num)};
                if (udp_send(session->peer_ip, session->local_port, session->peer_port,
                            (uint8_t*)ack_packet, sizeof(ack_packet)) < 0) {
                    return -1;
                }
                
                // 检查是否为最后一个包
                if (data_len - 2 < session->options.block_size) {
                    last_packet = true;
                } else {
                    session->block_num++;
                }
            }
        } else if (opcode == TFTP_ERROR) {
            NET_LOGE("Received ERROR packet");
            return -1;
        }
        
        // 接收下一个包
        if (!last_packet) {
            NET_LOGD("Waiting for next packet");
            ret = tftp_receive_packet(session, &opcode, data, &data_len, 
                                     session->options.timeout_ms);
            if (ret < 0) return -1;
        }
    }
    
    return 0;
}