#pragma once
#include "Core/Id/StableId.h"

namespace Catalpa::Editor {
    class Document;

    // 每个命令属于一个明确的文档。成功返回时变更已实际提交。
    class EditorCommand {
      public:
        explicit EditorCommand(StableId document, const char* labelKey = "history.component")
            : m_Document(document), m_LabelKey(labelKey) {}
        const char* LabelKey() const noexcept { return m_LabelKey; }

        virtual ~EditorCommand() = default;

        StableId Owner() const noexcept {
            return m_Document;
        }

        virtual bool Apply(Document&, std::string& error) = 0;
        virtual bool Revert(Document&, std::string& error) = 0;
        virtual bool TryMerge(const EditorCommand&) noexcept { return false; }

      private:
        StableId m_Document;
        const char* m_LabelKey; // 静态语言键，切换语言后历史名称同步刷新。
    };
}
