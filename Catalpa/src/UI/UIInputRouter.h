#pragma once
#include "Core/Base.h"
#include "UI/UIEvent.h"
namespace Catalpa {
	class Event;
	/* 原生窗口坐标到 UI 目标像素的转换由宿主提供；按键协议不泄露 RmlUi 头文件。 */
	class CATALPA_API UIInputRouter final {
	public:
		/**
		 * @brief 将原生按键、指针和滚轮等输入转换为 UI 的局部事件。
		 * 把窗口指针位置换算为 UI 局部坐标，并将原生滚轮方向转换为 RmlUi 约定。
		 *
		 * @param[in] event 待转换的原生输入事件。
		 * @param[in] originX UI 显示区域在窗口事件坐标系中的左边界。
		 * @param[in] originY UI 显示区域在窗口事件坐标系中的上边界。
		 * @param[in] scaleX 从事件横坐标到 UI 局部横坐标的缩放比例。
		 * @param[in] scaleY 从事件纵坐标到 UI 局部纵坐标的缩放比例。
		 * @param[out] output 接收转换为 UI 坐标和按键约定的事件。
		 *
		 * @return 事件可转换且坐标或输入码有效时为 true，无法交付给 UI 时为 false。
		 */
		bool Translate(const Event&, float originX, float originY, float scaleX, float scaleY, UIInputEvent& output);
		static int TranslatePlatformKey(int key);
		void Reset() noexcept {
			m_Modifiers = 0;
			m_Pressed.fill(false);
		}

	private:
		int m_Modifiers = 0;
		std::array<bool, 512> m_Pressed{};
	};
} /* 命名空间 Catalpa */
