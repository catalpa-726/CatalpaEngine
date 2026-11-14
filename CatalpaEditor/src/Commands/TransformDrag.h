#pragma once
#include "Documents/SceneDocument.h"

namespace Catalpa::Editor {
	/* 每帧将经过验证的局部值送入真实场景。结束时合并为一条命令，取消恢复起点。 */
	class TransformDrag {
	public:
		/**
		 * @brief 捕获所选实体的起始变换并开启拖动预览。
		 *
		 * @param[in] doc 绑定被拖动实体的场景文档。
		 * @param[in] entity 本次拖动的场景实体引用。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Begin(SceneDocument&, EntityReference, std::string&);
		bool Preview(const Math::TransformTRS&, std::string&);
		/**
		 * @brief 结束拖动，将已确认的变换提交为可撤销编辑。
		 *
		 * @param[in] doc 绑定被拖动实体的场景文档。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Finish(SceneDocument&, std::string&);
		/**
		 * @brief 结束拖动并恢复起始变换。
		 *
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Cancel(std::string&);

		bool Active() const noexcept { return m_Scenes != nullptr; }

		bool Matches(const SceneDocument& doc, EntityReference entity) const noexcept {
			return Active() && doc.Id() == m_Document && entity == m_Entity && doc.Handle() == m_Handle;
		}

	private:
		bool Set(const Math::TransformTRS&, std::string&);
		SceneManager* m_Scenes = nullptr;
		SceneHandle m_Handle;
		StableId m_Document;
		EntityReference m_Entity;
		Math::TransformTRS m_Before, m_Current;
	};
}
