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

    // 初始化：需要传入输出回调函数
    int init(int width, int height, int fps, DataCallback callback);
    
    // 输入 H.264 数据
    int input_frame(void* data, int size, uint32_t timestamp, bool is_key);

    void close();

private:
    AVFormatContext* fmt_ctx = nullptr;
    AVStream* stream = nullptr;
    AVIOContext* avio_ctx = nullptr;
    uint8_t* avio_buffer = nullptr;
    int avio_buffer_size = 32768;


    DataCallback output_callback;
    static int write_packet_cb(void* opaque, uint8_t* buf, int buf_size);
};