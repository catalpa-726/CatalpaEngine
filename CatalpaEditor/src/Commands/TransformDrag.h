#pragma once
#include "Documents/SceneDocument.h"

namespace Catalpa::Editor {
    // 每帧将经过验证的局部值送入真实场景。结束时合并为一条命令，取消恢复起点。
    class TransformDrag {
      public:
        bool Begin(SceneDocument&, EntityReference, std::string&);
        bool Preview(const Math::TransformTRS&, std::string&);
        bool Finish(SceneDocument&, std::string&);
        bool Cancel(std::string&);

        bool Active() const noexcept {
            return m_Scenes != nullptr;
        }

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
