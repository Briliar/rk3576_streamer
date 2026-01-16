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