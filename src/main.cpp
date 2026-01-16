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
// 打印帮助信息
void print_help(const char* prog_name) {
    printf("用法: %s [选项]\n", prog_name);
    printf("选项:\n");
    printf("  -s, --stream   开启网络推流\n");
    printf("  -r, --record   开启本地录像\n");
    printf("  -a, --ai       开启AI检测\n");
    printf("  -h, --help     显示帮助\n");
}

int main(int argc, char** argv) {
    // 注册信号
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    AppConfig config;
   
    bool has_args = false;
    for (int i = 1; i < argc; i++) {
        // 开关配置
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--stream") == 0) {
            config.enable_stream = true;
            has_args = true;
        }
        else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--record") == 0) {
            config.enable_record = true;
            has_args = true;
        }
        else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--ai") == 0) {
            config.enable_ai = true;
            has_args = true;
        }
        else if ((strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)) {
            print_help(argv[0]);
            return 0;
        }
    }

    if (!has_args) {
        printf(">>[Info] 未指定参数，程序自动退出\n");
        print_help(argv[0]);
        _exit(0);
    }

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

