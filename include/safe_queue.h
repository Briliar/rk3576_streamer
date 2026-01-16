#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <iostream>


enum MediaType {
    MEDIA_VIDEO,
    MEDIA_AUDIO
};

// 定义一个结构体来存 H.264 包
struct MediaPacket {
    void* data;         // 数据指针
    size_t size;        // 数据长度
    uint32_t timestamp; // 时间戳
    bool is_keyframe;   // 是否关键帧
    MediaType type;      // 标记类型
};

class MediaPacketQueue {
public:
    // max_size: 队列最大长度，超过就开始丢帧
    MediaPacketQueue(int max_size, std::string name = "Queue") 
        : max_size_(max_size), queue_name_(name) {}

    ~MediaPacketQueue() {
        clear();
    }

    // 【生产者调用】推入数据
    void push(const void* data, size_t size, uint32_t timestamp, bool keyframe, MediaType type = MEDIA_VIDEO) {
        std::lock_guard<std::mutex> lock(mtx_);

        // --- 丢帧策略 (Drop Head) ---
        if (queue_.size() >= max_size_) {
            MediaPacket& old = queue_.front();
            if (old.data) free(old.data);
            queue_.pop();
            
            //  智能日志：每隔 1000ms 最多打印一次，防止刷屏
            long long now = get_current_ms();
            if (now - last_log_time_ > 1000) {
                // 如果是推流队列，这是警告；如果是录像队列，这是严重错误
                std::cout << ">>[Warn] " << queue_name_ << " 拥堵! 自动丢弃旧帧 (当前缓存: " 
                          << max_size_ << ")" << std::endl;
                last_log_time_ = now;
            }
        }

        // --- 正常入队 ---
        MediaPacket packet;
        packet.size = size;
        packet.timestamp = timestamp;
        packet.is_keyframe = keyframe;
        packet.type = type;

        // 深拷贝数据
        packet.data = malloc(size);
        if (packet.data) {
            memcpy(packet.data, data, size);
            queue_.push(packet);
            cv_.notify_one();
        }
    }

    // 【消费者调用】取出数据 (阻塞等待)
    bool pop(MediaPacket& packet) {
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
    //给外部清空队列用（例如断开重连时）
    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        while (!queue_.empty()) {
            MediaPacket p = queue_.front();
            if (p.data) free(p.data);
            queue_.pop();
        }
    }
private:
    // 辅助：获取毫秒时间戳
    long long get_current_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
private:
    std::queue<MediaPacket> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    size_t max_size_;
    bool stop_flag_ = false;

    std::string queue_name_;    // 队列名字
    long long last_log_time_ = 0; // 上次打印警告的时间
};