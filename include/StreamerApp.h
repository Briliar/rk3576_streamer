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

class StreamerApp {
public:
    StreamerApp();
    ~StreamerApp();

    /**
     * @brief 初始化系统资源 (摄像头, 编码器, AI模型, 内存池)
     * @param dev_name 摄像头设备节点 (如 "/dev/video0")
     * @param w        采集宽度
     * @param h        采集高度
     * @param model_path YOLO 模型文件路径
     * @return true 成功, false 失败
     */
    bool init(const std::string& dev_name, int w, int h, int fps, const std::string& model_path);
    /**
     * @brief 启动后台工作线程 (网络推送、音频采集)
     * @param ip        推流服务器 IP
     * @param port      推流服务器端口
     * @param stream_id 推流 ID
     */
    void start(const std::string& ip, int port, const std::string& stream_id);

    /**
     * @brief 停止运行
     * 1. 设置退出标志
     * 2. 唤醒并等待所有线程退出
     * 3. 释放硬件资源
     */
    void stop();

    /**
     * @brief 运行主视频循环 (阻塞函数)
     * 包含: V4L2采集 -> RGA处理 -> (AI检测) -> MPP编码 -> 推入队列
     * 注意：这个函数会卡住主线程，直到 stop() 被调用
     */
    void runMainLoop();

    // ------------------------------------------
    // 【控制指令】
    // ------------------------------------------

    /**
     * @brief 信号处理专用停止接口
     */
    void signalStop() { m_is_running = false; }

    /**
     * @brief 动态开关 AI 检测功能
     * @param enable true=开启, false=关闭
     */
    void setAiEnabled(bool enable);
private:
    // 网络推流线程函数
    void networkWorker(std::string ip, int port, std::string stream_id);

    // 音频采集线程函数
    void audioWorker();
    // 统一资源释放 (被 stop 和 析构函数调用)
    void releaseResources();
private:
    // --- 1. 状态控制 ---
    std::atomic<bool> m_is_running;   // 全局运行开关
    std::atomic<bool> m_ai_enabled;   // AI 功能开关

    // --- 2. 核心数据结构 ---
    MediaPacketQueue m_queue;         // 音视频包缓存队列

    // --- 3. 硬件/算法对象指针 ---
    // 使用指针是为了控制初始化时机 (init 时才 new)
    MppEncoder* m_encoder  = nullptr;
    YoloDetector* m_detector = nullptr;

    // --- 4. 摄像头相关 ---
    int           m_camera_fd = -1;
    CameraBuffer* m_camera_buffers = nullptr; // V4L2 映射出的内存数组
    int           m_n_buffers = 0;            // 缓冲区数量

    // --- 5. 线程句柄 ---
    std::thread* m_net_thread   = nullptr;
    std::thread* m_audio_thread = nullptr;

    // --- 6. 专用内存池 (避免循环内 malloc) ---
    void* m_ai_buf   = nullptr; // 给 AI 用的 (640x640 RGB)
    void* m_draw_buf = nullptr; // 给 OpenCV 画图用的 (1280x720 RGB)

    // --- 7. 配置参数 ---
    int m_width  = 1280;
    int m_height = 720;
    int m_fps    = 30;
};