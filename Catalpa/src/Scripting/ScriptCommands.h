#pragma once
#include "Scene/EntityReference.h"
#include "Core/Math/TransformMath.h"

namespace Catalpa {
	class Scene;
	/* 一个受保护脚本调用的有序写集。只在脚本成功返回后发布，实体代次再次核对。 */
	class CATALPA_API ScriptCommands {
	public:
		/**
		 * @brief 将位移增量合并到脚本事务中的局部变换。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] ref 指向要读取或修改局部位置的场景实体。
		 * @param[in] delta 需要施加的三维平移增量。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Translate(Scene&, EntityReference, Math::Vec3 delta, std::string& error);
		/**
		 * @brief 设置平移。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] ref 指向要读取或修改局部位置的场景实体。
		 * @param[in] value 为目标实体指定的新局部平移。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool SetTranslation(Scene&, EntityReference, Math::Vec3 value, std::string& error);
		/**
		 * @brief 读取平移。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] ref 指向要读取或修改局部位置的场景实体。
		 * @param[out] output 接收三维向量结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool ReadTranslation(const Scene&, EntityReference, Math::Vec3&, std::string& error) const;
		/**
		 * @brief 暂存新实体的名称和局部变换，等待场景提交。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] name 要赋予新实体的名称。
		 * @param[in] local 新实体初始的父相对平移、旋转与缩放。
		 * @param[out] output 接收持久实体标识结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Create(Scene&, std::string_view name, const Math::TransformTRS&, PersistentEntityId&, std::string& error);
		/**
		 * @brief 登记实体删除，或撤销同批尚未发布的创建。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] id 要查找或操作的对象标识。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Destroy(Scene&, PersistentEntityId, std::string& error);
		/**
		 * @brief 在结构修改边界校验并提交脚本暂存的变换、创建与删除命令。
		 * 在场景的结构修改边界提交脚本暂存的实体操作。
		 *
		 * @param[in,out] scene 此次操作所针对的场景。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 所有命令成功提交时为 true，否则为 false 并报告失败或回滚原因。
		 */
		bool Commit(Scene&, std::string& error);
		/**
		 * @brief 丢弃暂存的创建、删除与变换写入。
		 */
		void Clear() noexcept {
			m_Writes.clear();
			m_Creates.clear();
			m_Destroys.clear();
		}

	private:
		struct Write {
			EntityReference target;
			Math::TransformTRS before, after;
		};
		std::vector<Write> m_Writes;
		struct CreateValue {
			PersistentEntityId id;
			std::string name;
			Math::TransformTRS local;
		};
		std::vector<CreateValue> m_Creates;
		std::vector<EntityReference> m_Destroys;
	};
}
