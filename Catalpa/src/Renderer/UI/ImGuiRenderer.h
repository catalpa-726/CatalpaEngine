#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
#include "RHI/RHIHandle.h"
#include "Renderer/Resources/TextureGpuCache.h"
#include "Renderer/Resources/PipelineLibrary.h"
#include "RHI/RHIError.h"
#include "RHI/RHIBufferDesc.h"
#include "Images/PixelFormat.h"
#include "Renderer/UI/ImGuiFrameData.h"
#include "Renderer/RenderGraph/RenderGraphResource.h"

namespace Catalpa {

	class RHIDevice;
	class AssetManager;
	class RenderGraph;
	class UploadScheduler;

	/* 主窗口 UI 的 RHI 桥；只消费自有 CPU 数据，不访问 ImGui 上下文或原生 Vulkan。 */
	class ImGuiRenderer {
	public:
		ImGuiRenderer(RHIDevice& device, AssetManager&, TextureGpuCache&, PipelineLibrary&);
		~ImGuiRenderer();
		ImGuiRenderer(const ImGuiRenderer&) = delete;
		ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

		/**
		 * @brief 固定字体图集对应的图像资产版本，并安排纹理上传。
		 * 固定英文界面使用的字形图集，立即将像素复制给现有上传器，并在首帧渲染图中完成上传。
		 *
		 * @param[in] version 用于创建字体图集 GPU 纹理的固定版本图像引用。
		 *
		 * @return 返回字体图集纹理及其上传准备状态，实际上传由后续渲染图执行。
		 */
		[[nodiscard]] RHIStatus InitFont(const AssetVersionRef<ImageAsset>&);
		/**
		 * @brief 仅在 PrepareFrame 返回 ready 后调用，安全覆盖该槽的 host-visible 缓冲。
		 *
		 * @param[in] slot 已可安全写入的帧槽索引。
		 * @param[in] frame 本次 ImGui 绘制快照。
		 * @param[in] format 绘制目标的像素格式。
		 * @param[in] collectRetired 是否按本帧仍使用的标识清理旧纹理。
		 *
		 * @return 返回当前帧槽的界面绘制资源准备状态。
		 */
		[[nodiscard]] RHIStatus Prepare(
		    uint32_t slot, const ImGuiFrameData& frame, PixelFormat format, bool collectRetired = true);
		/**
		 * @brief 在场景之后、Present 之前按 UI 顺序登记 Load/Store 绘制及采样访问。
		 *
		 * @param[in,out] graph 接收界面绘制任务及资源访问声明的渲染图。
		 * @param[in] color 作为界面叠加绘制目标的图内颜色纹理。
		 * @param[in] extent 绘制目标的实际像素宽高，用于换算裁剪矩形。
		 * @param[in] slot 已为该界面帧准备动态缓冲区的帧槽索引。
		 * @param[in] frame 界面的绘制列表、显示区域和帧缓冲缩放信息。
		 *
		 * @return 返回将界面绘制和资源访问加入渲染图的结果。
		 */
		[[nodiscard]] RHIStatus AddPasses(
		    RenderGraph& graph, RGTexture color, Extent2D extent, uint32_t slot, const ImGuiFrameData& frame);
		/**
		 * @brief 借用引用维持真实资源所有者的生命周期。注册表不接管 RHI 资源，也不向编辑器暴露图像视图句柄。
		 *
		 * @param[in] view 已创建的纹理视图句柄，查询它的格式以选择过滤方式。
		 * @param[in] lease 与注册项共同持有的资源所有权引用，防止对应纹理提前释放。
		 * @param[out] token 成功时接收界面绘制命令用于引用该纹理的不透明编号。
		 * @param[in] mip 预览所选的纹理细节层级索引，0 为基础层。
		 * @param[in] channelMask RGBA 通道选择掩码，低四位分别对应四个通道，至少启用一个。
		 * @param[in] preview 是否启用纹理预览的层级与通道显示逻辑。
		 *
		 * @return 返回界面纹理登记状态；成功时 token 可用于后续绘制。
		 */
		[[nodiscard]] RHIStatus RegisterTexture(TextureViewHandle, std::shared_ptr<void> lease, uint64_t& token,
		    uint32_t mip = 0, uint32_t channelMask = 15, bool preview = false);
		/**
		 * @brief 标记外部界面纹理登记项待回收，保留仍可能被在途绘制引用的资源。
		 *
		 * @param[in] token RegisterTexture 返回的界面令牌；未知令牌不会改变登记表。
		 */
		void ReleaseTexture(uint64_t token) noexcept;

	private:
		struct FrameBuffers {
			BufferHandle vertices{}, indices{};
			uint64_t vertexCapacity = 0, indexCapacity = 0;
		};
		struct RegisteredTexture {
			TextureViewHandle view{};
			std::shared_ptr<void> lease;
			uint32_t mip = 0;
			uint32_t channelMask = 15;
			bool preview = false;
			bool retired = false;
			bool linearFiltering = true;
		};

		[[nodiscard]] RHIStatus EnsurePipeline(PixelFormat format);
		[[nodiscard]] RHIStatus WriteDynamicBuffer(
		    BufferHandle& buffer, uint64_t& capacity, RHIBufferUsage usage, std::span<const std::byte> bytes);

		RHIDevice& m_Device;
		std::vector<FrameBuffers> m_Frames;
		AssetManager& m_Assets;
		TextureGpuCache& m_Textures;
		PipelineLibrary& m_Pipelines;
		std::shared_ptr<TextureGpuResource> m_Font;
		std::shared_ptr<PipelineResource> m_Pipeline;
		SamplerHandle m_Sampler{};
		SamplerHandle m_NearestSampler{};
		PixelFormat m_Format = PixelFormat::Unknown;
		std::map<uint64_t, RegisteredTexture> m_Registered;
		uint64_t m_NextTextureToken = ImGuiFontAtlasId + 1;
		std::set<PixelFormat> m_ReportedNearestFormats;
	};

}
