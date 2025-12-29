#pragma once
#include <string>
#include <srt/srt.h>

class SrtPusher {
public:
    SrtPusher();
    ~SrtPusher();

    // 连接服务器
    // stream_id 格式: #!::u=publish/live/test
    int connect(const std::string& ip, int port, const std::string& stream_id);
    
    // 发送数据
    int send(void* data, int len);
    
    void close();

private:
    SRTSOCKET sock = SRT_INVALID_SOCK;
    bool is_connected = false;
};