#pragma once
#include "AssetPipeline/IAssetImporter.h"

namespace Catalpa::Tools {
	/**
	 * @brief 识别普通扩展名和作者资产的复合扩展名。
	 *
	 * @param[in] source 输入的文件系统路径。
	 *
	 * @return 含前导点的小写扩展名。已知作者复合后缀移除末尾 .json；无扩展名时返回空字符串。
	 */
	std::string AssetSourceExtension(const std::filesystem::path& source);

	/* 扩展名不区分大小写，每种格式固定使用注册的解析器并保留其错误。 */
	class AssetImportRegistry final {
	public:
		/**
		 * @brief 为规范化扩展名登记导入器，拒绝空或重复的注册。
		 *
		 * @param[in] extension 要交由该导入器处理的文件扩展名。
		 * @param[in] importer 负责解析该类型源文件的导入器实例。
		 * @param[out] error 失败时接收当前操作的具体原因。
		 *
		 * @return 完成上述操作时为 true，操作未成功时为 false。
		 */
		bool Register(std::string extension, std::shared_ptr<const IAssetImporter>, std::string& error);
		const IAssetImporter* Find(const std::filesystem::path&) const;
		/**
		 * @brief 创建内置源文件类型到导入器的默认注册表。
		 *
		 * @return 已按文件扩展名注册内置导入器的独立注册表；默认条目无法登记时抛出异常。
		 */
		static AssetImportRegistry Defaults();

	private:
		std::map<std::string, std::shared_ptr<const IAssetImporter>, std::less<>> m_Importers;
	};
}
