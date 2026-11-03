#include "catpch.h"
#include "VulkanResourceRetirement.h"

namespace Catalpa {

    void VulkanResourceRetirement::RetirePipeline(VulkanPipelineRecord pipeline) noexcept { DestroyVulkanPipeline(m_Device, pipeline); }

    void VulkanResourceRetirement::RetireBuffer(VulkanBufferRecord buffer) noexcept { DestroyVulkanBuffer(m_Device, buffer); }

    void VulkanResourceRetirement::RetireTexture(VulkanTextureRecord texture) noexcept { DestroyVulkanTexture(m_Device, texture); }

    void VulkanResourceRetirement::RetireTextureView(VulkanTextureViewRecord view) noexcept { DestroyVulkanTextureView(m_Device, view); }

    void VulkanResourceRetirement::RetireSampler(VulkanSamplerRecord sampler) noexcept { DestroyVulkanSampler(m_Device, sampler); }

}