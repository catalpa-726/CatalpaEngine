#pragma once

#include "Core/Input.h"

namespace Catalpa {

	class WindowsInput : public Input {

	public:
	protected:
		/**
		 * @brief 通过平台输入后端查询指定按键状态。
		 *
		 * @param[in] keycode 键盘按键码。
		 *
		 * @return 满足上述条件时为 true，否则为 false。
		 */
		virtual bool IsKeyPressedImpl(int keycode) override;

		/**
		 * @brief 查询当前绑定窗口是否具有焦点且指定鼠标键处于按下状态。
		 *
		 * @param[in] button GLFW 鼠标键编号，有效范围为 0 至 GLFW_MOUSE_BUTTON_LAST。
		 *
		 * @return 鼠标键有效、绑定窗口有焦点且按键已按下时返回 true，否则返回 false。
		 */
		virtual bool IsMouseButtonPressedImpl(int button) override;
		/**
		 * @brief 从当前绑定且具有焦点的窗口读取指针相对内容区的位置。
		 *
		 * @return 返回相对窗口内容区左上角的横、纵坐标；窗口无效、未获得焦点或坐标不能表示为 float 时返回 (0, 0)。
		 */
		virtual std::pair<float, float> GetMousePostionImpl() override;
		virtual float GetMouseXImpl() override;
		virtual float GetMouseYImpl() override;
	};

}