#pragma once
#include "Images/ImageAssetLoader.h"

namespace Catalpa::Tools {
	struct TextureCookReport {
		bool mipmaps = false, compressed = false;
		double linearRmse = 0, maximumLinearError = 0;
		std::string policy;
	};
	/* 只处理派生 Cook 值：原始解码像素及源文件保持不变。 */
	class TextureCooker final {
	public:
		static constexpr std::string_view Signature = "rgba8-mips-area-linear-premult-normal-v1-bc1-rmse008";
		/**
		 * @brief 从源图生成符合运行时约束的派生图像和烘焙质量报告。
		 *
		 * @param[in] source 输入的 ImageAsset。
		 * @param[out] output 接收 ImageAsset 结果。
		 * @param[out] report 接收纹理处理策略、生成的 mip 层数和压缩误差等统计。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Prepare(const ImageAsset&, ImageAsset&, TextureCookReport&, std::string&);
		/**
		 * @brief 准备派生图像并序列化为运行时图像载荷。
		 *
		 * @param[in] source 输入的 ImageAsset。
		 * @param[out] output 接收 std::vector<std::byte> 结果。
		 * @param[out] error 失败时接收原因说明。
		 * @param[out] report 可选的详细处理报告；非空时接收本次纹理准备和编码的统计。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Encode(const ImageAsset&, std::vector<std::byte>&, std::string&, TextureCookReport* = nullptr);
	};
} /* 命名空间 Catalpa::Tools */
