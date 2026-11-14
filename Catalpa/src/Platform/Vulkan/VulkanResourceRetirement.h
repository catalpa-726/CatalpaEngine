#pragma once

#include "VulkanPipeline.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"

namespace Catalpa {

	class VulkanDevice;

	/* 这里只接收已解除命令引用的资源；销毁图像前还须解除存活视图对图像的引用。 */
	class VulkanResourceRetirement {
	public:
		explicit VulkanResourceRetirement(const VulkanDevice& device) : m_Device(device) {}

	private:
		friend class VulkanResourceRegistry;

		void RetirePipeline(VulkanPipelineRecord pipeline) noexcept;

		void RetireBuffer(VulkanBufferRecord buffer) noexcept;

		void RetireTexture(VulkanTextureRecord texture) noexcept;

		void RetireTextureView(VulkanTextureViewRecord view) noexcept;

		void RetireSampler(VulkanSamplerRecord sampler) noexcept;

	private:
		const VulkanDevice& m_Device;
	};

}