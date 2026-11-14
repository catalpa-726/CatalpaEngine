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

		/**
		 * @brief 轮询本帧待处理的原生窗口与输入事件。
		 */
		void OnUpdate() override;

		inline unsigned int GetWidth() const override { return m_Data.Width; }
		inline unsigned int GetHeight() const override { return m_Data.Height; }

		/**
		 * @brief 读取窗口实际帧缓冲的像素尺寸。
		 *
		 * @return 依次返还帧缓冲的宽和高，单位为物理像素，可与窗口逻辑尺寸不同。
		 */
		std::pair<uint32_t, uint32_t> GetFramebufferSize() const override;
		bool IsMinimized() const override;
		void Maximize() override;

		inline void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
		void SetVSync(bool enabled) override;
		bool IsVSync() const override;
		/**
		 * @brief 绑定 Win32 输入法目标，并按文本光标位置放置预编辑窗口和候选窗口。
		 *
		 * @param[in] owner 文本输入目标的非零所有者标识。
		 * @param[in] region 含文本输入激活状态和候选窗定位矩形的快照。
		 * @param[in] callback 接收预编辑、提交或取消事件的回调。
		 * @param[out] error 失败时接收诊断消息。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool SetTextInputTarget(
		    uint64_t, const TextInputSnapshot&, TextCompositionCallback, std::string& error) override;
		/**
		 * @brief 解除指定所有者的输入法目标。
		 *
		 * @param[in] owner 要清除的目标标识；关闭应用时传 0 可清理最后一个目标。
		 */
		void ClearTextInputTarget(uint64_t) override;
		bool HasTextInputTarget() const noexcept override { return m_Data.imeOwner != 0; }

		virtual void* GetNativeWindow() const { return m_Window; }

	private:
		virtual void Init(const WindowProps& props);
		virtual void Shutdown();

	private:
		/* 原生窗口及 GLFW 回调共享的数据。 */
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

} /* 命名空间 Catalpa */
