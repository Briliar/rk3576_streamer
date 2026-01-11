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

int TsMuxer::init(int width, int height, int fps, int sample_rate, int channels, DataCallback callback) {
    output_callback = callback;
    
    // 缓冲区设置
    avio_buffer_size = 1316 * 10; 
    avio_buffer = (uint8_t*)av_malloc(avio_buffer_size);

    avio_ctx = avio_alloc_context(avio_buffer, avio_buffer_size, 1, this, NULL, write_packet_cb, NULL);
    if (!avio_ctx) return -1;

    avformat_alloc_output_context2(&fmt_ctx, NULL, "mpegts", NULL);
    fmt_ctx->pb = avio_ctx;

    
    // 创建视频流
    video_stream = avformat_new_stream(fmt_ctx, NULL);
    video_stream->id = 0; // PID 自动分配，或者你可以指定
    video_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    video_stream->codecpar->codec_id = AV_CODEC_ID_H264;
    video_stream->codecpar->width = width;
    video_stream->codecpar->height = height;

    // 创建音频流
    audio_stream = avformat_new_stream(fmt_ctx, NULL);
    audio_stream->id = 1;
    audio_stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    audio_stream->codecpar->codec_id = AV_CODEC_ID_AAC;
    audio_stream->codecpar->sample_rate = sample_rate;
    audio_stream->codecpar->channels = channels;
    audio_stream->codecpar->channel_layout = av_get_default_channel_layout(channels);
    // 注意：旧版 FFmpeg 可能用 audio_stream->codecpar->channels = channels;
    
    // 告诉 FFmpeg 这是一个 ADTS 格式的 AAC (通常不需要额外设置，TS Muxer 很智能)

    // 写 TS 头 (PAT/PMT)
    if (avformat_write_header(fmt_ctx, NULL) < 0) return -1;
    return 0;
}
// 视频写入函数
int TsMuxer::write_video(void* data, int size, uint32_t timestamp, bool is_key) {
    if (!fmt_ctx || !video_stream) return -1;

    AVPacket* pkt = av_packet_alloc();
    uint8_t* p_data = (uint8_t*)data;
    
    // 检测 Start Code (H.264 Annex-B)
    bool has_start_code = (size > 4 && p_data[0] == 0 && p_data[1] == 0 && p_data[2] == 0 && p_data[3] == 1);

    if (has_start_code) {
        av_new_packet(pkt, size);
        memcpy(pkt->data, p_data, size);
    } else {
        // 补全 StartCode
        av_new_packet(pkt, size + 4);
        pkt->data[0] = 0; pkt->data[1] = 0; pkt->data[2] = 0; pkt->data[3] = 1;
        memcpy(pkt->data + 4, p_data, size);
    }

    // 时间戳转换：毫秒 -> 视频流时间基 (90k)
    AVRational ms_base = {1, 1000}; 
    pkt->pts = av_rescale_q(timestamp, ms_base, video_stream->time_base);
    pkt->dts = pkt->pts;
    
    if (is_key) pkt->flags |= AV_PKT_FLAG_KEY;
    pkt->stream_index = video_stream->index;

    // 写入 
    av_interleaved_write_frame(fmt_ctx, pkt);
    
    av_packet_free(&pkt);
    return 0;
}

// 音频写入函数
int TsMuxer::write_audio(void* data, int size, uint32_t timestamp) {
    if (!fmt_ctx || !audio_stream) return -1;

    AVPacket* pkt = av_packet_alloc();
    
    // 音频可以直接拷贝，FAAC 输出的已经是带 ADTS 头的 AAC 数据
    // FFmpeg 的 mpegts muxer 能直接处理 ADTS
    av_new_packet(pkt, size);
    memcpy(pkt->data, data, size);

    //  时间戳转换：毫秒 -> 音频流时间基 (例如 1/44100)
    AVRational ms_base = {1, 1000};
    pkt->pts = av_rescale_q(timestamp, ms_base, audio_stream->time_base);
    pkt->dts = pkt->pts; // 音频通常没有 B 帧，DTS=PTS
    
    pkt->stream_index = audio_stream->index;
    pkt->flags |= AV_PKT_FLAG_KEY; // AAC 每帧都是独立的，可以说是关键帧

    // 写入 (使用 interleaved 函数保证音视频 PTS 同步)
    av_interleaved_write_frame(fmt_ctx, pkt);

    av_packet_free(&pkt);
    return 0;
}