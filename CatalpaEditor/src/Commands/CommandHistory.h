#pragma once
#include "Commands/EditorCommand.h"

namespace Catalpa::Editor {
	/* 文档私有的有界撤销栈。执行失败不移动游标，也不移除已有 redo 分支。 */
	class CommandHistory {
	public:
		/**
		 * @brief 成功应用编辑命令后更新历史记录与撤销位置。
		 *
		 * @param[in,out] doc 当前编辑的文档。
		 * @param[in] command 要执行并在成功后移入撤销记录的编辑命令。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Execute(Document&, std::unique_ptr<EditorCommand>, std::string& error);
		/**
		 * @brief 撤销当前位置之前的命令，成功后移动历史游标。
		 *
		 * @param[in,out] doc 当前编辑的文档。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Undo(Document&, std::string& error);
		/**
		 * @brief 重做当前位置的命令，成功后前移历史游标。
		 *
		 * @param[in,out] doc 当前编辑的文档。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Redo(Document&, std::string& error);

		size_t Size() const noexcept { return m_Entries.size(); }
		size_t Position() const noexcept { return m_Position; }
		const char* LabelKey(size_t index) const { return m_Entries.at(index)->LabelKey(); }

		bool CanUndo() const noexcept { return m_Position > 0; }

		bool CanRedo() const noexcept { return m_Position < m_Entries.size(); }

		bool Dirty() const noexcept { return !m_SavedPosition || m_Position != *m_SavedPosition; }
		void MarkSaved() noexcept { m_SavedPosition = m_Position; }
		void MarkUnsaved() noexcept { m_SavedPosition.reset(); }
		/**
		 * @brief 清空编辑历史并初始化已保存位置。
		 */
		void Reset() noexcept {
			m_Entries.clear();
			m_Position = 0;
			m_SavedPosition = 0;
		}

	private:
		static constexpr size_t Limit = 128;
		std::vector<std::unique_ptr<EditorCommand>> m_Entries;
		size_t m_Position = 0;
		std::optional<size_t> m_SavedPosition{0};
	};
}
