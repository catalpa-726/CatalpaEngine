--[[
编译 Tools 使用的 SPIR-V 反射静态库；不构建上游 CLI、示例或测试。
使用随包的 SPIR-V 公共头，不链接 Vulkan 或向 Runtime 暴露工具依赖。
]]
local engineRoot = _MAIN_SCRIPT_DIR
local reflectRoot = path.join(engineRoot, "Catalpa/third_party/SPIRV-Reflect")

project "SPIRV-Reflect"
    kind "StaticLib"
    language "C"
    cdialect "C11"
    staticruntime "Off"

    location (path.join(engineRoot, "out/projects/SPIRV-Reflect"))
    targetdir (path.join(engineRoot, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(engineRoot, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files {
        path.join(reflectRoot, "spirv_reflect.c"),
        path.join(reflectRoot, "spirv_reflect.h"),
        path.join(reflectRoot, "include/spirv/unified1/spirv.h")
    }

    includedirs {
        reflectRoot
    }

    filter {}
