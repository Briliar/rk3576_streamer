cmake_minimum_required(VERSION 3.10)
project(rk3576_streamer)

# 1. 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# 2. 包含本地头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 3. 添加系统头文件搜索路径 (针对 Rockchip 的特殊位置)
# 板子上的 MPP 头文件通常在 /usr/include/rockchip
include_directories(/usr/include/rockchip)
include_directories(/usr/include/libdrm)
include_directories(${CMAKE_SOURCE_DIR}/3rdparty/rknn)
include_directories(/usr/include/rkaiq)
find_library(RKNN_LIB rknnrt)
# 查找 FFmpeg 库
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
find_package(ALSA REQUIRED)
find_package(OpenCV REQUIRED)


include_directories(
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    include
)
# 4. 查找源文件
file(GLOB_RECURSE SOURCES "src/*.cpp")

# 5. 生成可执行文件
add_executable(rk3576_streamer ${SOURCES})
# 6. 链接所需的库
target_link_libraries(rk3576_streamer
    PRIVATE
    avformat    # 封装库 (必须在最前)
    avcodec     # 编码库
    avutil      # 工具库 (必须在最后)
    rockchip_mpp    # 硬件编解码
    drm            # Zero-Copy 内存管理
    rga            # RGA 硬件加速
    srt           # SRT 协议支持
    pthread         # 多线程
    dl              # 动态加载
    ALSA::ALSA          # ALSA 音频支持
    faac         # AAC 编码库
    ${RKNN_LIB}  # RKNN 库
    ${OpenCV_LIBS} # OpenCV 库
)

# 编译选项
add_compile_options(-O2 -Wall -g)