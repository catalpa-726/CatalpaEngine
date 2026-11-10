#include "catpch.h"
#include "WindowsWindow.h"

#include "Events/ApplicationEvent.h"
#include "Events/MouseEvent.h"
#include "Events/KeyEvent.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <imm.h>

//#include <glad/glad.h>

namespace Catalpa {

	static unsigned int s_GLFWWindowCount = 0;

	static void GLFWErrorCallback(int error, const char* description) {
		CAT_CORE_ERROR("GLFW Error ({0}): {1}", error, description);
	}

	Window* Window::Create(const WindowProps& props) {
		return new WindowsWindow(props);
	}

	WindowsWindow::WindowsWindow(const WindowProps& props) {
		Init(props);
	}

	WindowsWindow::~WindowsWindow() {
		Shutdown();
	}

	void WindowsWindow::Init(const WindowProps& props) {
		m_Data.Title = props.Title;
		m_Data.Width = props.Width;
		m_Data.Height = props.Height;

		if (s_GLFWWindowCount == 0) {
			glfwSetErrorCallback(GLFWErrorCallback);
			if (!glfwInit())
				CAT_CORE_ASSERT(false, "Failed to initialize GLFW.");
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		m_Window = glfwCreateWindow(static_cast<int>(props.Width), static_cast<int>(props.Height), m_Data.Title.c_str(), nullptr, nullptr);

		if (!m_Window) {
			if (s_GLFWWindowCount == 0)
				glfwTerminate();
			CAT_CORE_ASSERT(false, "Failed to create window.");
		}

		++s_GLFWWindowCount;

		//m_Context = new OpenGLContext(m_Window);
		//m_Context->Init();
		// 以上 OpenGL Context 初始化仅保留为注释；当前窗口使用 GLFW_NO_API。

		glfwSetWindowUserPointer(m_Window, &m_Data);
                const auto hwnd = glfwGetWin32Window(m_Window);
                if (SetPropW(hwnd, L"Catalpa.TextInput", &m_Data)) {
                    const auto procedure =
                        +[](HWND window, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
                        auto* data = static_cast<WindowData*>(GetPropW(window, L"Catalpa.TextInput"));
                        if (!data || !data->originalProcedure)
                            return DefWindowProcW(window, message, wParam, lParam);
                        const auto previous = reinterpret_cast<WNDPROC>(data->originalProcedure);
                        if (data->imeOwner && data->compositionCallback)
                            try {
                                const auto emit = [&](TextComposition value) {
                                    data->compositionCallback(value);
                                };
                                if (message == WM_IME_STARTCOMPOSITION) {
                                    data->composing = true;
                                    emit({TextComposition::Phase::Start});
                                    return 0;
                                }
                                if (message == WM_IME_ENDCOMPOSITION) {
                                    if (data->composing) {
                                        data->composing = false;
                                        emit({TextComposition::Phase::Cancel});
                                    }
                                    return 0;
                                }
                                if (message == WM_IME_CHAR || message == WM_IME_REQUEST)
                                    return 0;
                                if (message == WM_IME_COMPOSITION) {
                                    const auto context = ImmGetContext(window);
                                    if (!context)
                                        return 0;
                                    struct Release {
                                        HWND window;
                                        HIMC context;
                                        ~Release() {
                                            ImmReleaseContext(window, context);
                                        }
                                    } release{window, context};
                                    if (!data->composing) {
                                        data->composing = true;
                                        emit({TextComposition::Phase::Start});
                                    }
                                    const auto read = [&](DWORD kind, TextComposition::Phase phase) {
                                        const auto count =
                                            ImmGetCompositionStringW(context, kind, nullptr, 0);
                                        if (count < 0 || count > 8192 || count % 2)
                                            throw std::runtime_error(
                                                "IME UTF-16 composition byte budget rejected");
                                        std::wstring wide(size_t(count) / 2, L'\0');
                                        if (count && ImmGetCompositionStringW(context, kind, wide.data(),
                                                                              DWORD(count)) != count)
                                            throw std::runtime_error("Cannot read IME composition");
                                        const auto convert = [](std::wstring_view input) {
                                            if (input.empty())
                                                return std::string{};
                                            const auto size = WideCharToMultiByte(
                                                CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
                                                int(input.size()), nullptr, 0, nullptr, nullptr);
                                            if (size <= 0)
                                                throw std::runtime_error("IME input contains invalid UTF-16");
                                            std::string text(size, '\0');
                                            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                                                    input.data(), int(input.size()),
                                                                    text.data(), size, nullptr,
                                                                    nullptr) != size)
                                                throw std::runtime_error(
                                                    "Cannot convert IME composition to UTF-8");
                                            return text;
                                        };
                                        TextComposition value;
                                        value.phase = phase;
                                        value.text = convert(wide);
                                        const auto raw = phase == TextComposition::Phase::Commit ||
                                                                 !(lParam & GCS_CURSORPOS)
                                                             ? LONG(wide.size())
                                                             : ImmGetCompositionStringW(
                                                                   context, GCS_CURSORPOS, nullptr, 0);
                                        const auto caret = std::clamp<LONG>(raw, 0, LONG(wide.size()));
                                        const auto prefix =
                                            convert(std::wstring_view(wide).substr(0, size_t(caret)));
                                        for (unsigned char c : prefix)
                                            if ((c & 0xc0) != 0x80)
                                                ++value.caret;
                                        emit(std::move(value));
                                    };
                                    if (lParam & GCS_RESULTSTR) {
                                        read(GCS_RESULTSTR, TextComposition::Phase::Commit);
                                        data->composing = false;
                                    }
                                    if ((lParam & GCS_COMPSTR) ||
                                        ((lParam & GCS_CURSORPOS) && !(lParam & GCS_RESULTSTR))) {
                                        if (!data->composing) {
                                            data->composing = true;
                                            emit({TextComposition::Phase::Start});
                                        }
                                        read(GCS_COMPSTR, TextComposition::Phase::Update);
                                    }
                                    if (!lParam) {
                                        data->composing = false;
                                        emit({TextComposition::Phase::Cancel});
                                    }
                                    return 0;
                                }
                                if (message == WM_KILLFOCUS && data->composing) {
                                    data->composing = false;
                                    emit({TextComposition::Phase::Cancel});
                                }
                            } catch (const std::exception& error) {
                                data->composing = false;
                                try {
                                    data->compositionCallback({TextComposition::Phase::Cancel});
                                    CAT_CORE_WARN("Windows IME: {0}", error.what());
                                } catch (...) {
                                }
                                return 0;
                            }
                        return CallWindowProcW(previous, window, message, wParam, lParam);
                    };
                    m_Data.originalProcedure = reinterpret_cast<void*>(
                        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(procedure)));
                    if (!m_Data.originalProcedure)
                        RemovePropW(hwnd, L"Catalpa.TextInput");
                }
                SetVSync(true);

		// 注册 GLFW 回调，转换为现有 Events 并交给窗口事件回调。
		glfwSetWindowFocusCallback(m_Window, [](GLFWwindow* window, int focused) {
			auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			if (!data.EventCallback) return;
			if (focused == GLFW_TRUE) {
				WindowFocusEvent event;
				data.EventCallback(event);
			}
			else {
				WindowLostFocusEvent event;
				data.EventCallback(event);
			}
		});

		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			data.Width = width;
			data.Height = height;

			WindowResizeEvent event(width, height);
			data.EventCallback(event);
		});

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			WindowCloseEvent event;
			data.EventCallback(event);
		});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			switch (action) {
				case GLFW_PRESS:
				{
					KeyPressedEvent event(key, 0);
					data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					KeyReleasedEvent event(key);
					data.EventCallback(event);
					break;
				}
				case GLFW_REPEAT:
				{
					KeyPressedEvent event(key, true);
					data.EventCallback(event);
					break;
				}
			}
		});

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			switch (action)
			{
				case GLFW_PRESS:
				{
					MouseButtonPressedEvent event(button);
					data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					MouseButtonReleasedEvent event(button);
					data.EventCallback(event);
					break;
				}
			}
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xoffset, double yoffset) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			MouseScrolledEvent event((float)xoffset, (float)yoffset);
			data.EventCallback(event);
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xpos, double ypos) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			MouseMovedEvent event((float)xpos, (float)ypos);
			data.EventCallback(event);
		});

		glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int keycode) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			KeyTypedEvent event(keycode);
			data.EventCallback(event);
		});

		glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {
			auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			WindowFramebufferResizeEvent event(static_cast<unsigned int>(width), static_cast<unsigned int>(height));
			data.EventCallback(event);
		});

		glfwSetWindowIconifyCallback(m_Window, [](GLFWwindow* window, int iconified) {
			auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			WindowMinimizeEvent event(iconified == GLFW_TRUE);
			data.EventCallback(event);
		});
	}

	void WindowsWindow::Shutdown()
	{
		if (!m_Window) {
			return;
		}
                ClearTextInputTarget(m_Data.imeOwner);
                const auto hwnd = glfwGetWin32Window(m_Window);
                if (m_Data.originalProcedure)
                    SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                                      reinterpret_cast<LONG_PTR>(m_Data.originalProcedure));
                RemovePropW(hwnd, L"Catalpa.TextInput");
                m_Data.originalProcedure = nullptr;
                glfwDestroyWindow(m_Window);
		m_Window = nullptr;
		--s_GLFWWindowCount;
		if (s_GLFWWindowCount == 0) {
			glfwTerminate();
		}
	}

	void WindowsWindow::OnUpdate() {
		glfwPollEvents();
		//m_Context->SwapBuffers();
		//m_Context.GetSwapChain().Flush();
	}

	std::pair<uint32_t, uint32_t> WindowsWindow::GetFramebufferSize() const
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(m_Window, &width, &height);

		return {
			static_cast<uint32_t>(width),
			static_cast<uint32_t>(height)
		};
	}

	bool WindowsWindow::IsMinimized() const
	{
		return glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED) == GLFW_TRUE;
	}

	void WindowsWindow::Maximize()
	{
		glfwMaximizeWindow(m_Window);
	}

	void WindowsWindow::SetVSync(bool enabled) {
		// 保存 VSync 请求；EngineRuntime 据此选择并重建交换链的呈现模式。
		m_Data.VSync = enabled;
	}

	bool WindowsWindow::IsVSync() const {
		return m_Data.VSync;
	}

        bool WindowsWindow::SetTextInputTarget(uint64_t owner, const TextInputSnapshot& region,
                                               TextCompositionCallback callback, std::string& error) {
            if (!owner || !region.active || !callback || !m_Data.originalProcedure ||
                !std::isfinite(region.x) || !std::isfinite(region.y) || !std::isfinite(region.width) ||
                !std::isfinite(region.height) || region.width < 0 || region.height <= 0 ||
                std::abs(region.x) > 1e6f || std::abs(region.y) > 1e6f || region.width > 1e6f ||
                region.height > 1e6f) {
                error = "Invalid or unavailable Windows IME target";
                return false;
            }
            if (m_Data.imeOwner && m_Data.imeOwner != owner)
                ClearTextInputTarget(m_Data.imeOwner);
            m_Data.imeOwner = owner;
            m_Data.compositionCallback = std::move(callback);
            const auto hwnd = glfwGetWin32Window(m_Window);
            const auto context = ImmGetContext(hwnd);
            if (!context) {
                ClearTextInputTarget(owner);
                error = "Windows IME context is unavailable";
                return false;
            }
            COMPOSITIONFORM composition{};
            composition.dwStyle = CFS_FORCE_POSITION;
            composition.ptCurrentPos = {LONG(region.x), LONG(region.y)};
            CANDIDATEFORM candidate{};
            candidate.dwStyle = CFS_CANDIDATEPOS;
            candidate.ptCurrentPos = {LONG(region.x), LONG(region.y + region.height)};
            int logicalWidth = 0, logicalHeight = 0;
            glfwGetWindowSize(m_Window, &logicalWidth, &logicalHeight);
            RECT client{};
            GetClientRect(hwnd, &client);
            const float sx = logicalWidth > 0 ? float(client.right - client.left) / logicalWidth : 1;
            const float sy = logicalHeight > 0 ? float(client.bottom - client.top) / logicalHeight : 1;
            composition.ptCurrentPos = {LONG(region.x * sx), LONG(region.y * sy)};
            candidate.ptCurrentPos = {LONG(region.x * sx), LONG((region.y + region.height) * sy)};
            const bool compositionApplied = ImmSetCompositionWindow(context, &composition) != FALSE;
            const bool candidateApplied = ImmSetCandidateWindow(context, &candidate) != FALSE;
            const bool applied = compositionApplied && candidateApplied;
            ImmReleaseContext(hwnd, context);
            if (!applied) {
                ClearTextInputTarget(owner);
                error = "Cannot position Windows IME composition/candidate window";
                return false;
            }
            error.clear();
            return true;
        }
        void WindowsWindow::ClearTextInputTarget(uint64_t owner) {
            if (!owner)
                owner = m_Data.imeOwner; // 应用关闭阶段显式清理任一最后的文本目标。
            if (!owner || m_Data.imeOwner != owner)
                return;
            auto callback = std::move(m_Data.compositionCallback);
            const bool composing = m_Data.composing;
            m_Data.composing = false;
            m_Data.imeOwner = 0;
            if (composing) {
                const auto hwnd = glfwGetWin32Window(m_Window);
                const auto context = ImmGetContext(hwnd);
                if (context) {
                    ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
                    ImmReleaseContext(hwnd, context);
                }
                if (callback)
                    try {
                        callback({TextComposition::Phase::Cancel});
                    } catch (...) {
                        CAT_CORE_WARN("IME cancellation callback failed");
                    }
            }
        }
}
