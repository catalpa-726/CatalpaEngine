#pragma once
#include "Commands/EditorCommand.h"

namespace Catalpa::Editor {
    // 文档私有的有界撤销栈。执行失败不移动游标，也不移除已有 redo 分支。
    class CommandHistory {
      public:
        bool Execute(Document&, std::unique_ptr<EditorCommand>, std::string& error);
        bool Undo(Document&, std::string& error);
        bool Redo(Document&, std::string& error);

        bool CanUndo() const noexcept {
            return m_Position > 0;
        }

        bool CanRedo() const noexcept {
            return m_Position < m_Entries.size();
        }

        bool Dirty() const noexcept {
            return m_Dropped || m_Position != 0;
        }

      private:
        static constexpr size_t Limit = 128;
        std::vector<std::unique_ptr<EditorCommand>> m_Entries;
        size_t m_Position = 0;
        bool m_Dropped = false;
    };
}
