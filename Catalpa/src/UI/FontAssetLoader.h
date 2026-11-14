#pragma once
#include "UI/FontAsset.h"
namespace Catalpa {
	class AssetManager;
	class CATALPA_API FontAssetLoader final {
	public:
		/**
		 * @brief 校验字体字节后写出可加载的字体资产。
		 *
		 * @param[in] asset 要打包为字体资产载荷的字体数据。
		 * @param[out] out 接收 std::vector<std::byte> 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Encode(const FontAsset&, std::vector<std::byte>&, std::string& error);
		/**
		 * @brief 将字体资产解码器登记到资产管理器。
		 *
		 * @param[in] manager 提供资产查找和版本访问的管理器。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Register(AssetManager&, std::string& error);
	};
} /* 命名空间 Catalpa */
