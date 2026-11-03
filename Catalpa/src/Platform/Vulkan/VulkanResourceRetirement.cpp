#include "catpch.h"
#include "VulkanResourceRetirement.h"

namespace Catalpa {

    void VulkanResourceRetirement::RetirePipeline(VulkanPipelineRecord pipeline) noexcept
    {
        DestroyVulkanPipeline(m_Device, pipeline);
    }

}