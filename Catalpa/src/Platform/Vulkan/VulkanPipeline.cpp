#include "catpch.h"
#include "VulkanPipeline.h"
#include "VulkanShader.h"
#include "VulkanDevice.h"
#include "VulkanConversions.h"
#include "VulkanUtils.h"
#include "VulkanBinding.h"

namespace Catalpa {

    RHIStatus CreateVulkanGraphicsPipeline(VulkanDevice& device, const RHIGraphicsPipelineDesc& desc, VulkanPipelineRecord& pipeline)
    {
        pipeline = {};
        std::string shaderError;
        if (!ValidateRHIPipelineShaderInterface(desc, shaderError))
            return { .code = RHIErrorCode::InvalidDescription, .operation = "CreateVulkanGraphicsPipeline/metadata" };

        if (desc.vertex.stage != RHIShaderStage::Vertex || desc.fragment.stage != RHIShaderStage::Fragment)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "CreateVulkanGraphicsPipeline" };

        const bool hasColor = desc.colorFormat != PixelFormat::Unknown;
        const bool hasDepth = desc.depthFormat != PixelFormat::Unknown;
        if (desc.sampleCount != 1 || (!hasColor && !hasDepth)
            || (hasColor && !device.GetFormatSupport(desc.colorFormat).colorAttachment)
            || (hasDepth && (desc.depthFormat != PixelFormat::D32_Float || !device.GetFormatSupport(desc.depthFormat).depthAttachment)))
            return { .code = RHIErrorCode::Unsupported, .operation = "CreateVulkanGraphicsPipeline" };
        if ((!hasDepth && (desc.depthState.testEnabled || desc.depthState.writeEnabled))
            || (desc.depthState.writeEnabled && !desc.depthState.testEnabled))
            return { .code = RHIErrorCode::InvalidDescription, .operation = "CreateVulkanGraphicsPipeline" };
        VkCompareOp depthCompare = VK_COMPARE_OP_MAX_ENUM;
        switch (desc.depthState.compareOp) {
        case RHICompareOp::Always: depthCompare=VK_COMPARE_OP_ALWAYS; break;
        case RHICompareOp::Less: depthCompare=VK_COMPARE_OP_LESS; break;
        case RHICompareOp::LessEqual: depthCompare=VK_COMPARE_OP_LESS_OR_EQUAL; break;
        case RHICompareOp::Equal: depthCompare=VK_COMPARE_OP_EQUAL; break;
        case RHICompareOp::Greater: depthCompare=VK_COMPARE_OP_GREATER; break;
        case RHICompareOp::GreaterEqual: depthCompare=VK_COMPARE_OP_GREATER_OR_EQUAL; break;
        default: return { .code=RHIErrorCode::InvalidDescription,.operation="CreateVulkanGraphicsPipeline" };
        }
        VkCullModeFlags cullMode=VK_CULL_MODE_NONE;
        switch (desc.cullMode) {
        case RHICullMode::None: break;
        case RHICullMode::Back: cullMode=VK_CULL_MODE_BACK_BIT; break;
        case RHICullMode::Front: cullMode=VK_CULL_MODE_FRONT_BIT; break;
        default: return { .code=RHIErrorCode::InvalidDescription,.operation="CreateVulkanGraphicsPipeline" };
        }
        if (desc.frontFace != RHIFrontFace::CounterClockwise && desc.frontFace != RHIFrontFace::Clockwise)
            return { .code=RHIErrorCode::InvalidDescription,.operation="CreateVulkanGraphicsPipeline" };

        if (desc.blendMode != RHIBlendMode::Opaque && desc.blendMode != RHIBlendMode::StraightAlpha)
            return { .code = RHIErrorCode::Unsupported, .operation = "CreateVulkanGraphicsPipeline" };

        if (desc.blendMode == RHIBlendMode::StraightAlpha && (!hasColor || !device.GetFormatSupport(desc.colorFormat).colorAttachmentBlend))
            return { .code = RHIErrorCode::Unsupported, .operation = "CreateVulkanGraphicsPipeline" };

        const auto& vertexLayout = desc.vertexLayout;
        const auto& capabilities = device.GetCapabilities();
        const bool hasVertexInput = !vertexLayout.attributes.empty();

        const auto& pushConstants = desc.pushConstants;
        const VkShaderStageFlags pushConstantStages = ToVulkanShaderStages(pushConstants.visibility);

        if (pushConstants.size % 4 != 0 ||
            (pushConstants.size == 0 && pushConstants.visibility != RHIShaderVisibility::None) ||
            (pushConstants.size != 0 && pushConstantStages == 0))
            return { .code = RHIErrorCode::InvalidDescription, .operation = "CreateVulkanGraphicsPipeline" };

        if (pushConstants.size > capabilities.maxPushConstantBytes)
            return { .code = RHIErrorCode::Unsupported, .operation = "CreateVulkanGraphicsPipeline" };

        const auto bindingEntries = desc.bindingLayout.entries;

        if (bindingEntries.size() > RHIMaxBindingsPerSet)
            return { .code = RHIErrorCode::Unsupported, .operation = "CreateVulkanGraphicsPipeline" };

        std::array<VkDescriptorSetLayoutBinding, RHIMaxBindingsPerSet> nativeBindings{};
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &properties);
        if (const auto bindingStatus = BuildVulkanBindingLayout(bindingEntries, properties.limits,
            hasColor ? 1u : 0u, nativeBindings); !bindingStatus.IsSuccess()) return bindingStatus;

        // 在原生对象创建前复制调用方描述，成功发布时只交换容器。
        std::vector<RHIBindingLayoutEntry> bindings(bindingEntries.begin(), bindingEntries.end());

        if ((!hasVertexInput && vertexLayout.stride != 0) ||
            (hasVertexInput && (vertexLayout.stride == 0 || vertexLayout.stride % 4 != 0)))
            return { .code = RHIErrorCode::InvalidDescription, .operation = "CreateVulkanGraphicsPipeline" };

        if (vertexLayout.attributes.size() > capabilities.maxVertexInputAttributes ||
            vertexLayout.stride > capabilities.maxVertexInputBindingStride)
            return { .code = RHIErrorCode::Unsupported, .operation = "CreateVulkanGraphicsPipeline" };

        std::vector<VkVertexInputAttributeDescription> attributes;
        attributes.reserve(vertexLayout.attributes.size());
        uint32_t vertexAttributeEnd = 0;

        for (const auto& attribute : vertexLayout.attributes) {
            const uint32_t byteSize = GetVertexFormatByteSize(attribute.format);
            const VkFormat format = ToVulkanFormat(attribute.format);

            if (byteSize == 0 || format == VK_FORMAT_UNDEFINED || attribute.offset % 4 != 0)
                return { .code = RHIErrorCode::InvalidDescription, .operation = "CreateVulkanGraphicsPipeline" };

            if (attribute.location >= capabilities.maxVertexInputAttributes ||
                attribute.offset > capabilities.maxVertexInputAttributeOffset)
                return { .code = RHIErrorCode::Unsupported, .operation = "CreateVulkanGraphicsPipeline" };

            if (attribute.offset > vertexLayout.stride || byteSize > vertexLayout.stride - attribute.offset)
                return { .code = RHIErrorCode::InvalidDescription, .operation = "CreateVulkanGraphicsPipeline" };

            if (std::ranges::any_of(attributes, [&](const auto& existing) { return existing.location == attribute.location; }))
                return { .code = RHIErrorCode::InvalidDescription, .operation = "CreateVulkanGraphicsPipeline" };

            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDevice(), format, &properties);

            if ((properties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) == 0)
                return { .code = RHIErrorCode::Unsupported, .operation = "CreateVulkanGraphicsPipeline" };

            attributes.push_back({ .location = attribute.location, .binding = 0, .format = format, .offset = attribute.offset });
            vertexAttributeEnd = std::max(vertexAttributeEnd, attribute.offset + byteSize);
        }

        VulkanShaderRecord vertex{}, fragment{};

        RHIStatus status = CreateVulkanShader(device, desc.vertex, vertex);
        if (!status.IsSuccess()) return status;

        status = CreateVulkanShader(device, desc.fragment, fragment);

        if (!status.IsSuccess()) {
            DestroyVulkanShader(device, vertex);
            return status;
        }

        if (!bindings.empty()) {
            const VkDescriptorSetLayoutCreateInfo bindingInfo{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = static_cast<uint32_t>(bindings.size()),
                .pBindings = nativeBindings.data()
            };

            VkDescriptorSetLayout bindingLayout = VK_NULL_HANDLE;
            status = MakeVulkanStatus(
                vkCreateDescriptorSetLayout(device.GetHandle(), &bindingInfo, nullptr, &bindingLayout),
                "vkCreateDescriptorSetLayout");

            if (!status.IsSuccess()) {
                DestroyVulkanShader(device, fragment);
                DestroyVulkanShader(device, vertex);
                return status;
            }

            pipeline.bindingLayout = bindingLayout;
        }

        const VkPushConstantRange pushConstantRange{
            .stageFlags = pushConstantStages,
            .offset = 0,
            .size = pushConstants.size
        };

        const VkPipelineLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = bindings.empty() ? 0u : 1u,
            .pSetLayouts = bindings.empty() ? nullptr : &pipeline.bindingLayout,
            .pushConstantRangeCount = pushConstants.size != 0 ? 1u : 0u,
            .pPushConstantRanges = pushConstants.size != 0 ? &pushConstantRange : nullptr
        };

        VkPipelineLayout layout = VK_NULL_HANDLE;
        status = MakeVulkanStatus(
            vkCreatePipelineLayout(device.GetHandle(), &layoutInfo, nullptr, &layout),
            "vkCreatePipelineLayout");

        if (status.IsSuccess()) {
            pipeline.layout = layout;

            const std::array stages{
                VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = vertex.stage, .module = vertex.module, .pName = vertex.entryPoint },
                VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = fragment.stage, .module = fragment.module, .pName = fragment.entryPoint }
            };

            const VkVertexInputBindingDescription binding{
                .binding = 0,
                .stride = vertexLayout.stride,
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
            };

            const VkPipelineVertexInputStateCreateInfo vertexInput{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .vertexBindingDescriptionCount = hasVertexInput ? 1u : 0u,
                .pVertexBindingDescriptions = hasVertexInput ? &binding : nullptr,
                .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
                .pVertexAttributeDescriptions = hasVertexInput ? attributes.data() : nullptr
            };

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
                .cullMode = cullMode,
                .frontFace = desc.frontFace == RHIFrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .lineWidth = 1.0f
            };

            const VkPipelineMultisampleStateCreateInfo multisample{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
            };

            const VkPipelineColorBlendAttachmentState attachment{
                .blendEnable = static_cast<VkBool32>(desc.blendMode == RHIBlendMode::StraightAlpha),
                .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .colorBlendOp = VK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .alphaBlendOp = VK_BLEND_OP_ADD,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
            };

            const VkPipelineColorBlendStateCreateInfo blend{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = hasColor ? 1u : 0u,
                .pAttachments = hasColor ? &attachment : nullptr
            };

            const VkPipelineDepthStencilStateCreateInfo depthState{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = static_cast<VkBool32>(desc.depthState.testEnabled),
                .depthWriteEnable = static_cast<VkBool32>(desc.depthState.writeEnabled),
                .depthCompareOp = depthCompare,
                .minDepthBounds = 0.0f,
                .maxDepthBounds = 1.0f
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
                .colorAttachmentCount = hasColor ? 1u : 0u,
                .pColorAttachmentFormats = hasColor ? &colorFormat : nullptr,
                .depthAttachmentFormat = ToVulkanFormat(desc.depthFormat)
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
                .pDepthStencilState = &depthState,
                .pColorBlendState = &blend,
                .pDynamicState = &dynamic,
                .layout = pipeline.layout,
                .basePipelineIndex = -1
            };

            status = MakeVulkanStatus(vkCreateGraphicsPipelines(device.GetHandle(), device.GetPipelineCache(), 1, &info, nullptr, &pipeline.pipeline), "vkCreateGraphicsPipelines");
        }

        DestroyVulkanShader(device, fragment);
        DestroyVulkanShader(device, vertex);

        if (!status.IsSuccess()) {
            DestroyVulkanPipeline(device, pipeline);
            return status;
        }

        pipeline.colorFormat = desc.colorFormat;
        pipeline.depthFormat = desc.depthFormat;
        pipeline.depthWrite = desc.depthState.writeEnabled;
        pipeline.sampleCount = desc.sampleCount;
        pipeline.vertexStride = vertexLayout.stride;
        pipeline.vertexAttributeEnd = vertexAttributeEnd;
        pipeline.pushConstantBytes = pushConstants.size;
        pipeline.pushConstantStages = pushConstantStages;
        pipeline.bindings.swap(bindings);
        return {};
    }

    void DestroyVulkanPipeline(const VulkanDevice& device, VulkanPipelineRecord& pipeline) noexcept
    {
        if (pipeline.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device.GetHandle(), pipeline.pipeline, nullptr);
        if (pipeline.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device.GetHandle(), pipeline.layout, nullptr);
        if (pipeline.bindingLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device.GetHandle(), pipeline.bindingLayout, nullptr);
        pipeline = {};
    }

}
