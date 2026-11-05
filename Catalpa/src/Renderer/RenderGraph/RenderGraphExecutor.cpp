#include "catpch.h"
#include "RenderGraphExecutor.h"
#include "RenderGraphPlan.h"
#include "RHI/RHICommandList.h"

namespace Catalpa {

    RHIStatus RenderGraphExecutor::Record(const RenderGraphPlan& plan, RHICommandList& commands)
    {
        if (!plan.m_Valid)
            return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraphPlan::Record" };

        if (const RHIStatus status = commands.Begin(); !status.IsSuccess()) return status;

        if (const RHIStatus status = commands.SetResourceStatePlan(plan.m_BufferUpdates, plan.m_TextureUpdates); !status.IsSuccess())
            return status;

        if (plan.m_Queries) {
            if (const auto status = commands.ResetQueries(plan.m_Queries, 0, 2); !status.IsSuccess()) return status;
            if (const auto status = commands.WriteTimestamp(plan.m_Queries, 0, false); !status.IsSuccess()) return status;
        }

        {
            RHICommandLabelScope frameLabel(commands, "Frame", { 0.2f, 0.4f, 0.8f, 1.0f });

            for (const auto& step : plan.m_Steps) {
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
                    case RenderGraphPlan::StepKind::Copy:
                        if (const RHIStatus status = commands.CopyBuffer(step.source, step.destination, step.copy); !status.IsSuccess())
                            return status;
                        break;

                    case RenderGraphPlan::StepKind::TextureUpload:
                        if (const RHIStatus status = commands.CopyBufferToTexture(step.source, step.destinationView, step.textureCopy); !status.IsSuccess())
                            return status;
                        break;

                    case RenderGraphPlan::StepKind::TextureReadback:
                        if (const auto status = commands.CopyTextureToBuffer(step.sourceView, step.destination, step.readbackCopy); !status.IsSuccess())
                            return status;
                        break;

                    case RenderGraphPlan::StepKind::Clear:
                    {
                        const RHIColorAttachment color{
                            .view = step.color, .loadOp = RHILoadOp::Clear,
                            .storeOp = RHIStoreOp::Store, .clearColor = step.draw.clearColor
                        };
                        RHIRenderingDesc rendering{
                            .renderArea = { .extent = step.extent }, .sampleCount = 1
                        };
                        if (step.color.HasValue()) rendering.colorAttachments = { &color, 1 };
                        if (step.depth.HasValue()) rendering.depthAttachment = RHIDepthAttachment{
                            .view = step.depth, .loadOp = RHILoadOp::Clear,
                            .storeOp = RHIStoreOp::Store, .clearDepth = step.draw.clearDepth };
                        if (const auto status = commands.BeginRendering(rendering); !status.IsSuccess()) return status;
                        if (const auto status = commands.EndRendering(); !status.IsSuccess()) return status;
                        break;
                    }

                    case RenderGraphPlan::StepKind::Draw:
                    {
                        const auto& draw = step.draw;

                        const RHIColorAttachment color{
                            .view = step.color,
                            .loadOp = draw.loadOp,
                            .storeOp = draw.storeOp,
                            .clearColor = draw.clearColor
                        };

                        RHIRenderingDesc rendering{
                            .renderArea = {.extent = step.extent },
                            .sampleCount = 1
                        };
                        if (step.color.HasValue()) rendering.colorAttachments = { &color, 1 };
                        if (step.depth.HasValue()) rendering.depthAttachment = RHIDepthAttachment{
                            .view = step.depth, .loadOp = draw.depthLoadOp, .storeOp = draw.depthStoreOp,
                            .clearDepth = draw.clearDepth, .readOnly = draw.depthReadOnly
                        };

                        if (const RHIStatus status = commands.BeginRendering(rendering); !status.IsSuccess()) return status;
                        if (const RHIStatus status = commands.BindGraphicsPipeline(draw.pipeline); !status.IsSuccess()) return status;
                        if (!step.bindings.empty()) {
                            if (const RHIStatus status = commands.SetGraphicsBindings(step.bindings); !status.IsSuccess())
                                return status;
                        }
                        if (!draw.pushConstants.empty()) {
                            if (const RHIStatus status = commands.SetGraphicsPushConstants(draw.pushConstants); !status.IsSuccess())
                                return status;
                        }
                        if (step.vertices.HasValue())
                            if (const RHIStatus status = commands.BindVertexBuffer(0, step.vertices, draw.vertexOffset); !status.IsSuccess()) return status;
                        if (draw.vertexCount == 0)
                            if (const RHIStatus status = commands.BindIndexBuffer(step.indices, draw.indexType, draw.indexOffset); !status.IsSuccess()) return status;
                        if (const RHIStatus status = commands.SetViewportAndScissor(draw.viewport, draw.scissor); !status.IsSuccess()) return status;
                        const auto drawStatus = draw.vertexCount != 0
                            ? commands.Draw(draw.vertexCount, draw.instanceCount, draw.firstVertex, draw.firstInstance)
                            : commands.DrawIndexed(draw.indexCount, draw.instanceCount, draw.firstIndex, draw.baseVertex, draw.firstInstance);
                        if (!drawStatus.IsSuccess()) return drawStatus;
                        if (const RHIStatus status = commands.EndRendering(); !status.IsSuccess()) return status;
                        break;
                    }

                    case RenderGraphPlan::StepKind::Transition:
                        // 只执行上面的屏障；实际 Present 仍由交换链接口完成。
                        break;

                    default:
                        return { .code = RHIErrorCode::InvalidState, .operation = "RenderGraphPlan::Step" };
                }
            }
        }

        if (plan.m_Queries)
            if (const auto status = commands.WriteTimestamp(plan.m_Queries, 1, true); !status.IsSuccess()) return status;
        return commands.End();
    }

}
