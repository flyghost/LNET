#ifndef TFTP_CLIENT_H
#define TFTP_CLIENT_H

#include "tftp.h"

// 客户端接口
int tftp_client_put(tftp_session_t* session, const char* filename, 
                   tftp_get_data_callback get_data, void* user_data);

int tftp_client_get(tftp_session_t* session, const char* filename,
                   tftp_data_callback data_cb, void* user_data);

#endif // TFTP_CLIENT_H