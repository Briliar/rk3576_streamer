#include <iostream>   
#include <unistd.h>     // 用于 close 函数
#include <termios.h>

#include "StreamerApp.h"
#include "config.h"

// 全局指针供信号处理使用
StreamerApp* g_app = nullptr;

void sig_handler(int sig) {
    if (g_app) {
        printf("\n>>[Signal] 收到退出信号 (%d)\n", sig);
        g_app->signalStop(); 
    }
}
// 将终端设置为“即时读取模式” (不需要按回车)
void setTerminalRawMode(bool enable) {
    static struct termios oldt, newt;
    if (enable) {
        // 获取当前终端属性
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        // 关闭 ICANON (规范模式，即缓冲模式) 和 ECHO (回显)
        newt.c_lflag &= ~(ICANON | ECHO);
        // 设置立刻读取
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    } else {
        // 恢复原有属性
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }
}

void keyboard_listener() {
    printf(">>[Key] 键盘监听已启动 (按 'a' 切换AI, 'q' 退出)\n");
    
    while (true) {
        // getchar 在 Raw 模式下会阻塞等待，但按键瞬间就会返回
        char c = getchar();

        if (g_app) {
            if (c == 'a' || c == 'A') {
                // 读取当前状态并取反
                // 注意：这里我们需要给 StreamerApp 加一个 getAiEnabled 接口，
                // 或者简单一点，我们在 StreamerApp 里加一个 toggleAiEnabled 函数
                // 这里暂时假设我们自己维护一个状态，或者直接设置 true/false
                
                // 更加优雅的做法是在 App 里加一个 toggle 函数，这里我们演示传参：
                // 我们可以用一个静态变量模拟 toggle，或者去 StreamerApp 加接口
                static bool current_ai = false;
                current_ai = !current_ai;
                g_app->setAiEnabled(current_ai);
            }
            else if (c == 'q' || c == 'Q') {
                printf("\n>>[Key] 收到退出指令\n");
                g_app->signalStop();
                break; // 退出监听循环
            }
        }
        
        // 防止 CPU 占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main(int argc, char** argv) {
    // 注册信号
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // 1. 创建应用实例
    StreamerApp app;
    g_app = &app;

    // 2. 初始化
    if (!app.init("/dev/video0",WIDTH, HEIGHT, FPS, "model/yolov8.rknn")) {
        return -1;
    }

    // 3. 启动后台服务
    app.start(SERVER_IP, SERVER_PORT, STREAM_KEY);

    // 1. 先设置终端为 Raw 模式 (无回车，无回显)
    setTerminalRawMode(true);
    
    // 2. 启动线程
    std::thread key_thread(keyboard_listener);
    key_thread.detach(); // 分离线程，让它自己在后台跑

    // 4. 运行主业务 (阻塞)
    app.runMainLoop();

    setTerminalRawMode(false);

    // 5. 停止与清理
    app.stop();

    // 6. 强制退出 
    printf(">>[System] Byte!\n");
    fflush(stdout);
    _exit(0);
}

