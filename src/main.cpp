#include <iostream>
#include <thread>
#include <chrono>
#include <sys/time.h>
#include <fcntl.h>      // 用于 open 函数
#include <unistd.h>     // 用于 close 函数
#include <sys/ioctl.h>  // 用于 ioctl (核心)
#include <linux/videodev2.h> // V4L2 的标准头文件
#include <cstring>      // 用于 memset
#include "v4l2.h"
#include "rga.h"
#include "mpp_encoder.h"
#include "safe_queue.h"
#include "srt_pusher.h"
#include "ts_muxer.h"
#include "config.h"

using namespace std;

// ---------------------------------------------------------
// 全局变量
// ---------------------------------------------------------
VideoPacketQueue packet_queue(60); // 缓存队列，最多存60帧
bool is_running = true;

// 获取时间戳
uint32_t get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

// ---------------------------------------------------------
// 子线程：SRT 发送线程 (消费者)
// ---------------------------------------------------------
void network_thread_func(string ip, int port, string stream_id) {
    SrtPusher pusher;
    
    // 尝试连接
    if (pusher.connect(ip, port, stream_id) < 0) {
        cerr << ">>[SRT] 线程退出：无法连接服务器" << endl;
        return;
    }
    TsMuxer muxer;
    
    // 定义一个 Lambda 回调函数
    // 当 Muxer 生成了 TS 数据包时，立刻调用 pusher 发送出去
    auto send_callback = [&](void* data, int len) -> int {
        return pusher.send(data, len);
    };

    // 初始化
    if (muxer.init(WIDTH, HEIGHT, FPS, send_callback) < 0) {
        cerr << ">>[TSMUXER] TS Muxer 初始化失败" << endl;
        return;
    }
    VideoPacket packet;
    while (is_running) {
        // 从队列取包 (会阻塞，直到有数据)
        if (packet_queue.pop(packet)) {
            
            // 发送数据
            //pusher.send(packet.data, packet.size);
            muxer.input_frame(packet.data, packet.size, packet.timestamp, packet.is_keyframe);
            // 【重要】释放队列里 malloc 的内存
            free(packet.data);
        }
    }
    muxer.close();
    pusher.close();
}

int main(int argc, char **argv) {
    if (argc != 2)
    {
        printf("Usage:\n");
        printf("%s </dev/video0,1,...>\n", argv[0]);
        return -1;
    }
    const char* dev_name = argv[1];
    int fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);
    //int fd = query_device_info(dev_name);
    // 测试代码
    //run_capture_test(fd, 1280, 720, 120, "output_1280x720.yuv");
    //run_convert_test(fd, 1280, 720, 120, "output_1280x720.nv12");
    //run_encoder_test(fd, 1280, 720,300,"output.h264");


    open_camera(fd, WIDTH, HEIGHT);

    int n_buffers = 8;
    CameraBuffer* buffers = map_buffers(fd, &n_buffers);
    start_capturing(fd, n_buffers);

    // 2. 初始化 RGA
    init_rga();

    // 3. 初始化 MPP
    MppEncoder encoder;
    if (encoder.init(WIDTH, HEIGHT, FPS) < 0) return -1;

    // 4. 启动 SRT 发送线程
    std::thread net_thread(network_thread_func, SERVER_IP, SERVER_PORT, STREAM_KEY);
    net_thread.detach(); // 让它在后台跑

    cout << ">>[SRT] 推流启动: " << SERVER_IP << " -> " << STREAM_KEY << endl;
    
    uint32_t start_time = get_time_ms();
    long long last_log_time = get_time_ms();
    int frame_count = 0;
    int total_bytes = 0;
    // 5. 主循环
    while (is_running) {
        int index = wait_and_get_frame(fd);
        if (index < 0) continue;
        //printf(">> [推流中] index: %d\n", index);
        // A. RGA: V4L2(FD) -> MPP(FD)
        int rga_ret = convert_yuyv_to_nv12(buffers[index].export_fd, encoder.get_input_fd(), WIDTH, HEIGHT);

        return_frame(fd, index);

        if (rga_ret == 0) {
            
            // B. MPP: 编码
            //printf(">> [推流中] 执行编码...\n");
            void* enc_data = nullptr;
            size_t enc_len = 0;
            bool is_key = false; 

            if (encoder.encode_to_memory(&enc_data, &enc_len, &is_key) == 0) {
               // printf(">> [推流中] 编码得到 %zu 字节数据\n", enc_len);
               if (is_key) printf(">>[推流中] 这是一个关键帧 (IDR)\n");
                // C. 推入队列 (非阻塞，极快)
                // 这里发生了内存拷贝 (enc_data -> queue)，但对于 H264 来说数据量很小(几十KB)，不影响性能
                packet_queue.push(enc_data, enc_len, get_time_ms() - start_time, is_key);
                frame_count++;
                total_bytes += enc_len;
                //cout << "." << flush; // 打印心跳
            }
        }
        // 每隔一秒打印一次状态
        long long now = get_time_ms();
        if (now - last_log_time >= 1000) {
            // 计算码率 (Kbps)
            float bitrate_kbps = (total_bytes * 8.0) / 1000.0;

            printf(">> [推流中] FPS: %d | 码率: %.2f Kbps\n", 
                   frame_count, bitrate_kbps);

            // 重置计数器
            last_log_time = now;
            frame_count = 0;
            total_bytes = 0;
        }
    }

    // 清理
    packet_queue.stop();
    stop_capturing(fd);
    // ... release buffers ...
    return 0;
}

