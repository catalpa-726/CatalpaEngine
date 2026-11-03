#include "catpch.h"
#include "VulkanPipeline.h"
#include "VulkanShader.h"
#include "VulkanDevice.h"
#include "VulkanConversions.h"
#include "VulkanUtils.h"

namespace Catalpa {

    RHIStatus CreateVulkanGraphicsPipeline(VulkanDevice& device, const RHIGraphicsPipelineDesc& desc, VulkanPipelineRecord& pipeline)
    {
        pipeline = {};

        if (desc.vertex.stage != RHIShaderStage::Vertex || desc.fragment.stage != RHIShaderStage::Fragment)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "CreateVulkanGraphicsPipeline" };

        if (desc.sampleCount != 1 || !device.GetFormatSupport(desc.colorFormat).colorAttachment)
            return { .code = RHIErrorCode::Unsupported, .operation = "CreateVulkanGraphicsPipeline" };

        VulkanShaderRecord vertex{}, fragment{};

        RHIStatus status = CreateVulkanShader(device, desc.vertex, vertex);
        if (!status.IsSuccess()) return status;

        status = CreateVulkanShader(device, desc.fragment, fragment);

        if (!status.IsSuccess()) {
            DestroyVulkanShader(device, vertex);
            return status;
        }

        const VkPipelineLayoutCreateInfo layoutInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        status = MakeVulkanStatus(vkCreatePipelineLayout(device.GetHandle(), &layoutInfo, nullptr, &pipeline.layout), "vkCreatePipelineLayout");

        if (status.IsSuccess()) {
            const std::array stages{
                VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = vertex.stage, .module = vertex.module, .pName = vertex.entryPoint },
                VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = fragment.stage, .module = fragment.module, .pName = fragment.entryPoint }
            };

            const VkPipelineVertexInputStateCreateInfo vertexInput{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

            const VkPipelineInputAssemblyStateCreateInfo assembly{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
            };

            const VkPipelineViewportStateCreateInfo viewport{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .scissorCount = 1
            };

            const VkPipelineRasterizationStateCreateInfo rasterization{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = VK_CULL_MODE_NONE,
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .lineWidth = 1.0f
            };

            const VkPipelineMultisampleStateCreateInfo multisample{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
            };

            const VkPipelineColorBlendAttachmentState attachment{
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
            };

            const VkPipelineColorBlendStateCreateInfo blend{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = 1,
                .pAttachments = &attachment
            };

            constexpr std::array dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

            const VkPipelineDynamicStateCreateInfo dynamic{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
                .pDynamicStates = dynamicStates.data()
            };

            const VkFormat colorFormat = ToVulkanFormat(desc.colorFormat);

            const VkPipelineRenderingCreateInfo rendering{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                .colorAttachmentCount = 1,
                .pColorAttachmentFormats = &colorFormat
            };

            const VkGraphicsPipelineCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .pNext = &rendering,
                .stageCount = static_cast<uint32_t>(stages.size()),
                .pStages = stages.data(),
                .pVertexInputState = &vertexInput,
                .pInputAssemblyState = &assembly,
                .pViewportState = &viewport,
                .pRasterizationState = &rasterization,
                .pMultisampleState = &multisample,
                .pColorBlendState = &blend,
                .pDynamicState = &dynamic,
                .layout = pipeline.layout,
                .basePipelineIndex = -1
            };

            status = MakeVulkanStatus(vkCreateGraphicsPipelines(device.GetHandle(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline.pipeline), "vkCreateGraphicsPipelines");
        }

        DestroyVulkanShader(device, fragment);
        DestroyVulkanShader(device, vertex);

        if (!status.IsSuccess()) {
            DestroyVulkanPipeline(device, pipeline);
            return status;
        }

        pipeline.colorFormat = desc.colorFormat;
        pipeline.sampleCount = desc.sampleCount;
        return {};
    }

    void DestroyVulkanPipeline(const VulkanDevice& device, VulkanPipelineRecord& pipeline) noexcept
    {
        if (pipeline.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device.GetHandle(), pipeline.pipeline, nullptr);
        if (pipeline.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device.GetHandle(), pipeline.layout, nullptr);
        pipeline = {};
    }

}