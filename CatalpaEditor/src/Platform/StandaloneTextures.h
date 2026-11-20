#pragma once

#include "Assets/AssetManager.h"
#include "Images/ImageAsset.h"

namespace Catalpa::Editor {
	class Document;
	/** @brief 管理独立文档的贴图。保存时把所用像素写到相邻目录，以便文档搬移或跨进程打开。 */
	class StandaloneTextures {
	public:
		/**
		 * @brief 按槽语义读取图片并发布可供材质使用的固定版本。
		 * @param[in,out] assets 当前工作区共用的资产管理器。
		 * @param[in] source 用户选择的图片路径。
		 * @param[in] semantic 此槽对颜色、法线或数据的解释方式。
		 * @param[out] image 成功时指向已发布的贴图。
		 * @param[out] error 读取、解码或发布失败时的原因。
		 * @return 成功发布或复用同内容的贴图时为 true。
		 */
		bool Import(AssetManager& assets, const std::filesystem::path& source, ImageSemantic semantic,
		    AssetHandle<ImageAsset>& image, std::string& error);
		/**
		 * @brief 在文档旁保存其依赖的独立图片，相同内容复用原有文件。
		 * @param[in,out] assets 提供文档引用的贴图版本。
		 * @param[in] doc 待保存的场景或材质。
		 * @param[in] target 用户指定的文档保存路径。
		 * @param[out] error 贴图未能保存的原因。
		 * @return 所需的独立贴图均已保存时为 true。
		 */
		bool Save(AssetManager& assets, const Document& doc, const std::filesystem::path& target,
		    std::string& error) const;
		/**
		 * @brief 打开文档时，校验其相邻目录中所引用的贴图并发布给渲染器。
		 * @param[in,out] assets 注册恢复的独立贴图。
		 * @param[in] document 场景或材质文件的路径。
		 * @param[in] ids 文档依赖的资产标识，其中也可能包括项目资产。
		 * @param[out] error 独立贴图校验或加载失败的原因。
		 * @return 此文档的相邻贴图均已处理时为 true。
		 */
		bool Restore(AssetManager& assets, const std::filesystem::path& document, std::span<const AssetId> ids,
		    std::string& error);

	private:
		std::set<AssetId> m_Images;
	};
}
