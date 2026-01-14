# 🚧 RK3576 Video Streamer (Developing)

![Status](https://img.shields.io/badge/Status-Work_in_Progress-yellow)
![Platform](https://img.shields.io/badge/Platform-Rockchip_RK3576-blue)
![Language](https://img.shields.io/badge/Language-C++17-green)

> ⚠️ **注意 / Warning**
> 
> 本项目目前处于 **开发初期 (WIP)** 阶段。代码结构可能会频繁变动，功能尚未完全稳定。
> This project is currently under active development. APIs are subject to change.

## 📖 项目简介

这是一个基于 Rockchip RK3576 平台的高性能视频推流项目。
旨在实现从摄像头采集、硬件格式转换、硬件编码到网络推流的全链路 **零拷贝 (Zero-Copy)** 处理。

**核心技术栈：**
* **采集**: Linux V4L2 (Video for Linux 2)
* **处理**: Rockchip RGA (2D Raster Graphic Acceleration)
* **编码**: Rockchip MPP (Media Process Platform)
* **推流**: SRT(MediaMTX)

## 开发路线

- [x] **V4L2 基础采集**
    - [x] 支持设备枚举与 Capability 查询
    - [x] 支持 YUYV (YUV 4:2:2) 格式采集
    - [x] 实现 mmap 内存映射与 DMA-BUF 导出
- [x] **RGA 硬件加速**
    - [x] 引入 librga 库
    - [x] 实现 YUYV -> NV12 的硬件格式转换
    - [x] 验证转换结果 (无花屏/绿屏)
- [x] **MPP 硬件编码** 
    - [x] MPP 编码器初始化 (H.264)
    - [x] 实现 NV12 数据输入与 Packet 输出
    - [x] 保存 H.264 裸流文件并播放验证
- [x] **网络推流**
    - [x] 集成 SRT 库 (libsrt)
    - [x] 实现 H.264 NALU 封装与发送
- [x] **音频支持**
    - [x] 集成ALSA与ffmpeg音频编码库
    - [x] 支持AAC格式音频输出

## 🛠️ 编译与运行

### 依赖环境
* Rockchip RK3576 开发板 (Linux)
* `librga-dev`
* `librockchip-mpp-dev`
* `cmake` & `g++`

### 构建步骤
```bash
mkdir build
cd build
cmake ..
make -j4