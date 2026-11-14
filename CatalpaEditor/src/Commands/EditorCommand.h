#pragma once
#include "Core/Id/StableId.h"

namespace Catalpa::Editor {
	class Document;

	/* 每个命令属于一个明确的文档。成功返回时变更已实际提交。 */
	class EditorCommand {
	public:
		explicit EditorCommand(StableId document, const char* labelKey = "history.component")
		    : m_Document(document), m_LabelKey(labelKey) {}
		const char* LabelKey() const noexcept { return m_LabelKey; }

		virtual ~EditorCommand() = default;

		StableId Owner() const noexcept { return m_Document; }

		/**
		 * @brief 对目标文档执行命令定义的修改。
		 * @details 第 1 个参数（Document&）为输入输出。接受此编辑命令的目标文档。
		 *
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 操作成功时为 true，失败时为 false。
		 */
		virtual bool Apply(Document&, std::string& error) = 0;
		/**
		 * @brief 将目标文档中的修改恢复到此命令执行前的状态。
		 * @details 第 1 个参数（Document&）为输入输出。接受此编辑命令的目标文档。
		 *
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 操作成功时为 true，失败时为 false。
		 */
		virtual bool Revert(Document&, std::string& error) = 0;
		virtual bool TryMerge(const EditorCommand&) noexcept { return false; }

	private:
		StableId m_Document;
		const char* m_LabelKey; /* 静态语言键，切换语言后历史名称同步刷新。 */
	};
}
