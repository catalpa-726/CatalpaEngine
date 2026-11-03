outputdir = "%{cfg.system}-%{cfg.architecture}/%{cfg.buildcfg}"
local vulkanSDK = os.getenv("VULKAN_SDK")

IncludeDir = {}
IncludeDir["GLFW"] = "Catalpa/third_party/glfw/include"
IncludeDir["GLAD"] = "Catalpa/third_party/glad/include"
IncludeDir["imgui"] = "Catalpa/third_party/imgui"
IncludeDir["VulkanSDK"] = path.join(vulkanSDK, "Include")
IncludeDir["VMA"] = "Catalpa/third_party/VulkanMemoryAllocator/include"
IncludeDir["JSON"] = "Catalpa/third_party/json/include"

-- 工程生成前检查子模块、生成的 GLAD 和随源码提供的头文件依赖。
local requiredDependencies = {
    "Catalpa/third_party/glfw/include/GLFW/glfw3.h",
    "Catalpa/third_party/spdlog/include/spdlog/spdlog.h",
    "Catalpa/third_party/imgui/imgui.h",
    "Catalpa/third_party/glad/include/glad/vulkan.h",
    "Catalpa/third_party/glad/include/vk_platform.h",
    "Catalpa/third_party/glad/src/vulkan.c",
    "Catalpa/third_party/VulkanMemoryAllocator/include/vk_mem_alloc.h",
    "Catalpa/third_party/json/include/nlohmann/json.hpp"
}
for _, dependency in ipairs(requiredDependencies) do
    if not os.isfile(path.join(_MAIN_SCRIPT_DIR, dependency)) then
        error("Missing dependency: " .. dependency ..
            ". Follow the dependency preparation steps in README.md.")
    end
end

workspace "CatalpaEngine"
    configurations { "Debug", "Release", "Dist" }
    architecture "x86_64"
    -- 未经用户明确说明，CatalpaBox 是唯一启动应用。
    startproject "CatalpaBox"

    system "windows"
    language "C++"
    cppdialect "C++20"
    systemversion "latest"

    characterset "Unicode"

    staticruntime "Off"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    filter "action:vs2022"
        toolset "msc-v143"
        buildoptions { "/utf-8" }

    filter "configurations:Debug"
        defines { "CATALPA_DEBUG" }
        runtime "Debug"
        symbols "On"
        optimize "Off"

    filter "configurations:Release"
        defines { "CATALPA_RELEASE" }
        runtime "Release"
        symbols "On"
        optimize "Speed"
    
    filter "configurations:Dist"
		defines { "CATALPA_DIST" }
        runtime "Debug"
        symbols "On"
        optimize "Off"


filter {}
include "Script/Build/ImGui.lua"
include "Script/Build/GLFW.lua"
include "Script/Build/GLAD.lua"


project "Catalpa"
    location "Catalpa"
    kind "SharedLib"

    pchheader "catpch.h"
	pchsource "Catalpa/src/catpch.cpp"

    files {
        "Catalpa/src/**.h",
        "Catalpa/src/**.hpp",
        "Catalpa/src/**.cpp"
    }

    includedirs { 
        "Catalpa/src",
        "%{prj.name}/third_party/spdlog/include",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.GLAD}",
        "%{IncludeDir.imgui}",
        "%{IncludeDir.VulkanSDK}",
        "%{IncludeDir.VMA}",
        "%{IncludeDir.JSON}",
    }

    links { 
        "GLFW",
        "GLAD",
        "imgui",
    }

    include "Script/Build/Slang.lua"

    filter "system:windows"
		defines {
			"CATALPA_BUILD_DLL",
            "CATALPA_PLATFORM_WINDOW",
            "GLFW_INCLUDE_NONE",
            "CATALPA_ENABLE_ASSERTS",
            "VK_NO_PROTOTYPES",
            "IMGUI_IMPL_VULKAN_NO_PROTOTYPES",
            "VMA_VULKAN_VERSION=1003000",
            "VMA_STATIC_VULKAN_FUNCTIONS=0",
            "VMA_DYNAMIC_VULKAN_FUNCTIONS=0",
            "VMA_BUFFER_DEVICE_ADDRESS=0"
		}

		postbuildcommands {
            ("{MKDIR} ../bin/" .. outputdir .. "/CatalpaBox"),
            ("{COPYFILE} %{cfg.buildtarget.relpath} ../bin/" ..outputdir .. "/CatalpaBox/Catalpa.dll"),
        }


filter {}
project "CatalpaBox"
    location "CatalpaBox"
    kind "ConsoleApp"

    files {
        "CatalpaBox/src/**.h",
        "CatalpaBox/src/**.cpp",
        "Catalpa/Resources/Localization/*.json"
    }

    includedirs {
        "Catalpa/third_party/spdlog/include",
        "Catalpa/src",
        "Catalpa/third_party",
        "CatalpaEditor/src"
    }

    links {
        "Catalpa",
        "CatalpaEditor"
    }

    filter "system:windows"
        defines {
            "CATALPA_PLATFORM_WINDOW"
        }

    filter { "system:windows", "files:**/Localization/*.json" }
        buildmessage "Copying workspace language pack"
        -- 运行资源归唯一宿主 Box；命令 token 与错误检查分行，避免 /b 被当作路径。
        buildcommands {
            '{MKDIR} "%{cfg.targetdir}/Resources/Localization"',
            'if errorlevel 1 exit /b 1',
            '{COPYFILE} "%{file.abspath}" "%{cfg.targetdir}/Resources/Localization/%{file.name}"',
            'if errorlevel 1 exit /b 1'
        }
        buildoutputs {
            "%{cfg.targetdir}/Resources/Localization/%{file.name}"
        }

filter {}
project "CatalpaEditor"
    location "CatalpaEditor"
    -- 独立可见的功能库，由 CatalpaBox 链接，不提供 main 或第二个启动程序。
    kind "StaticLib"

    files {
        "CatalpaEditor/src/**.h",
        "CatalpaEditor/src/**.cpp"
    }

    includedirs {
        "Catalpa/third_party/spdlog/include",
        "Catalpa/src",
        "CatalpaEditor/src",
        "%{IncludeDir.imgui}",
        "%{IncludeDir.JSON}"
    }

    links {
        "Catalpa"
    }

    filter "system:windows"
        defines {
            "CATALPA_PLATFORM_WINDOW",
            "CATALPA_ENABLE_ASSERTS",
            -- 只调用 Catalpa.dll 中的唯一 ImGui 实现。
            "IMGUI_API=__declspec(dllimport)"
        }

filter {}
