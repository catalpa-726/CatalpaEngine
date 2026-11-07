#pragma once
#include "Scene/Scene.h"

namespace Catalpa {
    // 动态领域统一在 Scene 的结构安全点应用值命令。回调不能捕获 SDK/World 借用指针。
    CATALPA_API WorldStatus QueueDynamicComponent(Scene&, EntityId,
                                                  std::function<WorldStatus(World&)> operation);
} // namespace Catalpa
