#pragma once
#include "Renderer/Resources/ComputeBuffer.h"
#include "Renderer/Resources/ComputePipelineLibrary.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/RenderFramePacket.h"
namespace Catalpa {
	struct DepthPyramidLevel {
		uint32_t offset = 0, width = 0, height = 0;
	};
	struct GpuSceneFrame {
		ComputeBuffer depthPyramid, lightRects, lightLists, cullInput, cullParameters, indirect;
		std::vector<DepthPyramidLevel> levels;
		std::shared_ptr<ComputePipelineResource> depthKernel, lightKernel, cullKernel;
		uint32_t tilesX = 0, tilesY = 0;
		RGBuffer lightListsGraph;
		uint64_t lightListBytes = 0;
	};
	class GpuScenePasses {
	public:
		/**
		 * @brief 为场景计算任务准备分块参数、深度层级和所需 GPU 资源。
		 *
		 * @param[in] device 提供计算能力和缓冲区的设备。
		 * @param[in] library 提供深度、光源与剔除内核的管线库。
		 * @param[in] packet 当前场景帧。
		 * @param[in] view 本次处理的视图。
		 * @param[in] extent 渲染目标的像素宽高。
		 * @param[out] frame 接收供后续计算任务使用的帧资源。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		static RHIStatus Prepare(
		    RHIDevice&, ComputePipelineLibrary&, const RenderFramePacket&, const RenderView&, Extent2D, GpuSceneFrame&);
		/**
		 * @brief 为深度金字塔各级登记 GPU 归约任务。
		 *
		 * @param[out] graph 接收归约任务的渲染图。
		 * @param[in] depth 原始深度附件。
		 * @param[in] sampler 读取深度使用的采样器。
		 * @param[in] reversedZ 是否使用反向深度约定。
		 * @param[in] frame 保存各级尺寸、偏移与中间资源的帧数据。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		static RHIStatus AddDepth(RenderGraph&, RGTexture, SamplerHandle, bool reversedZ, GpuSceneFrame&);
		/**
		 * @brief 登记为屏幕分块生成光源列表的 GPU 计算任务。
		 *
		 * @param[in] graph 当前渲染图。
		 * @param[in] lights 帧数据中的光源数量。
		 * @param[in] frame 分块配置与光源列表所在的帧资源。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		static RHIStatus AddLights(RenderGraph&, uint32_t lights, GpuSceneFrame&);
		/**
		 * @brief 计算可见实例并将间接绘制参数接入后续绘制。
		 *
		 * @param[in] device 创建和更新帧资源的渲染设备。
		 * @param[out] graph 接收剔除任务的渲染图。
		 * @param[in] packet 提供实例包围盒和绘制身份的场景帧。
		 * @param[in] view 用于剔除的相机视图。
		 * @param[in] useDepth 是否用深度金字塔参与剔除。
		 * @param[in] draws 需要关联间接绘制参数的索引绘制列表。
		 * @param[in] frame 保存可见性计算所需的临时 GPU 资源。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		static RHIStatus AddVisibility(RHIDevice&, RenderGraph&, const RenderFramePacket&, const RenderView&,
		    bool useDepth, std::vector<RGIndexedDrawDesc>&, GpuSceneFrame&);
	};
} /* 命名空间 Catalpa */
