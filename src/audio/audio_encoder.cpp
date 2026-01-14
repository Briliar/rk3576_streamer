#include "audio/audio_encoder.h"
#include <cstring>
#include <iostream>

AudioEncoder::AudioEncoder() {}

AudioEncoder::~AudioEncoder() {
    if (handle_) faacEncClose(handle_);
}

int AudioEncoder::init(int sample_rate, int channels) {
    // 1. 打开编码器
    handle_ = faacEncOpen(sample_rate, channels, &input_samples_, &max_output_bytes_);
    if (!handle_) {
        std::cerr << ">>[FAAC] 打开失败" << std::endl;
        return -1;
    }

    // 2. 获取当前配置
    faacEncConfigurationPtr conf = faacEncGetCurrentConfiguration(handle_);
    
    // 3. 设置输入格式为 16位 (跟 ALSA 一致)
    conf->inputFormat = FAAC_INPUT_16BIT;
    
    // 4. 设置输出格式为 ADTS
    // 0 = Raw , 1 = ADTS
    conf->outputFormat = 1; 

    // 5. 应用配置
    if (faacEncSetConfiguration(handle_, conf) == 0) {
        std::cerr << ">>[FAAC] 配置失败" << std::endl;
        return -1;
    }

    std::cout << ">>[FAAC] 初始化成功 | InputSamples: " << input_samples_ << std::endl;
    return 0;
}

int AudioEncoder::encode(const char* input_pcm, std::vector<uint8_t>& output_aac) {
    if (!handle_) return -1;

    // 确保输出 buffer 够大
    if (output_aac.size() < max_output_bytes_) {
        output_aac.resize(max_output_bytes_);
    }

    // 执行编码
    // 注意：input_pcm 是 char*，但 FAAC 要 int32_t*，这里需要强转
    // 实际上它读取的是 16bit 数据，内部会自动处理
    int bytes_out = faacEncEncode(handle_, (int32_t*)input_pcm, input_samples_, output_aac.data(), max_output_bytes_);

    if (bytes_out > 0) {
        // 调整 vector 大小为实际数据量
        output_aac.resize(bytes_out);
    }

    return bytes_out;
}