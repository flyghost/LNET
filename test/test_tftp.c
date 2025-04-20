#include "tftp.h"
#include "tftpclient.h"
#include "tftpserver.h"
#include "net_wrapper.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// 测试文件内容
static const char *test_download_file_content = "this is a test download file";
static const char *test_download_filename = "test_download.txt";

static const char *test_upload_file_content = "this is a test upload file";
static const char *test_upload_filename = "test_uplaod.txt";

// 网络配置
static net_config_t client_config = {
    .ip_addr = 0x0201A8C0,    // 192.168.1.2
    .netmask = 0x00FFFFFF,    // 255.255.255.0
    .gateway = 0x0101A8C0,    // 192.168.1.1
    .mac_addr = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}
};

static net_config_t server_config = {
    .ip_addr = 0x0301A8C0,    // 192.168.1.3
    .netmask = 0x00FFFFFF,    // 255.255.255.0
    .gateway = 0x0101A8C0,    // 192.168.1.1
    .mac_addr = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
};

// 文件操作回调函数
static int read_file_cb(void *user_data, const char *filename, uint8_t *buffer, size_t max_size) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return -1;
    
    size_t bytes_read = fread(buffer, 1, max_size, fp);
    fclose(fp);
    
    return bytes_read;
}

static int write_file_cb(void *user_data, const char *filename, const uint8_t *data, size_t size) {
    FILE *fp = fopen(filename, "ab");
    if (!fp) return -1;
    
    size_t bytes_written = fwrite(data, 1, size, fp);
    fclose(fp);
    
    return (bytes_written == size) ? 0 : -1;
}

static int get_data_cb(void *user_data, uint8_t *buffer, size_t max_size) {
    const char **content = (const char **)user_data;
    static size_t pos = 0;
    size_t remaining = strlen(*content) - pos;
    
    if (remaining == 0) return 0;
    
    size_t to_copy = (remaining < max_size) ? remaining : max_size;
    memcpy(buffer, *content + pos, to_copy);
    pos += to_copy;
    
    return to_copy;
}

static int data_cb(void *user_data, const uint8_t *data, size_t size) {
    FILE *fp = (FILE *)user_data;
    return (fwrite(data, 1, size, fp) == size) ? 0 : -1;
}

// 创建测试文件
static int create_test_file(const char *filename, const char *filecontent) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) return -1;
    
    size_t len = strlen(filecontent);
    if (fwrite(filecontent, 1, len, fp) != len) {
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    return 0;
}

// 验证文件内容
static int verify_file_content(const char *filename, const char *filecontent) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return -1;
    
    char buffer[512];
    size_t len = strlen(filecontent);
    if (fread(buffer, 1, len, fp) != len) {
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    return memcmp(buffer, filecontent, len) == 0 ? 0 : -1;
}

// 客户端上传文件
static int tftp_put_file(const char *filename, uint32_t server_ip, const char *mode, void *user_data) {
    tftp_session_t session = {
        .peer_ip = server_ip,
        .peer_port = TFTP_DEFAULT_PORT,
        .local_port = 0,  // 让系统自动分配
        .block_num = 0,
        .retry_count = 0
    };
    tftp_init_default_options(&session.options);
    
    return tftp_client_put(&session, filename, get_data_cb, user_data);
}

// 客户端下载文件
static int tftp_get_file(const char *filename, uint32_t server_ip, const char *mode) {

// 测试使用OACK
#if 1
    tftp_session_t session = {
        .peer_ip = server_ip,
        .peer_port = TFTP_DEFAULT_PORT,
        .local_port = 0,  // 让系统自动分配
        .block_num = 0,
        .retry_count = 0,
        .options = {
            .block_size = 1024,
            .timeout_ms = TFTP_DEFAULT_TIMEOUT_MS,
            .transfer_size = 0,
            .wait_oack = true,
            .retries = TFTP_DEFAULT_RETRIES
        }
    };
    // tftp_init_default_options(&session.options);
#else
    tftp_session_t session = {
        .peer_ip = server_ip,
        .peer_port = TFTP_DEFAULT_PORT,
        .local_port = 0,  // 让系统自动分配
        .block_num = 0,
        .retry_count = 0,
    };
    tftp_init_default_options(&session.options);
#endif
    
    FILE *fp = fopen(filename, "wb");
    if (!fp) return -1;
    
    int result = tftp_client_get(&session, filename, data_cb, fp);
    fclose(fp);
    
    return result;
}

// 客户端测试
static void test_client(uint32_t server_ip) {
    printf("=== Starting TFTP Client Test ===\n");
    
    if (net_wrapper_init(&client_config) != 0) {
        printf("Client network init failed\n");
        return;
    }
    
    // 测试上传文件
    printf("Testing file upload...\n");
    if (create_test_file(test_upload_filename, test_upload_file_content) != 0) {
        printf("Failed to create test file\n");
        return;
    }

    const char *content = test_upload_file_content;
    
    if (tftp_put_file(test_upload_filename, server_ip, "octet", (void *)&content) == 0) {
        printf("File upload successful\n");
    } else {
        printf("File upload failed\n");
    }
    
    // 测试下载文件
    printf("Testing file download...\n");
    if (tftp_get_file(test_download_filename, server_ip, "octet") == 0) {
        printf("File download successful\n");
        
        if (verify_file_content(test_download_filename, test_download_file_content) == 0) {
            printf("test file content verified success\n");
        } else {
            printf("test file content verification failed\n");
        }
    } else {
        printf("File download failed\n");
    }
    
    printf("=== TFTP Client Test Complete ===\n");
}

// 服务器测试
static void test_server(void) {
    printf("=== Starting TFTP Server Test ===\n");
    
    if (net_wrapper_init(&server_config) != 0) {
        printf("Server network init failed\n");
        return;
    }
    
    printf("TFTP server running...\n");
    printf("Press Ctrl+C to stop the server\n");
    
    while (1) {
        tftp_server_process(read_file_cb, write_file_cb, NULL);
    }
    
    printf("=== TFTP Server Test Complete ===\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage:\n");
        printf("  %s client    - Run TFTP client test\n", argv[0]);
        printf("  %s server    - Run TFTP server test\n", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "client") == 0) {
        test_client(server_config.ip_addr);
    } 
    else if (strcmp(argv[1], "server") == 0) {
        test_server();
    }
    else {
        printf("Invalid argument\n");
        return 1;
    }
    
    return 0;
}