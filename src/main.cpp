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
    run_camera_encoder_test(fd, 1280, 720,300,"output.h264");
    //Open the camera with specific resolution and frame rate
    
    return 0;
}

