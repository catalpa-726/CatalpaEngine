#include "catpch.h"
#include "Runtime/Input/InputActions.h"
#include "Events/ApplicationEvent.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"
#include <nlohmann/json.hpp>

namespace Catalpa {
    namespace {
        bool IsImpulse(InputSource source) {
            return source == InputSource::MouseDeltaX || source == InputSource::MouseDeltaY
                || source == InputSource::ScrollX || source == InputSource::ScrollY;
        }

        bool ValidId(std::string_view value) {
            if (value.empty() || value.size() > 64 || value.front() < 'a' || value.front() > 'z') return false;
            return std::all_of(value.begin(), value.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
            });
        }

        bool OnlyFields(const nlohmann::json& object, std::initializer_list<std::string_view> fields) {
            if (!object.is_object()) return false;
            for (const auto& [key, value] : object.items()) {
                (void)value;
                if (std::find(fields.begin(), fields.end(), key) == fields.end()) return false;
            }
            return true;
        }

        bool ReadCode(const nlohmann::json& value, InputSource source, int& result) {
            if (value.is_number_integer()) {
                const auto code = value.get<std::int64_t>();
                if (code < 0 || code > 511) return false;
                result = static_cast<int>(code);
                return true;
            }
            if (!value.is_string()) return false;
            const auto& name = value.get_ref<const std::string&>();
            if (source == InputSource::MouseButton) {
                if (name == "left") result = Mouse::ButtonLeft;
                else if (name == "right") result = Mouse::ButtonRight;
                else if (name == "middle") result = Mouse::ButtonMiddle;
                else return false;
                return true;
            }
            if (name.size() == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9'))) {
                result = static_cast<unsigned char>(name[0]);
                return true;
            }
            if (name == "F6") result = Key::F6;
            else if (name == "F7") result = Key::F7;
            else if (name == "Space") result = Key::Space;
            else if (name == "Escape") result = Key::Escape;
            else if (name == "Enter") result = Key::Enter;
            else if (name == "Tab") result = Key::Tab;
            else if (name == "LeftShift") result = Key::LeftShift;
            else if (name == "RightShift") result = Key::RightShift;
            else if (name == "LeftControl") result = Key::LeftControl;
            else if (name == "RightControl") result = Key::RightControl;
            else return false;
            return true;
        }
    }

    ActionMap MakeSandboxActionMap()
    {
        ActionMap map{ .profile = "sandbox" };
        const auto axis = [&](std::string id, int negative, int positive) {
            map.actions.push_back({ .id = std::move(id), .type = ActionType::Axis,
                .bindings = { { InputSource::Key, negative, -1.0 }, { InputSource::Key, positive, 1.0 } } });
        };
        axis("move.right", Key::A, Key::D);
        axis("move.up", Key::Q, Key::E);
        axis("move.forward", Key::S, Key::W);
        map.actions.push_back({ .id = "look.yaw", .type = ActionType::Axis, .gateMouseButton = Mouse::ButtonRight,
            .bindings = { { InputSource::MouseDeltaX, 0, 1.0 } } });
        map.actions.push_back({ .id = "look.pitch", .type = ActionType::Axis, .gateMouseButton = Mouse::ButtonRight,
            .bindings = { { InputSource::MouseDeltaY, 0, 1.0 } } });
        map.actions.push_back({ .id = "simulation.pause", .context = ActionContext::System,
            .bindings = { { InputSource::Key, Key::F6, 1.0 } } });
        map.actions.push_back({ .id = "simulation.step", .context = ActionContext::System,
            .bindings = { { InputSource::Key, Key::F7, 1.0 } } });
        return map;
    }

    InputStatus InputActions::Configure(ActionMap map)
    {
        if (!ValidId(map.profile) || map.actions.empty() || map.actions.size() > 64)
            return { InputError::InvalidProfile, "Input profile requires a valid ID and 1..64 actions." };
        std::unordered_set<std::string> ids;
        for (const auto& action : map.actions) {
            if (!ValidId(action.id) || !ids.insert(action.id).second || action.bindings.empty() || action.bindings.size() > 16
                || !std::isfinite(action.deadZone) || action.deadZone < 0.0 || action.deadZone >= 1.0
                || (action.context != ActionContext::Gameplay && action.context != ActionContext::System)
                || (action.type != ActionType::Button && action.type != ActionType::Axis)
                || (action.gateMouseButton && *action.gateMouseButton >= PhysicalInputSnapshot::MouseButtonCount))
                return { InputError::InvalidProfile, "Invalid/duplicate action, context, gate or dead zone: " + action.id };
            bool hasImpulse = false, hasLevel = false;
            for (const auto& binding : action.bindings) {
                if (!std::isfinite(binding.scale) || std::abs(binding.scale) > 1000.0 || binding.scale == 0.0)
                    return { InputError::InvalidBinding, "Binding scale must be finite, nonzero and within +/-1000: " + action.id };
                if (action.type == ActionType::Button && binding.scale != 1.0)
                    return { InputError::InvalidBinding, "Button bindings use binary values and require scale 1: " + action.id };
                switch (binding.source) {
                case InputSource::Key:
                    if (binding.code < Key::Space || binding.code > Key::Menu)
                        return { InputError::InvalidBinding, "Key code outside the engine key range: " + action.id };
                    hasLevel = true;
                    break;
                case InputSource::MouseButton:
                    if (binding.code < 0 || binding.code >= static_cast<int>(PhysicalInputSnapshot::MouseButtonCount))
                        return { InputError::InvalidBinding, "Mouse button outside the engine button range: " + action.id };
                    hasLevel = true;
                    break;
                case InputSource::MouseDeltaX: case InputSource::MouseDeltaY: case InputSource::ScrollX: case InputSource::ScrollY:
                    if (binding.code != 0) return { InputError::InvalidBinding, "Pointer axes do not have key/button codes: " + action.id };
                    hasImpulse = true;
                    break;
                default: return { InputError::InvalidBinding, "Unknown input source: " + action.id };
                }
            }
            if ((hasImpulse && hasLevel) || (hasImpulse && action.type != ActionType::Axis))
                return { InputError::InvalidBinding, "An action cannot mix continuous keys with one-shot pointer axes: " + action.id };
        }
        // 所有可能分配的替换状态先构造，避免分配失败后动作表/快照长度不一致。
        ActionSnapshot replacement;
        replacement.actions.reserve(map.actions.size());
        std::vector<PendingAction> pending(map.actions.size());
        std::vector<bool> previous(map.actions.size(), false), impulses(map.actions.size(), false);
        std::vector<bool> buttonHeld(map.actions.size(), false), buttonPressed(map.actions.size(), false), buttonReleased(map.actions.size(), false);
        for (std::size_t i = 0; i < map.actions.size(); ++i) {
            replacement.actions.push_back({ .id = map.actions[i].id });
            impulses[i] = IsImpulse(map.actions[i].bindings.front().source);
        }
        m_Map = std::move(map);
        m_Frame = std::move(replacement);
        m_Pending = std::move(pending);
        m_PreviousHeld = std::move(previous);
        m_ImpulseActions = std::move(impulses);
        m_DigitalPhysicalHeld = std::move(buttonHeld);
        m_DigitalPressed = std::move(buttonPressed);
        m_DigitalReleased = std::move(buttonReleased);
        Reset();
        return {};
    }

    InputStatus InputActions::ParseProfile(std::string_view source)
    {
        if (source.empty() || source.size() > 64 * 1024)
            return { InputError::InvalidJson, "Input profile must be 1..65536 bytes." };
        try {
            using Json = nlohmann::json;
            bool excessiveDepth = false;
            auto document = Json::parse(source.begin(), source.end(), [&](int depth, Json::parse_event_t, Json&) {
                if (depth > 8) { excessiveDepth = true; return false; }
                return true;
            }, false);
            if (document.is_discarded() || excessiveDepth || !OnlyFields(document, { "schemaVersion", "profile", "actions" })
                || !document.contains("schemaVersion") || !document["schemaVersion"].is_number_integer() || document["schemaVersion"] != 1
                || !document.contains("profile") || !document["profile"].is_string()
                || !document.contains("actions") || !document["actions"].is_array()
                || document["actions"].empty() || document["actions"].size() > 64)
                return { InputError::InvalidJson, "Invalid input profile root/version/size." };
            ActionMap map{ .profile = document["profile"].get<std::string>() };
            for (const auto& value : document["actions"]) {
                if (!OnlyFields(value, { "id", "context", "type", "deadZone", "gateMouseButton", "bindings" })
                    || !value.contains("id") || !value["id"].is_string() || !value.contains("context") || !value["context"].is_string()
                    || !value.contains("type") || !value["type"].is_string() || !value.contains("bindings") || !value["bindings"].is_array()
                    || value["bindings"].empty() || value["bindings"].size() > 16)
                    return { InputError::InvalidJson, "Invalid action object or binding count." };
                ActionDefinition action{ .id = value["id"].get<std::string>() };
                if (value["context"] == "gameplay") action.context = ActionContext::Gameplay;
                else if (value["context"] == "system") action.context = ActionContext::System;
                else return { InputError::InvalidJson, "Unknown action context." };
                if (value["type"] == "button") action.type = ActionType::Button;
                else if (value["type"] == "axis") action.type = ActionType::Axis;
                else return { InputError::InvalidJson, "Unknown action type." };
                if (value.contains("deadZone")) {
                    if (!value["deadZone"].is_number()) return { InputError::InvalidJson, "deadZone must be a number." };
                    action.deadZone = value["deadZone"].get<double>();
                }
                if (value.contains("gateMouseButton")) {
                    int gate = 0;
                    if (!ReadCode(value["gateMouseButton"], InputSource::MouseButton, gate)
                        || gate >= static_cast<int>(PhysicalInputSnapshot::MouseButtonCount))
                        return { InputError::InvalidJson, "Invalid gateMouseButton." };
                    action.gateMouseButton = static_cast<MouseCode>(gate);
                }
                for (const auto& bindingValue : value["bindings"]) {
                    if (!OnlyFields(bindingValue, { "source", "code", "scale" }) || !bindingValue.contains("source")
                        || !bindingValue["source"].is_string()) return { InputError::InvalidJson, "Invalid binding object." };
                    ActionBinding binding;
                    const auto& name = bindingValue["source"].get_ref<const std::string&>();
                    if (name == "key") binding.source = InputSource::Key;
                    else if (name == "mouseButton") binding.source = InputSource::MouseButton;
                    else if (name == "mouseDeltaX") binding.source = InputSource::MouseDeltaX;
                    else if (name == "mouseDeltaY") binding.source = InputSource::MouseDeltaY;
                    else if (name == "scrollX") binding.source = InputSource::ScrollX;
                    else if (name == "scrollY") binding.source = InputSource::ScrollY;
                    else return { InputError::InvalidJson, "Unknown binding source." };
                    if (IsImpulse(binding.source)) {
                        if (bindingValue.contains("code")) return { InputError::InvalidJson, "Pointer source cannot contain code." };
                    }
                    else if (!bindingValue.contains("code") || !ReadCode(bindingValue["code"], binding.source, binding.code))
                        return { InputError::InvalidJson, "A key/button binding requires an engine code." };
                    if (bindingValue.contains("scale")) {
                        if (!bindingValue["scale"].is_number()) return { InputError::InvalidJson, "scale must be a number." };
                        binding.scale = bindingValue["scale"].get<double>();
                    }
                    action.bindings.push_back(binding);
                }
                map.actions.push_back(std::move(action));
            }
            return Configure(std::move(map)); // 失败不会覆盖当前有效配置或输入状态。
        }
        catch (const nlohmann::json::exception& error) {
            return { InputError::InvalidJson, std::string("Cannot parse input profile: ") + error.what() };
        }
    }

    void InputActions::Reset() noexcept
    {
        m_Physical = {};
        m_FramePhysical = {};
        m_Frame.frameIndex = 0;
        m_Frame.focused = m_Frame.gameplayActive = true;
        for (auto& action : m_Frame.actions) { action.value = 0.0; action.held = action.pressed = action.released = false; }
        m_Capture = {};
        m_BlockedKeys.fill(false); m_BlockedGameplayKeys.fill(false);
        m_BlockedMouse.fill(false); m_BlockedGameplayMouse.fill(false);
        m_HasCursor = false;
        m_DiscardGameplayFrameEdges = false;
        std::fill(m_Pending.begin(), m_Pending.end(), PendingAction{});
        std::fill(m_PreviousHeld.begin(), m_PreviousHeld.end(), false);
        std::fill(m_DigitalPhysicalHeld.begin(), m_DigitalPhysicalHeld.end(), false);
        std::fill(m_DigitalPressed.begin(), m_DigitalPressed.end(), false);
        std::fill(m_DigitalReleased.begin(), m_DigitalReleased.end(), false);
    }

    void InputActions::ResetGameplayContext() noexcept
    {
        for (std::size_t i = 0; i < m_Physical.keys.size(); ++i)
            m_BlockedGameplayKeys[i] |= m_Physical.keys[i];
        for (std::size_t i = 0; i < m_Physical.mouseButtons.size(); ++i)
            m_BlockedGameplayMouse[i] |= m_Physical.mouseButtons[i];
        for (std::size_t i = 0; i < m_Map.actions.size(); ++i) {
            if (m_Map.actions[i].context != ActionContext::Gameplay) continue;
            auto& value = m_Frame.actions[i];
            value.value = 0.0;
            value.held = value.pressed = value.released = false;
            m_Pending[i] = {};
            m_PreviousHeld[i] = false;
            m_DigitalPressed[i] = m_DigitalReleased[i] = false;
        }
        // 事件泵先于世界切换检测；下一次发布不把旧事件泵的快速点击/位移分给新世界。
        m_DiscardGameplayFrameEdges = true;
    }

    void InputActions::SetFocused(bool focused)
    {
        if (m_Physical.focused == focused) return;
        m_Physical.focused = focused;
        m_HasCursor = false;
        m_Physical.deltaX = m_Physical.deltaY = m_Physical.scrollX = m_Physical.scrollY = 0.0;
        m_Physical.keyPressed.fill(false); m_Physical.mousePressed.fill(false);
        if (!focused) {
            for (std::size_t i = 0; i < m_Physical.keys.size(); ++i) {
                m_Physical.keyReleased[i] |= m_Physical.keys[i];
                m_Physical.keys[i] = false;
            }
            for (std::size_t i = 0; i < m_Physical.mouseButtons.size(); ++i) {
                m_Physical.mouseReleased[i] |= m_Physical.mouseButtons[i];
                m_Physical.mouseButtons[i] = false;
            }
            m_BlockedKeys.fill(false); m_BlockedGameplayKeys.fill(false);
            m_BlockedMouse.fill(false); m_BlockedGameplayMouse.fill(false);
            for (auto& pending : m_Pending) { pending.pressed = false; pending.impulse = 0.0; }
        }
    }

    void InputActions::OnEvent(const Event& event)
    {
        switch (event.GetEventType()) {
        case EventType::WindowFocus: SetFocused(true); break;
        case EventType::WindowLostFocus: case EventType::WindowClose: SetFocused(false); break;
        case EventType::WindowMinimize:
            if (static_cast<const WindowMinimizeEvent&>(event).IsMinimized()) SetFocused(false);
            break;
        case EventType::KeyPressed: {
            const auto& key = static_cast<const KeyPressedEvent&>(event);
            const std::size_t index = key.GetKeyCode();
            if (!m_Physical.focused || index >= m_Physical.keys.size()) break;
            // 重复事件不能把失焦时已清空的键重新激活；物理按下只由 PRESS 建立。
            if (key.IsRepeat()) break;
            if (!m_Physical.keys[index]) m_Physical.keyPressed[index] = true;
            m_Physical.keys[index] = true;
            break;
        }
        case EventType::KeyReleased: {
            const std::size_t index = static_cast<const KeyReleasedEvent&>(event).GetKeyCode();
            if (index >= m_Physical.keys.size()) break;
            m_Physical.keyReleased[index] |= m_Physical.keys[index];
            m_Physical.keys[index] = false;
            m_BlockedKeys[index] = m_BlockedGameplayKeys[index] = false;
            break;
        }
        case EventType::MouseButtonPressed: {
            const std::size_t index = static_cast<const MouseButtonPressedEvent&>(event).GetMouseButton();
            if (!m_Physical.focused || index >= m_Physical.mouseButtons.size()) break;
            if (!m_Physical.mouseButtons[index]) m_Physical.mousePressed[index] = true;
            m_Physical.mouseButtons[index] = true;
            // 开始按钮拖动时丢弃按钮按下之前的移动，不产生相机跳变。
            m_Physical.deltaX = m_Physical.deltaY = 0.0;
            m_HasCursor = false;
            break;
        }
        case EventType::MouseButtonReleased: {
            const std::size_t index = static_cast<const MouseButtonReleasedEvent&>(event).GetMouseButton();
            if (index >= m_Physical.mouseButtons.size()) break;
            m_Physical.mouseReleased[index] |= m_Physical.mouseButtons[index];
            m_Physical.mouseButtons[index] = false;
            m_BlockedMouse[index] = m_BlockedGameplayMouse[index] = false;
            break;
        }
        case EventType::MouseMoved: {
            const auto& mouse = static_cast<const MouseMovedEvent&>(event);
            if (!m_Physical.focused || !std::isfinite(mouse.GetX()) || !std::isfinite(mouse.GetY())) break;
            if (m_HasCursor) { m_Physical.deltaX += mouse.GetX() - m_Physical.cursorX; m_Physical.deltaY += mouse.GetY() - m_Physical.cursorY; }
            m_Physical.cursorX = mouse.GetX(); m_Physical.cursorY = mouse.GetY();
            m_HasCursor = true;
            break;
        }
        case EventType::MouseScrolled: {
            const auto& mouse = static_cast<const MouseScrolledEvent&>(event);
            if (!m_Physical.focused || !std::isfinite(mouse.GetXOffset()) || !std::isfinite(mouse.GetYOffset())) break;
            m_Physical.scrollX += mouse.GetXOffset(); m_Physical.scrollY += mouse.GetYOffset();
            break;
        }
        default: break;
        }
        ObserveDigitalActions();
    }

    void InputActions::ObserveDigitalActions()
    {
        // 在原始事件序列上观察数字按钮/键轴，可保留同一事件泵内松开再按下；
        // 多个绑定之一保持时，另一绑定不会重复触发。UI 归属在 BeginFrame 再过滤。
        for (std::size_t i = 0; i < m_Map.actions.size(); ++i) {
            const auto& action = m_Map.actions[i];
            if (m_ImpulseActions[i]) continue;
            const bool gate = !action.gateMouseButton || m_Physical.mouseButtons[*action.gateMouseButton];
            double sum = 0.0;
            for (const auto& binding : action.bindings) {
                const bool down = binding.source == InputSource::Key ? m_Physical.keys[binding.code] : m_Physical.mouseButtons[binding.code];
                if (down) sum += binding.scale;
            }
            const bool held = m_Physical.focused && gate && std::abs(sum) > action.deadZone;
            if (held && !m_DigitalPhysicalHeld[i]) m_DigitalPressed[i] = true;
            if (!held && m_DigitalPhysicalHeld[i]) m_DigitalReleased[i] = true;
            m_DigitalPhysicalHeld[i] = held;
        }
    }

    bool InputActions::IsKeyboardAllowed(ActionContext context) const {
        return m_Physical.focused && !m_Capture.keyboardCaptured && (context == ActionContext::System || m_Capture.gameplayActive);
    }

    bool InputActions::IsMouseAllowed(ActionContext context) const {
        return m_Physical.focused && !m_Capture.mouseCaptured && (context == ActionContext::System || m_Capture.gameplayActive);
    }

    bool InputActions::IsKeyAvailable(int code, ActionContext context) const {
        return IsKeyboardAllowed(context) && !m_BlockedKeys[code]
            && (context == ActionContext::System || !m_BlockedGameplayKeys[code]);
    }

    bool InputActions::IsMouseAvailable(int code, ActionContext context) const {
        return IsMouseAllowed(context) && !m_BlockedMouse[code]
            && (context == ActionContext::System || !m_BlockedGameplayMouse[code]);
    }

    const ActionSnapshot& InputActions::BeginFrame(InputCapture capture)
    {
        m_Capture = capture;
        ++m_Frame.frameIndex;
        m_Frame.focused = m_Physical.focused;
        m_Frame.gameplayActive = capture.gameplayActive;
        for (std::size_t i = 0; i < m_Physical.keys.size(); ++i) {
            if (capture.keyboardCaptured && m_Physical.keys[i]) m_BlockedKeys[i] = true;
            if (!capture.gameplayActive && m_Physical.keys[i]) m_BlockedGameplayKeys[i] = true;
        }
        for (std::size_t i = 0; i < m_Physical.mouseButtons.size(); ++i) {
            if (capture.mouseCaptured && m_Physical.mouseButtons[i]) m_BlockedMouse[i] = true;
            if (!capture.gameplayActive && m_Physical.mouseButtons[i]) m_BlockedGameplayMouse[i] = true;
        }
        for (std::size_t i = 0; i < m_Map.actions.size(); ++i) {
            const auto& definition = m_Map.actions[i];
            auto& value = m_Frame.actions[i];
            const bool discardEdges = definition.context == ActionContext::Gameplay && m_DiscardGameplayFrameEdges;
            const bool gate = !definition.gateMouseButton || (IsMouseAvailable(*definition.gateMouseButton, definition.context)
                && m_Physical.mouseButtons[*definition.gateMouseButton]);
            const bool allowed = gate && std::any_of(definition.bindings.begin(), definition.bindings.end(), [&](const ActionBinding& binding) {
                return binding.source == InputSource::Key ? IsKeyboardAllowed(definition.context) : IsMouseAllowed(definition.context);
            });
            double sum = 0.0;
            bool pressed = false;
            for (const auto& binding : definition.bindings) {
                if (!allowed) break;
                switch (binding.source) {
                case InputSource::Key:
                    if (IsKeyAvailable(binding.code, definition.context)) {
                        sum += m_Physical.keys[binding.code] ? binding.scale : 0.0;
                        pressed |= !discardEdges && m_Physical.keyPressed[binding.code];
                    }
                    break;
                case InputSource::MouseButton:
                    if (IsMouseAvailable(binding.code, definition.context)) {
                        sum += m_Physical.mouseButtons[binding.code] ? binding.scale : 0.0;
                        pressed |= !discardEdges && m_Physical.mousePressed[binding.code];
                    }
                    break;
                case InputSource::MouseDeltaX: if (!discardEdges) sum += m_Physical.deltaX * binding.scale; break;
                case InputSource::MouseDeltaY: if (!discardEdges) sum += m_Physical.deltaY * binding.scale; break;
                case InputSource::ScrollX: if (!discardEdges) sum += m_Physical.scrollX * binding.scale; break;
                case InputSource::ScrollY: if (!discardEdges) sum += m_Physical.scrollY * binding.scale; break;
                }
            }
            value.value = std::abs(sum) > definition.deadZone ? sum : 0.0;
            if (definition.type == ActionType::Button) value.value = value.value != 0.0 ? 1.0 : 0.0;
            value.held = value.value != 0.0;
            // 同一事件泵内按下又松开仍保留一次 press/release，不要求恰好有模拟 tick。
            if (!m_ImpulseActions[i]) {
                value.pressed = allowed && ((!discardEdges && m_DigitalPressed[i] && (value.held || pressed)) || (!m_PreviousHeld[i] && value.held));
                value.released = (m_PreviousHeld[i] && !value.held)
                    || (allowed && m_DigitalReleased[i] && (m_PreviousHeld[i] || value.pressed));
            }
            else {
                value.pressed = allowed && !m_PreviousHeld[i] && (value.held || pressed);
                value.released = (m_PreviousHeld[i] && !value.held) || (value.pressed && !value.held);
            }
            if (!allowed) {
                m_Pending[i].pressed = false;
                m_Pending[i].impulse = 0.0;
            }
            m_Pending[i].pressed |= value.pressed;
            m_Pending[i].released |= value.released;
            if (m_ImpulseActions[i]) m_Pending[i].impulse += value.value;
            m_PreviousHeld[i] = value.held;
        }
        m_FramePhysical = m_Physical;
        ClearFrameEvents();
        m_DiscardGameplayFrameEdges = false;
        return m_Frame;
    }

    ActionSnapshot InputActions::ConsumeTick()
    {
        ActionSnapshot tick = m_Frame;
        for (std::size_t i = 0; i < tick.actions.size(); ++i) {
            auto& value = tick.actions[i];
            value.pressed = std::exchange(m_Pending[i].pressed, false);
            value.released = std::exchange(m_Pending[i].released, false);
            if (m_ImpulseActions[i]) {
                value.value = std::exchange(m_Pending[i].impulse, 0.0);
                value.held = value.value != 0.0;
            }
        }
        return tick;
    }

    void InputActions::ClearFrameEvents()
    {
        m_Physical.keyPressed.fill(false); m_Physical.keyReleased.fill(false);
        m_Physical.mousePressed.fill(false); m_Physical.mouseReleased.fill(false);
        m_Physical.deltaX = m_Physical.deltaY = m_Physical.scrollX = m_Physical.scrollY = 0.0;
        std::fill(m_DigitalPressed.begin(), m_DigitalPressed.end(), false);
        std::fill(m_DigitalReleased.begin(), m_DigitalReleased.end(), false);
    }
}
