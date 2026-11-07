#pragma once
#include "RHI/RHIHandle.h"
#include <vector>

namespace Catalpa {
    class RHIDevice;
    // 图临时物理buffer的唯一owner。计划销毁只归还owner；在飞命令继续保留原生引用。
    struct RGTransientStorage final {
        explicit RGTransientStorage(RHIDevice& value) : device(value) {}
        ~RGTransientStorage();
        RHIDevice& device;
        std::vector<BufferHandle> buffers;
    };
} // namespace Catalpa
