#pragma once

#include "catpch.h"
#include "RHI/RHICommandList.h"
#include "RHI/RHIDevice.h"

namespace Catalpa::Tests {
    // 纯CPU命令替身：记录调用次序并注入指定录制失败，不模拟GPU执行或原生引用退休。
    class FakeCommandList final : public RHICommandList {
    public:
        std::vector<std::string> events;
        std::string failAt;
        RHICommandListState state = RHICommandListState::Initial;
        std::vector<RHIBufferStateUpdate> bufferUpdates;
        std::vector<RHITextureStateUpdate> textureUpdates;
        size_t copyCount = 0;

        RHIStatus Begin() override
        {
            if (state != RHICommandListState::Initial) return Invalid();
            if (auto status = Visit("Begin"); !status.IsSuccess()) return status;
            state = RHICommandListState::Recording;
            return {};
        }
        RHIStatus End() override
        {
            if (state != RHICommandListState::Recording) return Invalid();
            if (auto status = Visit("End"); !status.IsSuccess()) return status;
            state = RHICommandListState::Executable;
            return {};
        }
        RHICommandListState GetState() const override { return state; }
        RHIStatus SetResourceStatePlan(std::span<const RHIBufferStateUpdate> buffers,
            std::span<const RHITextureStateUpdate> textures) override
        {
            if (state != RHICommandListState::Recording) return Invalid();
            if (auto status = Visit("Plan"); !status.IsSuccess()) return status;
            bufferUpdates.assign(buffers.begin(), buffers.end());
            textureUpdates.assign(textures.begin(), textures.end());
            return {};
        }
        RHIStatus BeginRendering(const RHIRenderingDesc&) override
        {
            if (state != RHICommandListState::Recording) return Invalid();
            if (auto status = Visit("BeginRendering"); !status.IsSuccess()) return status;
            state = RHICommandListState::Rendering;
            return {};
        }
        RHIStatus EndRendering() override
        {
            if (state != RHICommandListState::Rendering) return Invalid();
            if (auto status = Visit("EndRendering"); !status.IsSuccess()) return status;
            state = RHICommandListState::Recording;
            return {};
        }
        RHIStatus PipelineBarrier(std::span<const RHITextureBarrier>) override
        {
            return state == RHICommandListState::Recording ? Visit("TextureBarrier") : Invalid();
        }
        RHIStatus PipelineBarrier(std::span<const RHIBufferBarrier>) override
        {
            return state == RHICommandListState::Recording ? Visit("BufferBarrier") : Invalid();
        }
        RHIStatus CopyBuffer(BufferHandle, BufferHandle, const RHIBufferCopy&) override
        {
            if (state != RHICommandListState::Recording) return Invalid();
            if (auto status = Visit("CopyBuffer"); !status.IsSuccess()) return status;
            ++copyCount;
            return {};
        }
        // 本批只验证clear/copy/transition调度；未实现的绘制行为明确失败，不能默认为通过。
        RHIStatus CopyBufferToTexture(BufferHandle, TextureViewHandle, const RHIBufferTextureCopy&) override { return Unsupported(); }
        RHIStatus BindGraphicsPipeline(GraphicsPipelineHandle) override { return Unsupported(); }
        RHIStatus SetGraphicsBindings(std::span<const RHIBinding>) override { return Unsupported(); }
        RHIStatus SetGraphicsPushConstants(std::span<const std::byte>) override { return Unsupported(); }
        RHIStatus BindVertexBuffer(uint32_t, BufferHandle, uint64_t) override { return Unsupported(); }
        RHIStatus BindIndexBuffer(BufferHandle, RHIIndexType, uint64_t) override { return Unsupported(); }
        RHIStatus SetViewportAndScissor(const RHIViewport&, const Rect2D&) override { return Unsupported(); }
        RHIStatus Draw(uint32_t, uint32_t, uint32_t, uint32_t) override { return Unsupported(); }
        RHIStatus DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override { return Unsupported(); }
        bool BeginDebugLabel(const char*, const std::array<float, 4>&) override { return false; }
        void EndDebugLabel() override {}

    private:
        RHIStatus Visit(const char* name)
        {
            events.emplace_back(name);
            return failAt == name ? RHIStatus{ .code = RHIErrorCode::BackendFailure, .operation = "FakeCommandList::Injected" } : RHIStatus{};
        }
        static RHIStatus Invalid() { return { .code = RHIErrorCode::InvalidState, .operation = "FakeCommandList::State" }; }
        static RHIStatus Unsupported() { return { .code = RHIErrorCode::Unsupported, .operation = "FakeCommandList::NotImplemented" }; }
    };

    // 纯CPU提交替身：accepted与completed由用例分别推进，不把Submit成功当作完成。
    class FakeQueue final : public RHIQueue {
    public:
        DeviceId device{ 11 };
        QueueId id{ 1 };
        uint64_t submitted = 0, completed = 0;
        size_t submitCalls = 0;
        RHIStatus failure{};
        bool returnInvalidCompletion = false;
        RHISubmission lastSubmission{};

        QueueId GetId() const override { return id; }
        RHIStatus Submit(const RHISubmission& submission, RHICompletionPoint& point) override
        {
            ++submitCalls;
            point = {};
            if (!failure.IsSuccess()) return failure;
            auto* commands = dynamic_cast<FakeCommandList*>(submission.commands);
            if (!commands || commands->GetState() != RHICommandListState::Executable || submission.frame.device != device)
                return { .code = RHIErrorCode::InvalidDescription, .operation = "FakeQueue::Submit" };
            // 所有可抛分配在接受前发生；后续只记录不可回滚的值。
            commands->events.emplace_back("Submit");
            lastSubmission = submission;
            commands->state = RHICommandListState::Pending;
            ++submitted;
            if (!returnInvalidCompletion) point = { device, id, submitted };
            return {};
        }
        RHIStatus IsComplete(const RHICompletionPoint& point, bool& complete) override
        {
            complete = false;
            if (!failure.IsSuccess()) return failure;
            if (!point.HasValue() || point.device != device || point.queue != id || point.sequence > submitted)
                return { .code = RHIErrorCode::InvalidDescription, .operation = "FakeQueue::IsComplete" };
            complete = point.sequence <= completed;
            return {};
        }
    };

    // 只提供图导入所需的自有状态快照；创建/帧等待/原生对象操作全部Unsupported。
    // 该替身用于编译与录制契约，不提供GPU内存、WSI或引用计数正确性的证明。
    class FakeDevice final : public RHIDevice {
    public:
        FakeQueue queue;
        RHICapabilities capabilities{};
        std::vector<RHIBufferStateSnapshot> buffers;
        std::vector<RHITextureStateSnapshot> textures;

        BufferHandle AddBuffer(RHIBufferStateSnapshot snapshot)
        {
            const auto index = static_cast<uint32_t>(buffers.size());
            buffers.push_back(std::move(snapshot));
            return { queue.device, { index, 1 } };
        }
        TextureViewHandle AddTexture(RHITextureStateSnapshot snapshot)
        {
            const auto index = static_cast<uint32_t>(textures.size());
            textures.push_back(std::move(snapshot));
            return { queue.device, { index, 1 } };
        }
        RHIStatus GetBufferState(BufferHandle buffer, RHIBufferStateSnapshot& snapshot) const override
        {
            if (buffer.device != queue.device || buffer.slot.generation != 1 || buffer.slot.index >= buffers.size()) return Invalid();
            snapshot = buffers[buffer.slot.index];
            return {};
        }
        RHIStatus GetTextureState(TextureViewHandle view, RHITextureStateSnapshot& snapshot) const override
        {
            if (view.device != queue.device || view.slot.generation != 1 || view.slot.index >= textures.size()) return Invalid();
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
        RHIStatus PrepareFrame(uint32_t, uint64_t, RHIFrameResources& frame, bool& ready) override
        { frame = {}; ready = false; return Unsupported(); }
        std::unique_ptr<RHISwapchain> CreateSwapchain(const RHISwapchainDesc&, RHIStatus& status) override
        { status = Unsupported(); return {}; }
        RHIStatus CreateBuffer(const RHIBufferDesc&, BufferHandle&) override { return Unsupported(); }
        RHIStatus WriteBuffer(BufferHandle, uint64_t, std::span<const std::byte>) override { return Unsupported(); }
        RHIStatus CreateTexture(const RHITextureDesc&, TextureHandle&) override { return Unsupported(); }
        RHIStatus CreateTextureView(TextureHandle, const RHITextureViewDesc&, TextureViewHandle&) override { return Unsupported(); }
        RHIStatus CreateSampler(const RHISamplerDesc&, SamplerHandle&) override { return Unsupported(); }
        RHIStatus CreateGraphicsPipeline(const RHIGraphicsPipelineDesc&, GraphicsPipelineHandle&) override { return Unsupported(); }
        // 不创建任何原生对象，因此以下关闭/owner释放没有底层工作。
        void Shutdown() override {}
        void ReleaseBuffer(BufferHandle) override {}
        void ReleaseTexture(TextureHandle) override {}
        void ReleaseTextureView(TextureViewHandle) override {}
        void ReleaseSampler(SamplerHandle) override {}
        void ReleaseGraphicsPipeline(GraphicsPipelineHandle) override {}

    private:
        static RHIStatus Invalid() { return { .code = RHIErrorCode::InvalidDescription, .operation = "FakeDevice::Handle" }; }
        static RHIStatus Unsupported() { return { .code = RHIErrorCode::Unsupported, .operation = "FakeDevice::NotImplemented" }; }
    };
}
