#pragma once

#include "catpch.h"


namespace Catalpa {
    struct ActionSnapshot;
    struct RenderFramePacket;

    // 只在当前系统调用期间借用的输入和提取目标；不提供设备或全局服务定位器。
    struct RuntimeServices {
        const ActionSnapshot* input = nullptr;
        uint32_t targetWidth = 0;
        uint32_t targetHeight = 0;
        RenderFramePacket* renderFrame = nullptr;
    };
}
