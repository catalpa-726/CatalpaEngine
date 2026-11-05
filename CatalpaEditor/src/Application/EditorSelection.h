#pragma once
#include "Documents/SceneDocument.h"

namespace Catalpa::Editor {
    // 选择绑定文档、World 和实体代次。撤销删除创建的新实体不会继承旧点击。
    class EditorSelection {
      public:
        void Clear() noexcept {
            m_Document = {};
            m_Entity = {};
        }

        bool Select(SceneDocument&, EntityId);
        EntityId Resolve(const SceneDocument&) const;

      private:
        StableId m_Document;
        EntityReference m_Entity;
    };
}
