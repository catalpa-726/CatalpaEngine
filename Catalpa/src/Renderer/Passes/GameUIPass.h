#pragma once
#include "UI/UIDrawData.h"
#include "Renderer/Resources/ComputeBuffer.h"
#include "Renderer/Resources/TextureGpuCache.h"
#include "Renderer/Resources/PipelineLibrary.h"
#include "Renderer/RenderGraph/RenderGraphResource.h"
namespace Catalpa {
	class RenderGraph;
	class GameUIPass {
	public:
		GameUIPass(RHIDevice& device, TextureGpuCache& textures)
		    : m_Device(device), m_Textures(textures), m_Frames(device.GetFrameCount()) {}
		~GameUIPass();
		/**
		 * @brief 为当前帧槽准备游戏 UI 的顶点、索引、纹理和管线。
		 *
		 * @param[in] slot 轮换帧槽的索引。
		 * @param[in] data 本帧的 UI 绘制快照。
		 * @param[in] pipeline 供 UI 绘制使用的管线资源。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus Prepare(uint32_t slot, const UIDrawData&, std::shared_ptr<PipelineResource>);
		/**
		 * @brief 将帧槽内的游戏 UI 批次叠加到目标纹理。
		 *
		 * @param[in] graph 当前渲染图。
		 * @param[in] target 接收 UI 的颜色附件。
		 * @param[in] extent 目标尺寸，以像素计。
		 * @param[in] slot 已经准备绘制快照的帧槽。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus Add(RenderGraph&, RGTexture target, Extent2D extent, uint32_t slot);

	private:
		struct Frame {
			ComputeBuffer vertices, indices;
			UIDrawData data;
			std::vector<std::shared_ptr<TextureGpuResource>> textures;
			std::shared_ptr<PipelineResource> pipeline;
		};
		struct TextureEntry {
			std::shared_ptr<const UITextureData> source;
			std::shared_ptr<TextureGpuResource> gpu;
		};
		RHIDevice& m_Device;
		TextureGpuCache& m_Textures;
		std::vector<Frame> m_Frames;
		std::vector<TextureEntry> m_Cache;
		uint64_t m_NextTexture = 1;
		SamplerHandle m_Sampler{};
	};
} /* 命名空间 Catalpa */
