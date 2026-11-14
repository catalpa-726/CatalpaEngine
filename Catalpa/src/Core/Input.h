#pragma once

#include "catpch.h"

#include "Base.h"

namespace Catalpa {

	class Window;

	/* 主窗口物理输入值。指针位置/位移为逻辑窗口坐标，不能当 framebuffer 像素。 */
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
		/**
		 * @brief 绑定供物理输入轮询借用的宿主窗口；窗口销毁前应解除绑定。
		 * 兼容物理轮询只借用宿主绑定的窗口；销毁窗口前必须解绑。
		 *
		 * @param[in] window 输入轮询期间借用的宿主窗口；传空指针解除绑定。
		 */
		static void BindWindow(Window* window);
		inline static bool IsKeyPressed(int keycode) { return s_Instance->IsKeyPressedImpl(keycode); }

		inline static bool IsMouseButtonPressed(int button) { return s_Instance->IsMouseButtonPressedImpl(button); }
		inline static std::pair<float, float> GetMousePostion() { return s_Instance->GetMousePostionImpl(); }
		inline static float GetMouseX() { return s_Instance->GetMouseXImpl(); }
		inline static float GetMouseY() { return s_Instance->GetMouseYImpl(); }

	protected:
		static Window* GetBoundWindow() { return s_Window; }
		/**
		 * @brief 通过平台输入后端查询指定按键状态。
		 *
		 * @param[in] keycode 键盘按键码。
		 *
		 * @return 满足上述条件时为 true，否则为 false。
		 */
		virtual bool IsKeyPressedImpl(int keycode) = 0;

		/**
		 * @brief 通过平台输入后端查询指定鼠标按键状态。
		 *
		 * @param[in] button 待查询按下状态的鼠标按钮码。
		 *
		 * @return 满足上述条件时为 true，否则为 false。
		 */
		virtual bool IsMouseButtonPressedImpl(int button) = 0;
		virtual std::pair<float, float> GetMousePostionImpl() = 0;
		virtual float GetMouseXImpl() = 0;
		virtual float GetMouseYImpl() = 0;

	private:
		static Input* s_Instance;
		static Window* s_Window;
	};

}
