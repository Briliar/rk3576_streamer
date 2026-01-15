#include <iostream>
#include <thread>
#include <chrono>
#include <fcntl.h>      // 用于 open 函数
#include <unistd.h>     // 用于 close 函数
#include <sys/ioctl.h>  // 用于 ioctl (核心)
#include <linux/videodev2.h> // V4L2 的标准头文件
#include <cstring>      // 用于 memset
#include <sys/mman.h> // 用于 mmap
#include <vector>
#include <sys/select.h>
#include <sys/time.h>
#include "video/v4l2.h"


using namespace std;

int query_device_info(const char* dev_name) {

    int fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
        perror("无法打开摄像头");
        return -1;
    }
    cout << "成功打开设备: " << dev_name << " (fd=" << fd << ")" << endl;

    // 2. 查询设备能力 (Capability)
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    
    // ioctl 是“Input/Output Control”的缩写，是控制设备的万能函数
    // VIDIOC_QUERYCAP = Video Input/Output Control Query Capability
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("查询设备能力失败");
        close(fd);
        return -1;
    }

    cout << "---------------------------------" << endl;
    cout << "驱动名称 (Driver): " << cap.driver << endl;
    cout << "设备名称 (Card):   " << cap.card << endl;
    cout << "总线信息 (Bus):    " << cap.bus_info << endl;
    cout << "版本号 (Version):  " << ((cap.version >> 16) & 0xFF) << "." 
                                  << ((cap.version >> 8) & 0xFF) << endl;

    // 检查是否是视频采集设备
    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        cout << ">> 这是一个视频采集设备" << endl;
    }
    if (cap.capabilities & V4L2_CAP_STREAMING) {
        cout << ">> 支持流媒体 (Streaming) I/O" << endl;
    }

    // 3. 枚举支持的像素格式
    cout << "---------------------------------" << endl;
    cout << "支持的像素格式:" << endl;

    struct v4l2_fmtdesc fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // 指定我们查询的是视频捕获类型

    // 循环查询，index 从 0 开始递增，直到失败
    for (int i = 0; ; ++i) {
        fmt.index = i;
        if (ioctl(fd, VIDIOC_ENUM_FMT, &fmt) < 0) {
            break; // 查询完了
        }

        // 打印格式名称 (description) 和 四字符代码 (pixelformat)
        // 四字符代码 (FourCC) 比如 'NV12', 'MJPG'
        char fourcc[5] = {0};
        // 这里的位运算是把 32位整数 拆成 4个字符
        fourcc[0] = (fmt.pixelformat) & 0xFF;
        fourcc[1] = (fmt.pixelformat >> 8) & 0xFF;
        fourcc[2] = (fmt.pixelformat >> 16) & 0xFF;
        fourcc[3] = (fmt.pixelformat >> 24) & 0xFF;

        cout << "[" << i << "] 格式名称: " << fmt.description 
             << " | 代码: " << fourcc << endl;

        // --- 进阶：查询该格式下支持的分辨率 ---
        struct v4l2_frmsizeenum frmsize;
        memset(&frmsize, 0, sizeof(frmsize));
        frmsize.pixel_format = fmt.pixelformat;
        frmsize.index = 0;

        while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                // 离散分辨率 (如 1920x1080, 1280x720)
                cout << "    - 分辨率: " << frmsize.discrete.width 
                     << "x" << frmsize.discrete.height << endl;
            } else {
                cout << "    - 分辨率: 连续可变范围 (Stepwise)" << endl;
            }
            frmsize.index++;
        }
    }
    return fd;
}

int open_camera(int fd, int width, int height,int fps) {

    // 2. 设置格式 (VIDIOC_S_FMT)
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12; // 优先尝试 NV12
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("设置格式失败");
        close(fd);
        return -1;
    }

    // 打印实际协商结果
    cout << ">>[V4L2] 设置分辨率: " << fmt.fmt.pix.width << "x" << fmt.fmt.pix.height << endl;
    
    char fourcc[5] = {0};
    *(int*)fourcc = fmt.fmt.pix.pixelformat;
    cout << ">>[V4L2] 像素格式: " << fourcc << endl;

   

    // 3. 设置帧率 (30 FPS)
    struct v4l2_streamparm streamparm;
    memset(&streamparm, 0, sizeof(streamparm));
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamparm.parm.capture.timeperframe.numerator = 1;
    streamparm.parm.capture.timeperframe.denominator = fps;

    if (ioctl(fd, VIDIOC_S_PARM, &streamparm) == 0) {
        cout << ">>[V4L2] 帧率设置成功" << endl;
        printf(">>[V4L2] 最终驱动帧率: %u/%u fps\n", 
           streamparm.parm.capture.timeperframe.denominator,
           streamparm.parm.capture.timeperframe.numerator);
    }
    return 0;
}

CameraBuffer* map_buffers(int fd, int* count) {
    // 1. 向内核申请缓冲区 (REQBUFS)
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = *count;                 // 期望申请的数量 (比如 4)
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;      // 使用内存映射模式

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("REQBUFS 失败");
        return nullptr;
    }

    // 驱动可能申请不到 4 个，只给了 2 个，所以要更新 count
    if (req.count < 2) {
        std::cerr << "缓冲区数量不足 (" << req.count << ")" << std::endl;
        return nullptr;
    }
    *count = req.count;
    std::cout << ">>[V4L2] 成功申请缓冲区数量: " << req.count << std::endl;

    // 2. 分配用户空间的管理数组
    // 我们用 new 动态分配一个数组，记得最后要 delete[]
    CameraBuffer* buffers = new CameraBuffer[req.count];
    memset(buffers, 0, sizeof(CameraBuffer) * req.count);

    // 3. 逐个查询、映射、导出 (QUERYBUF -> MMAP -> EXPBUF)
    for (int i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        // A. 查询 (Query): 问内核第 i 个盘子在哪里、多大
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("QUERYBUF 失败");
            return nullptr;
        }

        buffers[i].index = i;
        buffers[i].length = buf.length;

        // B. 映射 (Mmap): 将内核地址映射到用户空间指针
        // PROT_READ | PROT_WRITE: 可读可写
        // MAP_SHARED: 共享模式 (必须)
        buffers[i].start = mmap(NULL, buf.length, 
                                PROT_READ | PROT_WRITE, 
                                MAP_SHARED, 
                                fd, buf.m.offset);

        if (buffers[i].start == MAP_FAILED) {
            perror("mmap 失败");
            return nullptr;
        }

        // C. 【关键步骤】导出 DMA-BUF (Export DMA)
        // 这是实现零拷贝的核心！我们需要拿到一个 fd 传给 RGA/MPP。
        struct v4l2_exportbuffer expbuf;
        memset(&expbuf, 0, sizeof(expbuf));
        expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        expbuf.index = i;
        
        if (ioctl(fd, VIDIOC_EXPBUF, &expbuf) < 0) {
            perror("EXPBUF (DMA-BUF导出) 失败");
            // 如果不支持 EXPBUF，这里可以置为 -1，后续就要走 CPU 拷贝了
            buffers[i].export_fd = -1;
        } else {
            buffers[i].export_fd = expbuf.fd;
            // std::cout << "   Buffer[" << i << "] Export FD: " << expbuf.fd << std::endl;
        }
    }
    
    std::cout << ">>[V4L2] 缓冲区映射 & DMA导出 完成" << std::endl;
    return buffers;
}

void release_buffers(CameraBuffer* buffers, int count) {
    if (!buffers) return;
    for (int i = 0; i < count; ++i) {
        // 解除映射
        if (buffers[i].start) {
            munmap(buffers[i].start, buffers[i].length);
        }
        // 关闭 DMA-BUF fd
        if (buffers[i].export_fd >= 0) {
            close(buffers[i].export_fd);
        }
    }
    delete[] buffers;
    std::cout << ">>[V4L2] 缓冲区资源已释放" << std::endl;
}

int start_capturing(int fd, int buffer_count) {
    // 1. 把所有空盘子 (Buffer) 依次投放入队 (QBUF)
    for (int i = 0; i < buffer_count; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i; // 告诉驱动：我把第 i 号盘子还给你

        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("QBUF (入队) 失败");
            return -1;
        }
    }
    std::cout << ">>[V4L2] 所有缓冲区已入队 (QBUF Done)" << std::endl;

    // 2. 开启视频流 (STREAMON)
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("STREAMON (开启流) 失败");
        return -1;
    }
    std::cout << ">>[V4L2] 视频流已开启 (STREAMON)" << std::endl;
    return 0;
}

void stop_capturing(int fd) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    std::cout << ">>[V4L2] 视频流已停止" << std::endl;
}

int wait_and_get_frame(int fd) {
    // A. 使用 select 等待数据可读 (超时时间 2秒)
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    struct timeval tv;
    tv.tv_sec = 2; // 2秒超时
    tv.tv_usec = 0;

    int r = select(fd + 1, &fds, NULL, NULL, &tv);
    if (r == -1) {
       // perror(">>[V4L2] select 错误");
        return -1;
    } else if (r == 0) {
        std::cerr << ">>[V4L2] 等待帧超时 (Timeout)" << std::endl;
        return -1;
    }

    // B. 数据来了！执行出队操作 (DQBUF)
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("DQBUF (取帧) 失败");
        return -1;
    }

    // 返回当前帧的索引 (0~3)
    return buf.index;
}

int return_frame(int fd, int index) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index; // 指定归还哪一个

    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        perror("QBUF (归还) 失败");
        return -1;
    }
    return 0;
}

/**
 * @brief 封装好的测试函数：抓取指定数量的帧并保存为文件
 * @param w        宽度
 * @param h        高度
 * @param count    抓取多少帧
 * @param filename 保存的文件名
 */
void run_capture_test(int fd, int w, int h, int count, const char* filename) {
    cout << "==================================================" << endl;
    cout << "开始测试: " << filename << " (" << w << "x" << h << ")" << endl;

    // 1. 打开设备
    open_camera(fd, w, h, 30);
    if (fd < 0) return;

    // 2. 申请内存
    int n_buffers = 4;
    CameraBuffer* buffers = map_buffers(fd, &n_buffers);
    if (!buffers) {
        close(fd);
        return;
    }

    // 3. 启动流
    if (start_capturing(fd, n_buffers) < 0) {
        release_buffers(buffers, n_buffers);
        close(fd);
        return;
    }

    // 4. 打开文件
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        perror("无法创建文件");
        stop_capturing(fd);
        release_buffers(buffers, n_buffers);
        close(fd);
        return;
    }

    // 5. 循环抓取
    int success_count = 0;
    for (int i = 0; i < count; ++i) {
        int index = wait_and_get_frame(fd);
        if (index < 0) {
            cerr << "丢帧或超时" << endl;
            continue;
        }

        // 写入文件
        size_t written = fwrite(buffers[index].start, 1, buffers[index].length, fp);
        if (written != buffers[index].length) {
            cerr << "写入不完整" << endl;
        }

        // 打印进度条
        cout << "正在录制: [" << (i + 1) << "/" << count << "] 帧\r" << flush;

        // 归还
        if (return_frame(fd, index) < 0) break;
        success_count++;
    }
    cout << endl;

    // 6. 清理资源 (倒序清理: 关文件 -> 停流 -> 释放内存 -> 关设备)
    fclose(fp);
    stop_capturing(fd);
    release_buffers(buffers, n_buffers);
    close(fd);

    cout << "测试结束！成功保存 " << success_count << " 帧到 " << filename << endl;
    cout << "==================================================" << endl;
}