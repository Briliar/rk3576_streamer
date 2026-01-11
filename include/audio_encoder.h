#pragma once
#include <faac.h>
#include <vector>
#include <cstdint>

class AudioEncoder {
public:
    AudioEncoder();
    ~AudioEncoder();

    // 初始化编码器
    int init(int sample_rate, int channels);

    // 编码一帧
    // input_pcm: 从 ALSA 读到的原始数据
    // output_aac: 编码后的数据会存到这里
    // 返回: 编码后的字节数
    int encode(const char* input_pcm, std::vector<uint8_t>& output_aac);

    // 获取编码器每帧需要的采样数 (通常是 1024)
    unsigned long get_input_samples() const { return input_samples_; }

private:
    faacEncHandle handle_ = nullptr;
    unsigned long input_samples_ = 0;   // 编码器每次输入需要多少个采样点
    unsigned long max_output_bytes_ = 0; // 编码后最大可能的字节数
};