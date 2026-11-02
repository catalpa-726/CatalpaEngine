#pragma once

#include "VulkanHeaders.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHIError.h"
#include "Images/PixelFormat.h"

// 登记表保存视图信息，并校验设备身份、槽位和代次。
namespace Catalpa {

    struct VulkanTextureViewRecord {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        Extent2D extent{};
        PixelFormat format = PixelFormat::Unknown;
        uint32_t sampleCount = 1;
    };

    class VulkanResourceRegistry {
    public:
        explicit VulkanResourceRegistry(DeviceId device) : m_DeviceId(device) {}

        VulkanResourceRegistry(const VulkanResourceRegistry&) = delete;
        VulkanResourceRegistry& operator=(const VulkanResourceRegistry&) = delete;

        DeviceId GetDeviceId() const { return m_DeviceId; }

        // 登记借用的原生对象，不接管销毁。
        [[nodiscard]] RHIStatus RegisterBorrowedTextureView(VulkanTextureViewRecord record, TextureViewHandle& handle);

        // 返回临时借用的信息，不得跨登记或注销操作保存此指针。
        const VulkanTextureViewRecord* ResolveTextureView(TextureViewHandle handle) const noexcept;

        // 撤销句柄，不销毁原生对象。
        void UnregisterBorrowedTextureView(TextureViewHandle handle) noexcept;

    private:
        static constexpr uint32_t InvalidIndex = GenerationalHandle<RHITextureViewTag>::InvalidIndex;

        struct ViewSlot {
            uint32_t generation = 1;
            uint32_t nextFree = InvalidIndex;
            VulkanTextureViewRecord record{};
        };

        const DeviceId m_DeviceId;
        std::vector<ViewSlot> m_TextureViews;
        uint32_t m_FirstFree = InvalidIndex;
    };

}