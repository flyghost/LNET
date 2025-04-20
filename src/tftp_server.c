#include "tftpserver.h"
#include "net_wrapper.h"
#include <string.h>

static int tftp_handle_read(tftp_session_t* session, const char* filename,
                          tftp_server_read_cb read_cb, void* user_data) {
    uint8_t data_packet[4 + TFTP_MAX_BLOCK_SIZE];
    uint16_t block_num = 1;
    bool last_packet = false;
    
    while (!last_packet) {
        // 读取数据
        size_t bytes_read = read_cb(user_data, filename, 
                                   data_packet + 4, session->options.block_size);
        if (bytes_read == 0) break;
        
        // 构建DATA包
        *((uint16_t*)data_packet) = htons(TFTP_DATA);
        *((uint16_t*)(data_packet + 2)) = htons(block_num);
        
        // 发送并等待ACK
        int retry = 0;
        bool ack_received = false;
        
        while (retry < session->options.retries && !ack_received) {
            if (udp_send(session->peer_ip, session->local_port, session->peer_port,
                        data_packet, bytes_read + 4) < 0) {
                return -1;
            }
            
            tftp_opcode_t opcode;
            uint8_t ack_data[4];
            size_t ack_len;
            
            if (tftp_receive_packet(session, &opcode, ack_data, &ack_len,
                                   session->options.timeout_ms) == 0 &&
                opcode == TFTP_ACK && 
                ntohs(*(uint16_t*)ack_data) == block_num) {
                ack_received = true;
            } else {
                retry++;
            }
        }
        
        if (!ack_received) {
            return -1;
        }
        
        if (bytes_read < session->options.block_size) {
            last_packet = true;
        } else {
            block_num++;
        }
    }
    
    return 0;
}

static int tftp_handle_write(tftp_session_t* session, const char* filename,
                           tftp_server_write_cb write_cb, void* user_data) {
    // 发送ACK0
    uint16_t ack_packet[2] = {htons(TFTP_ACK), htons(0)};
    if (udp_send(session->peer_ip, session->local_port, session->peer_port,
                (uint8_t*)ack_packet, sizeof(ack_packet)) < 0) {
        return -1;
    }
    
    uint16_t expected_block = 1;
    bool transfer_complete = false;
    
    while (!transfer_complete) {
        tftp_opcode_t opcode;
        uint8_t data_packet[4 + TFTP_MAX_BLOCK_SIZE];
        size_t data_len;
        
        // 接收数据包
        if (tftp_receive_packet(session, &opcode, data_packet, &data_len,
                               session->options.timeout_ms) < 0) {
            return -1;
        }
        
        if (opcode == TFTP_DATA && 
            ntohs(*(uint16_t*)data_packet) == expected_block) {
            // 写入数据
            if (write_cb(user_data, filename, data_packet + 2, data_len - 2) != 0) {
                return -1;
            }
            
            // 发送ACK
            ack_packet[0] = htons(TFTP_ACK);
            ack_packet[1] = htons(expected_block);
            if (udp_send(session->peer_ip, session->local_port, session->peer_port,
                        (uint8_t*)ack_packet, sizeof(ack_packet)) < 0) {
                return -1;
            }
            
            // 检查是否最后一个包
            if (data_len - 2 < session->options.block_size) {
                transfer_complete = true;
            } else {
                expected_block++;
            }
        } else if (opcode == TFTP_ERROR) {
            return -1;
        }
    }
    
    return 0;
}

void tftp_server_process(tftp_server_read_cb read_cb, 
                        tftp_server_write_cb write_cb,
                        void* user_data) {
    uint8_t packet[TFTP_PACKET_MAX_SIZE];
    uint32_t client_ip;
    uint16_t client_port, server_port;
    
    // 接收UDP包
    int len = udp_receive(&client_ip, &client_port, &server_port, 
                         packet, sizeof(packet), 100);
    if (len <= 0) return;
    
    // 解析TFTP操作码
    uint16_t opcode = ntohs(*(uint16_t*)packet);
    
    switch (opcode) {
        case TFTP_RRQ: {
            // 解析请求
            char* filename = (char*)(packet + 2);
            char* mode = filename + strlen(filename) + 1;
            
            // 初始化会话
            tftp_session_t session = {
                .peer_ip = client_ip,
                .peer_port = client_port,
                .local_port = server_port,
                .block_num = 0,
                .retry_count = 0
            };
            tftp_init_default_options(&session.options);
            
            // 检查并处理选项
            const char* options = mode + strlen(mode) + 1;
            if (options < (char*)packet + len) {
                tftp_parse_options((uint8_t*)options, len - (options - (char*)packet), 
                                  &session.options);
                
                // 发送OACK
                uint8_t oack_packet[TFTP_PACKET_MAX_SIZE];
                int oack_len = tftp_build_options(&session.options, oack_packet + 2, 
                                                 sizeof(oack_packet) - 2);
                if (oack_len > 0) {
                    *((uint16_t*)oack_packet) = htons(TFTP_OACK);
                    udp_send(client_ip, server_port, client_port, 
                            oack_packet, oack_len + 2);
                }
            }
            
            // 处理读请求
            tftp_handle_read(&session, filename, read_cb, user_data);
            break;
        }
        
        case TFTP_WRQ: {
            // 解析请求
            char* filename = (char*)(packet + 2);
            char* mode = filename + strlen(filename) + 1;
            
            // 初始化会话
            tftp_session_t session = {
                .peer_ip = client_ip,
                .peer_port = client_port,
                .local_port = server_port,
                .block_num = 0,
                .retry_count = 0
            };
            tftp_init_default_options(&session.options);
            
            // 检查并处理选项
            const char* options = mode + strlen(mode) + 1;
            if (options < (char*)packet + len) {
                tftp_parse_options((uint8_t*)options, len - (options - (char*)packet), 
                                  &session.options);
                
                // 发送OACK
                uint8_t oack_packet[TFTP_PACKET_MAX_SIZE];
                int oack_len = tftp_build_options(&session.options, oack_packet + 2, 
                                                 sizeof(oack_packet) - 2);
                if (oack_len > 0) {
                    *((uint16_t*)oack_packet) = htons(TFTP_OACK);
                    udp_send(client_ip, server_port, client_port, 
                            oack_packet, oack_len + 2);
                }
            }
            
            // 处理写请求
            tftp_handle_write(&session, filename, write_cb, user_data);
            break;
        }
        
        default:
            // 不支持的TFTP操作
            tftp_send_error(client_ip, client_port, TFTP_ERR_ILLEGAL_OP, "Illegal operation");
            break;
    }
}