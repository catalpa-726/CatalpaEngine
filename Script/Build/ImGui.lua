local engineRoot = _MAIN_SCRIPT_DIR
local imguiRoot = path.join(engineRoot, "Catalpa/third_party/imgui")

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
