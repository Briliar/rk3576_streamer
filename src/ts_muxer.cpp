#include "ts_muxer.h"
#include <iostream>
#include <cstring>

using namespace std;

// 回调：把 TS 数据吐给 SRT
int TsMuxer::write_packet_cb(void* opaque, uint8_t* buf, int buf_size) {
    TsMuxer* self = (TsMuxer*)opaque;
    if (self->output_callback) return self->output_callback(buf, buf_size);
    return buf_size;
}

TsMuxer::TsMuxer() {}
TsMuxer::~TsMuxer() { close(); }

void TsMuxer::close() {
    if (fmt_ctx) {
        av_write_trailer(fmt_ctx);
        avformat_free_context(fmt_ctx);
        fmt_ctx = nullptr;
    }
    if (avio_ctx) {
        av_free(avio_ctx->buffer);
        av_free(avio_ctx);
        avio_ctx = nullptr;
    }
}

int TsMuxer::init(int width, int height, int fps, DataCallback callback) {
    output_callback = callback;
    
    // 缓冲区稍微改小一点，或者是 1316 的倍数
    avio_buffer_size = 1316 * 10; 
    avio_buffer = (uint8_t*)av_malloc(avio_buffer_size);

    avio_ctx = avio_alloc_context(avio_buffer, avio_buffer_size, 1, this, NULL, write_packet_cb, NULL);
    if (!avio_ctx) return -1;

    avformat_alloc_output_context2(&fmt_ctx, NULL, "mpegts", NULL);
    fmt_ctx->pb = avio_ctx;

    stream = avformat_new_stream(fmt_ctx, NULL);
    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->codec_id = AV_CODEC_ID_H264;
    stream->codecpar->width = width;
    stream->codecpar->height = height;

    // 【注意】不需要手动设置 extradata 了，因为流里自带
    if (avformat_write_header(fmt_ctx, NULL) < 0) return -1;
    return 0;
}

int TsMuxer::input_frame(void* data, int size, uint32_t timestamp, bool is_key) {
    if (!fmt_ctx || !stream) return -1;

    AVPacket* pkt = av_packet_alloc();
    pkt->data = (uint8_t*)data;
    pkt->size = size;
    uint8_t* p_data = (uint8_t*)data;
    // 标准的 H.264 必须以 00 00 00 01 开头
    bool has_start_code = (size > 4 && p_data[0] == 0 && p_data[1] == 0 && p_data[2] == 0 && p_data[3] == 1);

    if (has_start_code) {
        // 情况 A: 数据正常，直接复用
        // 注意：这里用 av_packet_from_data 可以避免一次内存拷贝（如果 data 生命周期能保证的话）
        // 但为了安全，我们还是拷贝一份
        av_new_packet(pkt, size);
        memcpy(pkt->data, p_data, size);
    } else {
        // 情况 B: 缺少 StartCode，我们手动加上！
        std::cout << "补全 StartCode" << std::endl;
        av_new_packet(pkt, size + 4);
        pkt->data[0] = 0;
        pkt->data[1] = 0;
        pkt->data[2] = 0;
        pkt->data[3] = 1;
        memcpy(pkt->data + 4, p_data, size);
    }

    AVRational time_base = {1, 1000}; // 毫秒转内部时间基
    pkt->pts = av_rescale_q(timestamp, time_base, stream->time_base);
    pkt->dts = pkt->pts;
    if (is_key) pkt->flags |= AV_PKT_FLAG_KEY;
    pkt->stream_index = stream->index;

    // 直接写入！不需要过滤器！
    av_interleaved_write_frame(fmt_ctx, pkt);
    
    av_packet_free(&pkt);
    return 0;
}