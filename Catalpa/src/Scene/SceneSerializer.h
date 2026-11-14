#pragma once
#include "Scene/SceneAsset.h"
#include "Scene/SceneManager.h"

namespace Catalpa {
	class AssetManager;
	class Scene;
	struct AssetDecodeContext;
	class SchemaMigrationRegistry;
	class CATALPA_API SceneSerializer {
	public:
		/**
		 * @brief 从可编辑的运行时场景提取作者场景快照。
		 *
		 * @param[in] scene 待捕获的运行时场景。
		 * @param[out] output 成功时写入作者资产。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Capture(const Scene&, SceneAsset&, std::string& error);
		/**
		 * @brief 检查实体顺序并将作者场景序列化为 JSON。
		 *
		 * @param[in] asset 待编码的场景资产。
		 * @param[out] output 接收生成的 JSON 场景文本。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Write(const SceneAsset&, std::string&, std::string& error);
		/**
		 * @brief 校验作者场景文档的版本、实体及组件，并重建场景资产。
		 *
		 * @param[in] text 包含场景实体的 JSON 文本。
		 * @param[out] output 成功时写入场景资产。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Read(std::string_view, SceneAsset&, std::string& error);
		/**
		 * @brief 读取已迁移的。
		 *
		 * @param[in] text 待迁移到当前场景格式的序列化文本。
		 * @param[in] registry 用于将旧版作者数据升级至当前格式的迁移规则。
		 * @param[out] output 接收 SceneAsset 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool ReadMigrated(std::string_view, const SchemaMigrationRegistry&, SceneAsset&, std::string& error);
		/**
		 * @brief 收集场景组件和实体所引用的外部资产并去重。
		 *
		 * @param[in] asset 用于分析引用关系的作者场景。
		 *
		 * @return 上述遍历或筛选得到的资产标识列表。
		 */
		static std::vector<AssetId> Dependencies(const SceneAsset&);
		/**
		 * @brief 解析网格。
		 *
		 * @param[in] source 输入的 SceneMeshAsset。
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[out] output 接收 MeshRendererComponent 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool ResolveMesh(const SceneMeshAsset&, AssetManager*, MeshRendererComponent&, std::string& error);
		/**
		 * @brief 绑定依赖项。
		 *
		 * @param[in] asset 需要绑定所引用资产版本的场景数据。
		 * @param[in] context 提供取消状态、驻留字节上限和依赖资产的解码上下文。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool BindDependencies(SceneAsset&, const AssetDecodeContext&, std::string& error);
		/**
		 * @brief 在候选场景中实例化作者实体和组件，完成派生状态后发布句柄。
		 * 候选场景全部创建及派生成功后才发布 handle，不改变 active 场景。
		 *
		 * @param[in] asset 待实例化的作者场景资产。
		 * @param[in,out] scenes 用于创建与管理新场景。
		 * @param[in] assets 解析场景资源及版本的资产管理器。
		 * @param[out] output 成功时接收新场景的句柄。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 候选场景构建成功时为 true，否则为 false；成功也不自动切换活动场景。
		 */
		static bool Instantiate(const SceneAsset&, SceneManager&, AssetManager*, SceneHandle&, std::string& error);
	};
}
