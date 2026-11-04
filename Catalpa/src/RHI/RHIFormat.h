#pragma once

#include "Images/PixelFormat.h"

// 普通最优布局图像的单项格式能力；静态格式身份不代表设备支持。
namespace Catalpa {
    struct RHIFormatSupport {
        bool sampledImage = false;
        bool linearFiltering = false;
        bool storageImage = false;
        bool colorAttachment = false;
        bool colorAttachmentBlend = false;
        bool transferSource = false;
        bool transferDestination = false;
        bool depthAttachment = false;
    };
}
