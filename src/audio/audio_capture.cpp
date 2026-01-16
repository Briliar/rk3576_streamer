#include "audio/audio_capture.h"
#include <iostream>
#include <vector>
#include <cmath> // 为了计算音量

AudioCapture::AudioCapture() : pcm_handle_(nullptr), sample_rate_(44100), channels_(2), frames_(1024) {}

AudioCapture::~AudioCapture() {
    if (pcm_handle_) {
        snd_pcm_close(pcm_handle_);
    }
}

int AudioCapture::init(const std::string& device_name, int sample_rate, int channels) {
    this->sample_rate_ = sample_rate;
    this->channels_ = channels;
    // AAC 编码通常每帧需要 1024 个采样点
    this->frames_ = 1024; 

    int rc;

    // 1. 打开 PCM 设备 (Capture 模式)
    rc = snd_pcm_open(&pcm_handle_, device_name.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        std::cerr << ">>[ALSA] 无法打开音频设备: " << device_name 
                  << " 错误: " << snd_strerror(rc) << std::endl;
        return -1;
    }

    // 2. 分配硬件参数对象
    snd_pcm_hw_params_t* params;
    snd_pcm_hw_params_alloca(&params);

    // 3. 填充默认参数
    snd_pcm_hw_params_any(pcm_handle_, params);

    // 4. 设置为【交错模式】(Interleaved)
    // 意思是数据排列为：左声道样本1, 右声道样本1, 左样本2, 右样本2...
    snd_pcm_hw_params_set_access(pcm_handle_, params, SND_PCM_ACCESS_RW_INTERLEAVED);

    // 5. 设置格式为 S16_LE (16位小端)
    // 这是最通用的格式，兼容性最好
    snd_pcm_hw_params_set_format(pcm_handle_, params, SND_PCM_FORMAT_S16_LE);

    // 6. 设置通道数 (2 = 立体声)
    snd_pcm_hw_params_set_channels(pcm_handle_, params, channels);

    // 7. 设置采样率 (44100Hz)
    unsigned int val = sample_rate;
    int dir = 0;
    snd_pcm_hw_params_set_rate_near(pcm_handle_, params, &val, &dir);

    // 8. 将参数写入硬件
    rc = snd_pcm_hw_params(pcm_handle_, params);
    if (rc < 0) {
        std::cerr << ">>[ALSA] 无法设置硬件参数: " << snd_strerror(rc) << std::endl;
        return -1;
    }

    std::cout << ">>[ALSA] 音频初始化成功 | 采样率: " << val << " | 通道: " << channels << std::endl;
    return 0;
}

int AudioCapture::read_frame(char* buffer) {
    if (!pcm_handle_) return -1;

    // 9. 读取数据 (核心步骤)
    // 注意：这里传入的是 frames (1024)，不是字节数
    // ALSA 会自动算出需要多少字节 (1024 * 2声道 * 2字节 = 4096字节)
    int rc = snd_pcm_readi(pcm_handle_, buffer, frames_);

    if (rc == -EPIPE) {
        // 这是一个非常经典的错误：Overrun (录音溢出)
        // 意思是你的程序卡了一下，没来得及读，麦克风缓存满了
        std::cerr << ">>[ALSA] Buffer Overrun! (掉帧了)" << std::endl;
        snd_pcm_prepare(pcm_handle_); // 必须调用这个恢复设备
        return 0; // 这次没读到数据，但不要退出程序
    } else if (rc < 0) {
        std::cerr << ">>[ALSA] 读取错误: " << snd_strerror(rc) << std::endl;
        return -1;
    }

    return rc; // 返回读取到的帧数
}

size_t AudioCapture::get_buffer_size() const {
    // 帧数 * 通道数 * 每个样本的字节数(16bit=2bytes)
    return frames_ * channels_ * 2; 
}

