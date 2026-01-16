#  RK3576 Video Streamer

![Status](https://img.shields.io/badge/Status-Developing-yellow)
![Platform](https://img.shields.io/badge/Platform-Rockchip_RK3576-blue)
![Language](https://img.shields.io/badge/Language-C++17-green)

基于 Rockchip RK3576 平台的嵌入式视频推流与录制工具。
项目利用 RK3576 的硬件资源（RGA/MPP/NPU）实现从采集、处理、编码到推流的全链路硬件加速。

##  核心特性

* **硬件全链路**：V4L2 采集 -> RGA 格式转换/缩放 -> MPP H.264 编码，CPU 占用极低。
* **多路分发**：支持同时进行 SRT 网络推流和本地 SD 卡录像。
* **AI 集成**：集成 RKNN (NPU) 运行 YOLOv5/v8 目标检测，支持 OSD 画框。
* **灵活配置**：支持命令行参数启动 (`-s`, `-r`, `-a`) 和 `config.h` 静态配置。
* **音频支持**：ALSA 采集 + FAAC 编码，音视频同步封装 (MPEG-TS)。
* **文件管理**：录像自动按日期分目录存储，支持按时长自动切片。

##  开发进度 (Roadmap)

- [x] **基础链路**
    - [x] V4L2 采集 (YUYV) & DMA-BUF 零拷贝
    - [x] RGA 硬件色彩空间转换 (YUYV -> NV12 / RGB)
    - [x] MPP H.264 硬件编码 (CBR/VBR)
- [x] **功能模块**
    - [x] SRT 网络推流 
    - [x] 本地录像 (MPEG-TS 封装，支持自动分段/I帧对齐)
    - [x] 音频采集与 AAC 编码
- [x] **AI 扩展**
    - [x] RKNN 模型加载与推理 (YOLO)
    - [x] OpenCV/RGA 混合绘制检测框
- [x] **工程化**
    - [x] 命令行参数解析
    - [x] 线程资源管理

## 🛠️ 编译与运行

### 1. 依赖环境
* Rockchip BSP SDK (librga, mpp, rknn-api)
* OpenCV 
* ALSA (libasound)
* SRT (libsrt)

### 2. 编译
```bash
mkdir build && cd build
cmake ..
make -j4