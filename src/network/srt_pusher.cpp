#include "network/srt_pusher.h"
#include <iostream>
#include <arpa/inet.h>

using namespace std;

SrtPusher::SrtPusher() {
    //srt_startup();
}

SrtPusher::~SrtPusher() {
    close();
    //srt_cleanup();
}

int SrtPusher::connect(const std::string& ip, int port, const std::string& stream_id) {
    sock = srt_create_socket();
    if (sock == SRT_INVALID_SOCK) {
        cerr << ">>[SRT] SRT Socket 创建失败" << endl;
        return -1;
    }

    // 1. 配置 SRT 发送模式
    bool yes = true;
    int latency = 200; // 延迟缓冲 (毫秒)，可根据网络状况调整 (50-500)
    srt_setsockopt(sock, 0, SRTO_SENDER, &yes, sizeof yes);
    srt_setsockopt(sock, 0, SRTO_LATENCY, &latency, sizeof latency);

    // 2. 配置 Stream ID (MediaMTX 必须)
    if (!stream_id.empty()) {
        srt_setsockopt(sock, 0, SRTO_STREAMID, stream_id.c_str(), stream_id.length());
    }

    // 3. 设置连接地址
    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &sa.sin_addr);

    cout << ">>[SRT] 正在连接 SRT: " << ip << ":" << port << " (" << stream_id << ")..." << endl;
    
    // 4. 发起连接 (阻塞直到成功或超时)
    if (srt_connect(sock, (sockaddr*)&sa, sizeof sa) == SRT_ERROR) {
        cerr << ">>[SRT] SRT 连接失败" << endl;
        return -1;
    }

    is_connected = true;
    cout << ">>[SRT] SRT 连接成功!" << endl;
    return 0;
}

int SrtPusher::send(void* data, int len) {
    if (!is_connected) return -1;
    
    // 发送数据
    const int MAX_CHUNK = 1316; // SRT 黄金法则
    char* ptr = (char*)data;
    int remaining = len;

    while (remaining > 0) {
        int chunk = (remaining > MAX_CHUNK) ? MAX_CHUNK : remaining;
        
        // 循环发送，直到发完
        int ret = srt_sendmsg(sock, ptr, chunk, -1, 0);
        if (ret == SRT_ERROR) {
            // std::cerr << "SRT Send Error" << std::endl;
            return -1;
        }
        ptr += chunk;
        remaining -= chunk;
    }
    return len;
}

void SrtPusher::close() {
    if (is_connected) {
        srt_close(sock);
        is_connected = false;
    }
}