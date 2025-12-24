#include <iostream>
#include <thread>
#include <chrono>
#include <fcntl.h>      // 用于 open 函数
#include <unistd.h>     // 用于 close 函数
#include <sys/ioctl.h>  // 用于 ioctl (核心)
#include <linux/videodev2.h> // V4L2 的标准头文件
#include <cstring>      // 用于 memset
#include "v4l2.h"
#include "rga.h"
#include "mpp_encoder.h"

using namespace std;


int main(int argc, char **argv) {
    if (argc != 2)
    {
        printf("Usage:\n");
        printf("%s </dev/video0,1,...>\n", argv[0]);
        return -1;
    }
    const char* dev_name = argv[1];
    int fd = query_device_info(dev_name);
    //run_capture_test(fd, 1280, 720, 120, "output_1280x720.yuv");
    //run_convert_test(fd, 1280, 720, 120, "output_1280x720.nv12");
    //Open the camera with specific resolution and frame rate
    open_camera(fd, 1280, 720);
    
    int n_buffers = 4; // 想要 4 个
    CameraBuffer* buffers = map_buffers(fd, &n_buffers);
    
    if (!buffers) {
        cerr << "缓冲区初始化失败" << endl;
        close(fd);
        return -1;
    }

    cout << "🎉 资源准备就绪！共 " << n_buffers << " 个缓冲区。" << endl;
    cout << "Buffer[0] 的虚拟地址: " << buffers[0].start << endl;
    cout << "Buffer[0] 的 DMA-FD : " << buffers[0].export_fd << endl;

    if (start_capturing(fd, n_buffers) < 0) {
        release_buffers(buffers, n_buffers);
        close(fd);
        return -1;
    }

    // 2. RGA Init
    init_rga();
    int w = 1280; // 你的 720P
    int h = 720;
    // 3. MPP Init (这里会自动分配好 shared_fd)
    MppContext mpp_ctx;
    if (init_mpp(mpp_ctx, w, h, 30) < 0) {
        return -1;
    }

    FILE* fp = fopen("output.h264", "wb");
    cout << "🚀 开始录制 H.264 (720P)..." << endl;
    
    for (int i = 0; i < 100; ++i) { // 录制 100 帧
        int index = wait_and_get_frame(fd);
        if (index < 0) continue;

        // ===========================================
        // 核心步骤 A: RGA 转换 (FD -> FD)
        // src: V4L2 的 export_fd
        // dst: MPP 的 shared_fd
        // ===========================================
        int ret_rga = convert_yuyv_to_nv12(buffers[index].export_fd, mpp_ctx.shared_fd, w, h);
        
        if (ret_rga == 0) {
            // ===========================================
            // 核心步骤 B: MPP 编码
            // 数据已经在 mpp_ctx.shared_fd 里了，直接编！
            // ===========================================
            encode_frame(mpp_ctx, fp);
            
            cout << "Encoded Frame: " << i << "\r" << flush;
        } else {
            cerr << "RGA 转换失败" << endl;
        }

        return_frame(fd, index);
    }

    // 清理
    cout << endl << "✅ 录制完成" << endl;
    fclose(fp);
    cleanup_mpp(mpp_ctx);
    stop_capturing(fd);

    // 退出前的清理
    release_buffers(buffers, n_buffers);
    close(fd);
    return 0;
}

