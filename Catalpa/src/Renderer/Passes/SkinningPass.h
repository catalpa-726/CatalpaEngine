#pragma once
#include "Renderer/Resources/ComputeBuffer.h"
#include "Renderer/Resources/ComputePipelineLibrary.h"
#include "Renderer/RenderFramePacket.h"
namespace Catalpa {
	class RenderGraph;
	struct SkinningFrame {
		ComputeBuffer weights, palette, output, status, readback;
		bool reportedFailure = false;
		AssetVersionRef<MeshSkinBinding> binding;
		std::shared_ptr<ComputePipelineResource> pipeline;
	};
	class SkinningPass {
	public:
		/**
		 * @brief 准备骨骼蒙皮着色器、权重数据和当前关节矩阵。
		 *
		 * @param[in] device 提供存储缓冲区的设备。
		 * @param[in] library 提供蒙皮计算程序的管线库。
		 * @param[in] input 包含蒙皮绑定与骨骼姿态的源数据。
		 * @param[in] frame 保存该帧使用的 GPU 蒙皮资源。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		static RHIStatus Prepare(RHIDevice&, ComputePipelineLibrary&, const RenderSkinningInput&, SkinningFrame&);
		/**
		 * @brief 在渲染图登记计算蒙皮任务并导入源顶点及输出缓冲区。
		 *
		 * @param[out] graph 接收计算任务的渲染图。
		 * @param[in] source 未蒙皮的源顶点缓冲区。
		 * @param[in] count 需要处理的顶点数。
		 * @param[out] frame 绑定输入、输出缓冲区及管线的帧资源。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		static RHIStatus Add(RenderGraph&, BufferHandle source, uint32_t count, SkinningFrame&);
	};
} /* 命名空间 Catalpa */
