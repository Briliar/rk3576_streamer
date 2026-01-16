#pragma once
#include <rga/im2d.h> // RGA 的核心头文件
#include <rga/RgaApi.h>

// 初始化 RGA (其实主要是打印一下版本)
int init_rga();

int rga_convert(void* src_ptr, int src_fd, int src_w, int src_h, int src_fmt,
                void* dst_ptr, int dst_fd, int dst_w, int dst_h, int dst_fmt);