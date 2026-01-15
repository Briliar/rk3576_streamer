#include "StreamerApp.h"

using namespace std;

// 获取时间戳
static uint32_t get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

StreamerApp::StreamerApp() : m_queue(60) {
    // 初始化状态
    m_is_running = false;
    m_ai_enabled = false; // 默认关闭 AI，可改为 true
}

StreamerApp::~StreamerApp() {
    stop(); // 确保析构时停止一切
}

//初始化
bool StreamerApp::init(const std::string& dev_name, int w, int h,int fps, const std::string& model_path) {
    m_width = w;
    m_height = h;
    m_fps = fps;

    cout << ">>[App] 正在初始化..." << endl;

    // 1. 打开摄像头
    m_camera_fd = open(dev_name.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (m_camera_fd < 0) {
        cerr << ">>[V4L2] 无法打开摄像头设备: " << dev_name << endl;
        return false;
    }
    open_camera(m_camera_fd, m_width, m_height, m_fps);

    // 2. 映射缓冲区
    m_n_buffers = 4;
    m_camera_buffers = map_buffers(m_camera_fd, &m_n_buffers);
    if (!m_camera_buffers) {
        cerr << ">>[V4L2] 映射缓冲区失败" << endl;
        return false;
    }
    start_capturing(m_camera_fd, m_n_buffers);
    cout << ">>[V4L2] 摄像头初始化成功" << endl;

    // 3. 初始化 RGA
    if (init_rga() < 0) {
        cerr << ">>[RGA] 初始化失败" << endl;
        return false;
    }
    cout << ">>[RGA] 初始化成功" << endl;

    // 4. 初始化 MPP 编码器
    m_encoder = new MppEncoder();
    if (m_encoder->init(m_width, m_height, m_fps) < 0) {
        cerr << ">>[MPP] 编码器初始化失败" << endl;
        return false;
    }
    cout << ">>[MPP] 编码器初始化成功" << endl;

    // 5. 初始化 AI 模型
    m_detector = new YoloDetector();
    if (m_detector->init(model_path.c_str()) != 0) {
        cerr << ">>[Yolo] AI模型初始化失败，检查模型路径: " << model_path << endl;
        return false;
    }
    cout << ">>[Yolo] AI模型加载成功: " << model_path << endl;

    // 6. 分配专用内存池
    m_ai_buf = malloc(640 * 640 * 3); // 给 AI 用
    m_draw_buf = malloc(m_width * m_height * 3); // 给 OpenCV 画图用
    if (!m_ai_buf || !m_draw_buf) {
        cerr << ">>[内存] 专用内存池分配失败" << endl;
        return false;
    }
    cout << ">>[内存] 专用内存池分配成功" << endl;

    cout << ">>[App] 初始化完成" << endl;
    return true;
}

//启动
void StreamerApp::start(const std::string& server_ip, int server_port, const std::string& stream_id) {
    if (m_is_running) {
        cerr << ">>[App] 已在运行中，无法重复启动" << endl;
    }
    m_is_running = true;

    cout << ">>[App] 启动推流线程..." << endl;
    // 启动网络推流线程
    m_net_thread = new std::thread(&StreamerApp::networkWorker, this, server_ip, server_port, stream_id);

    cout << ">>[App] 启动音频采集线程..." << endl;
    // 启动音频采集线程
    m_audio_thread = new std::thread(&StreamerApp::audioWorker, this);

}

//停止
void StreamerApp::stop() {
    
    if (m_camera_fd == -1 && m_net_thread == nullptr) {
        return; 
    }
    
    cout << ">>[App] 正在停止..." << endl;
    m_is_running = false;

   // 1. 唤醒队列阻塞，让网络线程能退出
    m_queue.stop();

    // 2. 等待线程退出 (Join)
    if (m_net_thread && m_net_thread->joinable()) {
        m_net_thread->join();
        delete m_net_thread; 
        m_net_thread = nullptr;
        cout << ">>[App] 网络线程退出" << endl;
    }

    if (m_audio_thread && m_audio_thread->joinable()) {
        m_audio_thread->join();
        delete m_audio_thread;
        m_audio_thread = nullptr;
        cout << ">>[App] 音频线程退出" << endl;
    }

    // 3. 清理残留数据
    m_queue.clear();

    // 4. 释放所有硬件资源
    releaseResources();

    cout << ">>[App] 已完全停止" << endl;
}

//资源释放
void StreamerApp::releaseResources() {
    cout << ">>[App] 释放资源..." << endl;

    // 释放堆内存
    if (m_ai_buf) { free(m_ai_buf); m_ai_buf = nullptr; }
    if (m_draw_buf) { free(m_draw_buf); m_draw_buf = nullptr; }

    // 释放 AI
    if (m_detector) { delete m_detector; m_detector = nullptr; }

    // 释放 MPP (要在 V4L2 之前)
    if (m_encoder) { delete m_encoder; m_encoder = nullptr; }

    // 释放 V4L2
    if (m_camera_fd > 0) {
        stop_capturing(m_camera_fd);
        release_buffers(m_camera_buffers, m_n_buffers);

        // 归还内核缓冲
        struct v4l2_requestbuffers req = {0};
        req.count = 0;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        ioctl(m_camera_fd, VIDIOC_REQBUFS, &req);
        close(m_camera_fd);
        m_camera_fd = -1;
    }
}

// 主视频循环
void StreamerApp::runMainLoop() {
    cout << ">>[App] 启动主视频循环..." << endl;

    long long last_log_time = get_time_ms();
    long long start_pts_base = get_time_ms();
    int frame_count = 0;
    int total_bytes = 0;

    
    while (m_is_running) {
        // 1. 获取 V4L2 帧
        int index = wait_and_get_frame(m_camera_fd);
        if (index < 0) continue; // 超时或错误

        int src_fd = m_camera_buffers[index].export_fd; // V4L2 (YUYV)
        int dst_fd = m_encoder->get_input_fd();         // MPP (NV12)

        // 2. 根据开关处理逻辑
        if (m_ai_enabled) {
            // --- AI 开启模式 ---
            
            // A. 转 640x640 RGB 给 AI
            rga_convert(nullptr, src_fd, m_width, m_height, RK_FORMAT_YUYV_422,
                       m_ai_buf, -1, 640, 640, RK_FORMAT_RGB_888);

            // B. 推理
            std::vector<Object> objects = m_detector->detect(m_ai_buf);

            // C. 转 720P RGB 准备画图
            rga_convert(nullptr, src_fd, m_width, m_height, RK_FORMAT_YUYV_422,
                       m_draw_buf, -1, m_width, m_height, RK_FORMAT_RGB_888);

            // D. OpenCV 画框
            cv::Mat frame_rgb(m_height, m_width, CV_8UC3, m_draw_buf);
            float scale_x = (float)m_width / 640.0;
            float scale_y = (float)m_height / 640.0;

            for (auto& obj : objects) {
                int x = obj.x * scale_x;
                int y = obj.y * scale_y;
                int w = obj.w * scale_x;
                int h = obj.h * scale_y;
                
                // 简单边界保护
                x = std::max(0, x); y = std::max(0, y);
                if (x + w > m_width) w = m_width - x;
                if (y + h > m_height) h = m_height - y;

                cv::rectangle(frame_rgb, cv::Rect(x, y, w, h), cv::Scalar(0, 255, 0), 2);
                
                // 显示 Label
                string label = obj.label + " " + to_string(obj.prob).substr(0, 3);
                cv::putText(frame_rgb, label, cv::Point(x, y - 5), 
                            cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
            }

            // E. RGB 转回 NV12 给编码器
            rga_convert(m_draw_buf, -1, m_width, m_height, RK_FORMAT_RGB_888,
                       nullptr, dst_fd, m_width, m_height, RK_FORMAT_YCbCr_420_SP);

        } else {
            // --- AI 关闭模式 (直通) ---
            
            // 直接 YUYV -> NV12 (FD to FD, 零拷贝)
            rga_convert(nullptr, src_fd, m_width, m_height, RK_FORMAT_YUYV_422,
                       nullptr, dst_fd, m_width, m_height, RK_FORMAT_YCbCr_420_SP);
        }

        // 3. 归还 V4L2 帧
        return_frame(m_camera_fd, index);

        // 4. MPP 编码
        void* enc_data = nullptr;
        size_t enc_len = 0;
        bool is_key = false;

        if (m_encoder->encode_to_memory(&enc_data, &enc_len, &is_key) == 0) {
            // 推入队列
            m_queue.push(enc_data, enc_len, get_time_ms() - start_pts_base, is_key, MEDIA_VIDEO);
            
            frame_count++;
            total_bytes += enc_len;
        }

        // 5. 打印状态
        long long now = get_time_ms();
        if (now - last_log_time >= 1000) {
            float bitrate = (total_bytes * 8.0) / 1000.0;
            printf(">>[推流中] %s | FPS: %d | 码率: %.2f Kbps\n", 
                   m_ai_enabled ? "AI ON " : "AI OFF", 
                   frame_count, bitrate);
            
            last_log_time = now;
            frame_count = 0;
            total_bytes = 0;
        }
    }
}

//网络线程
void StreamerApp::networkWorker(std::string ip, int port, std::string stream_id) {
    SrtPusher pusher;
    if (pusher.connect(ip, port, stream_id) < 0) {
        cerr << ">>[SRT] 连接失败，网络线程退出" << endl;
        return;
    }

    TsMuxer muxer;
    auto send_cb = [&](void* d, int l) { return pusher.send(d, l); };
    muxer.init(m_width, m_height, m_fps, 44100, 2, send_cb);

    MediaPacket pkt;
    while (m_is_running) {
        if (m_queue.pop(pkt)) { // 阻塞等待
            if (pkt.type == MEDIA_VIDEO) {
                muxer.write_video(pkt.data, pkt.size, pkt.timestamp, pkt.is_keyframe);
            } else if (pkt.type == MEDIA_AUDIO) {
                muxer.write_audio(pkt.data, pkt.size, pkt.timestamp);
            }
            free(pkt.data); // 释放内存
        }
    }
    // 退出清理
    muxer.close();
    pusher.close();
}


// 音频线程逻辑
void StreamerApp::audioWorker() {
    AudioCapture capture;
    AudioEncoder encoder;

    if (capture.init("default", 44100, 2) < 0 || encoder.init(44100, 2) < 0) {
        cerr << ">>[Audio] 初始化失败" << endl;
        return;
    }

    std::vector<char> pcm_buf(capture.get_buffer_size());
    std::vector<uint8_t> aac_buf;
    uint64_t total_samples = 0;

    std::cout << ">>[ALSA] 音频采集线程已启动" << std::endl;
    while (m_is_running) {
        int frames = capture.read_frame(pcm_buf.data());
        if (frames > 0) {
            int len = encoder.encode(pcm_buf.data(), aac_buf);
            if (len > 0) {
                uint32_t pts = (uint32_t)(total_samples * 1000 / 44100);
                total_samples += 1024;
                m_queue.push(aac_buf.data(), len, pts, false, MEDIA_AUDIO);
            }
        }
    }
}

// 设置 AI 开关
void StreamerApp::setAiEnabled(bool enable) {
    m_ai_enabled = enable;
    cout << ">>[CMD] AI 切换为: " << (enable ? "ON" : "OFF") << endl;
}