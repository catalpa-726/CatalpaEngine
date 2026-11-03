#pragma once

#include "Core/Base.h"
#include "RHIError.h"
#include "RHITypes.h"
#include "RHIRenderingDesc.h"

// 提供不包含 Vulkan 类型的命令录制接口
namespace Catalpa {

    enum class RHICommandListState { Uninitialized, Initial, Recording, Rendering, Executable, Pending, Invalid };

    class CATALPA_API RHICommandList {
    public:
        virtual ~RHICommandList() = default;

        [[nodiscard]] virtual RHIStatus Begin() = 0;
        [[nodiscard]] virtual RHIStatus End() = 0;

        // 当前支持二维、单层、单采样颜色附件。
        // 附件执行时须已处于颜色附件状态；本函数不执行布局转换。
        [[nodiscard]] virtual RHIStatus BeginRendering(const RHIRenderingDesc& desc) = 0;

        [[nodiscard]] virtual RHIStatus EndRendering() = 0;

        virtual RHICommandListState GetState() const = 0;

        [[nodiscard]] virtual RHIStatus SetViewportAndScissor(const RHIViewport& viewport, const Rect2D& scissor) = 0;
    };

}