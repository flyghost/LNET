#include "tftp.h"
#include "net_wrapper.h"
#include <stdio.h>
#include <string.h>

// 客户端配置（192.168.1.2 → 手动计算大端序值）
static net_config_t client_config = {
    .ip_addr = 0x0201A8C0,    // 存储为小端的0x0201A8C0 → 网络层解析为大端的C0 A8 01 02 (192.168.1.2)
    .netmask = 0x00FFFFFF,    // 255.255.255.0 → 存储为0x00FFFFFF → 解析为FF FF FF 00
    .gateway = 0x0101A8C0,    // 192.168.1.1 → 存储为0x0101A8C0 → 解析为C0 A8 01 01
    .mac_addr = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}  // MAC: 11:22:33:44:55:66
};

// 服务器配置（192.168.1.3 → 手动计算大端序值）
static net_config_t server_config = {
    .ip_addr = 0x0101A8C0,    // 存储为小端的0x0301A8C0 → 网络层解析为大端的C0 A8 01 01 (192.168.1.1)
    .netmask = 0x00FFFFFF,    // 255.255.255.0 → 存储为0x00FFFFFF → 解析为FF FF FF 00
    .gateway = 0x0101A8C0,    // 192.168.1.1 → 存储为0x0101A8C0 → 解析为C0 A8 01 01
    .mac_addr = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}  // MAC: AA:BB:CC:DD:EE:FF
};

// 网络设备操作回调
static void client_rx_callback(uint8_t *buffer, size_t length) {
    NET_LOGD("Client received %zu bytes", length);
}

static void client_tx_callback(uint8_t *buffer, size_t length) {
    NET_LOGD("Client sent %zu bytes", length);
}

static void server_rx_callback(uint8_t *buffer, size_t length) {
    NET_LOGD("Server received %zu bytes", length);
}

static void server_tx_callback(uint8_t *buffer, size_t length) {
    NET_LOGD("Server sent %zu bytes", length);
}

// 客户端测试
void test_tftp_client(void) {
    NET_LOGD("=== Starting TFTP Client Test ===");
    
    net_device_ops_t ops = {
        .tx_callback = client_tx_callback,
        .rx_callback = client_rx_callback
    };
    
    // 初始化网络
    if (net_wrapper_init(&client_config) != 0) {
        NET_LOGD("Client network init failed");
        return;
    }
    
    // 初始化TFTP
    // if (tftp_init() != 0) {
    //     NET_LOGD("TFTP init failed");
    //     return;
    // }
    
    // 测试上传文件
    // NET_LOGD("Testing file upload...");
    // if (tftp_put_file("test.txt", server_config.ip_addr, "octet") == 0) {
    //     NET_LOGD("File upload successful");
    // } else {
    //     NET_LOGD("File upload failed");
    // }
    
    // 测试下载文件
    NET_LOGD("Testing file download...");
    if (tftp_get_file("test.txt", server_config.ip_addr, "octet") == 0) {
        NET_LOGD("File download successful");
    } else {
        NET_LOGD("File download failed");
    }
    
    NET_LOGD("=== TFTP Client Test Complete ===");
}

// 服务器测试
void test_tftp_server(void) {
    NET_LOGD("=== Starting TFTP Server Test ===");
    
    net_device_ops_t ops = {
        .tx_callback = server_tx_callback,
        .rx_callback = server_rx_callback
    };
    
    // 初始化网络
    if (net_wrapper_init(&server_config) != 0) {
        NET_LOGD("Server network init failed");
        return;
    }
    
    // 初始化TFTP
    // if (tftp_init() != 0) {
    //     NET_LOGD("TFTP init failed");
    //     return;
    // }
    
    NET_LOGD("TFTP server running...");
    while (1) {
        tftp_server_process();
        // 在实际应用中，这里应该有其他任务或休眠
    }
    
    NET_LOGD("=== TFTP Server Test Complete ===");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        NET_LOGD("Usage:");
        NET_LOGD("  %s client    - Run TFTP client test", argv[0]);
        NET_LOGD("  %s server    - Run TFTP server test", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "client") == 0) {
        test_tftp_client();
    } 
    else if (strcmp(argv[1], "server") == 0) {
        test_tftp_server();
    }
    else {
        NET_LOGD("Invalid argument");
        return 1;
    }
    
    return 0;
}