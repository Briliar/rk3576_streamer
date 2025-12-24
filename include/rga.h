#include <rga/im2d.h> // RGA 的核心头文件
#include <rga/RgaApi.h>

// 初始化 RGA (其实主要是打印一下版本)
int init_rga();

/**
 * @brief 执行 YUYV -> NV12 的格式转换
 * * @param src_fd    输入数据的 DMA-FD (来自 V4L2)
 * @param dst_ptr   输出数据的虚拟地址 (我们暂时用 malloc 的内存来测试)
 * @param width     宽
 * @param height    高
 * @return int      0 成功, -1 失败
 */
//int convert_yuyv_to_nv12(int src_fd, void* dst_ptr, int width, int height);
void run_convert_test(int fd, int w, int h, int count, const char* filename);
int convert_yuyv_to_nv12(int src_fd, int dst_fd, int width, int height);