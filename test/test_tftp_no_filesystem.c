#include "tftp.h"
#include "tftpclient.h"
#include "tftpserver.h"
#include "net_wrapper.h"
#include <string.h>

#define TEST_MALLOC(size)       malloc(size)
#define TEST_FREE(ptr)          free(ptr)

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

// 文件存储结构
typedef struct {
    char name[256];
    uint8_t *data;
    size_t size;
} file_entry_t;

// 简单的内存文件系统
#define MAX_FILES 10
static file_entry_t file_system[MAX_FILES];
static size_t file_count = 0;

// 文件操作回调函数
static int read_file_cb(void *user_data, const char *filename, uint8_t *buffer, size_t max_size) {
    for (size_t i = 0; i < file_count; i++) {
        if (strcmp(file_system[i].name, filename) == 0) {
            size_t to_copy = (file_system[i].size < max_size) ? file_system[i].size : max_size;
            memcpy(buffer, file_system[i].data, to_copy);
            return to_copy;
        }
    }
    return -1;
}

static int write_file_cb(void *user_data, const char *filename, const uint8_t *data, size_t size) {
    // 查找是否已存在
    for (size_t i = 0; i < file_count; i++) {
        if (strcmp(file_system[i].name, filename) == 0) {
            // 追加数据
            uint8_t *new_data = realloc(file_system[i].data, file_system[i].size + size);
            if (!new_data) return -1;
            
            memcpy(new_data + file_system[i].size, data, size);
            file_system[i].data = new_data;
            file_system[i].size += size;
            return 0;
        }
    }
    
    // 新文件
    if (file_count >= MAX_FILES) return -1;
    
    file_system[file_count].data = TEST_MALLOC(size);
    if (!file_system[file_count].data) return -1;
    
    strncpy(file_system[file_count].name, filename, sizeof(file_system[file_count].name) - 1);
    memcpy(file_system[file_count].data, data, size);
    file_system[file_count].size = size;
    file_count++;
    
    return 0;
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
    uint8_t **buffer = (uint8_t **)user_data;
    
    // 追加数据到缓冲区
    uint8_t *new_buffer = realloc(*buffer, (*buffer ? strlen((char *)*buffer) : 0) + size + 1);
    if (!new_buffer) return -1;
    
    *buffer = new_buffer;
    memcpy(*buffer + (*buffer ? strlen((char *)*buffer) : 0), data, size);
    (*buffer)[(*buffer ? strlen((char *)*buffer) : 0) + size] = '\0';
    
    return 0;
}

// 创建测试文件
static int create_test_file(const char *filename, const char *filecontent) {
    return write_file_cb(NULL, filename, (const uint8_t *)filecontent, strlen(filecontent));
}

// 验证文件内容
static int verify_file_content(const char *filename, const char *filecontent) {
    for (size_t i = 0; i < file_count; i++) {
        if (strcmp(file_system[i].name, filename) == 0) {
            if (file_system[i].size != strlen(filecontent)) {
                return -1;
            }
            return memcmp(file_system[i].data, filecontent, file_system[i].size) == 0 ? 0 : -1;
        }
    }
    return -1;
}

// 客户端上传文件
static int tftp_put_file(const char *filename, uint32_t server_ip, const char *mode, void *user_data) {
    tftp_session_t session = {
        .peer_ip = server_ip,
        .peer_port = TFTP_DEFAULT_PORT,
        .local_port = 0,
        .block_num = 0,
        .retry_count = 0
    };
    tftp_init_default_options(&session.options);
    
    return tftp_client_put(&session, filename, get_data_cb, user_data);
}

// 客户端下载文件
static int tftp_get_file(const char *filename, uint32_t server_ip, const char *mode) {
    tftp_session_t session = {
        .peer_ip = server_ip,
        .peer_port = TFTP_DEFAULT_PORT,
        .local_port = 0,
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
    
    uint8_t *buffer = NULL;
    int result = tftp_client_get(&session, filename, data_cb, &buffer);
    
    if (result == 0) {
        result = write_file_cb(NULL, filename, buffer, strlen((char *)buffer));
    }
    
    TEST_FREE(buffer);
    return result;
}

// 客户端测试
static void test_client(uint32_t server_ip) {
    NET_LOGI("=== Starting TFTP Client Test ===");
    
    if (net_wrapper_init(&client_config) != 0) {
        NET_LOGE("Client network init failed");
        return;
    }
    
    NET_LOGI("Testing file upload...");
    if (create_test_file(test_upload_filename, test_upload_file_content) != 0) {
        NET_LOGE("Failed to create test file");
        return;
    }

    const char *content = test_upload_file_content;
    
    if (tftp_put_file(test_upload_filename, server_ip, "octet", (void *)&content) == 0) {
        NET_LOGI("File upload successful");
    } else {
        NET_LOGE("File upload failed");
    }
    
    NET_LOGI("Testing file download...");
    if (tftp_get_file(test_download_filename, server_ip, "octet") == 0) {
        NET_LOGI("File download successful");
        
        if (verify_file_content(test_download_filename, test_download_file_content) == 0) {
            NET_LOGI("test file content verified success");
        } else {
            NET_LOGE("test file content verification failed");
        }
    } else {
        NET_LOGE("File download failed");
    }
    
    NET_LOGI("=== TFTP Client Test Complete ===");
}

// 服务器测试
static void test_server(void) {
    NET_LOGI("=== Starting TFTP Server Test ===");
    
    if (net_wrapper_init(&server_config) != 0) {
        NET_LOGE("Server network init failed");
        return;
    }
    
    create_test_file(test_download_filename, test_download_file_content);
    
    NET_LOGI("TFTP server running...");
    NET_LOGI("Press Ctrl+C to stop the server");
    
    while (1) {
        tftp_server_process(read_file_cb, write_file_cb, NULL);
    }
    
    NET_LOGI("=== TFTP Server Test Complete ===");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        NET_LOGI("Usage:");
        NET_LOGI("  %s client    - Run TFTP client test", argv[0]);
        NET_LOGI("  %s server    - Run TFTP server test", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "client") == 0) {
        test_client(server_config.ip_addr);
    } 
    else if (strcmp(argv[1], "server") == 0) {
        test_server();
    }
    else {
        NET_LOGE("Invalid argument");
        return 1;
    }
    
    // 清理内存
    for (size_t i = 0; i < file_count; i++) {
        TEST_FREE(file_system[i].data);
    }
    
    return 0;
}