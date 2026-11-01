local engineRoot = _MAIN_SCRIPT_DIR
local glfwRoot = path.join(engineRoot, "Catalpa/third_party/glfw")

project "GLFW"
    kind "StaticLib"
    language "C"
    staticruntime "Off"
    warnings "off"

    location (path.join(engineRoot, "out/projects/GLFW"))
    targetdir (path.join(engineRoot, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(engineRoot, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files {
        path.join(glfwRoot, "include/GLFW/glfw3.h"),
        path.join(glfwRoot, "include/GLFW/glfw3native.h"),
        path.join(glfwRoot, "src/context.c"),
        path.join(glfwRoot, "src/init.c"),
        path.join(glfwRoot, "src/input.c"),
        path.join(glfwRoot, "src/monitor.c"),
        path.join(glfwRoot, "src/null_init.c"),
        path.join(glfwRoot, "src/null_joystick.c"),
        path.join(glfwRoot, "src/null_monitor.c"),
        path.join(glfwRoot, "src/null_window.c"),
        path.join(glfwRoot, "src/platform.c"),
        path.join(glfwRoot, "src/vulkan.c"),
        path.join(glfwRoot, "src/window.c")
    }

    filter "system:windows"
        systemversion "latest"
        files {
            path.join(glfwRoot, "src/win32_init.c"),
            path.join(glfwRoot, "src/win32_joystick.c"),
            path.join(glfwRoot, "src/win32_module.c"),
            path.join(glfwRoot, "src/win32_monitor.c"),
            path.join(glfwRoot, "src/win32_time.c"),
            path.join(glfwRoot, "src/win32_thread.c"),
            path.join(glfwRoot, "src/win32_window.c"),
            path.join(glfwRoot, "src/wgl_context.c"),
            path.join(glfwRoot, "src/egl_context.c"),
            path.join(glfwRoot, "src/osmesa_context.c")
        }
        defines {
            "_GLFW_WIN32",
            "_CRT_SECURE_NO_WARNINGS"
        }

    filter "configurations:Debug"
        runtime "DEBUG"
        symbols "on"

    filter { "system:windows", "configurations:Debug-AS" }
        runtime "Debug"
        symbols "on"
        sanitize { "Address" }
        runtimechecks "Off"
        incrementallink "Off"

    filter "configurations:Release"
        runtime "Release"
        optimize "speed"

    filter {}
