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

    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        g_buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        cout << ">>[V4L2] 设备类型: MIPI/ISP" << endl;
    } else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        g_buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cout << ">>[V4L2] 设备类型: USB/UVC" << endl;
    } else {
        cout << ">>[V4L2] 不是视频采集设备" << endl;
        close(fd);
        return -1;
    }

    // 3. 枚举支持的像素格式
    cout << "---------------------------------" << endl;
    cout << "支持的像素格式:" << endl;

    struct v4l2_fmtdesc fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = g_buf_type; // 指定我们查询的是视频捕获类型

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
                // 离散分辨率 
                cout << "    - 分辨率: " << frmsize.discrete.width 
                     << "x" << frmsize.discrete.height << endl;
            } else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE || 
                     frmsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
                // 范围分辨率
                cout << "    - [范围] " 
                     << frmsize.stepwise.min_width << "x" << frmsize.stepwise.min_height
                     << " -> "
                     << frmsize.stepwise.max_width << "x" << frmsize.stepwise.max_height
                     << " (对齐: " << frmsize.stepwise.step_width << "x" << frmsize.stepwise.step_height << ")" 
                     << endl;
            }
            frmsize.index++;
        }
    }
    return fd;
}

int open_camera(int fd, int width, int height,int fps) {

    // 先查询一次 Capability，确定是 USB 还是 MIPI
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("查询能力失败");
        return -1;
    }

    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        g_buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; // MIPI 改用 MPLANE
        std::cout << ">>[V4L2] 模式: Multi-Planar (MPLANE)" << std::endl;
    } else {
        g_buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        std::cout << ">>[V4L2] 模式: Single-Planar" << std::endl;
    }

    // 2. 设置格式 (VIDIOC_S_FMT)
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = g_buf_type; 

    if (g_buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        // --- MIPI (MPLANE) 设置方式 ---
        fmt.fmt.pix_mp.width = width;
        fmt.fmt.pix_mp.height = height;
        fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12; // 推荐 NV12
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        // fmt.fmt.pix_mp.num_planes = 1; // 通常驱动会自动修正，不写也行
    } else {
        // --- USB (Single-Plane) 设置方式 ---
        fmt.fmt.pix.width = width;
        fmt.fmt.pix.height = height;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV; 
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
    }

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror(">>[V4L2] 设置格式失败 (S_FMT)");
        return -1;
    }

    // 打印实际结果 
    if (g_buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        cout << ">>[V4L2-MIPI] 设置分辨率: " << fmt.fmt.pix_mp.width << "x" << fmt.fmt.pix_mp.height << endl;
        char fourcc[5] = {0};
        *(int*)fourcc = fmt.fmt.pix_mp.pixelformat;
        cout << ">>[V4L2-MIPI] 像素格式: " << fourcc << endl;
    } else {
        cout << ">>[V4L2-USB] 设置分辨率: " << fmt.fmt.pix.width << "x" << fmt.fmt.pix.height << endl;
        char fourcc[5] = {0};
        *(int*)fourcc = fmt.fmt.pix.pixelformat;
        cout << ">>[V4L2-USB] 像素格式: " << fourcc << endl;
    }

    // 3. 设置帧率 
    struct v4l2_streamparm streamparm;
    memset(&streamparm, 0, sizeof(streamparm));
    streamparm.type = g_buf_type; 
    
    streamparm.parm.capture.timeperframe.numerator = 1;
    streamparm.parm.capture.timeperframe.denominator = fps;

    if (ioctl(fd, VIDIOC_S_PARM, &streamparm) == 0) {
        printf(">>[V4L2] 最终驱动帧率: %u/%u fps\n", 
            streamparm.parm.capture.timeperframe.denominator,
            streamparm.parm.capture.timeperframe.numerator);
    } else {
        printf(">>[V4L2] 驱动不支持设置帧率\n");
    }
    
    return 0;
}

CameraBuffer* map_buffers(int fd, int* count) {
    // 1. 向内核申请缓冲区 (REQBUFS)
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = *count;                 // 期望申请的数量 (比如 4)
    req.type = g_buf_type;
    req.memory = V4L2_MEMORY_MMAP;      // 使用内存映射模式

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror(">>[V4L2] REQBUFS 失败");
        return nullptr;
    }

    // 驱动可能申请不到 4 个，只给了 2 个，所以要更新 count
    if (req.count < 2) {
        std::cerr << ">>[V4L2] 缓冲区数量不足 (" << req.count << ")" << std::endl;
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
        buf.type = g_buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

       // MPLANE 特殊处理：length 和 offset 位置不同
        if (g_buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            // MPLANE 需要分配 planes 数组
            struct v4l2_plane planes[1]; 
            memset(planes, 0, sizeof(planes));
            buf.m.planes = planes;
            buf.length = 1; // plane 数量

            if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
                perror(">>[V4L2] QUERYBUF (MPLANE) 失败");
                return nullptr;
            }
            
            buffers[i].index = i;
            buffers[i].length = buf.m.planes[0].length; // 长度在 plane 里
            
            buffers[i].start = mmap(NULL, buf.m.planes[0].length,
                                    PROT_READ | PROT_WRITE, MAP_SHARED,
                                    fd, buf.m.planes[0].m.mem_offset); // offset 在 plane 里
        } 
        else {
            // 普通模式
            if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
                perror(">>[V4L2] QUERYBUF 失败");
                return nullptr;
            }
            buffers[i].index = i;
            buffers[i].length = buf.length;
            buffers[i].start = mmap(NULL, buf.length,
                                    PROT_READ | PROT_WRITE, MAP_SHARED,
                                    fd, buf.m.offset);
        }
        if (buffers[i].start == MAP_FAILED) {
            perror(">>[V4L2] mmap 失败");
            return nullptr;
        }

        // C. 导出 DMA-BUF (Export DMA)
        struct v4l2_exportbuffer expbuf;
        memset(&expbuf, 0, sizeof(expbuf));
        expbuf.type = g_buf_type;
        expbuf.index = i;

        if (g_buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            expbuf.plane = 0; 
        }

        if (ioctl(fd, VIDIOC_EXPBUF, &expbuf) < 0) {
            perror(">>[V4L2] EXPBUF (DMA-BUF导出) 失败");
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
    for (int i = 0; i < buffer_count; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = g_buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (g_buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            struct v4l2_plane planes[1];
            memset(planes, 0, sizeof(planes));
            buf.m.planes = planes;
            buf.length = 1;
            planes[0].bytesused = 0;
            planes[0].length = 0;
        }

        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror(">>[V4L2] QBUF (入队) 失败");
            return -1;
        }
    }
    std::cout << ">>[V4L2] 所有缓冲区已入队 (QBUF Done)" << std::endl;

    // 2. 开启视频流 (STREAMON)
    enum v4l2_buf_type type = (v4l2_buf_type)g_buf_type;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror(">>[V4L2] STREAMON (开启流) 失败");
        return -1;
    }
    std::cout << ">>[V4L2] 视频流已开启 (STREAMON)" << std::endl;
    return 0;
}

void stop_capturing(int fd) {
    enum v4l2_buf_type type = (v4l2_buf_type)g_buf_type;
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
    
    struct v4l2_plane planes[1];
    memset(planes, 0, sizeof(planes));

    buf.type = g_buf_type;
    buf.memory = V4L2_MEMORY_MMAP;

    // MPLANE 需要挂载 planes 接收信息
    if (g_buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        buf.m.planes = planes;
        buf.length = 1;
    }

    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        perror(">>[V4L2] DQBUF (取帧) 失败");
        return -1;
    }

    // 返回当前帧的索引 (0~3)
    return buf.index;
}

int return_frame(int fd, int index) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = g_buf_type;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;

    if (g_buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        struct v4l2_plane planes[1];
        memset(planes, 0, sizeof(planes));
        buf.m.planes = planes;
        buf.length = 1;
    }

    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        perror(">>[V4L2] QBUF (归还) 失败");
        return -1;
    }
    return 0;
}

int get_v4l2_buf_type() {
    return g_buf_type;
}
