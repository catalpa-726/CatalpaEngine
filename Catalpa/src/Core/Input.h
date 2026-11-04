#pragma once

#include "catpch.h"

#include "Base.h"

namespace Catalpa {

    class Window;

    // 主窗口物理输入值。指针位置/位移为逻辑窗口坐标，不能当 framebuffer 像素。
    struct PhysicalInputSnapshot {
        static constexpr std::size_t KeyCount = 512;
        static constexpr std::size_t MouseButtonCount = 8;
        std::array<bool, KeyCount> keys{};
        std::array<bool, KeyCount> keyPressed{};
        std::array<bool, KeyCount> keyReleased{};
        std::array<bool, MouseButtonCount> mouseButtons{};
        std::array<bool, MouseButtonCount> mousePressed{};
        std::array<bool, MouseButtonCount> mouseReleased{};
        double cursorX = 0.0, cursorY = 0.0;
        double deltaX = 0.0, deltaY = 0.0;
        double scrollX = 0.0, scrollY = 0.0;
        bool focused = true;
    };

	class CATALPA_API Input {
	public:
		// 兼容物理轮询只借用宿主绑定的窗口；销毁窗口前必须解绑。
		static void BindWindow(Window* window);
		// 统一的输入轮询入口。
		inline static bool IsKeyPressed(int keycode) { return s_Instance->IsKeyPressedImpl(keycode); }

		inline static bool IsMouseButtonPressed(int button) { return s_Instance->IsMouseButtonPressedImpl(button); }
		inline static std::pair<float, float> GetMousePostion() { return s_Instance->GetMousePostionImpl(); }
		inline static float GetMouseX() { return s_Instance->GetMouseXImpl(); }
		inline static float GetMouseY() { return s_Instance->GetMouseYImpl(); }

	protected:
		static Window* GetBoundWindow() { return s_Window; }
		// 由平台实现具体查询。
		virtual bool IsKeyPressedImpl(int keycode) = 0;

		virtual bool IsMouseButtonPressedImpl(int button) = 0;
		virtual std::pair<float, float> GetMousePostionImpl() = 0;
		virtual float GetMouseXImpl() = 0;
		virtual float GetMouseYImpl() = 0;

	private:
		static Input* s_Instance;
		static Window* s_Window;
	};

}
