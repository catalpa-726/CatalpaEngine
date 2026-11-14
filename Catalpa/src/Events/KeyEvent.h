#pragma once

#include "catpch.h"

#include "Event.h"
#include "Core/KeyCodes.h"

namespace Catalpa {

	class KeyEvent : public Event {
	public:
		KeyCode GetKeyCode() const { return m_KeyCode; }

		EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	protected:
		KeyEvent(const KeyCode keycode) : m_KeyCode(keycode) {}

		KeyCode m_KeyCode;
	};

	class KeyPressedEvent : public KeyEvent {
	public:
		KeyPressedEvent(const KeyCode keycode, bool isRepeat = false) : KeyEvent(keycode), m_IsRepeat(isRepeat) {}

		bool IsRepeat() const { return m_IsRepeat; }

		/**
		 * @brief 转换为字符串。
		 *
		 * @return 当前对象的文本表示。
		 */
		std::string ToString() const override {
			std::stringstream ss;
			ss << "KeyPressedEvent: " << m_KeyCode << " (repeat = " << m_IsRepeat << ")";
			return ss.str();
		}

		EVENT_CLASS_TYPE(KeyPressed)

	private:
		bool m_IsRepeat;
	};

	class KeyReleasedEvent : public KeyEvent {
	public:
		KeyReleasedEvent(const KeyCode keycode) : KeyEvent(keycode) {}

		/**
		 * @brief 转换为字符串。
		 *
		 * @return 当前对象的文本表示。
		 */
		std::string ToString() const override {
			std::stringstream ss;
			ss << "KeyReleasedEvent: " << m_KeyCode;
			return ss.str();
		}

		EVENT_CLASS_TYPE(KeyReleased)
	};

	/* 文本事件传Unicode scalar，与物理KeyCode的uint16协议分开。 */
	class KeyTypedEvent : public Event {
	public:
		explicit KeyTypedEvent(uint32_t codepoint) : m_Codepoint(codepoint) {}
		uint32_t GetCodepoint() const noexcept { return m_Codepoint; }
		std::string ToString() const override { return "KeyTypedEvent: " + std::to_string(m_Codepoint); }
		EVENT_CLASS_TYPE(KeyTyped)
		EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)
	private:
		uint32_t m_Codepoint;
	};
}
