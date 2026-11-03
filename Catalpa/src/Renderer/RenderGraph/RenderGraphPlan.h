#pragma once

#include "RenderGraphPass.h"
#include "RHI/RHIResourceState.h"
#include "RHI/RHIError.h"

// 持有编译结果：固定执行顺序、屏障和待提交状态。编译完成后，外部不能任意修改内部步骤
namespace Catalpa {

    class RenderGraph;
    class RHICommandList;

    class RenderGraphPlan {
    public:
        RenderGraphPlan() = default;

        RenderGraphPlan(const RenderGraphPlan&) = delete;
        RenderGraphPlan& operator=(const RenderGraphPlan&) = delete;

        // 移动时使源计划失效，避免留下 valid 的空壳。
        RenderGraphPlan(RenderGraphPlan&& other) noexcept { *this = std::move(other); }

        RenderGraphPlan& operator=(RenderGraphPlan&& other) noexcept
        {
            if (this == &other) return *this;

            m_Steps = std::move(other.m_Steps);
            m_BufferUpdates = std::move(other.m_BufferUpdates);
            m_TextureUpdates = std::move(other.m_TextureUpdates);
            m_Valid = std::exchange(other.m_Valid, false);
            return *this;
        }

        // 按编译顺序录制，不自行提交或发布设备状态。
        [[nodiscard]] RHIStatus Record(RHICommandList& commands) const;

    private:
        friend class RenderGraph;

        enum class StepKind { Copy, TextureUpload, Draw, Transition };

        struct Step {
            std::string name;
            StepKind kind = StepKind::Copy;
            std::vector<RHIBufferBarrier> bufferBarriers;
            std::vector<RHITextureBarrier> textureBarriers;
            BufferHandle source{};
            BufferHandle destination{};
            RHIBufferCopy copy{};
            TextureViewHandle destinationView{};
            RHIBufferTextureCopy textureCopy{};
            RGIndexedDrawDesc draw{};
            BufferHandle vertices{};
            BufferHandle indices{};
            TextureViewHandle color{};
            Extent2D extent{};
        };

        std::vector<Step> m_Steps;
        std::vector<RHIBufferStateUpdate> m_BufferUpdates;
        std::vector<RHITextureStateUpdate> m_TextureUpdates;
        bool m_Valid = false;
    };

}