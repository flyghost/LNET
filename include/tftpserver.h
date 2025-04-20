#ifndef TFTP_SERVER_H
#define TFTP_SERVER_H

#include "tftp.h"

// 服务器回调类型
typedef int (*tftp_server_read_cb)(void* user_data, const char* filename, 
                                 uint8_t* buffer, size_t max_size);
typedef int (*tftp_server_write_cb)(void* user_data, const char* filename,
                                  const uint8_t* data, size_t size);

// 服务器接口
void tftp_server_process(tftp_server_read_cb read_cb, 
                        tftp_server_write_cb write_cb,
                        void* user_data);

#endif // TFTP_SERVER_H