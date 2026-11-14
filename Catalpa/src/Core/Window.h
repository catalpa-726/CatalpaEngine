#pragma once

#include "catpch.h"

#include "Events/Event.h"
#include "Core/TextInput.h"

namespace Catalpa {

	struct WindowProps {
		std::string Title;
		uint32_t Width;
		uint32_t Height;

		WindowProps(const std::string& title = "Catalpa Engine", uint32_t width = 1980, uint32_t height = 1020)
		    : Title(title), Width(width), Height(height) {}
	};

	/* 适配不同平台 */
	class Window {
	public:
		using EventCallbackFn = std::function<void(Event&)>;

		virtual ~Window() = default;

		/**
		 * @brief 轮询本帧待处理的原生窗口与输入事件。
		 */
		virtual void OnUpdate() = 0;

		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;

		virtual std::pair<uint32_t, uint32_t> GetFramebufferSize() const = 0;
		virtual bool IsMinimized() const = 0;
		/**
		 * @brief 请求窗口管理器最大化当前原生窗口。
		 */
		virtual void Maximize() = 0;

		/**
		 * @brief 指定接收窗口及输入事件的处理函数。
		 *
		 * @param[in] callback 收到窗口或输入事件时调用的处理函数。
		 */
		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		/**
		 * @brief 记录垂直同步请求，供运行时选择交换链呈现模式。
		 *
		 * @param[in] enabled 为 true 时请求垂直同步。
		 */
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;

		using TextCompositionCallback = std::function<void(const TextComposition&)>;
		virtual bool SetTextInputTarget(
		    uint64_t, const TextInputSnapshot&, TextCompositionCallback, std::string& error) {
			error = "Native IME target is unsupported by this window backend";
			return false;
		}
		virtual void ClearTextInputTarget(uint64_t) {} /* owner=0 仅用于应用关闭时清理任一最后目标。 */
		virtual bool HasTextInputTarget() const noexcept { return false; }

		virtual void* GetNativeWindow() const = 0;

		static Window* Create(const WindowProps& props = WindowProps());
	};

}
