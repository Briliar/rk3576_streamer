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
    //run_convert_test(fd, 1280, 720, 60, "output_1280x720.nv12");
    // run_capture_test(fd, 1280, 720, 60, "output_1280x720.yuv");

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

    cout << "=== 开始采集循环 (按 Ctrl+C 结束) ===" << endl;

    // 4. 循环采集 100 帧
    for (int i = 0; i < 100; ++i) {
        // A. 抓取一帧 (DQBUF)
        // 这一步会阻塞，直到摄像头拍好照片
        int index = wait_and_get_frame(fd);
        
        if (index < 0) {
            cerr << "抓取失败，跳过" << endl;
            continue;
        }

        // B. 这里可以处理图像了！
        // buffers[index].start     -> 图像数据的虚拟地址 (CPU读写)
        // buffers[index].length    -> 图像大小
        // buffers[index].export_fd -> 图像数据的 DMA-BUF (给RGA/MPP用)
        
        cout << "Frame [" << i << "] Index: " << index 
             << " | Size: " << buffers[index].length 
             << " | DMA-FD: " << buffers[index].export_fd << endl;

        // --- TODO: 下一步我们要在这里调用 RGA 库进行转码 ---

        // C. 处理完后，必须把盘子还回去 (QBUF)
        // 如果你不还，4次循环后，驱动手里就没盘子了，程序就会卡死在 DQBUF
        if (return_frame(fd, index) < 0) {
            cerr << "归还 Buffer 失败" << endl;
            break;
        }
    }

    // 5. 结束清理
    stop_capturing(fd);

    // 退出前的清理
    release_buffers(buffers, n_buffers);
    close(fd);
    return 0;
}

