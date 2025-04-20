cmake_minimum_required(VERSION 3.10)
project(mempool_network LANGUAGES C)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 添加 net_device 子目录（会自动包含其 3rd/mempool）
add_subdirectory(net_device)

# 添加 tftp 相关的所有源文件
set(TFTP_SOURCES
    src/tftp.c
    src/tftp_server.c  # 确保包含所有必要的源文件
    src/tftp_client.c  # 如果有的话
)

# 编译 tftp 库（包含所有相关源文件）
add_library(tftp ${TFTP_SOURCES})

# 设置头文件目录
target_include_directories(tftp PUBLIC include)

# 设置依赖关系
target_link_libraries(tftp PUBLIC net_device)

# 编译 net_wraper 库
add_library(net_wraper src/net_wraper.c)
target_include_directories(net_wraper PUBLIC include)
target_link_libraries(net_wraper PUBLIC net_device)

# 添加测试程序
# add_executable(test_tftp test/test_tftp.c)
add_executable(test_tftp test/test_tftp_no_filesystem.c)
target_link_libraries(test_tftp PRIVATE net_device net_wraper tftp)

# 启用测试
enable_testing()
add_test(NAME tftp_test COMMAND test_tftp)