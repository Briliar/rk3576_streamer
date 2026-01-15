#include "video/mpp_encoder.h"
#include "video/v4l2.h"
#include "video/rga.h"
#include <iostream>
#include <cstring>

using namespace std;

// 向上对齐宏 (16字节对齐)
#define MPP_ALIGN(x, a) (((x)+(a)-1)&~((a)-1))

MppEncoder::MppEncoder() {}

MppEncoder::~MppEncoder() {
    deinit();
}

int MppEncoder::init(int w, int h, int fps) {
    this->width = w;
    this->height = h;

    MPP_RET ret = MPP_OK;

    // 1. 创建上下文
    ret = mpp_create(&ctx, &mpi);
    if (ret != MPP_OK) { cerr << "mpp_create failed" << endl; return -1; }

    ret = mpp_init(ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC); // H.264
    if (ret != MPP_OK) { cerr << "mpp_init failed" << endl; return -1; }

    // 2. 配置参数 (分辨率、码率)
    ret = mpp_enc_cfg_init(&cfg);
    if (ret != MPP_OK) return -1;

    // 计算对齐后的步长
    int hor_stride = MPP_ALIGN(w, 16);
    int ver_stride = MPP_ALIGN(h, 16);

    mpp_enc_cfg_set_s32(cfg, "prep:width", w);
    mpp_enc_cfg_set_s32(cfg, "prep:height", h);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", hor_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP); // NV12

    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR); // 固定码率
    // 简单估算码率: 720P@30fps -> 约 2Mbps
    int bps = w * h * fps / 8; 
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", bps);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", bps * 1.2);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", bps * 0.8);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", fps);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", fps);

    ret = mpi->control(ctx, MPP_ENC_SET_CFG, cfg);
    if (ret != MPP_OK) { cerr << "mpp config failed" << endl; return -1; }
    // 设置每个 IDR 帧都带 SPS/PPS
    MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    ret = mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &header_mode);
    if (ret) {
        std::cerr << ">>[MPP] mpi control enc set header mode failed: " << ret << std::endl;
        return -1;
    }
    
    // 顺便确保一下 SEI 模式（可选，有时能提高兼容性）
    MppEncSeiMode sei_mode = MPP_ENC_SEI_MODE_ONE_FRAME;
    mpi->control(ctx, MPP_ENC_SET_SEI_CFG, &sei_mode);
    // 申请 MPP 专用内存 (NV12大小)
    size_t frame_size = hor_stride * ver_stride * 3 / 2;
    ret = mpp_buffer_get(NULL, &shared_input_buf, frame_size);
    if (ret != MPP_OK) { cerr << "mpp buffer alloc failed" << endl; return -1; }

    cout << ">> [MPP] 初始化成功！ FD=" << mpp_buffer_get_fd(shared_input_buf) 
         << " Size=" << frame_size << endl;

    return 0;
}

int MppEncoder::get_input_fd() const {
    if (shared_input_buf) {
        return mpp_buffer_get_fd(shared_input_buf);
    }
    return -1;
}

int MppEncoder::encode(FILE* out_fp) {
    if (!ctx || !mpi || !shared_input_buf) return -1;

    MPP_RET ret = MPP_OK;
    MppFrame frame = nullptr;
    MppPacket packet = nullptr;

    // 1. 包装 Frame (复用 shared_input_buf)
    mpp_frame_init(&frame);
    mpp_frame_set_width(frame, width);
    mpp_frame_set_height(frame, height);
    mpp_frame_set_hor_stride(frame, MPP_ALIGN(width, 16));
    mpp_frame_set_ver_stride(frame, MPP_ALIGN(height, 16));
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(frame, shared_input_buf);
    mpp_frame_set_eos(frame, 0);

    // 2. 送入编码器
    ret = mpi->encode_put_frame(ctx, frame);
    mpp_frame_deinit(&frame); // 提交后即可释放 frame 结构体引用

    if (ret != MPP_OK) {
        cerr << "encode_put_frame error" << endl;
        return -1;
    }

    // 3. 取出编码包
    ret = mpi->encode_get_packet(ctx, &packet);
    if (ret != MPP_OK) {
        cerr << "encode_get_packet error" << endl;
        return -1;
    }

    // 4. 写入文件
    if (packet) {
        void* ptr = mpp_packet_get_pos(packet);
        size_t len = mpp_packet_get_length(packet);
        
        if (out_fp) {
            fwrite(ptr, 1, len, out_fp);
        }
        
        mpp_packet_deinit(&packet); // 必须释放
        return 0; // 成功
    }

    return -1; 
}

int MppEncoder::encode_to_memory(void** out_data, size_t* out_len, bool* is_key) {
    if (!ctx || !mpi || !shared_input_buf) return -1;

    MPP_RET ret = MPP_OK;
    MppFrame frame = nullptr;
    MppPacket packet = nullptr;

    // 1. 包装 Frame (复用 shared_input_buf)
    mpp_frame_init(&frame);
    mpp_frame_set_width(frame, width);
    mpp_frame_set_height(frame, height);
    mpp_frame_set_hor_stride(frame, MPP_ALIGN(width, 16));
    mpp_frame_set_ver_stride(frame, MPP_ALIGN(height, 16));
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(frame, shared_input_buf);
    mpp_frame_set_eos(frame, 0);

    // 2. 送入编码器
    ret = mpi->encode_put_frame(ctx, frame);
    mpp_frame_deinit(&frame); // 提交后即可释放 frame 结构体引用

    if (ret != MPP_OK) {
        cerr << "encode_put_frame error" << endl;
        return -1;
    }

   
    ret = mpi->encode_get_packet(ctx, &packet);
    
    if (ret == MPP_OK && packet) {
        void* ptr = mpp_packet_get_pos(packet);
        size_t len = mpp_packet_get_length(packet);
        
        //  检查是否是结束包或空包
        if (len > 0) {
            // 深拷贝！
            // 因为我们要马上调用 mpp_packet_deinit，所以必须把数据拷出来
            *out_data = malloc(len); // 分配新内存
            if (*out_data) {
                memcpy(*out_data, ptr, len);
                *out_len = len;
                if (is_key) {
                MppMeta meta = mpp_packet_get_meta(packet);
                RK_S32 is_intra = 0;
                
                // 从元数据中读取 "OUTPUT_INTRA" 标记
                if (meta) {
                    mpp_meta_get_s32(meta, KEY_OUTPUT_INTRA, &is_intra);
                }
                
                // 如果是 IDR 帧，MPP 会把 is_intra 置为 1
                *is_key = (is_intra != 0);
            }
            } else {
                *out_len = 0;
            }
        } else {
            *out_data = nullptr;
            *out_len = 0;
        }

        // 归还 Packet 给 MPP
        mpp_packet_deinit(&packet); 
        
        return (*out_len > 0) ? 0 : -1;
        
       
        return 0;
    }

    return -1; 
}

void* MppEncoder::get_input_ptr() {
    if (shared_input_buf) {
        return mpp_buffer_get_ptr(shared_input_buf);
    }
    return nullptr;
}
void MppEncoder::deinit() {
    if (shared_input_buf) {
        mpp_buffer_put(shared_input_buf);
        shared_input_buf = nullptr;
    }
    if (ctx) {
        mpp_destroy(ctx);
        ctx = nullptr;
    }
    if (cfg) {
        mpp_enc_cfg_deinit(cfg);
        cfg = nullptr;
    }
}

/**
 * @brief 全链路测试: Camera -> RGA -> MPP -> File
 */
void run_encoder_test(int fd, int w, int h, int frame_count,const char* filename) {
   // const char* filename = "output.h264";
    cout << "启动全链路录制测试..." << endl;
    //cout << "   Source: " << dev_name << " (" << w << "x" << h << ")" << endl;
    cout << "   Target: " << filename << endl;

    // 1. V4L2 Init
    open_camera(fd, w, h, 30);
    if (fd < 0) return;

    int n_buffers = 4;
    CameraBuffer* buffers = map_buffers(fd, &n_buffers);
    if (!buffers) { close(fd); return; }

    if (start_capturing(fd, n_buffers) < 0) {
        release_buffers(buffers, n_buffers); close(fd); return;
    }

    // 2. RGA Init
    init_rga();

    // 3. MPP Init (使用封装类)
    MppEncoder encoder;
    if (encoder.init(w, h, 30) < 0) {
        cerr << "MPP Encoder 初始化失败" << endl;
        return;
    }

    // 4. 打开输出文件
    FILE* fp = fopen(filename, "wb");
    if (!fp) { perror("文件创建失败"); return; }

    // 5. 循环录制
    int encoded_frames = 0;
    for (int i = 0; i < frame_count; ++i) {
        // A. 获取 V4L2 帧 (FD)
        int index = wait_and_get_frame(fd);
        if (index < 0) continue;

        // B. RGA 转换 (V4L2 FD -> MPP FD)
        // 注意：这里需要确保你的 rga_utils 里的函数支持 (int dst_fd) 参数
        // 且 rga 里的格式设置是你调试通过的那一个 (比如 UYVY -> NV12)
        int src_fd = buffers[index].export_fd;
        int dst_fd = encoder.get_input_fd();

        if (convert_yuyv_to_nv12(src_fd, dst_fd, w, h) == 0) {
            
            // C. MPP 编码
            // 数据已经在 dst_fd 里了，直接调用 encode 即可
            if (encoder.encode(fp) == 0) {
                encoded_frames++;
                cout << "已录制: " << encoded_frames << "/" << frame_count << "\r" << flush;
            }
        } else {
            cerr << "RGA 转换失败 (丢帧)" << endl;
        }

        // D. 归还 V4L2 帧
        return_frame(fd, index);
    }
    cout << endl;

    // 6. 清理资源
    fclose(fp);
    encoder.deinit(); // 显式调用销毁
    stop_capturing(fd);
    release_buffers(buffers, n_buffers);
    close(fd);

    cout << "测试结束! 文件已保存: " << filename << endl;
}