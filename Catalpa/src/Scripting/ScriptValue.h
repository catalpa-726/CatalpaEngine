#pragma once
#include "Core/Base.h"
#include "Core/Memory/ValueMap.h"
#include "Scene/PersistentEntityId.h"
#include "Core/Math/TransformMath.h"

namespace Catalpa {
    // 数值/字符串界限与作者归档一致，失败不转换或截断原值。
    // 所有跨脚本调用的值均独占存储；实体引用只保存稳定身份。
    using ScriptValue = std::variant<bool, int64_t, double, std::string, Math::Vec3, PersistentEntityId>;
    using ScriptValues = ValueMap<std::string, ScriptValue, std::less<>>;
    CATALPA_API bool ValidateScriptValues(const ScriptValues&,std::string& error);
}
