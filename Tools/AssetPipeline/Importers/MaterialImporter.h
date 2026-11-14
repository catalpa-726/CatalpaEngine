#pragma once
#include "AssetPipeline/AssetBuildContext.h"

namespace Catalpa::Tools {
	/* 将跨格式材质参数写入统一材质定义。预览使用单独的派生 ID 和着色器参数。 */
	class MaterialImporter final {
	public:
		static AssetId PreviewId(const AssetBuildContext& c, std::string_view key) {
			return c.Identity(std::string(key) + "/preview");
		}

		/**
		 * @brief 生成导入材质或预览材质对应的定义文本。
		 *
		 * @param[in] m 已从模型读取的材质参数、纹理通道和采样信息。
		 * @param[in] shader 生成的材质定义引用的着色器标识。
		 * @param[in] preview 为 true 时生成仅展示基础色的无光照预览材质。
		 * @param[out] json 接收包含着色器标识和参数模式的材质定义 JSON。
		 * @param[out] error 失败时接收当前操作的具体原因。
		 *
		 * @return 已生成指定着色器和材质模式对应的定义 JSON 时为 true。
		 */
		static bool Definition(
		    const ImportedMaterial&, AssetId shader, bool preview, std::string& json, std::string& error);
		/**
		 * @brief 将源纹理的环绕和过滤枚举编码为引擎采样器标志。
		 * glTF枚举是跨导入器可复核的源协议值；输出领域采样策略，不持有RHI对象。
		 *
		 * @param[in] wrapU 纹理 U 方向的寻址方式。
		 * @param[in] wrapV 纹理 V 方向的寻址方式。
		 * @param[in] minFilter 缩小纹理时使用的采样过滤方式。
		 * @param[in] magFilter 放大纹理时使用的采样过滤方式。
		 * @param[out] output 成功时接收编码寻址和过滤方式的材质采样标志位。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 源环绕与过滤枚举均能转换为引擎采样标志时为 true。
		 */
		static bool SamplerFlags(uint32_t wrapU, uint32_t wrapV, uint32_t minFilter, uint32_t magFilter,
		    uint32_t& output, std::string& error);
	};
}
