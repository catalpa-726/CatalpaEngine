#pragma once

#include "Core/Window.h"
#include "Log/Log.h"

#include <GLFW/glfw3.h>

namespace Catalpa {

	class WindowsWindow : public Window {
	public:
		WindowsWindow(const WindowProps& props);
		virtual ~WindowsWindow();

		WindowsWindow(const WindowsWindow&) = delete;
		WindowsWindow& operator=(const WindowsWindow&) = delete;

		// 事件泵与窗口状态。
		void OnUpdate() override;

		inline unsigned int GetWidth() const override { return m_Data.Width; }
		inline unsigned int GetHeight() const override { return m_Data.Height; }

		std::pair<uint32_t, uint32_t> GetFramebufferSize() const override;
		bool IsMinimized() const override;
		void Maximize() override;

		// 事件分发与呈现配置请求。
		inline void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
		void SetVSync(bool enabled) override;
		bool IsVSync() const override;
                bool SetTextInputTarget(uint64_t, const TextInputSnapshot&, TextCompositionCallback,
                                        std::string& error) override;
                void ClearTextInputTarget(uint64_t) override;
                bool HasTextInputTarget() const noexcept override {
                    return m_Data.imeOwner != 0;
                }

                virtual void* GetNativeWindow() const { return m_Window; }

	private:
		virtual void Init(const WindowProps& props);
		virtual void Shutdown();

	private:
		// 原生窗口及 GLFW 回调共享的数据。
		GLFWwindow* m_Window = nullptr;

		struct WindowData {
			std::string Title;
			unsigned int Width, Height;
			bool VSync;

			EventCallbackFn EventCallback;
                        uint64_t imeOwner = 0;
                        bool composing = false;
                        void* originalProcedure = nullptr;
                        TextCompositionCallback compositionCallback;
                };

		WindowData m_Data{};
	};

} // namespace Catalpa
