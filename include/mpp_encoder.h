#pragma once

#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_meta.h>
#include <cstdio>
class MppEncoder {
public:
    MppEncoder();
    ~MppEncoder();

    /**
     * @brief 初始化编码器并分配输入内存
     * @param w 宽
     * @param h 高
     * @param fps 帧率
     * @return 0 成功, -1 失败
     */
    int init(int w, int h, int fps);

    /**
     * @brief 获取输入缓冲区的 DMA-FD (给 RGA 用)
     */
    int get_input_fd() const;

    /**
     * @brief 执行编码并将结果写入文件
     * @param out_fp 打开的文件指针 (h264文件)
     * @return 0 成功, -1 失败
     */
    int encode(FILE* out_fp);

    void* get_input_ptr();
    
    /**
     * @brief 销毁资源
     */
    void deinit();

private:
    int width = 0;
    int height = 0;
    
    // MPP 核心上下文
    MppCtx ctx = nullptr;
    MppApi* mpi = nullptr;
    MppEncCfg cfg = nullptr;

    // 零拷贝关键：这是 MPP 分配的物理连续内存
    // RGA 往这里写，MPP 从这里读
    MppBuffer shared_input_buf = nullptr;
};

void run_camera_encoder_test(int fd, int w, int h, int frame_count,const char* filename);