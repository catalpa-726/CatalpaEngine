#pragma once
#include "AssetPipeline/IAssetImporter.h"

#include <utility>

namespace Catalpa::Tools {
	/* 八位图像保留RGBA8；Radiance HDR保留线性RGBA32F，不翻转、缩放或归一化原件。 */
	class TextureImporter final : public IAssetImporter {
	public:
		std::string_view Name() const noexcept override { return "stb-image/rgba8-hdr-float-v3-mips-bc1"; }

		/**
		 * @brief 读取图像并生成后续资产烘焙所需的导入内容。
		 *
		 * @param[in] ctx 导入任务共享的项目、依赖与构建预算。
		 * @param[out] output 输出的导入对象与依赖记录。
		 * @param[out] e 失败时接收当前操作的具体原因。
		 *
		 * @return 完成上述操作时为 true，操作未成功时为 false。
		 */
		bool Import(AssetBuildContext&, AssetImportResult&, std::string&) const override;
		/**
		 * @brief 同一张原图可按不同材质通道使用。分类和派生 ID 后缀一同返回，保证二者一致。
		 *
		 * @param[in] channel 源材质通道名，例如 baseColor、emissive 或 normal。
		 *
		 * @return 图像语义及对应的派生标识后缀；未归为颜色或法线的通道使用数据语义。
		 */
		static std::pair<ImageSemantic, std::string> MaterialChannel(std::string_view channel);
		/**
		 * @brief 检查 HDR 解码尺寸及 512 MiB 预算并计算 RGBA32F 所需字节数。
		 * HDR 源解码为 RGBA32F，单张最多 512 MiB；只检查尺寸，不分配像素或修改原件。
		 *
		 * @param[in] width HDR 图像宽度，以像素计。
		 * @param[in] height HDR 图像高度，以像素计。
		 * @param[out] bytes 接收按解码格式计算的单张图像字节数。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 尺寸有效且单张 RGBA32F 图像解码后不超过 512 MiB 时为 true。
		 */
		static bool HdrDecodedSize(uint32_t width, uint32_t height, size_t& bytes, std::string& error);
		/**
		 * @brief 按格式及工作内存预算解码源图像，保留诊断。
		 *
		 * @param[in] ctx 为本次解码提供预算和诊断的导入上下文。
		 * @param[in] bytes 原始编码图片的文件字节。
		 * @param[in] source 用于诊断和来源记录的文件路径。
		 * @param[in] semantic 此贴图在材质或环境中的用途。
		 * @param[out] out 接收解码后的图像。
		 * @param[out] e 失败时接收解码错误说明。
		 *
		 * @return 完成上述操作时为 true，操作未成功时为 false。
		 */
		static bool Decode(AssetBuildContext&, std::span<const std::byte>, std::string source, ImageSemantic,
		    ImageAsset&, std::string&);
	};
}
