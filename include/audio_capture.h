#pragma once
#include <alsa/asoundlib.h>
#include <string>

class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    /**
     * @brief 初始化音频设备
     * @param device_name 设备名，板载一般是 "default"，USB麦克风可能是 "plughw:1,0"
     * @param sample_rate 采样率，推荐 44100
     * @param channels 通道数，推荐 2
     * @return 0 成功, -1 失败
     */
    int init(const std::string& device_name = "plughw:0,0", int sample_rate = 44100, int channels = 2);

    /**
     * @brief 读取一帧音频数据 (阻塞读取)
     * @param buffer 输出缓冲区，大小必须 >= get_buffer_size()
     * @return 实际读取的帧数，<0 表示错误
     */
    int read_frame(char* buffer);

    size_t get_buffer_size() const;
    int get_sample_rate() const { return sample_rate_; }
    int get_channels() const { return channels_; }

private:
    snd_pcm_t* pcm_handle_;       // ALSA 设备句柄
    int sample_rate_;
    int channels_;
    snd_pcm_uframes_t frames_;    // 每次读取多少帧 (AAC编码通常需要1024)
};

void run_audio_test();