#pragma once
#include "Documents/Document.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"

namespace Catalpa {
	class AssetManager;
}

namespace Catalpa::Editor {
	/* 只持有 SceneHandle，解析时由同一 SceneManager 检查所属实例与代次。 */
	class SceneDocument final : public Document {
	public:
		SceneDocument(StableId id, SceneManager& scenes, SceneHandle handle)
		    : Document(id), m_Scenes(scenes), m_Handle(handle) {}

		Scene* Resolve() const noexcept;
		/**
		 * @brief 将当前作者场景序列化为可保存的场景文档。
		 *
		 * @param[out] text 接收生成的文本。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Serialize(std::string&, std::string& error) const override;
		/**
		 * @brief 从作者场景创建独立的运行副本，复制游戏系统后将副本设为活动场景。
		 *
		 * @param[in,out] assets 用于解析场景依赖的资产管理器，必要时会发起资源加载请求。
		 * @param[out] error 失败时写入原因，成功时清空。
		 *
		 * @return 运行副本已激活时为 true；文档已在运行或创建失败时为 false，未提交的候选副本会卸载。
		 */
		bool StartPlay(AssetManager&, std::string& error);
		/**
		 * @brief 停止运行模式。
		 *
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool StopPlay(std::string& error);
		/**
		 * @brief 把候选运行场景接入当前 Play 会话。
		 *
		 * @param[in] candidate 待检查的场景句柄。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool AdoptPlayScene(SceneHandle candidate, std::string& error);
		bool Playing() const noexcept { return m_Play.HasValue(); }
		SceneHandle PlayHandle() const noexcept { return m_Play; }
		/**
		 * @brief 核对持久标识仍对应文档首次记录的实体代次。
		 * 首次使用时记录代次，只有本文件的受控删除/恢复能重新绑定持久身份。
		 *
		 * @param[in] id 命令目标的持久实体标识。空标识可跳过检查；首次出现时登记对应的运行时实体，后续检查要求同一实体仍存在。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 空目标或仍指向同一代实体时为 true，实体已删除或被替换时为 false。
		 */
		bool CheckTarget(PersistentEntityId, std::string& error);
		/**
		 * @brief 刷新已删除的目标。
		 */
		void RefreshDeletedTargets() noexcept;
		void RefreshRestoredTargets(const SceneSubtreeSnapshot&) noexcept;
		/**
		 * @brief 跟踪已创建的。
		 *
		 * @param[in] id 用于定位目标的持久实体标识。
		 */
		void TrackCreated(PersistentEntityId id) noexcept;
		void ReserveIdentity(PersistentEntityId id);

		SceneHandle Handle() const noexcept { return m_Handle; }

		SceneManager& Scenes() const noexcept { return m_Scenes; }

	private:
		SceneManager& m_Scenes;
		SceneHandle m_Handle;
		SceneHandle m_Play;
		std::map<PersistentEntityId, EntityId> m_Targets;
	};
}
