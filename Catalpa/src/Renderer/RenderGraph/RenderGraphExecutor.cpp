#include "catpch.h"

#include "RenderGraphExecutor.h"

#include "RenderGraphPlan.h"
#include "RHI/RHICommandList.h"

namespace Catalpa {

	RHIStatus RenderGraphExecutor::Record(const RenderGraphPlan& plan, RHICommandList& commands) {
		if (!plan.m_Valid)
			return {.code = RHIErrorCode::InvalidState, .operation = "RenderGraphPlan::Record"};

		/* 开始录制并交给命令列表本次资源状态更新计划；最终状态由队列受理提交后发布。 */
		if (const RHIStatus status = commands.Begin(); !status.IsSuccess())
			return status;

		if (const RHIStatus status = commands.SetResourceStatePlan(plan.m_BufferUpdates, plan.m_TextureUpdates);
		    !status.IsSuccess())
			return status;

		if (plan.m_Queries && plan.m_QueryBegin) {
			if (const auto status = commands.ResetQueries(plan.m_Queries, 0, 2); !status.IsSuccess())
				return status;
			if (const auto status = commands.WriteTimestamp(plan.m_Queries, 0, false); !status.IsSuccess())
				return status;
		}

		{
			RHICommandLabelScope frameLabel(commands, "Frame", {0.2f, 0.4f, 0.8f, 1.0f});

			for (const auto& step : plan.m_Steps) {
				RHICommandLabelScope passLabel(commands, step.name.c_str(), {0.4f, 0.6f, 0.8f, 1.0f});

				/* 每个步骤先执行计划生成的屏障，使资源对即将录制的复制、计算或绘制操作可见。 */
				if (!step.bufferBarriers.empty()) {
					if (const RHIStatus status =
					        commands.PipelineBarrier(std::span<const RHIBufferBarrier>{step.bufferBarriers});
					    !status.IsSuccess())
						return status;
				}

				if (!step.textureBarriers.empty()) {
					if (const RHIStatus status =
					        commands.PipelineBarrier(std::span<const RHITextureBarrier>{step.textureBarriers});
					    !status.IsSuccess())
						return status;
				}

				switch (step.kind) {
				case RenderGraphPlan::StepKind::Compute:
					if (const auto status = commands.BindComputePipeline(step.compute.pipeline); !status.IsSuccess())
						return status;
					if (!step.bindings.empty())
						if (const auto status = commands.SetComputeBindings(step.bindings); !status.IsSuccess())
							return status;
					if (!step.compute.pushConstants.empty())
						if (const auto status = commands.SetComputePushConstants(step.compute.pushConstants);
						    !status.IsSuccess())
							return status;
					if (const auto status =
					        commands.Dispatch(step.compute.groups[0], step.compute.groups[1], step.compute.groups[2]);
					    !status.IsSuccess())
						return status;
					break;
				case RenderGraphPlan::StepKind::Copy:
					if (const RHIStatus status = commands.CopyBuffer(step.source, step.destination, step.copy);
					    !status.IsSuccess())
						return status;
					break;

				case RenderGraphPlan::StepKind::TextureUpload:
					if (const RHIStatus status =
					        commands.CopyBufferToTexture(step.source, step.destinationView, step.textureCopy);
					    !status.IsSuccess())
						return status;
					break;

				case RenderGraphPlan::StepKind::TextureReadback:
					if (const auto status =
					        commands.CopyTextureToBuffer(step.sourceView, step.destination, step.readbackCopy);
					    !status.IsSuccess())
						return status;
					break;

				case RenderGraphPlan::StepKind::Clear: {
					const RHIColorAttachment color{.view = step.color,
					    .loadOp = RHILoadOp::Clear,
					    .storeOp = RHIStoreOp::Store,
					    .clearColor = step.draw.clearColor};
					RHIRenderingDesc rendering{.renderArea = {.extent = step.extent}, .sampleCount = 1};
					if (step.color.HasValue())
						rendering.colorAttachments = {&color, 1};
					if (step.depth.HasValue())
						rendering.depthAttachment = RHIDepthAttachment{.view = step.depth,
						    .loadOp = RHILoadOp::Clear,
						    .storeOp = RHIStoreOp::Store,
						    .clearDepth = step.draw.clearDepth};
					if (const auto status = commands.BeginRendering(rendering); !status.IsSuccess())
						return status;
					if (const auto status = commands.EndRendering(); !status.IsSuccess())
						return status;
					break;
				}

				case RenderGraphPlan::StepKind::Draw: {
					const auto& draw = step.draw;

					const RHIColorAttachment color{.view = step.color,
					    .loadOp = draw.loadOp,
					    .storeOp = draw.storeOp,
					    .clearColor = draw.clearColor};

					RHIRenderingDesc rendering{.renderArea = {.extent = step.extent}, .sampleCount = 1};
					if (step.color.HasValue())
						rendering.colorAttachments = {&color, 1};
					if (step.depth.HasValue())
						rendering.depthAttachment = RHIDepthAttachment{.view = step.depth,
						    .loadOp = draw.depthLoadOp,
						    .storeOp = draw.depthStoreOp,
						    .clearDepth = draw.clearDepth,
						    .readOnly = draw.depthReadOnly};

					if (const RHIStatus status = commands.BeginRendering(rendering); !status.IsSuccess())
						return status;
					if (const RHIStatus status = commands.BindGraphicsPipeline(draw.pipeline); !status.IsSuccess())
						return status;
					if (!step.bindings.empty()) {
						if (const RHIStatus status = commands.SetGraphicsBindings(step.bindings); !status.IsSuccess())
							return status;
					}
					if (!draw.pushConstants.empty()) {
						if (const RHIStatus status = commands.SetGraphicsPushConstants(draw.pushConstants);
						    !status.IsSuccess())
							return status;
					}
					if (step.vertices.HasValue())
						if (const RHIStatus status = commands.BindVertexBuffer(0, step.vertices, draw.vertexOffset);
						    !status.IsSuccess())
							return status;
					if (draw.vertexCount == 0)
						if (const RHIStatus status =
						        commands.BindIndexBuffer(step.indices, draw.indexType, draw.indexOffset);
						    !status.IsSuccess())
							return status;
					if (const RHIStatus status = commands.SetViewportAndScissor(draw.viewport, draw.scissor);
					    !status.IsSuccess())
						return status;
					/* 根据计划选择非索引、间接索引或直接索引绘制，然后关闭本步骤的渲染范围。 */
					const auto drawStatus = draw.vertexCount != 0
					    ? commands.Draw(draw.vertexCount, draw.instanceCount, draw.firstVertex, draw.firstInstance)
					    : step.indirect.HasValue() ? commands.DrawIndexedIndirect(step.indirect, draw.indirectOffset)
					                               : commands.DrawIndexed(draw.indexCount, draw.instanceCount,
					                                     draw.firstIndex, draw.baseVertex, draw.firstInstance);
					if (!drawStatus.IsSuccess())
						return drawStatus;
					if (const RHIStatus status = commands.EndRendering(); !status.IsSuccess())
						return status;
					break;
				}

				case RenderGraphPlan::StepKind::Transition:
					/* 只执行上面的屏障；实际 Present 仍由交换链接口完成。 */
					break;

				default:
					return {.code = RHIErrorCode::InvalidState, .operation = "RenderGraphPlan::Step"};
				}
			}
		}

		/* 配置了结束查询时在最后一道命令后写入时间戳，再结束录制。 */
		if (plan.m_Queries && plan.m_QueryEnd)
			if (const auto status = commands.WriteTimestamp(plan.m_Queries, 1, true); !status.IsSuccess())
				return status;
		return commands.End();
	}

}
