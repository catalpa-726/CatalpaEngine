#pragma once

#include "catpch.h"

#include "RHI/RHICommandList.h"
#include "RHI/RHIDevice.h"

namespace Catalpa::Tests {
	/* 纯CPU命令替身：记录调用次序并注入指定录制失败，不模拟GPU执行或原生引用退休。 */
	class FakeCommandList final : public RHICommandList {
	public:
		std::vector<std::string> events;
		std::string failAt;
		RHICommandListState state = RHICommandListState::Initial;
		std::vector<RHIBufferStateUpdate> bufferUpdates;
		std::vector<RHITextureStateUpdate> textureUpdates;
		size_t copyCount = 0;
		bool allowReadback = false;

		/*
		 * 按需记录布局命令，供提交替身独立回放；不把计划中的最终状态当作实际布局。
		 * 此模式只支持用于采样验证的无顶点缓冲全屏三角形，其他绘制仍明确拒绝。
		 */
		struct TextureCommand {
			enum class Kind { Barrier, RequireLayout };
			Kind kind = Kind::Barrier;
			RHITextureBarrier barrier;
			TextureViewHandle view;
			RHITextureLayout layout = RHITextureLayout::Undefined;
		};
		bool recordTextureLayouts = false;
		std::vector<TextureCommand> textureCommands;

		/**
		 * @brief 在测试命令列表中检查初始状态并开始记录。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus Begin() override {
			if (state != RHICommandListState::Initial)
				return Invalid();
			if (auto status = Visit("Begin"); !status.IsSuccess())
				return status;
			state = RHICommandListState::Recording;
			return {};
		}
		/**
		 * @brief 检查记录状态并结束测试命令列表。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus End() override {
			if (state != RHICommandListState::Recording)
				return Invalid();
			if (auto status = Visit("End"); !status.IsSuccess())
				return status;
			state = RHICommandListState::Executable;
			return {};
		}
		RHICommandListState GetState() const override { return state; }
		/**
		 * @brief 将资源状态更新计划关联到当前命令录制。
		 *
		 * @param[in] buffers 供模拟命令列表复制并保留的缓冲区状态更新。
		 * @param[in] textures 供模拟命令列表复制并保留的纹理状态更新。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus SetResourceStatePlan(
		    std::span<const RHIBufferStateUpdate> buffers, std::span<const RHITextureStateUpdate> textures) override {
			if (state != RHICommandListState::Recording)
				return Invalid();
			if (auto status = Visit("Plan"); !status.IsSuccess())
				return status;
			bufferUpdates.assign(buffers.begin(), buffers.end());
			textureUpdates.assign(textures.begin(), textures.end());
			return {};
		}
		/**
		 * @brief 开始使用描述中指定附件的渲染段。
		 *
		 * @param[in] rendering 描述此次模拟渲染使用的附件和绘制区域。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus BeginRendering(const RHIRenderingDesc& rendering) override {
			if (state != RHICommandListState::Recording)
				return Invalid();
			if (auto status = Visit("BeginRendering"); !status.IsSuccess())
				return status;
			if (recordTextureLayouts) {
				for (const auto& color : rendering.colorAttachments)
					RequireTextureLayout(color.view, RHITextureLayout::ColorAttachment);
				if (rendering.depthAttachment)
					RequireTextureLayout(rendering.depthAttachment->view, RHITextureLayout::DepthAttachment);
				m_GraphicsBindings.clear();
				m_GraphicsPipelineBound = false;
				m_ViewportSet = false;
			}
			state = RHICommandListState::Rendering;
			return {};
		}
		/**
		 * @brief 结束当前渲染段。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus EndRendering() override {
			if (state != RHICommandListState::Rendering)
				return Invalid();
			if (auto status = Visit("EndRendering"); !status.IsSuccess())
				return status;
			state = RHICommandListState::Recording;
			return {};
		}
		/**
		 * @brief 在命令流中记录资源访问与布局之间的屏障。
		 *
		 * @param[in] barriers 需要记录的资源访问与布局屏障。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus PipelineBarrier(std::span<const RHITextureBarrier> barriers) override {
			if (state != RHICommandListState::Recording)
				return Invalid();
			if (auto status = Visit("TextureBarrier"); !status.IsSuccess())
				return status;
			if (recordTextureLayouts)
				for (const auto& barrier : barriers)
					textureCommands.push_back({.kind = TextureCommand::Kind::Barrier, .barrier = barrier});
			return {};
		}
		RHIStatus PipelineBarrier(std::span<const RHIBufferBarrier>) override {
			return state == RHICommandListState::Recording ? Visit("BufferBarrier") : Invalid();
		}
		/**
		 * @brief 复制缓冲区。
		 * @details 第 1 个参数（BufferHandle）为输入。输入的 BufferHandle。
		 * @details 第 2 个参数（BufferHandle）为输入。目标位置。
		 * @details 第 3 个参数（const RHIBufferCopy&）为输入。副本。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus CopyBuffer(BufferHandle, BufferHandle, const RHIBufferCopy&) override {
			if (state != RHICommandListState::Recording)
				return Invalid();
			if (auto status = Visit("CopyBuffer"); !status.IsSuccess())
				return status;
			++copyCount;
			return {};
		}
		RHIStatus CopyBufferToTexture(BufferHandle, TextureViewHandle, const RHIBufferTextureCopy&) override {
			return Unsupported();
		}
		/**
		 * @brief 复制纹理目标缓冲区。
		 * @details 第 1 个参数（TextureViewHandle）为输入。当前默认实现未使用该参数。
		 * @details 第 2 个参数（BufferHandle）为输入。当前默认实现未使用该参数。
		 * @details 第 3 个参数（const RHITextureBufferCopy&）为输入。当前默认实现未使用该参数。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus CopyTextureToBuffer(TextureViewHandle, BufferHandle, const RHITextureBufferCopy&) override {
			if (!allowReadback)
				return Unsupported();
			return state == RHICommandListState::Recording ? Visit("CopyTextureToBuffer") : Invalid();
		}
		/**
		 * @brief 绑定后续绘制调用使用的图形管线。
		 *
		 * @param[in] pipeline 后续模拟绘制要使用的图形管线句柄。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus BindGraphicsPipeline(GraphicsPipelineHandle pipeline) override {
			if (!recordTextureLayouts)
				return Unsupported();
			if (state != RHICommandListState::Rendering || !pipeline.HasValue())
				return Invalid();
			if (auto status = Visit("BindGraphicsPipeline"); !status.IsSuccess())
				return status;
			m_GraphicsPipelineBound = true;
			return {};
		}
		/**
		 * @brief 设置图形绑定项。
		 *
		 * @param[in] bindings 提交给模拟图形命令列表的资源绑定项。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus SetGraphicsBindings(std::span<const RHIBinding> bindings) override {
			if (!recordTextureLayouts)
				return Unsupported();
			if (state != RHICommandListState::Rendering)
				return Invalid();
			for (const auto& binding : bindings)
				if (binding.type != RHIBindingType::SampledTexture && binding.type != RHIBindingType::Sampler)
					return Unsupported();
			if (auto status = Visit("SetGraphicsBindings"); !status.IsSuccess())
				return status;
			m_GraphicsBindings.assign(bindings.begin(), bindings.end());
			return {};
		}
		RHIStatus SetGraphicsPushConstants(std::span<const std::byte>) override { return Unsupported(); }
		RHIStatus BindVertexBuffer(uint32_t, BufferHandle, uint64_t) override { return Unsupported(); }
		RHIStatus BindIndexBuffer(BufferHandle, RHIIndexType, uint64_t) override { return Unsupported(); }
		/**
		 * @brief 设置视口与裁剪矩形。
		 * @details 第 1 个参数（const RHIViewport&）为输入。视口。
		 * @details 第 2 个参数（const Rect2D&）为输入。限定绘制范围的裁剪矩形。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus SetViewportAndScissor(const RHIViewport&, const Rect2D&) override {
			if (!recordTextureLayouts)
				return Unsupported();
			if (state != RHICommandListState::Rendering)
				return Invalid();
			if (auto status = Visit("SetViewportAndScissor"); !status.IsSuccess())
				return status;
			m_ViewportSet = true;
			return {};
		}
		/**
		 * @brief 记录测试绘制所观察的图形操作。
		 *
		 * @param[in] vertexCount 涉及的顶点数。
		 * @param[in] instanceCount 涉及的实例数。
		 * @param[in] firstVertex 起始顶点索引。
		 * @param[in] firstInstance 起始实例索引。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus Draw(
		    uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override {
			if (!recordTextureLayouts || vertexCount != 3 || instanceCount != 1 || firstVertex != 0 ||
			    firstInstance != 0)
				return Unsupported();
			if (state != RHICommandListState::Rendering || !m_GraphicsPipelineBound || !m_ViewportSet)
				return Invalid();
			if (auto status = Visit("Draw"); !status.IsSuccess())
				return status;
			for (const auto& binding : m_GraphicsBindings)
				if (binding.type == RHIBindingType::SampledTexture)
					RequireTextureLayout(binding.texture, RHITextureLayout::ShaderReadOnly);
			return {};
		}
		RHIStatus DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override { return Unsupported(); }
		bool BeginDebugLabel(const char*, const std::array<float, 4>&) override { return false; }
		void EndDebugLabel() override {}

	private:
		std::vector<RHIBinding> m_GraphicsBindings;
		bool m_GraphicsPipelineBound = false;
		bool m_ViewportSet = false;

		void RequireTextureLayout(TextureViewHandle view, RHITextureLayout layout) {
			textureCommands.push_back({.kind = TextureCommand::Kind::RequireLayout, .view = view, .layout = layout});
		}
		RHIStatus Visit(const char* name) {
			events.emplace_back(name);
			return failAt == name
			    ? RHIStatus{.code = RHIErrorCode::BackendFailure, .operation = "FakeCommandList::Injected"}
			    : RHIStatus{};
		}
		static RHIStatus Invalid() {
			return {.code = RHIErrorCode::InvalidState, .operation = "FakeCommandList::State"};
		}
		static RHIStatus Unsupported() {
			return {.code = RHIErrorCode::Unsupported, .operation = "FakeCommandList::NotImplemented"};
		}
	};

	/* 纯CPU提交替身：accepted与completed由用例分别推进，不把Submit成功当作完成。 */
	class FakeQueue final : public RHIQueue {
	public:
		DeviceId device{11};
		QueueId id{1};
		uint64_t submitted = 0, completed = 0;
		size_t submitCalls = 0;
		RHIStatus failure{};
		bool returnInvalidCompletion = false;
		RHISubmission lastSubmission{};

		QueueId GetId() const override { return id; }
		/**
		 * @brief 记录模拟队列提交并填写完成点。
		 *
		 * @param[in] submission 本次模拟队列要提交的命令列表和同步依赖。
		 * @param[in] point 本次操作使用的 RHICompletionPoint。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus Submit(const RHISubmission& submission, RHICompletionPoint& point) override {
			++submitCalls;
			point = {};
			if (!failure.IsSuccess())
				return failure;
			auto* commands = dynamic_cast<FakeCommandList*>(submission.commands);
			if (!commands || commands->GetState() != RHICommandListState::Executable ||
			    submission.frame.device != device)
				return {.code = RHIErrorCode::InvalidDescription, .operation = "FakeQueue::Submit"};
			/* 所有可抛分配在接受前发生；后续只记录不可回滚的值。 */
			commands->events.emplace_back("Submit");
			lastSubmission = submission;
			commands->state = RHICommandListState::Pending;
			++submitted;
			if (!returnInvalidCompletion)
				point = {device, id, submitted};
			return {};
		}
		/**
		 * @brief 检查相关操作是否已经完成。
		 *
		 * @param[in] point 待查询或等待的队列完成点。
		 * @param[out] complete 接收所查询队列完成点是否已到达。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus IsComplete(const RHICompletionPoint& point, bool& complete) override {
			complete = false;
			if (!failure.IsSuccess())
				return failure;
			if (!point.HasValue() || point.device != device || point.queue != id || point.sequence > submitted)
				return {.code = RHIErrorCode::InvalidDescription, .operation = "FakeQueue::IsComplete"};
			complete = point.sequence <= completed;
			return {};
		}
	};

	/*
	 * 只提供图导入所需的自有状态快照；创建/帧等待/原生对象操作全部Unsupported。
	 * 该替身用于编译与录制契约，不提供GPU内存、WSI或引用计数正确性的证明。
	 */
	class FakeDevice : public RHIDevice {
	public:
		FakeQueue queue;
		RHICapabilities capabilities{};
		std::vector<RHIBufferStateSnapshot> buffers;
		std::vector<RHITextureStateSnapshot> textures;

		/**
		 * @brief 添加缓冲区。
		 *
		 * @param[in] snapshot 新建模拟缓冲区的初始状态和所有权快照。
		 *
		 * @return 指向刚注册缓冲区的替身句柄，包含设备标识、新槽位索引及初始代次 1。
		 */
		BufferHandle AddBuffer(RHIBufferStateSnapshot snapshot) {
			const auto index = static_cast<uint32_t>(buffers.size());
			buffers.push_back(std::move(snapshot));
			return {queue.device, {index, 1}};
		}
		/**
		 * @brief 添加纹理。
		 *
		 * @param[in] snapshot 新建模拟纹理视图的初始状态和所有权快照。
		 *
		 * @return 指向刚注册纹理视图的替身句柄，包含设备标识、新槽位索引及初始代次 1。
		 */
		TextureViewHandle AddTexture(RHITextureStateSnapshot snapshot) {
			const auto index = static_cast<uint32_t>(textures.size());
			textures.push_back(std::move(snapshot));
			return {queue.device, {index, 1}};
		}
		/**
		 * @brief 返回缓冲区状态。
		 *
		 * @param[in] buffer 本次操作使用的 BufferHandle。
		 * @param[out] snapshot 接收该模拟缓冲区当前记录的资源状态。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus GetBufferState(BufferHandle buffer, RHIBufferStateSnapshot& snapshot) const override {
			if (buffer.device != queue.device || buffer.slot.generation != 1 || buffer.slot.index >= buffers.size())
				return Invalid();
			snapshot = buffers[buffer.slot.index];
			return {};
		}
		/**
		 * @brief 返回纹理状态。
		 *
		 * @param[in] view 本次操作使用的 TextureViewHandle。
		 * @param[out] snapshot 接收该模拟纹理视图当前记录的资源状态。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus GetTextureState(TextureViewHandle view, RHITextureStateSnapshot& snapshot) const override {
			if (view.device != queue.device || view.slot.generation != 1 || view.slot.index >= textures.size())
				return Invalid();
			snapshot = textures[view.slot.index];
			return {};
		}
		DeviceId GetDeviceId() const override { return queue.device; }
		GraphicsBackend GetBackend() const override { return GraphicsBackend::Vulkan; }
		const RHICapabilities& GetCapabilities() const override { return capabilities; }
		RHIFormatSupport GetFormatSupport(PixelFormat) const override { return {}; }
		RHIQueue& GetGraphicsQueue() override { return queue; }
		uint32_t GetFrameCount() const override { return 1; }
		RHIStatus WaitIdle() const override { return Unsupported(); }
		RHIStatus WaitIdleAndDiscardFrameCommands() override { return Unsupported(); }
		/**
		 * @brief 准备帧。
		 * @details 第 1 个参数（uint32_t）为输入。槽位。
		 * @details 第 2 个参数（uint64_t）为输入。允许等待的最长时间，以纳秒计。
		 *
		 * @param[out] frame 接收帧资源；该模拟设备不支持准备呈现帧，会将其清空。
		 * @param[out] ready 接收就绪状态。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus PrepareFrame(uint32_t, uint64_t, RHIFrameResources& frame, bool& ready) override {
			frame = {};
			ready = false;
			return Unsupported();
		}
		std::unique_ptr<RHISwapchain> CreateSwapchain(const RHISwapchainDesc&, RHIStatus& status) override {
			status = Unsupported();
			return {};
		}
		RHIStatus CreateBuffer(const RHIBufferDesc&, BufferHandle&) override { return Unsupported(); }
		RHIStatus WriteBuffer(BufferHandle, uint64_t, std::span<const std::byte>) override { return Unsupported(); }
		RHIStatus CreateTexture(const RHITextureDesc&, TextureHandle&) override { return Unsupported(); }
		RHIStatus CreateTextureView(TextureHandle, const RHITextureViewDesc&, TextureViewHandle&) override {
			return Unsupported();
		}
		RHIStatus CreateSampler(const RHISamplerDesc&, SamplerHandle&) override { return Unsupported(); }
		RHIStatus CreateGraphicsPipeline(const RHIGraphicsPipelineDesc&, GraphicsPipelineHandle&) override {
			return Unsupported();
		}
		void Shutdown() override {}
		void ReleaseBuffer(BufferHandle) override {}
		void ReleaseTexture(TextureHandle) override {}
		void ReleaseTextureView(TextureViewHandle) override {}
		void ReleaseSampler(SamplerHandle) override {}
		void ReleaseGraphicsPipeline(GraphicsPipelineHandle) override {}

	private:
		static RHIStatus Invalid() {
			return {.code = RHIErrorCode::InvalidDescription, .operation = "FakeDevice::Handle"};
		}
		static RHIStatus Unsupported() {
			return {.code = RHIErrorCode::Unsupported, .operation = "FakeDevice::NotImplemented"};
		}
	};
}
