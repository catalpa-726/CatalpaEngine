#pragma once
#include "Core/Base.h"
#include "Assets/AssetHandle.h"
#include "UI/FontAsset.h"
#include "Images/ImageAsset.h"

namespace Catalpa {
	/* UI 包只保存受检的逻辑相对路径；运行时不回读作者目录或操作系统文件。 */
	struct UIDocumentAsset {
		std::string entry;
		std::map<std::string, std::string, std::less<>> files;
		std::map<std::string, AssetId, std::less<>> imageIds, fontIds;
		std::map<std::string, std::string, std::less<>> values;
		std::map<std::string, AssetVersionRef<ImageAsset>, std::less<>> images;
		std::map<std::string, AssetVersionRef<FontAsset>, std::less<>> fonts;
	};
	/**
	 * @brief 判断是否界面相对路径。
	 *
	 * @param[in] path 待校验为界面文档相对资源路径的字符串。
	 *
	 * @return 满足上述条件时为 true，否则为 false。
	 */
	CATALPA_API bool IsUIRelativePath(std::string_view) noexcept;
	/**
	 * @brief 判断是否界面 UTF-8。
	 *
	 * @param[in] text 需要按 UTF-8 规则检查的字节序列。
	 *
	 * @return 满足上述条件时为 true，否则为 false。
	 */
	CATALPA_API bool IsUIUtf8(std::string_view) noexcept;
	/**
	 * @brief 校验界面文档。
	 *
	 * @param[in] asset 含界面标记及资源依赖的待验证文档资产。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateUIDocument(const UIDocumentAsset&, std::string& error);

	class AssetManager;
	class CATALPA_API UIDocumentLoader final {
	public:
		/**
		 * @brief 校验界面文档并写出二进制资产载荷。
		 *
		 * @param[in] asset 要编码为运行时载荷的界面文档资产。
		 * @param[out] out 接收 std::vector<std::byte> 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Encode(const UIDocumentAsset&, std::vector<std::byte>&, std::string& error);
		/**
		 * @brief 登记界面文档的资产加载回调。
		 *
		 * @param[in] manager 提供资产查找和版本访问的管理器。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Register(AssetManager&, std::string& error);
	};
} /* 命名空间 Catalpa */
