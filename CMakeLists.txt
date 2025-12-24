cmake_minimum_required(VERSION 3.10)
project(rk3576_streamer)

# 1. 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. 包含本地头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 3. 添加系统头文件搜索路径 (针对 Rockchip 的特殊位置)
# 板子上的 MPP 头文件通常在 /usr/include/rockchip
include_directories(/usr/include/rockchip)
include_directories(/usr/include/libdrm)

# 4. 查找源文件
file(GLOB SOURCES "src/*.cpp")

# 5. 生成可执行文件
add_executable(rk3576_streamer ${SOURCES})

# =============================================================
# 库查找与链接 (板端原生模式)
# =============================================================

# 查找 MPP 库 (自动在 /usr/lib 下找 librockchip_mpp.so 或 libmpp.so)
find_library(MPP_LIB NAMES rockchip_mpp mpp)
if (NOT MPP_LIB)
    message(WARNING "未找到 MPP 库，尝试直接链接 'rockchip_mpp'")
    set(MPP_LIB rockchip_mpp)
endif()

# 查找 DRM 库
find_library(DRM_LIB NAMES drm)

# 查找 RTMP 库 (前提：你得在板子上 sudo apt install librtmp-dev)
find_library(RTMP_LIB NAMES rtmp)
if (NOT RTMP_LIB)
    message(FATAL_ERROR "未找到 librtmp！请在板子上运行: sudo apt install librtmp-dev")
endif()
# 查找 rga 库
find_library(RGA_LIB NAMES rga)
# 链接所有库
target_link_libraries(rk3576_streamer
    PRIVATE
    ${MPP_LIB}      # 硬件编解码
    ${DRM_LIB}      # Zero-Copy 内存管理
    ${RTMP_LIB}     # 推流
    ${RGA_LIB}
    pthread         # 多线程
    dl              # 动态加载
)

# 编译选项
add_compile_options(-O2 -Wall -g)