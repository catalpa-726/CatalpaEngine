#include "Commands/CommandHistory.h"
#include "Documents/Document.h"

namespace Catalpa::Editor {
    bool CommandHistory::Execute(Document& doc, std::unique_ptr<EditorCommand> command, std::string& error) {
        if (!command || command->Owner() != doc.Id()) {
            error = "Command belongs to another document";
            return false;
        }
        // 在变更前取得全部栈空间，避免成功修改后因分配异常失去撤销记录。
        try {
            m_Entries.reserve(Limit);
        } catch (const std::bad_alloc&) {
            error = "Cannot reserve undo history before applying this command";
            return false;
        }
        if (!command->Apply(doc, error))
            return false;
        m_Entries.resize(m_Position);
        if (m_Entries.size() == Limit) {
            m_Entries.erase(m_Entries.begin());
            m_Dropped = true;
        }
        m_Entries.push_back(std::move(command));
        m_Position = m_Entries.size();
        return true;
    }

    bool CommandHistory::Undo(Document& doc, std::string& error) {
        if (!CanUndo()) {
            error = "Nothing to undo";
            return false;
        }
        if (m_Entries[m_Position - 1]->Owner() != doc.Id()) {
            error = "Wrong undo document";
            return false;
        }
        if (!m_Entries[m_Position - 1]->Revert(doc, error))
            return false;
        --m_Position;
        return true;
    }

    bool CommandHistory::Redo(Document& doc, std::string& error) {
        if (!CanRedo()) {
            error = "Nothing to redo";
            return false;
        }
        if (m_Entries[m_Position]->Owner() != doc.Id()) {
            error = "Wrong redo document";
            return false;
        }
        if (!m_Entries[m_Position]->Apply(doc, error))
            return false;
        ++m_Position;
        return true;
    }
}
