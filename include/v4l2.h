#ifndef V4L2_H
#define V4L2_H

#include <linux/videodev2.h> // 为了能引用 v4l2_buffer 类型

// 自定义结构体：描述一个图像缓冲区
struct CameraBuffer {
    void* start;    // 虚拟地址 (给 CPU 读写用的，比如保存文件)
    size_t length;  // 缓冲区大小
    int export_fd;  // 【关键】DMA-BUF 文件描述符 (给 RGA/MPP 用的)
    int index;      // 在 V4L2 队列中的编号 (0, 1, 2, 3)
};

int query_device_info(const char* dev_name);
int open_camera(int fd, int width, int height);
// 新增：申请并映射缓冲区
// 参数：fd (摄像头描述符), count (想要申请几个，通常传指针以便返回实际申请数)
// 返回值：返回一个 CameraBuffer 数组的首地址
CameraBuffer* map_buffers(int fd, int* count);
// 1. 启动摄像头 (把所有空buffer入队，并开启流)
int start_capturing(int fd, int buffer_count);
// 2. 停止摄像头 (关闭流)
void stop_capturing(int fd);
// 3. 等待并取出最新的一帧 (DQBUF)
// 返回值：>=0 表示成功取到的 buffer index，-1 表示失败
int wait_and_get_frame(int fd);
// 4. 处理完后归还 buffer (QBUF)
// 参数：index 是你刚才取出的那个 buffer 的编号
int return_frame(int fd, int index);
// 新增：释放资源
void run_capture_test(int fd, int w, int h, int count, const char* filename);
void release_buffers(CameraBuffer* buffers, int count);
#endif // V4L2_H