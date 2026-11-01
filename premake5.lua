outputdir = "%{cfg.system}-%{cfg.architecture}/%{cfg.buildcfg}"
local vulkanSDK = os.getenv("VULKAN_SDK")
IncludeDir = {}
IncludeDir["GLFW"] = "Catalpa/third_party/glfw/include"
IncludeDir["GLAD"] = "Catalpa/third_party/glad/include"
IncludeDir["imgui"] = "Catalpa/third_party/imgui"
-- IncludeDir["VulkanSDK"] = path.join(vulkanSDK, "Include")

-- Submodules and generated GLAD files must be available before project generation.
local requiredDependencies = {
    "Catalpa/third_party/glfw/include/GLFW/glfw3.h",
    "Catalpa/third_party/spdlog/include/spdlog/spdlog.h",
    "Catalpa/third_party/imgui/imgui.h",
    "Catalpa/third_party/glad/include/glad/vulkan.h",
    "Catalpa/third_party/glad/include/vk_platform.h",
    "Catalpa/third_party/glad/src/vulkan.c"
}
for _, dependency in ipairs(requiredDependencies) do
    if not os.isfile(path.join(_MAIN_SCRIPT_DIR, dependency)) then
        error("Missing dependency: " .. dependency ..
            ". Initialize submodules and generate GLAD as described in README.md.")
    end
end

workspace "CatalpaEngine"
    configurations { "Debug", "Release", "Dist" }
    architecture "x86_64"
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
        -- "%{IncludeDir.VulkanSDK}"
    }

    links { 
        "GLFW",
        "GLAD",
        "imgui",
    }

    filter "system:windows"
		defines {
			"CATALPA_BUILD_DLL",
            "CATALPA_PLATFORM_WINDOW",
            "GLFW_INCLUDE_NONE",
            "CATALPA_ENABLE_ASSERTS",
            -- "VK_NO_PROTOTYPES",
            -- "IMGUI_IMPL_VULKAN_NO_PROTOTYPES"
		}

		postbuildcommands {
            ("{MKDIR} ../bin/" .. outputdir .. "/CatalpaBox"),
            ("{COPYFILE} %{cfg.buildtarget.relpath} ../bin/" ..outputdir .. "/CatalpaBox/Catalpa.dll")
        }


    
project "CatalpaBox"
    location "CatalpaBox"
    kind "ConsoleApp"

    files {
        "CatalpaBox/src/**.h",
        "CatalpaBox/src/**.cpp"
    }

    includedirs {
        "Catalpa/third_party/spdlog/include",
        "Catalpa/src",
        "Catalpa/third_party"
    }

    links { 
        "Catalpa" 
    }

    filter "system:windows"
		defines {
            "CATALPA_PLATFORM_WINDOW"
		}
