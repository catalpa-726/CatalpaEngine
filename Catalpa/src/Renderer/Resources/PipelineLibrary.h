#pragma once
#include "RHI/RHIPipelineDesc.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHIError.h"
namespace Catalpa {
	class RHIDevice;
	struct PipelineResource {
		RHIDevice* device = nullptr;
		GraphicsPipelineHandle pipeline{};
		AssetVersionRef<ShaderAsset> vertex, fragment;
		std::vector<RHIBindingLayoutEntry> bindings;
		~PipelineResource();
	};
	/* 每个设备只有一个负责持有管线的缓存，键包含着色器版本、入口、布局、附件及状态。 */
	class CATALPA_API PipelineLibrary {
	public:
		explicit PipelineLibrary(RHIDevice& device) : m_Device(device) {}
		/**
		 * @brief 按着色器版本、布局、附件与绘制状态查找或新建图形管线。
		 *
		 * @param[in] desc 构成缓存键和原生创建参数的图形管线描述。
		 * @param[out] output 接收可保活管线的共享资源。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus Acquire(const RHIGraphicsPipelineDesc&, std::shared_ptr<PipelineResource>& output);
		/**
		 * @brief 移除仅余缓存自身持有的条目。
		 */
		void Prune();
		void Clear() noexcept { m_Entries.clear(); }
		[[nodiscard]] size_t Size() const noexcept { return m_Entries.size(); }

	private:
		RHIDevice& m_Device;
		std::map<std::string, std::shared_ptr<PipelineResource>> m_Entries;
	};
}
