#pragma once
#include "Core/Base.h"
#include "UI/UIEvent.h"
namespace Catalpa {
    class Event;
    // 原生窗口坐标到 UI 目标像素的转换由宿主提供；按键协议不泄露 RmlUi 头文件。
    class CATALPA_API UIInputRouter final {
      public:
        bool Translate(const Event&, float originX, float originY, float scaleX, float scaleY,
                       UIInputEvent& output);
        static int TranslatePlatformKey(int key);
        void Reset() noexcept {
            m_Modifiers = 0;
            m_Pressed.fill(false);
        }

      private:
        int m_Modifiers = 0;
        std::array<bool, 512> m_Pressed{};
    };
} // namespace Catalpa
