#pragma once
#include <string>   
#include <thread>   
#include <atomic>   
#include <vector>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/time.h>
#include <cstring>
#include <csignal>
#include <sys/stat.h>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
//依赖库
#include "video/v4l2.h"
#include "video/rga.h"
#include "video/mpp_encoder.h"
#include "safe_queue.h"
#include "network/srt_pusher.h"
#include "network/ts_muxer.h"
#include "audio/audio_capture.h"
#include "audio/audio_encoder.h"
#include "yolov8/YoloDetector.h"
#include <opencv2/opencv.hpp> // OpenCV 头文件

#include "config.h"

class StreamerApp {
public:
    StreamerApp();
    ~StreamerApp();
    // 初始化
    bool init(const AppConfig& config);
    // 启动
    void start();
    // 停止
    void stop();
    // 主循环 (阻塞)
    void runMainLoop();
    // 发出停止信号 (非阻塞)
    void signalStop() { m_is_running = false; }
private:
    // 网络推流线程函数
    void networkWorker();
    // 音频采集线程函数
    void audioWorker();
    //本地录像线程函数
    void recordWorker();
    //生成录像文件名
    std::string generateFileName();
    // 统一资源释放 (被 stop 和 析构函数调用)
    void releaseResources();
private:
    // --- 1. 状态控制 ---
    AppConfig m_config;
    std::atomic<bool> m_is_running;   // 全局运行开关

    // --- 2. 核心数据结构 ---
    MediaPacketQueue m_queue;         // 音视频包缓存队列
    MediaPacketQueue m_record_queue;   // 录像专用队列
    // --- 3. 硬件/算法对象指针 ---
    // 使用指针是为了控制初始化时机 (init 时才 new)
    MppEncoder* m_encoder  = nullptr;
    YoloDetector* m_detector = nullptr;

    // --- 4. 摄像头相关 ---
    int           m_src_width;
    int           m_src_height;
    int           m_src_format; // RK_FORMAT_YUYV_422 或 RK_FORMAT_YCbCr_420_SP
    int           m_camera_fd = -1;
    CameraBuffer* m_camera_buffers = nullptr; // V4L2 映射出的内存数组
    int           m_n_buffers = 4;            // 缓冲区数量

    // --- 5. 线程句柄 ---
    std::thread* m_net_thread   = nullptr;
    std::thread* m_audio_thread = nullptr;
    std::thread* m_record_thread = nullptr;

    // --- 6. 专用内存池 (避免循环内 malloc) ---
    void* m_ai_buf   = nullptr; // 给 AI 用的 (640x640 RGB)
    void* m_draw_buf = nullptr; // 给 OpenCV 画图用的 (1280x720 RGB)

};