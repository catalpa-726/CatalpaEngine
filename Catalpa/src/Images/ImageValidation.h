#pragma once
#include "Core/Base.h"
#include "Images/ImageAsset.h"

namespace Catalpa {
	/**
	 * @brief 校验图像格式、语义、子资源布局及其字节范围。
	 * 校验完整声明的mip/layer/face集合、溢出/pitch/范围及格式语义；输入保持不变。
	 *
	 * @param[in] image 待检查的像素载荷、格式、尺寸及子资源布局。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 各子资源完整、尺寸及范围有效、格式和语义相容时为 true。
	 */
	[[nodiscard]] CATALPA_API bool ValidateImageData(const ImageData& image, std::string& error);
	/**
	 * @brief 校验图像资产的来源描述及像素数据。
	 * 校验资产像素和来源文本，来源只作元数据，不自行打开任何文件。
	 *
	 * @param[in] image 需要同时校验像素数据和来源文本的图像资产。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 来源元数据与图像本体均符合约定时为 true，否则为 false。
	 */
	[[nodiscard]] CATALPA_API bool ValidateImageAsset(const ImageAsset& image, std::string& error);
}
