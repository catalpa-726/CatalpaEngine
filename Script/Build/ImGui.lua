local engineRoot = _MAIN_SCRIPT_DIR
local imguiRoot = path.join(engineRoot, "Catalpa/third_party/imgui")
local gizmoRoot = path.join(engineRoot, "Catalpa/third_party/ImGuizmo")

project "imgui"
    kind "StaticLib"
    language "C++"
    staticruntime "off"

    location (path.join(engineRoot, "out/projects/imgui"))
    targetdir (path.join(engineRoot, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(engineRoot, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files {
        path.join(imguiRoot, "imconfig.h"),
        path.join(imguiRoot, "imgui.h"),
        path.join(imguiRoot, "imgui.cpp"),
        path.join(imguiRoot, "imgui_draw.cpp"),
        path.join(imguiRoot, "imgui_internal.h"),
        path.join(imguiRoot, "imgui_tables.cpp"),
        path.join(imguiRoot, "imgui_widgets.cpp"),
        path.join(imguiRoot, "imstb_rectpack.h"),
        path.join(imguiRoot, "imstb_textedit.h"),
        path.join(imguiRoot, "imstb_truetype.h"),
        path.join(imguiRoot, "imgui_demo.cpp")
    }

    defines {
        "IMGUI_API=__declspec(dllexport)"
    }

    filter "system:windows"
        systemversion "latest"
        cppdialect "C++20"

    filter "system:linux"
        pic "On"
        systemversion "latest"
        cppdialect "C++20"

    filter {}

--[[
ImGuizmo 与唯一的 ImGui 实现共用 Catalpa.dll；Editor 仅导入并消费编辑意图。
由根脚本在 Catalpa 项目中调用，不创建新目标，也不编译上游示例或其他控件。
]]
CatalpaImGuiBuild = {}

function CatalpaImGuiBuild.AttachGizmoImplementation()
    filter {}

    files {
        path.join(gizmoRoot, "src/ImGuizmo.h"),
        path.join(gizmoRoot, "src/ImGuizmo.cpp")
    }

    -- 上游自己定义 ImGui 数学运算宏，不注入引擎 PCH，也不修改原始文件。
    filter "files:**/ImGuizmo.cpp"
        enablepch "Off"

    filter { "system:windows", "files:**/ImGuizmo.cpp" }
        defines { "IMGUI_API=__declspec(dllexport)" }

    filter {}
end
