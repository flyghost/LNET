cmake_minimum_required(VERSION 3.10)
project(mempool_network LANGUAGES C)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 添加net_device子目录（会自动包含其3rd/mempool）
add_subdirectory(net_device)

# 添加主项目源文件
add_library(net_wraper src/net_wraper.c)
add_library(tftp src/tftp.c)

# 设置头文件目录
target_include_directories(net_wraper PUBLIC include)
target_include_directories(tftp PUBLIC include)

# 设置依赖关系
target_link_libraries(net_wraper PUBLIC net_device)
target_link_libraries(tftp PUBLIC net_device)

# 添加测试程序
add_executable(test_tftp test/test_tftp.c)
target_link_libraries(test_tftp net_device net_wraper tftp)

# 启用测试
enable_testing()
add_test(NAME tftp_test COMMAND test_tftp)