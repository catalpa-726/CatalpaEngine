#pragma once
#include "RHI/RHIPipelineDesc.h"
#include "RHI/RHIError.h"
namespace Catalpa {
	class RHIDevice;
	class AssetManager;
	struct ComputePipelineResource {
		RHIDevice* device = nullptr;
		ComputePipelineHandle pipeline{};
		AssetVersionRef<ShaderAsset> shader;
		std::vector<RHIBindingLayoutEntry> bindings;
		uint32_t pushBytes = 0;
		~ComputePipelineResource();
	};
	class ComputePipelineLibrary {
	public:
		ComputePipelineLibrary(RHIDevice& device, AssetManager& assets) : m_Device(device), m_Assets(assets) {}
		/**
		 * @brief 按内置着色器路径获取当前版本对应的计算管线。
		 *
		 * @param[in] file 内置计算着色器的资源路径。
		 * @param[out] output 接收保活管线的共享引用。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus Acquire(std::string_view file, std::shared_ptr<ComputePipelineResource>&);
		/**
		 * @brief 移除仅余缓存自身持有的条目。
		 */
		void Prune();
		void Clear() noexcept { m_Entries.clear(); }

	private:
		RHIDevice& m_Device;
		AssetManager& m_Assets;
		std::map<std::pair<AssetId, uint64_t>, std::shared_ptr<ComputePipelineResource>> m_Entries;
	};
} /* 命名空间 Catalpa */
