#pragma once

#include "VulkanHeaders.h"
#include "VulkanBuffer.h"
#include "VulkanPipeline.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"
#include "VulkanResourceRetirement.h"

#include "Images/PixelFormat.h"

#include "RHI/RHIHandle.h"
#include "RHI/RHIError.h"
#include "RHI/RHIResourceState.h"
#include <mutex>

// 登记自有资源和交换链借用图像/视图，并校验设备身份、槽位和代次。
namespace Catalpa {

    class VulkanDevice;
    class VulkanResourceStateTracker;

    class VulkanResourceRegistry {
    public:
        // 构造与设备身份
        VulkanResourceRegistry(VulkanDevice& device, DeviceId id) : m_Device(device), m_DeviceId(id), m_Retirement(device) {}
        ~VulkanResourceRegistry();

        VulkanResourceRegistry(const VulkanResourceRegistry&) = delete;
        VulkanResourceRegistry& operator=(const VulkanResourceRegistry&) = delete;

        DeviceId GetDeviceId() const { return m_DeviceId; }
        RHIQueueInfo GetQueueInfo(QueueId) const noexcept;

        // 创建自有图像和独立视图；本阶段每张图像最多保留一个活视图。
        [[nodiscard]] RHIStatus CreateTexture(const RHITextureDesc& desc, TextureHandle& texture);
        [[nodiscard]] RHIStatus CreateTextureView(TextureHandle texture, const RHITextureViewDesc& desc, TextureViewHandle& view);

        // 请求释放所有者；活 View 保留图像，命令引用保留 View。
        void ReleaseTexture(TextureHandle texture) noexcept;
        void ReleaseTextureView(TextureViewHandle view) noexcept;

        // 交换链专用登记：同时登记借用图像和 View，不接管原生对象。
        [[nodiscard]] RHIStatus RegisterBorrowedTextureView(VulkanTextureViewRecord record, TextureViewHandle& handle);

        // 必须已无命令引用；撤销登记但不销毁原生对象。
        void UnregisterBorrowedTextureView(TextureViewHandle handle) noexcept;

        // 返回临时借用记录，不得跨登记或注销操作保存此指针。
        const VulkanTextureViewRecord* ResolveTextureView(TextureViewHandle handle) const noexcept;

        // 命令引用与 View 所有者释放分开。
        [[nodiscard]] RHIStatus RetainTextureView(TextureViewHandle handle) noexcept;
        void ReleaseTextureViewReference(TextureViewHandle handle) noexcept;

        // Sampler 所有权与命令引用。
        [[nodiscard]] RHIStatus CreateSampler(const RHISamplerDesc& desc, SamplerHandle& sampler);
        const VulkanSamplerRecord* ResolveSampler(SamplerHandle sampler) const noexcept;
        void ReleaseSampler(SamplerHandle sampler) noexcept;

        [[nodiscard]] RHIStatus RetainSampler(SamplerHandle sampler) noexcept;
        void ReleaseSamplerReference(SamplerHandle sampler) noexcept;

        // 自有管线：创建后由登记表持有原生载荷。
        [[nodiscard]] RHIStatus CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc, GraphicsPipelineHandle& pipeline);

        [[nodiscard]] RHIStatus CreateComputePipeline(const RHIComputePipelineDesc&, ComputePipelineHandle&);

        // 只解析仍允许新使用的管线。
        const VulkanPipelineRecord* ResolveGraphicsPipeline(GraphicsPipelineHandle pipeline) const noexcept;

        // 提出所有者释放请求；有命令引用时延迟退休。
        void ReleaseGraphicsPipeline(GraphicsPipelineHandle pipeline) noexcept;

        // 命令录制取得和归还引用。
        [[nodiscard]] RHIStatus RetainPipeline(GraphicsPipelineHandle pipeline) noexcept;
        void ReleasePipeline(GraphicsPipelineHandle pipeline) noexcept;

        // 自有 Buffer：逻辑所有权与命令引用分别管理。
        [[nodiscard]] RHIStatus CreateBuffer(const RHIBufferDesc& desc, BufferHandle& buffer);
        [[nodiscard]] RHIStatus WriteBuffer(BufferHandle buffer, uint64_t offset, std::span<const std::byte> data);
        const VulkanBufferRecord* ResolveBuffer(BufferHandle buffer) const noexcept;
        void ReleaseBuffer(BufferHandle buffer) noexcept;

        [[nodiscard]] RHIStatus RetainBuffer(BufferHandle buffer) noexcept;
        void ReleaseBufferReference(BufferHandle buffer) noexcept;

    private:
        friend class VulkanResourceStateTracker;

        static constexpr uint32_t InvalidIndex = GenerationalHandle<RHITextureViewTag>::InvalidIndex;

        struct TextureSlot {
            uint32_t generation = 1;
            uint32_t nextFree = InvalidIndex;

            // 由活 View 持有；本阶段最多为 1。
            uint32_t references = 0;
            bool releaseRequested = false;
            VulkanTextureRecord record{};

            // 同一图像的唯一状态；View 重建不重置这里。
            RHITextureState state{};
        };

        struct ViewSlot {
            uint32_t generation = 1;
            uint32_t nextFree = InvalidIndex;

            // 由命令列表持有，保留至安全 reset。
            uint32_t references = 0;
            bool releaseRequested = false;
            VulkanTextureViewRecord record{};
        };

        struct PipelineSlot {
            uint32_t generation = 1;
            uint32_t nextFree = InvalidIndex;
            uint32_t references = 0;
            bool releaseRequested = false;
            VulkanPipelineRecord record{};
        };

        struct SamplerSlot {
            uint32_t generation = 1;
            uint32_t nextFree = InvalidIndex;
            uint32_t references = 0;
            bool releaseRequested = false;
            VulkanSamplerRecord record{};
        };

        struct BufferSlot {
            uint32_t generation = 1;
            uint32_t nextFree = InvalidIndex;
            uint32_t references = 0;
            bool releaseRequested = false;
            VulkanBufferRecord record{};
            RHIBufferState state{};
        };

        // 内部查询允许找到已请求释放、但仍被引用保留的记录。
        const TextureSlot* FindTexture(TextureHandle texture) const noexcept;
        const ViewSlot* FindTextureView(TextureViewHandle view) const noexcept;
        const PipelineSlot* FindPipeline(GraphicsPipelineHandle pipeline) const noexcept;
        const BufferSlot* FindBuffer(BufferHandle buffer) const noexcept;
        const SamplerSlot* FindSampler(SamplerHandle sampler) const noexcept;

        // View 实际销毁后归还父图像引用。
        void ReleaseTextureReference(TextureHandle texture) noexcept;

        // 移交原生载荷给退休入口，再回收槽位。
        void RecycleTexture(uint32_t index) noexcept;
        void RecycleTextureView(uint32_t index) noexcept;
        void RecyclePipeline(uint32_t index) noexcept;
        void RecycleBuffer(uint32_t index) noexcept;
        void RecycleSampler(uint32_t index) noexcept;

    private:
        VulkanDevice& m_Device;
        // 录制worker只修改引用计数；资源创建/释放和状态接受仍由宿主线程串行执行。
        mutable std::recursive_mutex m_CommandReferenceMutex;
        const DeviceId m_DeviceId;

        std::vector<ViewSlot> m_TextureViews;
        uint32_t m_FirstFree = InvalidIndex;

        std::vector<TextureSlot> m_Textures;
        uint32_t m_FirstFreeTexture = InvalidIndex;

        // 自有资源通过唯一退休入口销毁。
        VulkanResourceRetirement m_Retirement;

        std::vector<PipelineSlot> m_Pipelines;
        uint32_t m_FirstFreePipeline = InvalidIndex;

        std::vector<BufferSlot> m_Buffers;
        uint32_t m_FirstFreeBuffer = InvalidIndex;

        std::vector<SamplerSlot> m_Samplers;
        uint32_t m_FirstFreeSampler = InvalidIndex;
    };

} // namespace Catalpa
