#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHIError.h"
#include "Images/PixelFormat.h"
#include "VulkanPipeline.h"
#include "VulkanResourceRetirement.h"

// 登记表保存借用视图和自有管线，并校验设备身份、槽位和代次。
namespace Catalpa {

    class VulkanDevice;

    struct VulkanTextureViewRecord {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        Extent2D extent{};
        PixelFormat format = PixelFormat::Unknown;
        uint32_t sampleCount = 1;
    };

    class VulkanResourceRegistry {
    public:
        // 构造与设备身份
        VulkanResourceRegistry(VulkanDevice& device, DeviceId id) : m_Device(device), m_DeviceId(id), m_Retirement(device) {}
        ~VulkanResourceRegistry();

        VulkanResourceRegistry(const VulkanResourceRegistry&) = delete;
        VulkanResourceRegistry& operator=(const VulkanResourceRegistry&) = delete;

        DeviceId GetDeviceId() const { return m_DeviceId; }

        // 借用视图：登记原生对象，不接管销毁。
        [[nodiscard]] RHIStatus RegisterBorrowedTextureView(VulkanTextureViewRecord record, TextureViewHandle& handle);

        // 返回临时借用的信息，不得跨登记或注销操作保存此指针。
        const VulkanTextureViewRecord* ResolveTextureView(TextureViewHandle handle) const noexcept;

        // 撤销句柄，不销毁原生对象。
        void UnregisterBorrowedTextureView(TextureViewHandle handle) noexcept;

        // 保留一次命令使用；句柄必须仍然有效。
        [[nodiscard]] RHIStatus RetainTextureView(TextureViewHandle handle) noexcept;

        // 释放一次已经取得的引用，不销毁原生对象。
        void ReleaseTextureView(TextureViewHandle handle) noexcept;

        // 自有管线：创建后由登记表持有原生载荷。
        [[nodiscard]] RHIStatus CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc, GraphicsPipelineHandle& pipeline);

        // 只解析仍允许新使用的管线。
        const VulkanPipelineRecord* ResolveGraphicsPipeline(GraphicsPipelineHandle pipeline) const noexcept;

        // 提出所有者释放请求；有命令引用时延迟退休。
        void ReleaseGraphicsPipeline(GraphicsPipelineHandle pipeline) noexcept;

        // 命令录制取得和归还引用。
        [[nodiscard]] RHIStatus RetainPipeline(GraphicsPipelineHandle pipeline) noexcept;
        void ReleasePipeline(GraphicsPipelineHandle pipeline) noexcept;

    private:
        // 管线槽位及其回收
        struct PipelineSlot {
            uint32_t generation = 1;
            uint32_t nextFree = InvalidIndex;
            uint32_t references = 0;
            bool releaseRequested = false;
            VulkanPipelineRecord record{};
        };

        // 内部查询允许找到已请求释放、但仍被命令保留的记录。
        const PipelineSlot* FindPipeline(GraphicsPipelineHandle pipeline) const noexcept;

        // 移交原生载荷给退休入口，再回收槽位。
        void RecyclePipeline(uint32_t index) noexcept;

    private:
        // 视图槽位
        static constexpr uint32_t InvalidIndex = GenerationalHandle<RHITextureViewTag>::InvalidIndex;

        struct ViewSlot {
            uint32_t generation = 1;
            uint32_t nextFree = InvalidIndex;
            uint32_t references = 0;
            VulkanTextureViewRecord record{};
        };

        // 设备身份与借用视图登记
        VulkanDevice& m_Device;
        const DeviceId m_DeviceId;
        std::vector<ViewSlot> m_TextureViews;
        uint32_t m_FirstFree = InvalidIndex;

        // 自有管线通过唯一退休入口销毁
        VulkanResourceRetirement m_Retirement;
        std::vector<PipelineSlot> m_Pipelines;
        uint32_t m_FirstFreePipeline = InvalidIndex;
    };

}