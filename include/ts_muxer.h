#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/bsf.h> 
}
#include <vector>
#include <functional>

// 定义一个回调函数类型，用来把 TS 数据吐出去
using DataCallback = std::function<int(void* data, int len)>;

class TsMuxer {
public:
    TsMuxer();
    ~TsMuxer();

    int init(int width, int height, int fps, int sample_rate, int channels, DataCallback callback);

    int write_video(void* data, int size, uint32_t timestamp, bool is_key);

    int write_audio(void* data, int size, uint32_t timestamp);

    void close();

private:

    AVFormatContext* fmt_ctx = nullptr;
    AVIOContext* avio_ctx = nullptr;
    uint8_t* avio_buffer = nullptr;
    int avio_buffer_size = 32768;

    AVStream* video_stream = nullptr; // 视频流
    AVStream* audio_stream = nullptr; // 音频流
    DataCallback output_callback;
    static int write_packet_cb(void* opaque, uint8_t* buf, int buf_size);
};