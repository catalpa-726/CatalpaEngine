#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"

namespace Catalpa {

    // 动作 ID 是稳定内部标识；显示名与窗口/编辑器语言无关。
    enum class ActionContext : std::uint8_t { Gameplay, System };
    // Button 的 value 为0/1；Axis 是绑定的代数和，不自动归一化向量或变成速度。
    enum class ActionType : std::uint8_t { Button, Axis };
    enum class InputSource : std::uint8_t { Key, MouseButton, MouseDeltaX, MouseDeltaY, ScrollX, ScrollY };

    struct ActionBinding {
        InputSource source = InputSource::Key;
        int code = 0; // 只对键/按钮有效，使用引擎 Key/Mouse 值，不公开 GLFW 类型。
        double scale = 1.0;
    };

    struct ActionDefinition {
        std::string id;
        ActionContext context = ActionContext::Gameplay;
        ActionType type = ActionType::Button;
        double deadZone = 0.0;
        std::optional<MouseCode> gateMouseButton;
        std::vector<ActionBinding> bindings;
    };

    // 作者配置的受检值；重绑定通过 Configure 原子替换，不修改原始 JSON。
    struct ActionMap {
        std::string profile;
        std::vector<ActionDefinition> actions;
    };

    // CPU 契约和明确创建样例时使用；坏 JSON 不会自动回退到此映射。
    [[nodiscard]] CATALPA_API ActionMap MakeSandboxActionMap();
}
