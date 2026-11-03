#include "catpch.h"
#include "VulkanResourceRetirement.h"

namespace Catalpa {

    void VulkanResourceRetirement::RetirePipeline(VulkanPipelineRecord pipeline) noexcept { DestroyVulkanPipeline(m_Device, pipeline); }

    void VulkanResourceRetirement::RetireBuffer(VulkanBufferRecord buffer) noexcept { DestroyVulkanBuffer(m_Device, buffer); }

}