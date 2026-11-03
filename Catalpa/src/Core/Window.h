#pragma once

#include "catpch.h"
#include "Events/Event.h"

namespace Catalpa {

	struct WindowProps
	{
		std::string Title;
		uint32_t Width;
		uint32_t Height;

		WindowProps(const std::string& title = "Catalpa Engine", uint32_t width = 1980, uint32_t height = 1020)
			: Title(title), Width(width), Height(height) {}
	};

	// 适配不同平台
	class Window
	{
	public:
		using EventCallbackFn = std::function<void(Event&)>;

		virtual ~Window() = default;

		virtual void OnUpdate() = 0;

		// 逻辑窗口尺寸。
		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;

		// framebuffer 像素尺寸与最小化状态。
		virtual std::pair<uint32_t, uint32_t> GetFramebufferSize() const = 0;
		virtual bool IsMinimized() const = 0;

		// 窗口事件回调与请求的 VSync 配置。
		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;

		// 原生窗口访问与平台工厂。
		virtual void* GetNativeWindow() const = 0;

		static Window* Create(const WindowProps& props = WindowProps());
	};

}
