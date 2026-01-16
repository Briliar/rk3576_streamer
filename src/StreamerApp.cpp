#include "StreamerApp.h"


using namespace std;

// 获取时间戳
static uint32_t get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

StreamerApp::StreamerApp() :
            m_queue(60, "StreamQueue"),       // 推流队列：叫 "StreamQueue"
            m_record_queue(60, "RecordQueue") // 录像队列：叫 "RecordQueue" 
{
    // 初始化状态
    m_is_running = false;
}

StreamerApp::~StreamerApp() {
    stop(); // 确保析构时停止一切
}

//初始化
bool StreamerApp::init(const AppConfig& config) {
    m_config = config;

    cout << ">>[App] 正在初始化..." << endl;
    printf(">>[App] 参数: %dx%d @ %dFPS | Dev: %s\n", 
           m_config.width, m_config.height, m_config.fps, m_config.dev_name.c_str());

    // 1. 打开摄像头
    m_camera_fd = open(m_config.dev_name.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (m_camera_fd < 0) {
        cerr << ">>[V4L2] 无法打开摄像头设备: " << m_config.dev_name << endl;
        return false;
    }
    open_camera(m_camera_fd, m_config.width, m_config.height, m_config.fps);

    // 2. 映射缓冲区
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
    if (m_encoder->init(m_config.width, m_config.height, m_config.fps) < 0) {
        cerr << ">>[MPP] 编码器初始化失败" << endl;
        return false;
    }
    cout << ">>[MPP] 编码器初始化成功" << endl;

    // 5. 初始化 AI 模型
    m_detector = new YoloDetector();
    if (m_detector->init(m_config.model_path.c_str()) != 0) {
        cerr << ">>[Yolo] AI模型初始化失败，检查模型路径: " << m_config.model_path << endl;
        return false;
    }
    cout << ">>[Yolo] AI模型加载成功: " << m_config.model_path << endl;

    // 6. 分配专用内存池
    m_ai_buf = malloc(640 * 640 * 3); // 给 AI 用
    m_draw_buf = malloc(m_config.width * m_config.height * 3); // 给 OpenCV 画图用
    if (!m_ai_buf || !m_draw_buf) {
        cerr << ">>[内存] 专用内存池分配失败" << endl;
        return false;
    }
    cout << ">>[内存] 专用内存池分配成功" << endl;

    cout << ">>[App] 初始化完成" << endl;
    return true;
}

//启动
void StreamerApp::start( ) {
    if (m_is_running) {
        cerr << ">>[App] 已在运行中，无法重复启动" << endl;
    }

    m_is_running = true;

    if (m_config.enable_stream) {
        cout << ">>[App] 启动推流线程..." << endl;
        // 启动网络推流线程
        m_net_thread = new std::thread(&StreamerApp::networkWorker, this);
    }
    if(m_config.enable_record) {
        cout << ">>[App] 启动本地录像线程..." << endl;
        // 启动本地录像线程
        m_record_thread = new std::thread(&StreamerApp::recordWorker, this);
    }
    if (m_config.enable_stream || m_config.enable_record) {
        cout << ">>[App] 启动音频采集线程..." << endl;
        // 启动音频采集线程
        m_audio_thread = new std::thread(&StreamerApp::audioWorker, this);
    }
}

//停止
void StreamerApp::stop() {
    
    if (m_camera_fd == -1 && m_net_thread == nullptr) {
        return; 
    }
    
    cout << ">>[App] 正在停止..." << endl;
    m_is_running = false;

    // ============================================================
    // 1. 清理网络推流线程
    // ============================================================
    if (m_net_thread) {
        // A. 唤醒队列阻塞 
        m_queue.stop();

        // B. 等待线程退出
        if (m_net_thread->joinable()) {
            m_net_thread->join();
        }
        delete m_net_thread;
        m_net_thread = nullptr;
        cout << ">>[App] 网络线程退出" << endl;
    }
    // C. 线程退出后，清理队列里残留的未发送数据 
    m_queue.clear();

    // ============================================================
    // 2. 清理本地录像线程 (新增)
    // ============================================================
    if (m_record_thread) {
        // A. 唤醒队列
        m_record_queue.stop();

        // B. 等待线程 (recordWorker 会执行 file_out.close 保存文件)
        if (m_record_thread->joinable()) {
            m_record_thread->join();
        }
        delete m_record_thread;
        m_record_thread = nullptr;
        cout << ">>[App] 录像线程退出 (文件已封包)" << endl;
    }
    // C. 清理残留数据
    m_record_queue.clear();

    // ============================================================
    // 3. 清理音频采集线程
    // ============================================================
    if (m_audio_thread) {
        if (m_audio_thread->joinable()) {
            m_audio_thread->join();
        }
        delete m_audio_thread;
        m_audio_thread = nullptr;
        cout << ">>[App] 音频线程退出" << endl;
    };

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
        if (m_config.enable_ai) {
            // --- AI 开启模式 ---
            
            // A. 转 640x640 RGB 给 AI
            rga_convert(nullptr, src_fd, m_config.width, m_config.height, RK_FORMAT_YUYV_422,
                       m_ai_buf, -1, 640, 640, RK_FORMAT_RGB_888);

            // B. 推理
            std::vector<Object> objects = m_detector->detect(m_ai_buf);

            // C. 转 720P RGB 准备画图
            rga_convert(nullptr, src_fd, m_config.width, m_config.height, RK_FORMAT_YUYV_422,
                       m_draw_buf, -1, m_config.width, m_config.height, RK_FORMAT_RGB_888);

            // D. OpenCV 画框
            cv::Mat frame_rgb(m_config.height, m_config.width, CV_8UC3, m_draw_buf);
            float scale_x = (float)m_config.width / 640.0;
            float scale_y = (float)m_config.height / 640.0;

            for (auto& obj : objects) {
                int x = obj.x * scale_x;
                int y = obj.y * scale_y;
                int w = obj.w * scale_x;
                int h = obj.h * scale_y;
                
                // 简单边界保护
                x = std::max(0, x); y = std::max(0, y);
                if (x + w > m_config.width) w = m_config.width - x;
                if (y + h > m_config.height) h = m_config.height - y;

                cv::rectangle(frame_rgb, cv::Rect(x, y, w, h), cv::Scalar(0, 255, 0), 2);
                
                // 显示 Label
                string label = obj.label + " " + to_string(obj.prob).substr(0, 3);
                cv::putText(frame_rgb, label, cv::Point(x, y - 5), 
                            cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
            }

            // E. RGB 转回 NV12 给编码器
            rga_convert(m_draw_buf, -1, m_config.width, m_config.height, RK_FORMAT_RGB_888,
                       nullptr, dst_fd, m_config.width, m_config.height, RK_FORMAT_YCbCr_420_SP);

        } else {
            // --- AI 关闭模式 (直通) ---
            // 直接 YUYV -> NV12 (FD to FD, 零拷贝)
            rga_convert(nullptr, src_fd, m_config.width, m_config.height, RK_FORMAT_YUYV_422,
                       nullptr, dst_fd, m_config.width, m_config.height, RK_FORMAT_YCbCr_420_SP);
        }

        // 3. 归还 V4L2 帧
        return_frame(m_camera_fd, index);

        // 4. MPP 编码
        void* enc_data = nullptr; size_t enc_len = 0; bool is_key = false;

        if (m_encoder->encode_to_memory(&enc_data, &enc_len, &is_key) == 0) {
            long long pts = get_time_ms() - start_pts_base;
            //  分支 A: 处理推流 
            if (m_config.enable_stream) {
                // 必须 malloc 复制一份，因为 enc_data 下一帧会被覆盖
                void* data_net = malloc(enc_len);
                if (data_net) {
                    memcpy(data_net, enc_data, enc_len);
                    m_queue.push(data_net, enc_len, pts, is_key, MEDIA_VIDEO);
                }
            }

            //  分支 B: 处理录像 
            if (m_config.enable_record) {
                // 必须再 malloc 复制一份，这是给录像线程专用的
                void* data_rec = malloc(enc_len);
                if (data_rec) {
                    memcpy(data_rec, enc_data, enc_len);
                    m_record_queue.push(data_rec, enc_len, pts, is_key, MEDIA_VIDEO);
                }
            }
            frame_count++;
            total_bytes += enc_len;
        }

        // 5. 打印状态
        long long now = get_time_ms();
        if (now - last_log_time >= 1000) {
            float bitrate_kbps = (total_bytes * 8.0) / 1000.0;
            
            // 构建动态状态字符串
            std::string status_str = "";
            
            // 检查推流
            if (m_config.enable_stream) status_str += "[SRT:ON] ";
            else                        status_str += "[SRT:--] ";

            // 检查录像
            if (m_config.enable_record) status_str += "[REC:ON] ";
            else                        status_str += "[REC:--] ";

            // 检查AI
            if (m_config.enable_ai)     status_str += "[AI:ON]";
            else                        status_str += "[AI:--]";

            
            printf(">> %s | 帧率: %d | 码率: %.2f Kbps\n", 
                   status_str.c_str(), 
                   frame_count, 
                   bitrate_kbps);
            last_log_time = now;
            frame_count = 0;
            total_bytes = 0;
        }
    }
}

//网络线程
void StreamerApp::networkWorker() {
    SrtPusher pusher;
    if (pusher.connect(m_config.ip, m_config.port, m_config.stream_id) < 0) {
        cerr << ">>[SRT] 连接失败，网络线程退出" << endl;
        return;
    }

    TsMuxer muxer;
    auto send_cb = [&](void* d, int l) { return pusher.send(d, l); };
    muxer.init(m_config.width, m_config.height, m_config.fps, 44100, 2, send_cb);

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
                if (m_config.enable_stream) {
                void* d = malloc(len);
                memcpy(d, aac_buf.data(), len);
                m_queue.push(d, len, pts, false, MEDIA_AUDIO);
                }
                if (m_config.enable_record) {
                void* d = malloc(len);
                memcpy(d, aac_buf.data(), len);
                m_record_queue.push(d, len, pts, false, MEDIA_AUDIO);
                }
            }
        }
    }
}

void StreamerApp::recordWorker() {
    TsMuxer muxer;
    std::ofstream file_out;
    std::string current_file_path;
    
    // 上次切片的时间戳
    long long last_segment_time = 0; 

    // --- Muxer 回调函数 ---
    // TsMuxer 封装好数据包后，会调用这个函数把数据写进 ofstream
    auto write_callback = [&](void* data, int len) {
        if (file_out.is_open()) {
            file_out.write((char*)data, len);
        }
        return len;
    };

    // --- 初始化 Muxer ---
    // 使用 m_config 中的参数
    muxer.init(m_config.width, m_config.height, m_config.fps, 44100, 2, write_callback);

    MediaPacket pkt;
    printf(">>[REC] 录像线程启动 | 存储目录: %s/ | 分段: %d分钟\n", 
           m_config.record_dir.c_str(), m_config.segment_ms / 60000);
    printf(">>[REC] 等待关键帧(I-Frame)以开始录制...\n");

    while (m_is_running) {
        // 从录像队列取出数据包
        if (m_record_queue.pop(pkt)) {
            long long now = get_time_ms();

            // =========================================================
            //  逻辑 1: 判断是否需要关闭旧文件 (切片)
            // =========================================================
            // 条件：文件已打开 && 时间超过设定值(默认5分钟) && 当前是关键帧
            bool is_time_up = (now - last_segment_time > m_config.segment_ms);
            
            if (file_out.is_open() && is_time_up && pkt.is_keyframe) {
                file_out.close();
                printf(">>[REC] 文件切片完成: %s\n", current_file_path.c_str());
            }

            // =========================================================
            //  逻辑 2: 判断是否需要创建新文件
            // =========================================================
            // 条件：文件未打开 (或刚被关闭) && 当前是关键帧
            // 必须由关键帧开始，否则播放器会花屏或报错
            if (!file_out.is_open()) {
                if (pkt.is_keyframe) {
                    current_file_path = generateFileName();
                    
                    if (!current_file_path.empty()) {
                        file_out.open(current_file_path, std::ios::binary);
                        
                        if (file_out.is_open()) {
                            last_segment_time = now;
                            printf(">>[REC] 开始录制新文件: %s\n", current_file_path.c_str());
                        } else {
                            perror(">>[REC] 无法打开文件写入");
                        }
                    }
                } else {
                    // 如果文件没开，且当前帧不是关键帧，这帧数据必须丢弃
                    // printf(">>[REC] 丢弃非关键帧...\n");
                    free(pkt.data);
                    continue; // 跳过后续写入
                }
            }

            // =========================================================
            //  逻辑 3: 写入数据
            // =========================================================
            if (file_out.is_open()) {
                if (pkt.type == MEDIA_VIDEO) {
                    muxer.write_video(pkt.data, pkt.size, pkt.timestamp, pkt.is_keyframe);
                } else if (pkt.type == MEDIA_AUDIO) {
                    muxer.write_audio(pkt.data, pkt.size, pkt.timestamp);
                }
            }

            // 重要：消费完队列里的数据后，必须释放内存
            // 这个 malloc 是在 runMainLoop 里分配的
            free(pkt.data);
        } else {
            // 队列为空，短暂休眠避免空转
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    // --- 退出清理 ---
    if (file_out.is_open()) {
        file_out.close();
        printf(">>[REC] 录像结束，文件已保存\n");
    }
    muxer.close();
    
    // 清理队列中剩余未处理的包，防止内存泄漏
    m_record_queue.clear();
}

// 辅助函数：生成文件名并自动创建目录
// 格式：video/20260116/183005.ts
std::string StreamerApp::generateFileName() {
    // 1. 获取当前时间
    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);

    char path_buf[256];
    
    // 2. 检查并创建根目录 (从 m_config 读取)
    if (access(m_config.record_dir.c_str(), F_OK) == -1) {
        // 0777 表示最高权限，允许读写执行
        if (mkdir(m_config.record_dir.c_str(), 0777) != 0) {
            perror(">>[REC] 创建根目录失败");
            return ""; 
        }
    }

    // 3. 生成日期子目录 (例如: video/20260116)
    // %Y=年, %m=月, %d=日
    std::string date_dir = m_config.record_dir + "/";
    char date_str[32];
    strftime(date_str, sizeof(date_str), "%Y%m%d", now);
    date_dir += date_str;

    // 4. 检查并创建日期子目录
    if (access(date_dir.c_str(), F_OK) == -1) {
        if (mkdir(date_dir.c_str(), 0777) != 0) {
            perror(">>[REC] 创建日期目录失败");
            return "";
        }
    }

    // 5. 生成最终文件名 (例如: 183005.ts)
    // %H=时, %M=分, %S=秒
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H%M%S.ts", now);

    // 6. 拼接全路径: video/20260116/183005.ts
    std::string full_path = date_dir + "/" + time_str;
    return full_path;
}