#pragma once
#include "Documents/SceneDocument.h"

namespace Catalpa::Editor {
	/* 选择绑定文档、World 和实体代次。撤销删除创建的新实体不会继承旧点击。 */
	class EditorSelection {
	public:
		void Clear() noexcept {
			m_Document = {};
			m_Entity = {};
		}

		/**
		 * @brief 把仍存活的场景实体登记为当前文档的选择。
		 *
		 * @param[in,out] doc 当前编辑的场景文档。
		 * @param[in] id 用于定位目标的实体标识。
		 *
		 * @return 完成上述操作时为 true，操作未成功时为 false。
		 */
		bool Select(SceneDocument&, EntityId);
		EntityId Resolve(const SceneDocument&) const;

	private:
		StableId m_Document;
		EntityReference m_Entity;
	};
}
