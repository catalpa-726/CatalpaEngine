#pragma once
#include "Core/Id/StableId.h"

namespace Catalpa::Editor {
    class Document;

    // 每个命令属于一个明确的文档。成功返回时变更已实际提交。
    class EditorCommand {
      public:
        explicit EditorCommand(StableId document) : m_Document(document) {}

        virtual ~EditorCommand() = default;

        StableId Owner() const noexcept {
            return m_Document;
        }

        virtual bool Apply(Document&, std::string& error) = 0;
        virtual bool Revert(Document&, std::string& error) = 0;

      private:
        StableId m_Document;
    };
}
