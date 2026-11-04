outputdir = "%{cfg.system}-%{cfg.architecture}/%{cfg.buildcfg}"
local vulkanSDK = os.getenv("VULKAN_SDK")

IncludeDir = {}
IncludeDir["GLFW"] = "Catalpa/third_party/glfw/include"
IncludeDir["GLAD"] = "Catalpa/third_party/glad/include"
IncludeDir["imgui"] = "Catalpa/third_party/imgui"
IncludeDir["VulkanSDK"] = path.join(vulkanSDK, "Include")
IncludeDir["VMA"] = "Catalpa/third_party/VulkanMemoryAllocator/include"
IncludeDir["JSON"] = "Catalpa/third_party/json/include"
IncludeDir["GLM"] = "Catalpa/third_party/glm"
IncludeDir["EnTT"] = "Catalpa/third_party/entt/src"

-- 工程生成前检查子模块、生成的 GLAD 和随源码提供的头文件依赖。
local requiredDependencies = {
    "Catalpa/third_party/glfw/include/GLFW/glfw3.h",
    "Catalpa/third_party/spdlog/include/spdlog/spdlog.h",
    "Catalpa/third_party/imgui/imgui.h",
    "Catalpa/third_party/glad/include/glad/vulkan.h",
    "Catalpa/third_party/glad/include/vk_platform.h",
    "Catalpa/third_party/glad/src/vulkan.c",
    "Catalpa/third_party/VulkanMemoryAllocator/include/vk_mem_alloc.h",
    "Catalpa/third_party/json/include/nlohmann/json.hpp",
    "Catalpa/third_party/glm/glm/glm.hpp",
    "Catalpa/third_party/glm/glm/detail/setup.hpp",
    "Catalpa/third_party/entt/src/entt/entity/registry.hpp",
    "Catalpa/third_party/entt/src/entt/config/version.h"
}
for _, dependency in ipairs(requiredDependencies) do
    if not os.isfile(path.join(_MAIN_SCRIPT_DIR, dependency)) then
        error("Missing dependency: " .. dependency ..
            ". Restore the listed dependency file before generating projects.")
    end
end

workspace "CatalpaEngine"
    configurations { "Debug", "Release", "Dist" }
    architecture "x86_64"
    -- CatalpaBox 是当前默认启动沙盒；功能库不依赖这个宿主。
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
        "%{IncludeDir.GLM}",
        "%{IncludeDir.EnTT}",
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


filter {}
project "CatalpaBox"
    location "CatalpaBox"
    kind "ConsoleApp"

    files {
        "CatalpaBox/src/**.h",
        "CatalpaBox/src/**.cpp",
        -- 以公共头作为部署构建项，DLL 是额外输入，不编译此头。
        "Catalpa/src/Catalpa.h",
        "Catalpa/Resources/Localization/*.json",
        "Config/InputProfiles.json"
    }

    includedirs {
        "Catalpa/third_party/spdlog/include",
        "Catalpa/src",
        "Catalpa/third_party",
        "CatalpaEditor/src",
        "%{IncludeDir.GLM}"
    }

    links {
        "Catalpa",
        "CatalpaEditor"
    }

    -- 开发配置调用验证库；Dist 的入口由现有 CATALPA_DIST 分支禁用。
    filter "configurations:Debug or Release"
        includedirs { "Tests" }
        links { "Tests" }
    filter {}

    filter "system:windows"
        defines {
            "CATALPA_PLATFORM_WINDOW"
        }

    -- 消费方部署引擎 DLL；输入/输出跟踪覆盖只改 DLL 的增量构建。
    filter { "system:windows", "files:**/Catalpa.h" }
        buildmessage "Deploying Catalpa runtime"
        buildinputs {
            path.join(_MAIN_SCRIPT_DIR, "bin/" .. outputdir .. "/Catalpa/Catalpa.dll")
        }
        buildcommands {
            '{MKDIR} "%{cfg.targetdir}"',
            'if errorlevel 1 exit /b 1',
            '{COPYFILE} "' .. path.join(_MAIN_SCRIPT_DIR, "bin/" .. outputdir .. '/Catalpa/Catalpa.dll') .. '" "%{cfg.targetdir}/Catalpa.dll"',
            'if errorlevel 1 exit /b 1'
        }
        buildoutputs { "%{cfg.targetdir}/Catalpa.dll" }
        compilebuildoutputs(false)
        linkbuildoutputs(false)

    filter { "system:windows", "files:**/Localization/*.json" }
        buildmessage "Copying workspace language pack"
        -- 当前宿主部署工作区资源；命令 token 与错误检查保持分行。
        buildcommands {
            '{MKDIR} "%{cfg.targetdir}/Resources/Localization"',
            'if errorlevel 1 exit /b 1',
            '{COPYFILE} "%{file.abspath}" "%{cfg.targetdir}/Resources/Localization/%{file.name}"',
            'if errorlevel 1 exit /b 1'
        }
        buildoutputs {
            "%{cfg.targetdir}/Resources/Localization/%{file.name}"
        }

    -- 当前宿主部署输入配置；Application 按可执行目录读取，不依赖工作目录。
    filter { "system:windows", "files:**/InputProfiles.json" }
        buildmessage "Copying input action profile"
        buildcommands {
            '{MKDIR} "%{cfg.targetdir}/Resources/Config"',
            'if errorlevel 1 exit /b 1',
            '{COPYFILE} "%{file.abspath}" "%{cfg.targetdir}/Resources/Config/InputProfiles.json"',
            'if errorlevel 1 exit /b 1'
        }
        buildoutputs { "%{cfg.targetdir}/Resources/Config/InputProfiles.json" }

filter {}
project "CatalpaEditor"
    location "CatalpaEditor"
    -- 独立可见的功能库，当前由 Box 挂载，不提供产品 main。
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
        "%{IncludeDir.JSON}",
        "%{IncludeDir.GLM}"
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

-- 开发验证功能库：只消费引擎公共接口，不提供进程入口或沙盒实现。
filter {}
project "Tests"
    location "Tests"
    kind "StaticLib"

    files {
        "Tests/**.h",
        "Tests/**.cpp"
    }

    includedirs {
        "Tests",
        "Catalpa/src",
        "Catalpa/third_party/spdlog/include",
        "%{IncludeDir.GLM}"
    }

    links { "Catalpa" }

    filter "system:windows"
        -- Tests 是 Catalpa.dll 的消费者，不定义 CATALPA_BUILD_DLL。
        defines { "CATALPA_PLATFORM_WINDOW" }

    filter "configurations:Dist"
        excludefrombuild "On"

filter {}
