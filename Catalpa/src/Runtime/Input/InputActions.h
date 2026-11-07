#pragma once

#include "catpch.h"

#include "Core/Input.h"
#include "Runtime/Input/ActionMap.h"

namespace Catalpa {

    class Event;

    enum class InputError : std::uint8_t { None, InvalidProfile, InvalidBinding, InvalidJson };

    struct InputStatus {
        InputError code = InputError::None;
        std::string message;
        [[nodiscard]] bool IsSuccess() const { return code == InputError::None; }
    };

    // 宿主在 UI NewFrame 后传入捕获结果；不把 Editor/ImGui 类型传到 Runtime。
    struct InputCapture {
        bool keyboardCaptured = false;
        bool mouseCaptured = false;
        bool gameplayActive = true;
    };

    struct ActionValue {
        std::string id;
        double value = 0.0;
        bool held = false;
        bool pressed = false;
        bool released = false;
    };

    struct ActionSnapshot {
        std::uint64_t frameIndex = 0;
        bool focused = true;
        bool gameplayActive = true;
        std::vector<ActionValue> actions;

        [[nodiscard]] const ActionValue* Find(std::string_view id) const {
            for (const auto& action : actions) if (action.id == id) return &action;
            return nullptr;
        }
    };

    // 单创建线程的事件收集与路由器：帧快照与模拟 tick 的一次性边沿分别保存。
    class CATALPA_API InputActions {
    public:
        InputActions() = default;
        InputActions(const InputActions&) = delete;
        InputActions& operator=(const InputActions&) = delete;

        // 完整校验并原子替换动作图，失败保留现有配置。
        [[nodiscard]] InputStatus Configure(ActionMap map);
        // 只解析调用方提供的 JSON 字符串，不自行选择工作目录或回退配置。
        [[nodiscard]] InputStatus ParseProfile(std::string_view json);
        // 即使 UI 已标记 Handled 也收集物理事件，发布时再应用捕获策略。
        void OnEvent(const Event& event);
        // 保留完整物理源，同时封住已交给游戏 UI 的动作边沿及按键，释放始终正常收集。
        void OnCapturedEvent(const Event&, bool keyboardFocus = false);
        // 事件泵和 UI NewFrame 之后发布一次展示帧快照。
        [[nodiscard]] const ActionSnapshot& BeginFrame(InputCapture capture = {});
        // 边沿/位移只交给首个消费 tick，连续键轴在补步中保持。
        [[nodiscard]] ActionSnapshot ConsumeTick();
        [[nodiscard]] const ActionSnapshot& GetFrameSnapshot() const { return m_Frame; }
        [[nodiscard]] const PhysicalInputSnapshot& GetPhysicalSnapshot() const { return m_FramePhysical; }
        void Reset() noexcept;
        // 场景/World 切换或停止时无分配取消玩法输入；系统动作与真实物理焦点保留。
        void ResetGameplayContext() noexcept;

    private:
        struct PendingAction {
            bool pressed = false;
            bool released = false;
            double impulse = 0.0;
        };

        [[nodiscard]] bool IsKeyboardAllowed(ActionContext context) const;
        [[nodiscard]] bool IsMouseAllowed(ActionContext context) const;
        [[nodiscard]] bool IsKeyAvailable(int code, ActionContext context) const;
        [[nodiscard]] bool IsMouseAvailable(int code, ActionContext context) const;
        void SetFocused(bool focused);
        void ObserveDigitalActions();
        void ClearFrameEvents();

        ActionMap m_Map;
        PhysicalInputSnapshot m_Physical;
        PhysicalInputSnapshot m_FramePhysical;
        ActionSnapshot m_Frame;
        std::vector<PendingAction> m_Pending;
        std::vector<bool> m_PreviousHeld;
        std::vector<bool> m_ImpulseActions;
        std::vector<bool> m_DigitalPhysicalHeld;
        std::vector<bool> m_DigitalPressed;
        std::vector<bool> m_DigitalReleased;
        InputCapture m_Capture;
        std::array<bool, PhysicalInputSnapshot::KeyCount> m_BlockedKeys{};
        std::array<bool, PhysicalInputSnapshot::KeyCount> m_BlockedGameplayKeys{};
        std::array<bool, PhysicalInputSnapshot::MouseButtonCount> m_BlockedMouse{};
        std::array<bool, PhysicalInputSnapshot::MouseButtonCount> m_BlockedGameplayMouse{};
        bool m_HasCursor = false;
        bool m_DiscardGameplayFrameEdges = false;
        std::array<bool, PhysicalInputSnapshot::KeyCount> m_UICapturedKeys{};
        std::array<bool, PhysicalInputSnapshot::MouseButtonCount> m_UICapturedMouse{};
        bool m_UICapturedMove = false, m_UICapturedScroll = false;
    };
}
