#include "video/rga.h"
#include "video/v4l2.h"
#include "video/mpp_encoder.h"
#include <iostream>
#include <cstring>

using namespace std;

int init_rga() {
    // 这里的 c_RkRgaInit 可能会在不同版本的库里名字不一样
    // 大部分新版 librga 不需要显式 Init，直接调 im... 函数即可
    // 我们这里打印一下版本确认库链接上了
    cout << ">>[RGA] RGA 模块已准备就绪" << endl;
    return 0;
}


int convert_yuyv_to_nv12(int src_fd, int dst_fd, int width, int height) {
    // 1. SRC (V4L2) 
    rga_buffer_t src = wrapbuffer_fd(src_fd, width, height, RK_FORMAT_YUYV_422); 
    

    // 2. DST (MPP FD)
    // 这里使用 wrapbuffer_fd，必须传入 fd
    rga_buffer_t dst = wrapbuffer_fd(dst_fd, width, height, RK_FORMAT_YCbCr_420_SP);
    

    return (imcvtcolor(src, dst, src.format, dst.format) == IM_STATUS_SUCCESS) ? 0 : -1;
}

void run_convert_test(int fd, int w, int h, int count, const char* filename) {
    cout << " 开始 RGA 转码测试: YUYV -> NV12 (使用 MPP 内存)" << endl;

    // 1. 打开设备 (修正了之前的调用方式)
    open_camera(fd, w, h, 30);
    if (fd < 0) return;

    int n_buffers = 4;
    CameraBuffer* buffers = map_buffers(fd, &n_buffers);
    if (!buffers) { close(fd); return; }

    if (start_capturing(fd, n_buffers) < 0) {
        release_buffers(buffers, n_buffers); close(fd); return;
    }

    // 2. 初始化 RGA
    init_rga();

    // 3. 【核心修改】初始化 MPP Encoder (代替 malloc)
    // 我们利用它来分配一块 RGA 喜欢的物理连续内存
    MppEncoder encoder;
    if (encoder.init(w, h, 30) < 0) { // 帧率随便填，只为分配内存
        cerr << " 内存分配失败" << endl;
    }
    
    // 获取这块内存的关键信息
    int dst_fd = encoder.get_input_fd();   // 给 RGA 用
    void* dst_ptr = encoder.get_input_ptr(); // 给 fwrite 用 (保存文件)
    size_t nv12_size = w * h * 1.5;

    cout << ">> MPP 内存就绪. FD=" << dst_fd << " Ptr=" << dst_ptr << endl;

    // 4. 打开输出文件
    FILE* fp = fopen(filename, "wb");
    if (!fp) { perror("文件创建失败"); }

    // 5. 循环处理
    for (int i = 0; i < count; ++i) {
        int index = wait_and_get_frame(fd);
        if (index < 0) continue;

        // ==========================================
        // 核心环节：调用 RGA 进行转码 (FD -> FD)
        // ==========================================
        // 输入：V4L2 的 export_fd
        // 输出：MPP 分配的 dst_fd (替换了原来的 malloc 指针)
        if (convert_yuyv_to_nv12(buffers[index].export_fd, dst_fd, w, h) == 0) {
            
            // 转码成功，使用虚拟地址指针把数据写入文件
            if (dst_ptr) {
                fwrite(dst_ptr, 1, nv12_size, fp);
                cout << "转换并保存第 " << i+1 << " 帧 \r" << flush;
            }
        } else {
            cerr << "RGA 转换失败" << endl;
        }

        if (return_frame(fd, index) < 0) break;
    }
    cout << endl;

    // 6. 清理
    fclose(fp);

    // encoder 析构函数会自动释放 MPP 内存，不需要手动 free
    stop_capturing(fd);
    release_buffers(buffers, n_buffers);
    close(fd);
    
    cout << "RGA 测试结束！请查看 " << filename << endl;
}

int rga_convert(void* src_ptr, int src_fd, int src_w, int src_h, int src_fmt,
                void* dst_ptr, int dst_fd, int dst_w, int dst_h, int dst_fmt) {

    rga_buffer_t src, dst;
    
    // 封装源 buffer
    if (src_fd > 0) src = wrapbuffer_fd(src_fd, src_w, src_h, src_fmt);
    else            src = wrapbuffer_virtualaddr(src_ptr, src_w, src_h, src_fmt);

    // 封装目的 buffer
    if (dst_fd > 0) dst = wrapbuffer_fd(dst_fd, dst_w, dst_h, dst_fmt);
    else            dst = wrapbuffer_virtualaddr(dst_ptr, dst_w, dst_h, dst_fmt);

    return (imcvtcolor(src, dst, src.format, dst.format) == IM_STATUS_SUCCESS) ? 0 : -1; // 执行拷贝/缩放/格式转换
}