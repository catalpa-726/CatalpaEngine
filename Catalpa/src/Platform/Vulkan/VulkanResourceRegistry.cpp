#include "catpch.h"
#include "VulkanResourceRegistry.h"
#include "Log/Log.h"

namespace Catalpa {

    // 取得空闲槽位，保存信息并返回句柄
    RHIStatus VulkanResourceRegistry::RegisterBorrowedTextureView(VulkanTextureViewRecord record, TextureViewHandle& handle)
    {
        handle = {};

        if (record.image == VK_NULL_HANDLE || record.view == VK_NULL_HANDLE) {
            CAT_CORE_ASSERT(false, "Texture view registration requires valid native objects!");
            return { .code = RHIErrorCode::InvalidDescription, .operation = "VulkanResourceRegistry::RegisterBorrowedTextureView" };
        }

        uint32_t index = m_FirstFree;

        if (index != InvalidIndex) {
            m_FirstFree = m_TextureViews[index].nextFree;
        }
        else {
            if (m_TextureViews.size() >= InvalidIndex)
                return { .code = RHIErrorCode::OutOfMemory, .operation = "VulkanResourceRegistry::RegisterBorrowedTextureView" };

            index = static_cast<uint32_t>(m_TextureViews.size());
            m_TextureViews.emplace_back();
        }

        auto& slot = m_TextureViews[index];
        slot.record = record;
        slot.nextFree = InvalidIndex;

        handle = { .device = m_DeviceId, .slot = {.index = index, .generation = slot.generation } };
        return {};
    }

    // 拒绝空句柄、其他设备的句柄、旧代次和已经撤销的登记
    const VulkanTextureViewRecord* VulkanResourceRegistry::ResolveTextureView(TextureViewHandle handle) const noexcept
    {
        if (!handle.HasValue() || handle.device != m_DeviceId || handle.slot.index >= m_TextureViews.size()) return nullptr;

        const auto& slot = m_TextureViews[handle.slot.index];
        return slot.generation == handle.slot.generation && slot.record.view != VK_NULL_HANDLE ? &slot.record : nullptr;
    }

    // 使当前句柄失效，增加代次后回收槽位；代次达到上限的槽位不再复用，避免回绕
    void VulkanResourceRegistry::UnregisterBorrowedTextureView(TextureViewHandle handle) noexcept
    {
        if (!ResolveTextureView(handle)) return;

        auto& slot = m_TextureViews[handle.slot.index];
        slot.record = {};

        if (slot.generation == std::numeric_limits<uint32_t>::max()) return;

        ++slot.generation;
        slot.nextFree = m_FirstFree;
        m_FirstFree = handle.slot.index;
    }

}