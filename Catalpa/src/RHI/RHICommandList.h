#pragma once

#include "Core/Base.h"
#include "RHIError.h"

// 提供不包含 Vulkan 类型的命令录制接口
namespace Catalpa {

    enum class RHICommandListState { Uninitialized, Initial, Recording, Executable, Pending, Invalid };

    class CATALPA_API RHICommandList {
    public:
        virtual ~RHICommandList() = default;

        [[nodiscard]] virtual RHIStatus Begin() = 0;
        [[nodiscard]] virtual RHIStatus End() = 0;

        virtual RHICommandListState GetState() const = 0;
    };

}