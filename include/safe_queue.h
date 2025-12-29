#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <iostream>

// 定义一个结构体来存 H.264 包
struct VideoPacket {
    void* data;         // 数据指针
    size_t size;        // 数据长度
    uint32_t timestamp; // 时间戳
    bool is_keyframe;   // 是否关键帧
};

class VideoPacketQueue {
public:
    // max_size: 队列最大长度，超过就开始丢帧
    VideoPacketQueue(size_t max_size = 60) : max_size_(max_size) {}

    ~VideoPacketQueue() {
        while (!queue_.empty()) {
            VideoPacket p = queue_.front();
            free(p.data);
            queue_.pop();
        }
    }

    // 【生产者调用】推入数据
    void push(const void* data, size_t size, uint32_t timestamp, bool keyframe) {
        std::lock_guard<std::mutex> lock(mtx_);

        // 策略：如果队列满了，丢弃最老的一帧 (Drop Head)
        // 这样能保证发出去的永远是比较新的画面
        if (queue_.size() >= max_size_) {
            VideoPacket& old = queue_.front();
            free(old.data); // 释放内存
            queue_.pop();
            std::cout << "网络拥堵，主动丢帧!" << std::endl;
        }

        VideoPacket packet;
        packet.size = size;
        packet.timestamp = timestamp;
        packet.is_keyframe = keyframe;
        
        // 【注意】必须深拷贝！
        // 因为 MPP 的 buffer 在下一轮循环就会被重写，不能只存指针
        packet.data = malloc(size);
        if (packet.data) {
            memcpy(packet.data, data, size);
            queue_.push(packet);
            cv_.notify_one(); // 唤醒消费者
        }
    }

    // 【消费者调用】取出数据 (阻塞等待)
    bool pop(VideoPacket& packet) {
        std::unique_lock<std::mutex> lock(mtx_);
        // 等待数据，或者收到停止信号
        cv_.wait(lock, [this]{ return !queue_.empty() || stop_flag_; });

        if (stop_flag_ && queue_.empty()) return false;

        packet = queue_.front();
        queue_.pop();
        return true;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mtx_);
        stop_flag_ = true;
        cv_.notify_all();
    }

private:
    std::queue<VideoPacket> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    size_t max_size_;
    bool stop_flag_ = false;
};