#include "rga.h"
#include "v4l2.h"
#include <iostream>
#include <cstring>

using namespace std;

int init_rga() {
    // 这里的 c_RkRgaInit 可能会在不同版本的库里名字不一样
    // 大部分新版 librga 不需要显式 Init，直接调 im... 函数即可
    // 我们这里打印一下版本确认库链接上了
    cout << ">> [RGA] RGA 模块已准备就绪" << endl;
    return 0;
}

int convert_yuyv_to_nv12(int src_fd, void* dst_ptr, int width, int height) {
    // 1. 包装输入 (Source): 来自 V4L2 的 DMA-FD
    // RK_FORMAT_YUYV_422 就是 YUYV
    rga_buffer_t src = wrapbuffer_fd(src_fd, width, height, RK_FORMAT_YUYV_422);

    // 2. 包装输出 (Destination): 这里的 dst_ptr 是我们 malloc 出来的内存地址
    // RK_FORMAT_YCbCr_420_SP 就是 NV12
    rga_buffer_t dst = wrapbuffer_virtualaddr(dst_ptr, width, height, RK_FORMAT_YCbCr_420_SP);

    // 3. 校验参数 (这是个好习惯)
    if (imcheck(src, dst, {}, {}) <= 0) {
        cerr << "❌ [RGA] 参数校验失败 (imcheck failed)" << endl;
        return -1;
    }

    // 4. 执行转换 (Convert Color)
    // 这一步是同步的，函数返回时，硬件已经把图搬完了
    IM_STATUS status = imcvtcolor(src, dst, src.format, dst.format);
    
    if (status != IM_STATUS_SUCCESS) {
        cerr << "❌ [RGA] 转换失败，错误码: " << status << endl;
        return -1;
    }

    return 0;
}

void run_convert_test(int fd, int w, int h, int count, const char* filename) {
    cout << "🧪 开始 RGA 转码测试: YUYV -> NV12" << endl;


    int n_buffers = 4;
    CameraBuffer* buffers = map_buffers(fd, &n_buffers);
    if (!buffers) { close(fd); return; }

    if (start_capturing(fd, n_buffers) < 0) {
        release_buffers(buffers, n_buffers); close(fd); return;
    }

    // 2. 初始化 RGA
    init_rga();

    // 3. 【新增】申请一块内存存放 NV12 结果
    // NV12 大小 = 宽 * 高 * 1.5
    size_t nv12_size = w * h * 1.5;
    void* nv12_buffer = malloc(nv12_size);
    if (!nv12_buffer) {
        perror("malloc 失败");
        return;
    }
    cout << ">> 分配 NV12 缓存大小: " << nv12_size << " 字节" << endl;

    // 4. 打开输出文件
    FILE* fp = fopen(filename, "wb");
    if (!fp) { perror("文件创建失败"); return; }

    // 5. 循环处理
    for (int i = 0; i < count; ++i) {
        int index = wait_and_get_frame(fd);
        if (index < 0) continue;

        // ==========================================
        // 核心环节：调用 RGA 进行转码
        // ==========================================
        // 输入：buffers[index].export_fd (V4L2 里的 YUYV 数据)
        // 输出：nv12_buffer (我们 malloc 的内存)
        if (convert_yuyv_to_nv12(buffers[index].export_fd, nv12_buffer, w, h) == 0) {
            // 转码成功，把 NV12 数据写入文件
            fwrite(nv12_buffer, 1, nv12_size, fp);
            cout << "转换并保存第 " << i+1 << " 帧 \r" << flush;
        }

        if (return_frame(fd, index) < 0) break;
    }
    cout << endl;

    // 6. 清理
    free(nv12_buffer); // 别忘了释放 malloc 的内存
    fclose(fp);
    stop_capturing(fd);
    release_buffers(buffers, n_buffers);
    close(fd);
    
    cout << "✅ RGA 测试结束！请查看 " << filename << endl;
}
