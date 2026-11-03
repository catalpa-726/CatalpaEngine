#include "catpch.h"
#include "RenderGraphPlan.h"
#include "RHI/RHICommandList.h"

namespace Catalpa {

    RHIStatus RenderGraphPlan::Record(RHICommandList& commands) const
    {
        if (!m_Valid)
            return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraphPlan::Record" };

        if (const RHIStatus status = commands.Begin(); !status.IsSuccess()) return status;

        if (const RHIStatus status = commands.SetResourceStatePlan(m_BufferUpdates, m_TextureUpdates); !status.IsSuccess())
            return status;

        {
            RHICommandLabelScope frameLabel(commands, "Frame", { 0.2f, 0.4f, 0.8f, 1.0f });

            for (const auto& step : m_Steps) {
                RHICommandLabelScope passLabel(commands, step.name.c_str(), { 0.4f, 0.6f, 0.8f, 1.0f });

                if (!step.bufferBarriers.empty()) {
                    if (const RHIStatus status = commands.PipelineBarrier(std::span<const RHIBufferBarrier>{ step.bufferBarriers }); !status.IsSuccess())
                        return status;
                }

                if (!step.textureBarriers.empty()) {
                    if (const RHIStatus status = commands.PipelineBarrier(std::span<const RHITextureBarrier>{ step.textureBarriers }); !status.IsSuccess())
                        return status;
                }

                switch (step.kind) {
                    case StepKind::Copy:
                        if (const RHIStatus status = commands.CopyBuffer(step.source, step.destination, step.copy); !status.IsSuccess())
                            return status;
                        break;

                    case StepKind::TextureUpload:
                        if (const RHIStatus status = commands.CopyBufferToTexture(step.source, step.destinationView, step.textureCopy); !status.IsSuccess())
                            return status;
                        break;

                    case StepKind::Draw:
                    {
                        const auto& draw = step.draw;

                        const RHIColorAttachment color{
                            .view = step.color,
                            .loadOp = draw.loadOp,
                            .storeOp = draw.storeOp,
                            .clearColor = draw.clearColor
                        };

                        const RHIRenderingDesc rendering{
                            .colorAttachments = { &color, 1 },
                            .renderArea = {.extent = step.extent },
                            .sampleCount = 1
                        };

                        if (const RHIStatus status = commands.BeginRendering(rendering); !status.IsSuccess()) return status;
                        if (const RHIStatus status = commands.BindGraphicsPipeline(draw.pipeline); !status.IsSuccess()) return status;
                        if (!draw.pushConstants.empty()) {
                            if (const RHIStatus status = commands.SetGraphicsPushConstants(draw.pushConstants); !status.IsSuccess())
                                return status;
                        }
                        if (const RHIStatus status = commands.BindVertexBuffer(0, step.vertices, draw.vertexOffset); !status.IsSuccess()) return status;
                        if (const RHIStatus status = commands.BindIndexBuffer(step.indices, draw.indexType, draw.indexOffset); !status.IsSuccess()) return status;
                        if (const RHIStatus status = commands.SetViewportAndScissor(draw.viewport, draw.scissor); !status.IsSuccess()) return status;
                        if (const RHIStatus status = commands.DrawIndexed(draw.indexCount, draw.instanceCount, draw.firstIndex, draw.baseVertex, draw.firstInstance); !status.IsSuccess()) return status;
                        if (const RHIStatus status = commands.EndRendering(); !status.IsSuccess()) return status;
                        break;
                    }

                    case StepKind::Transition:
                        // 只执行上面的屏障；实际 Present 仍由交换链接口完成。
                        break;

                    default:
                        return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraphPlan::Step" };
                }
            }
        }

        return commands.End();
    }

}