local engineRoot = _MAIN_SCRIPT_DIR
local gladRoot = path.join(engineRoot, "Catalpa/third_party/glad")

project "GLAD"
    kind "StaticLib"
    language "C"
    staticruntime "Off"

    location (path.join(engineRoot, "out/projects/GLAD"))
    targetdir (path.join(engineRoot, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(engineRoot, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files {
        path.join(gladRoot, "include/glad/vulkan.h"),
        path.join(gladRoot, "include/vk_platform.h"),
        path.join(gladRoot, "src/vulkan.c")
    }

    includedirs {
        path.join(gladRoot, "include")
    }

    filter {}
