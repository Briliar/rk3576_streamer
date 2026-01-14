#include "yolov8/YoloDetector.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>

static const char* COCO_LABELS[] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
    "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
    "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
    "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
    "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush"
};

// 构造函数：初始化指针
YoloDetector::YoloDetector() {
    memset(&app_ctx, 0, sizeof(rknn_app_context_t));
    model_data = nullptr;
}

// 析构函数：释放所有资源
YoloDetector::~YoloDetector() {
    if (app_ctx.input_attrs) free(app_ctx.input_attrs);
    if (app_ctx.output_attrs) free(app_ctx.output_attrs);
    if (model_data) free(model_data);
    if (app_ctx.rknn_ctx) rknn_destroy(app_ctx.rknn_ctx);
}

// 辅助函数：读取二进制文件到内存
unsigned char* YoloDetector::load_model(const char* filename, int* model_size) {
    FILE* fp = fopen(filename, "rb");
    if (fp == nullptr) {
        printf("Open model file %s failed\n", filename);
        return nullptr;
    }
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char* data = (unsigned char*)malloc(size);
    fread(data, 1, size, fp);
    fclose(fp);
    *model_size = size;
    return data;
}

int YoloDetector::init(const char* model_path) {
    int ret = 0;
    int model_data_size = 0;

    model_data = load_model(model_path, &model_data_size);
    if (!model_data) return -1;

    // 1. 初始化 
    ret = rknn_init(&app_ctx.rknn_ctx, model_data, model_data_size, 0, NULL);
    if (ret < 0) return -1;

    // 2. 获取各种属性填入结构体
    ret = rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_IN_OUT_NUM, &app_ctx.io_num, sizeof(app_ctx.io_num));
    
    app_ctx.input_attrs = (rknn_tensor_attr*)malloc(sizeof(rknn_tensor_attr) * app_ctx.io_num.n_input);
    memset(app_ctx.input_attrs, 0, sizeof(rknn_tensor_attr) * app_ctx.io_num.n_input);
    for (int i = 0; i < app_ctx.io_num.n_input; i++) {
        app_ctx.input_attrs[i].index = i;
        rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_INPUT_ATTR, &(app_ctx.input_attrs[i]), sizeof(rknn_tensor_attr));
    }

    app_ctx.output_attrs = (rknn_tensor_attr*)malloc(sizeof(rknn_tensor_attr) * app_ctx.io_num.n_output);
    memset(app_ctx.output_attrs, 0, sizeof(rknn_tensor_attr) * app_ctx.io_num.n_output);
    for (int i = 0; i < app_ctx.io_num.n_output; i++) {
        app_ctx.output_attrs[i].index = i;
        rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_OUTPUT_ATTR, &(app_ctx.output_attrs[i]), sizeof(rknn_tensor_attr));
    }

    // 3. 设置一些关键标记
    if (app_ctx.input_attrs[0].fmt == RKNN_TENSOR_NHWC) {
        app_ctx.model_height = app_ctx.input_attrs[0].dims[1];
        app_ctx.model_width = app_ctx.input_attrs[0].dims[2];
        app_ctx.model_channel = app_ctx.input_attrs[0].dims[3];
    } else {
        // NCHW 情况
        app_ctx.model_channel = app_ctx.input_attrs[0].dims[1];
        app_ctx.model_height = app_ctx.input_attrs[0].dims[2];
        app_ctx.model_width = app_ctx.input_attrs[0].dims[3];
    }

    // 这一步很重要：官方 post_process 里会检查 is_quant
    // 如果是 UINT8 量化模型，这里必须设为 true
    if (app_ctx.output_attrs[0].type == RKNN_TENSOR_INT8 || app_ctx.output_attrs[0].type == RKNN_TENSOR_UINT8) {
        app_ctx.is_quant = true;
    } else {
        app_ctx.is_quant = false;
    }

    return 0;
}

std::vector<Object> YoloDetector::detect(void* input_data) {
    std::vector<Object> results;
    int ret;

    // 设置 input 
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8; // 这里的类型要看你的模型量化类型，通常是 UINT8
    inputs[0].size = app_ctx.model_width * app_ctx.model_height * app_ctx.model_channel;
    inputs[0].fmt = RKNN_TENSOR_NHWC;   // RGA 输出的 RGB 也是 NHWC 排列
    inputs[0].pass_through = 0;
    inputs[0].buf = input_data;         // <--- 关键：直接挂载外部指针

    ret = rknn_inputs_set(app_ctx.rknn_ctx, app_ctx.io_num.n_input, inputs);
    if (ret < 0) {
        printf("rknn_inputs_set failed! ret=%d\n", ret);
        return results;
    }

    // 执行推理
    ret = rknn_run(app_ctx.rknn_ctx, NULL);

    // 获取输出
    rknn_output outputs[app_ctx.io_num.n_output];
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < app_ctx.io_num.n_output; i++) {
        outputs[i].want_float = 0; // 我们要原生数据，让 post_process 去处理反量化
    }
    ret = rknn_outputs_get(app_ctx.rknn_ctx, app_ctx.io_num.n_output, outputs, NULL);

    // 后处理
    letterbox_t lb;
    lb.target_width = app_ctx.model_width;
    lb.target_height = app_ctx.model_height;
    lb.scale = 1.0f; 
    lb.x_pad = 0;
    lb.y_pad = 0;

    // 2. 准备结果容器
    object_detect_result_list od_results;

    // 3. 调用官方函数 
    // conf_thresh = 0.25, nms_thresh = 0.45 (常用默认值)
    post_process(&app_ctx, outputs, &lb, 0.25f, 0.45f, &od_results);

    // 4. 转换结果
    for (int i = 0; i < od_results.count; i++) {
        Object obj;
        obj.id = od_results.results[i].cls_id;
        obj.prob = od_results.results[i].prop;
        // 这里的坐标是基于 640x640 的
        obj.x = od_results.results[i].box.left;
        obj.y = od_results.results[i].box.top;
        obj.w = od_results.results[i].box.right - od_results.results[i].box.left;
        obj.h = od_results.results[i].box.bottom - od_results.results[i].box.top;

        if (obj.id >= 0 && obj.id < 80) {
            obj.label = COCO_LABELS[obj.id];
        } else {
            obj.label = "unknown";
        }
        
        results.push_back(obj);
    }

    rknn_outputs_release(app_ctx.rknn_ctx, app_ctx.io_num.n_output, outputs);
    return results;
}