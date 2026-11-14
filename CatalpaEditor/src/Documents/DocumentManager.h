#pragma once
#include "Documents/AssetDocument.h"
#include "Documents/SceneDocument.h"
#include "Core/Id/StableIdGenerator.h"

namespace Catalpa::Editor {
	/* 同一场景或固定资产版本只打开一次；文档管理器不拥有 Scene/World。 */
	class DocumentManager {
	public:
		/**
		 * @brief 获取或创建绑定指定场景句柄的场景文档。
		 *
		 * @param[in] scenes 管理场景及其句柄的对象。
		 * @param[in] scene 要在文档管理器中查找或登记的场景句柄。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 与该场景句柄绑定的文档指针，按需创建；场景已卸载或文档无法创建时为 nullptr。
		 */
		SceneDocument* SceneFor(SceneManager&, SceneHandle, std::string& error);
		AssetDocument* Open(AssetVersionRef<MaterialInstance>, std::string& error);
		AssetDocument* Open(AssetVersionRef<ImageAsset>, std::string& error);
		/**
		 * @brief 打开源。
		 *
		 * @param[in] asset 本次操作使用的资产标识。
		 * @param[in] type 作者文档的资产类型键，用于选择编辑器。
		 * @param[in] source 打开文档时作为初始版本的完整作者文本。
		 * @param[in] path 文件系统中的目标路径。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 管理器持有的源文档指针，同一文件和类型已打开时复用并激活；类型冲突或打开失败时为 nullptr。
		 */
		AssetDocument* OpenSource(
		    AssetId, std::string type, std::string source, const std::filesystem::path& path, std::string& error);
		/**
		 * @brief 返回当前打开的文档集合。
		 *
		 * @return 当前所有打开文档的指针列表；列表独立，指针所指的文档仍由管理器持有。
		 */
		std::vector<Document*> All() const;
		Document* Active() const noexcept;
		Document* Find(StableId id) const noexcept;
		/**
		 * @brief 查找按路径。
		 *
		 * @param[in] path 文件系统中的目标路径。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 已打开的对应文档指针；无匹配或路径核验失败均返回 nullptr，后者另有错误说明。
		 */
		Document* FindByPath(const std::filesystem::path&, std::string& error) const;
		bool HasDirtyDocuments() const;
		/**
		 * @brief 检查能否保存目标。
		 *
		 * @param[in] document 请求写入目标路径的文档，用于排除其他已打开文档的路径冲突。
		 * @param[in] target 目标文件系统路径。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool CanSaveTo(const Document&, const std::filesystem::path& target, std::string& error) const;
		/**
		 * @brief 从所有已打开文档中筛选资源文档。
		 *
		 * @return 当前打开的资产文档指针列表；不包含场景文档，指针的生存期由管理器控制。
		 */
		std::vector<AssetDocument*> Assets() const;
		void Activate(StableId);
		/**
		 * @brief 关闭指定文档并更新活动文档状态。
		 *
		 * @param[in] id 用于定位目标的稳定标识。
		 */
		void Close(StableId);
		/**
		 * @brief 关闭已失效的场景。
		 */
		void CloseStaleScenes();
		void Clear();

	private:
		/**
		 * @brief 打开资产。
		 *
		 * @param[in] source 输入的 AssetVersionRef<T>。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 与资产标识及版本匹配的文档指针；无有效源版本或无法打开时为 nullptr。
		 */
		template <class T> AssetDocument* OpenAsset(AssetVersionRef<T>, std::string&);
		std::map<StableId, std::unique_ptr<Document>> m_Open;
		StableId m_Active;
	};
}
