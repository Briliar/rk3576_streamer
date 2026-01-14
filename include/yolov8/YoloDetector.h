#pragma once
#include <vector>
#include <string>
#include "rknn_api.h"
#include "yolov8/postprocess.h" 


// 定义检测结果结构体
struct Object {
    int id;             // 类别 ID
    float prob;         // 置信度 
    int x, y, w, h;     // 坐标框 
    std::string label;  // 类别名称 
};

class YoloDetector {
public:
    YoloDetector();
    ~YoloDetector();

    // 初始化：传入模型路径 (如 "model/yolov8.rknn")
    int init(const char* model_path);

    // 推理：传入 RGA 转换后的 RGB 数据指针
    // 返回检测到的物体列表
    std::vector<Object> detect(void* input_data);

private:
    // 读取文件辅助函数
    unsigned char* load_model(const char* filename, int* model_size);

private:

    rknn_app_context_t app_ctx;

    unsigned char* model_data;      // 模型二进制数据

};