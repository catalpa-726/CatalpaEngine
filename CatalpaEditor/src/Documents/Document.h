#pragma once
#include "Commands/CommandHistory.h"

namespace Catalpa::Editor {
    // 编辑会话身份不写入作者文件；关闭会话一并撤销异步请求对它的访问资格。
    class Document {
      public:
        explicit Document(StableId id) : m_Id(id) {}

        virtual ~Document() = default;
        Document(const Document&) = delete;
        Document& operator=(const Document&) = delete;

        StableId Id() const noexcept {
            return m_Id;
        }

        CommandHistory& History() noexcept {
            return m_History;
        }

      private:
        StableId m_Id;
        CommandHistory m_History;
    };
}
