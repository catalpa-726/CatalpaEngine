#pragma once

#include "RenderGraphResource.h"
#include "RHI/RHICommandTypes.h"
#include "RHI/RHIBindingDesc.h"
#include "RHI/RHIRenderingDesc.h"

/* 描述当前索引绘制 Pass。复制和绘制都采用明确参数，由同一份参数生成访问声明与命令 */
namespace Catalpa {

	/*
	 * 一份声明同时确定采样资源访问和 set 0 的纹理、Sampler 绑定。
	 * 当前只支持 fragment 采样；每对使用两个独立 binding。
	 */
	struct RGSampledTextureBinding {
		RGTexture texture{};
		SamplerHandle sampler{};
		uint32_t textureBinding = 0;
		uint32_t samplerBinding = 1;
	};

	/* 同一描述同时产生UniformRead访问及set 0字节范围绑定；数据须在导入前写入。 */
	struct RGUniformBufferBinding {
		RGBuffer buffer{};
		uint32_t binding = 0;
		uint64_t offset = 0;
		uint64_t size = 0;
		RHIShaderVisibility visibility = RHIShaderVisibility::VertexFragment;
	};

	struct RGStorageBufferBinding {
		RGBuffer buffer{};
		uint32_t binding = 0;
		uint64_t offset = 0, size = 0;
		RGAccessMode mode = RGAccessMode::Read;
		bool initializesRange = false;
	};

	struct RGComputeDesc {
		ComputePipelineHandle pipeline{};
		std::array<uint32_t, 3> groups{1, 1, 1};
		std::vector<std::byte> pushConstants;
		std::vector<RGStorageBufferBinding> storageBuffers;
		std::vector<RGUniformBufferBinding> uniformBuffers;
		std::vector<RGSampledTextureBinding> sampledTextures;
		bool sideEffect = false;
	};

	/* 使用单个顶点流和索引流。至少提供一个颜色或深度目标，允许仅使用深度附件进行绘制。 */
	struct RGIndexedDrawDesc {
		RGBuffer vertices{};
		RGBuffer indices{};
		RGTexture color{};
		GraphicsPipelineHandle pipeline{};
		uint64_t vertexOffset = 0;
		uint64_t indexOffset = 0;
		RHIIndexType indexType = RHIIndexType::UInt32;
		uint32_t indexCount = 0;
		uint32_t instanceCount = 1;
		uint32_t firstIndex = 0;
		int32_t baseVertex = 0;
		uint32_t firstInstance = 0;
		RHILoadOp loadOp = RHILoadOp::Clear;
		RHIStoreOp storeOp = RHIStoreOp::Store;
		std::array<float, 4> clearColor{0.0f, 0.0f, 0.0f, 1.0f};
		RHIViewport viewport{};
		Rect2D scissor{};
		std::vector<std::byte> pushConstants; /* 自有字节副本；空数组表示本次绘制未提供 push constants。 */
		std::vector<RGSampledTextureBinding> sampledTextures;
		std::vector<RGUniformBufferBinding> uniformBuffers;
		RGTexture depth{};
		RHILoadOp depthLoadOp = RHILoadOp::Clear;
		RHIStoreOp depthStoreOp = RHIStoreOp::Store;
		float clearDepth = 1.0f;
		bool depthReadOnly = false;
		/* 非零时使用非索引 Draw；无顶点布局可由 SV_VertexID 生成全屏三角形。 */
		uint32_t vertexCount = 0;
		uint32_t firstVertex = 0;
		std::vector<RGStorageBufferBinding> storageBuffers;
		RGBuffer indirect{};
		uint64_t indirectOffset = 0;
		uint32_t sourceInstance = UINT32_MAX;
	};

}
