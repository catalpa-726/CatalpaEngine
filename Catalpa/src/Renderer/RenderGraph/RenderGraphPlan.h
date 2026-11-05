#pragma once

#include "RenderGraphPass.h"
#include "RHI/RHIResourceState.h"
#include "RHI/RHIError.h"
#include "Core/Base.h"
#include "RHI/RHIQueryDesc.h"

// 持有编译结果：稳定拓扑顺序、屏障和待提交状态；外部只能读取诊断，不能改写命令步骤。
namespace Catalpa {

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
            m_Schedule = std::move(other.m_Schedule);
            m_Queries = std::move(other.m_Queries);
            m_Valid = std::exchange(other.m_Valid, false);
            return *this;
        }

        // 按编译顺序录制，不自行提交或发布设备状态。
        [[nodiscard]] CATALPA_API RHIStatus Record(RHICommandList& commands) const;
        // 只读编译诊断不暴露可改写的命令步骤或设备状态。
        [[nodiscard]] bool IsValid() const noexcept { return m_Valid; }
        [[nodiscard]] const RGCompiledSchedule& GetSchedule() const noexcept { return m_Schedule; }

    private:
        friend class RenderGraphCompiler;
        friend class RenderGraphExecutor;

        enum class StepKind { Copy, TextureUpload, TextureReadback, Draw, Transition, Clear };

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
            TextureViewHandle sourceView{};
            RHITextureBufferCopy readbackCopy{};
            RGIndexedDrawDesc draw{};
            std::vector<RHIBinding> bindings;
            BufferHandle vertices{};
            BufferHandle indices{};
            TextureViewHandle color{};
            TextureViewHandle depth{};
            Extent2D extent{};
        };

        std::vector<Step> m_Steps;
        std::vector<RHIBufferStateUpdate> m_BufferUpdates;
        std::vector<RHITextureStateUpdate> m_TextureUpdates;
        RGCompiledSchedule m_Schedule;
        std::shared_ptr<RHIQueryPool> m_Queries;
        bool m_Valid = false;
    };

}
