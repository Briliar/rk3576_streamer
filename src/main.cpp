#include <cstring>
#include <iostream>   
#include <unistd.h>     // 用于 close 函数
#include <termios.h>

#include "StreamerApp.h"


// 全局指针供信号处理使用
StreamerApp* g_app = nullptr;

void sig_handler(int sig) {
    if (g_app) {
        printf("\n>>[Signal] 收到退出信号 (%d)\n", sig);
        g_app->signalStop(); 
    }
}


int main(int argc, char** argv) {
    // 注册信号
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    AppConfig config;
    
    // 1. 创建应用实例
    StreamerApp app;
    g_app = &app;

    // 2. 初始化
    if (!app.init(config)) {
        return -1;
    }

    // 3. 启动后台服务
    app.start();

    // 4. 运行主业务 (阻塞)
    app.runMainLoop();

    // 5. 停止与清理
    app.stop();

    // 6. 强制退出 
    printf(">>[System] You are very handsome!\n");
    fflush(stdout);
    _exit(0);
}

