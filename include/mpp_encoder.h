#pragma once

#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_meta.h>
#include <cstdio>
struct MppContext {
    MppCtx ctx;
    MppApi* mpi;
    MppEncCfg cfg;
    MppBufferGroup buf_grp; // 内存池管理
    MppBuffer shared_buf;   // 【核心】RGA和MPP共享的buffer
    int shared_fd;          // 这块内存的 FD (给 RGA 用)
    void* shared_ptr;       // 这块内存的 虚拟地址 (调试用)
    size_t frame_size;
    int width;
    int height;
};

// 初始化 MPP，并分配好 shared_buf
// 返回 0 成功，-1 失败
int init_mpp(MppContext& mpp, int w, int h, int fps);

// 执行编码
// 这里的 buffer 已经在 init 里分配好了，RGA 只要往 mpp.shared_fd 里写数据即可
// 写入后，调用此函数进行编码
// out_fp: 打开的 FILE* 指针，用于保存 h264
int encode_frame(MppContext& mpp, FILE* out_fp);

// 销毁资源
void cleanup_mpp(MppContext& mpp);