#pragma once

#include "catpch.h"

#include "Event.h"

namespace Catalpa {

	class WindowResizeEvent : public Event {
	public:
		WindowResizeEvent(unsigned int width, unsigned int height) : m_Width(width), m_Height(height) {}

		unsigned int GetWidth() const { return m_Width; }
		unsigned int GetHeight() const { return m_Height; }

		/**
		 * @brief 转换为字符串。
		 *
		 * @return 当前对象的文本表示。
		 */
		std::string ToString() const override {
			std::stringstream ss;
			ss << "WindowResizeEvent: " << m_Width << ", " << m_Height;
			return ss.str();
		}

		EVENT_CLASS_TYPE(WindowResize)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		unsigned int m_Width, m_Height;
	};

	/** @brief 用户从系统文件管理器放入窗口的自有路径列表，事件不借用 GLFW 字符串。 */
	class FileDropEvent final : public Event {
	public:
		FileDropEvent(std::vector<std::filesystem::path> paths, float x, float y)
		    : m_Paths(std::move(paths)), m_X(x), m_Y(y) {}
		float X() const noexcept { return m_X; }
		float Y() const noexcept { return m_Y; }
		const std::vector<std::filesystem::path>& GetPaths() const noexcept { return m_Paths; }
		EVENT_CLASS_TYPE(FileDrop)
		EVENT_CLASS_CATEGORY(EventCategoryInput)
	private:
		std::vector<std::filesystem::path> m_Paths;
		float m_X, m_Y;
	};

	class WindowCloseEvent : public Event {
	public:
		WindowCloseEvent() = default;

		EVENT_CLASS_TYPE(WindowClose)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	/* 单主窗口焦点边沿用于释放玩法输入；文本/IME 仍通过各自事件处理。 */
	class WindowFocusEvent : public Event {
	public:
		EVENT_CLASS_TYPE(WindowFocus)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowLostFocusEvent : public Event {
	public:
		EVENT_CLASS_TYPE(WindowLostFocus)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class AppTickEvent : public Event {
	public:
		AppTickEvent() = default;

		EVENT_CLASS_TYPE(AppTick)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class AppUpdateEvent : public Event {
	public:
		AppUpdateEvent() = default;

		EVENT_CLASS_TYPE(AppUpdate)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class AppRenderEvent : public Event {
	public:
		AppRenderEvent() = default;

		EVENT_CLASS_TYPE(AppRender)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowFramebufferResizeEvent : public Event {
	public:
		WindowFramebufferResizeEvent(unsigned int width, unsigned int height) : m_Width(width), m_Height(height) {}

		unsigned int GetWidth() const { return m_Width; }
		unsigned int GetHeight() const { return m_Height; }

		/**
		 * @brief 转换为字符串。
		 *
		 * @return 当前对象的文本表示。
		 */
		std::string ToString() const override {
			std::stringstream ss;
			ss << "WindowFramebufferResizeEvent: " << m_Width << ", " << m_Height;
			return ss.str();
		}

		EVENT_CLASS_TYPE(WindowFramebufferResize)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		unsigned int m_Width, m_Height;
	};

	/* 区分最小化与恢复。 */
	class WindowMinimizeEvent : public Event {
	public:
		explicit WindowMinimizeEvent(bool minimized) : m_Minimized(minimized) {}

		bool IsMinimized() const { return m_Minimized; }
		std::string ToString() const override {
			return m_Minimized ? "WindowMinimizeEvent: true" : "WindowMinimizeEvent: false";
		}

		EVENT_CLASS_TYPE(WindowMinimize)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		bool m_Minimized;
	};

}
