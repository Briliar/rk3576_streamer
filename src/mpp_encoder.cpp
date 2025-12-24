#include "mpp_encoder.h"
#include <iostream>
#include <cstring>

using namespace std;

// 向上对齐帮助函数 (MPP 要求)
#define MPP_ALIGN(x, a) (((x)+(a)-1)&~((a)-1))

int init_mpp(MppContext& mpp, int w, int h, int fps) {
    memset(&mpp, 0, sizeof(MppContext));
    mpp.width = w;
    mpp.height = h;

    MPP_RET ret = MPP_OK;

    // 1. 创建 MPP 实例
    ret = mpp_create(&mpp.ctx, &mpp.mpi);
    if (ret != MPP_OK) { cerr << "mpp_create failed" << endl; return -1; }

    // 2. 配置编码参数 (H.264)
    ret = mpp_init(mpp.ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (ret != MPP_OK) { cerr << "mpp_init failed" << endl; return -1; }

    ret = mpp_enc_cfg_init(&mpp.cfg);
    
    // 【关键】对齐计算
    int hor_stride = MPP_ALIGN(w, 16);
    int ver_stride = MPP_ALIGN(h, 16);

    // 配置分辨率
    mpp_enc_cfg_set_s32(mpp.cfg, "prep:width", w);
    mpp_enc_cfg_set_s32(mpp.cfg, "prep:height", h);
    mpp_enc_cfg_set_s32(mpp.cfg, "prep:hor_stride", hor_stride);
    mpp_enc_cfg_set_s32(mpp.cfg, "prep:ver_stride", ver_stride);
    mpp_enc_cfg_set_s32(mpp.cfg, "prep:format", MPP_FMT_YUV420SP); // NV12

    // 配置码率 (CBR)
    mpp_enc_cfg_set_s32(mpp.cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    int bps = w * h * fps / 8; // 估算码率
    mpp_enc_cfg_set_s32(mpp.cfg, "rc:bps_target", bps);
    mpp_enc_cfg_set_s32(mpp.cfg, "rc:bps_max", bps * 1.2);
    mpp_enc_cfg_set_s32(mpp.cfg, "rc:bps_min", bps * 0.8);
    mpp_enc_cfg_set_s32(mpp.cfg, "rc:fps_in_num", fps);
    mpp_enc_cfg_set_s32(mpp.cfg, "rc:fps_out_num", fps);
    mpp_enc_cfg_set_s32(mpp.cfg, "rc:gop", fps * 2);

    mpp.mpi->control(mpp.ctx, MPP_ENC_SET_CFG, mpp.cfg);

    // 3. 【核心修复】分配共享内存 (Zero-Copy 关键)
    // NV12 大小 = w * h * 1.5
    // 必须要足够大，且包含 stride 对齐
    mpp.frame_size = hor_stride * ver_stride * 3 / 2;
    
    // 从 MPP 申请 buffer
    ret = mpp_buffer_get(NULL, &mpp.shared_buf, mpp.frame_size);
    if (ret != MPP_OK) { cerr << "Alloc MPP buffer failed" << endl; return -1; }

    // 获取 FD 和 虚拟地址
    mpp.shared_fd = mpp_buffer_get_fd(mpp.shared_buf);
    mpp.shared_ptr = mpp_buffer_get_ptr(mpp.shared_buf);

    cout << ">> [MPP Init] Success. Shared FD=" << mpp.shared_fd 
         << " Ptr=" << mpp.shared_ptr 
         << " Size=" << mpp.frame_size << endl;

    return 0;
}

int encode_frame(MppContext& mpp, FILE* out_fp) {
    MPP_RET ret = MPP_OK;
    
    // 1. 封装 Frame
    // 此时 RGA 已经把数据写进 mpp.shared_fd 了，我们直接把 mpp.shared_buf 包装成 Frame
    MppFrame frame = nullptr;
    mpp_frame_init(&frame);
    mpp_frame_set_width(frame, mpp.width);
    mpp_frame_set_height(frame, mpp.height);
    mpp_frame_set_hor_stride(frame, MPP_ALIGN(mpp.width, 16));
    mpp_frame_set_ver_stride(frame, MPP_ALIGN(mpp.height, 16));
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    
    // 直接复用这块 Buffer
    mpp_frame_set_buffer(frame, mpp.shared_buf);
    mpp_frame_set_eos(frame, 0);

    // 2. 发送给编码器
    ret = mpp.mpi->encode_put_frame(mpp.ctx, frame);
    if (ret != MPP_OK) {
        cerr << "encode_put_frame failed: " << ret << endl;
        mpp_frame_deinit(&frame);
        return -1;
    }
    mpp_frame_deinit(&frame);

    // 3. 获取编码结果
    MppPacket packet = nullptr;
    ret = mpp.mpi->encode_get_packet(mpp.ctx, &packet);
    if (ret != MPP_OK) {
        cerr << "encode_get_packet failed: " << ret << endl;
        return -1;
    }

    // 4. 写入文件
    if (packet) {
        void* ptr = mpp_packet_get_pos(packet);
        size_t len = mpp_packet_get_length(packet);
        
        // 关键帧(IDR)判断，可选打印
        // int is_intra = mpp_packet_is_intra(packet); 

        if (out_fp) {
            fwrite(ptr, 1, len, out_fp);
        }
        
        // 必须释放 packet 引用
        mpp_packet_deinit(&packet);
        return 0;
    }

    return -1;
}

void cleanup_mpp(MppContext& mpp) {
    if (mpp.shared_buf) {
        mpp_buffer_put(mpp.shared_buf);
    }
    if (mpp.ctx) {
        mpp_destroy(mpp.ctx);
    }
    if (mpp.cfg) {
        mpp_enc_cfg_deinit(mpp.cfg);
    }
}