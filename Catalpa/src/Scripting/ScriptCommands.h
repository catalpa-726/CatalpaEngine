#pragma once
#include "Scene/EntityReference.h"
#include "Core/Math/TransformMath.h"

namespace Catalpa {
    class Scene;
    // 一个受保护脚本调用的有序写集。只在脚本成功返回后发布，实体代次再次核对。
    class CATALPA_API ScriptCommands {
      public:
        bool Translate(Scene&, EntityReference, Math::Vec3 delta, std::string& error);
        bool SetTranslation(Scene&, EntityReference, Math::Vec3 value, std::string& error);
        bool ReadTranslation(const Scene&, EntityReference, Math::Vec3&, std::string& error) const;
        bool Commit(Scene&, std::string& error);
        void Clear() noexcept { m_Writes.clear(); }
      private:
        struct Write { EntityReference target; Math::TransformTRS before, after; };
        std::vector<Write> m_Writes;
    };
}
