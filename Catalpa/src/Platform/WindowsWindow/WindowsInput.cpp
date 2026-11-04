#include "catpch.h"
#include "WindowsInput.h"
#include "Core/Window.h"
#include <GLFW/glfw3.h>

namespace Catalpa {

    // 保留旧物理查询接口；窗口借用由宿主绑定，不再反向查询 Application 单例。
    static WindowsInput s_WindowsInput;
    Input* Input::s_Instance = &s_WindowsInput;
    Window* Input::s_Window = nullptr;

    void Input::BindWindow(Window* window) { s_Window = window; }

    bool WindowsInput::IsKeyPressedImpl(int keycode)
    {
        Window* bound = GetBoundWindow();
        if (!bound || keycode < GLFW_KEY_SPACE || keycode > GLFW_KEY_LAST) return false;
        auto* window = static_cast<GLFWwindow*>(bound->GetNativeWindow());
        if (!window || glfwGetWindowAttrib(window, GLFW_FOCUSED) != GLFW_TRUE) return false;
        // glfwGetKey 只返回 PRESS/RELEASE；REPEAT 仅是按键回调事件。
        return glfwGetKey(window, keycode) == GLFW_PRESS;
    }

    bool WindowsInput::IsMouseButtonPressedImpl(int button)
    {
        Window* bound = GetBoundWindow();
        if (!bound || button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
        auto* window = static_cast<GLFWwindow*>(bound->GetNativeWindow());
        return window && glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE
            && glfwGetMouseButton(window, button) == GLFW_PRESS;
    }

    std::pair<float, float> WindowsInput::GetMousePostionImpl()
    {
        Window* bound = GetBoundWindow();
        if (!bound) return {};
        auto* window = static_cast<GLFWwindow*>(bound->GetNativeWindow());
        if (!window || glfwGetWindowAttrib(window, GLFW_FOCUSED) != GLFW_TRUE) return {};
        double x = 0.0, y = 0.0;
        glfwGetCursorPos(window, &x, &y);
        if (!std::isfinite(x) || !std::isfinite(y)
            || std::abs(x) > std::numeric_limits<float>::max() || std::abs(y) > std::numeric_limits<float>::max()) return {};
        return { static_cast<float>(x), static_cast<float>(y) };
    }

    float WindowsInput::GetMouseXImpl() { return GetMousePostionImpl().first; }
    float WindowsInput::GetMouseYImpl() { return GetMousePostionImpl().second; }
}
