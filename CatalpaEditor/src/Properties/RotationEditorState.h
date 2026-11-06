#pragma once

#include "catpch.h"
#include "Core/Math/MathTypes.h"
#include "Scene/EntityReference.h"

namespace Catalpa::Editor {
    // 仅供 Inspector 使用的局部旋转草稿；Scene/schema/磁盘仍保存四元数。
    // XYZ 使用度，构造约定与 GLM quat(vec3) 一致：R = Rz * Ry * Rx。
    class RotationEditorState {
      public:
        bool Synchronize(StableId document, EntityReference target, const Math::Quat& rotation,
                         std::string& error);
        bool ToQuaternion(Math::Quat& output, std::string& error) const;
        void Accept(const Math::Quat& rotation) noexcept { m_Observed = rotation; }

        std::array<double, 3>& Degrees() noexcept { return m_Degrees; }
        const std::array<double, 3>& Degrees() const noexcept { return m_Degrees; }
        uint64_t Revision() const noexcept { return m_Revision; }

      private:
        StableId m_Document;
        EntityReference m_Target;
        Math::Quat m_Observed{1, 0, 0, 0};
        std::array<double, 3> m_Degrees{};
        uint64_t m_Revision = 0;
        bool m_Initialized = false;
    };
}
