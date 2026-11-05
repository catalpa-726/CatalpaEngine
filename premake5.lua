outputdir = "%{cfg.system}-%{cfg.architecture}/%{cfg.buildcfg}"
local vulkanSDK = os.getenv("VULKAN_SDK")
if not vulkanSDK or vulkanSDK == "" then error("VULKAN_SDK is required for the locked shader toolchain.") end
for _, tool in ipairs { "Bin/slangc.exe", "Bin/slang-compiler.dll", "Bin/spirv-val.exe" } do
    if not os.isfile(path.join(vulkanSDK, tool)) then error("Missing SDK tool: " .. path.join(vulkanSDK, tool)) end
end

IncludeDir = {}
IncludeDir["GLFW"] = "Catalpa/third_party/glfw/include"
IncludeDir["GLAD"] = "Catalpa/third_party/glad/include"
IncludeDir["imgui"] = "Catalpa/third_party/imgui"
IncludeDir["VulkanSDK"] = path.join(vulkanSDK, "Include")
IncludeDir["VMA"] = "Catalpa/third_party/VulkanMemoryAllocator/include"
IncludeDir["JSON"] = "Catalpa/third_party/json/include"
IncludeDir["GLM"] = "Catalpa/third_party/glm"
IncludeDir["EnTT"] = "Catalpa/third_party/entt/src"
IncludeDir["EnkiTS"] = "Catalpa/third_party/enkiTS/src"
IncludeDir["SPIRVReflect"] = "Catalpa/third_party/SPIRV-Reflect"
-- S05/S06源资产解析器只由Tools消费，不向Catalpa Runtime增加依赖。
IncludeDir["fastgltf"] = "Catalpa/third_party/fastgltf/include"
IncludeDir["simdjson"] = "Catalpa/third_party/simdjson/singleheader"
IncludeDir["simdutf"] = "Catalpa/third_party/simdutf"
IncludeDir["stb"] = "Catalpa/third_party/stb"
IncludeDir["MikkTSpace"] = "Catalpa/third_party/MikkTSpace"
IncludeDir["assimp"] = "Catalpa/third_party/assimp/include"
IncludeDir["AssimpConfig"] = "out/generated/Assimp/include"
IncludeDir["AssimpZlib"] = "Catalpa/third_party/assimp/contrib/zlib"
IncludeDir["AssimpZlibConfig"] = "out/generated/Assimp/zlib"

-- 工程生成前检查外部源码依赖和生成的 GLAD；获取版本见 Dependencies.lock.json。
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
    "Catalpa/third_party/entt/src/entt/config/version.h",
    "Catalpa/third_party/enkiTS/src/TaskScheduler.cpp",
    "Catalpa/third_party/enkiTS/src/TaskScheduler.h",
    "Catalpa/third_party/enkiTS/src/LockLessMultiReadPipe.h",
    "Catalpa/third_party/SPIRV-Reflect/spirv_reflect.c",
    "Catalpa/third_party/SPIRV-Reflect/spirv_reflect.h",
    "Catalpa/third_party/SPIRV-Reflect/include/spirv/unified1/spirv.h",
    "Catalpa/third_party/fastgltf/src/parser.cpp",
    "Catalpa/third_party/fastgltf/include/fastgltf/core.hpp",
    "Catalpa/third_party/simdjson/singleheader/simdjson.cpp",
    "Catalpa/third_party/simdjson/singleheader/simdjson.h",
    "Catalpa/third_party/simdutf/simdutf.cpp",
    "Catalpa/third_party/simdutf/simdutf.h",
    "Catalpa/third_party/stb/stb_image.h",
    "Catalpa/third_party/MikkTSpace/mikktspace.c",
    "Catalpa/third_party/MikkTSpace/mikktspace.h",
    "Catalpa/third_party/assimp/CMakeLists.txt",
    "Catalpa/third_party/assimp/include/assimp/config.h.in",
    "Catalpa/third_party/assimp/include/assimp/revision.h.in",
    "Catalpa/third_party/assimp/contrib/zlib/zconf.h.cmakein"
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
include "Script/Build/EnkiTS.lua"
-- SPIR-V反射由Tools私有消费；Runtime只读取cooked ShaderAsset。
include "Script/Build/SPIRVReflect.lua"
-- 工具依赖均由Premake直接编译；Assimp的派生头也由Lua生成。
include "Script/Build/AssetImport.lua"
include "Script/Build/Assimp.lua"
filter {}


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
        "%{IncludeDir.EnkiTS}",
    }

    links { 
        "GLFW",
        "GLAD",
        "imgui",
        "enkiTS",
    }

    include "Script/Build/Slang.lua"

    filter "system:windows"
        -- 系统随机源与Unicode命令行；系统库，不引入第三方源码。
        links { "bcrypt", "shell32" }
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
        "Config/InputProfiles.json",
        "Config/RendererProfiles.json"
    }

    includedirs {
        "Catalpa/third_party/spdlog/include",
        "Catalpa/src",
        "Catalpa/third_party",
        "CatalpaEditor/src",
        "Tools",
        "%{IncludeDir.GLM}"
    }

    links {
        "Catalpa",
        "CatalpaEditor",
        "Tools",
        -- 最终宿主显式链接工具静态闭包，避免依赖StaticLib传递链接的差异。
        "fastgltf", "simdjson", "simdutf", "MikkTSpace", "assimp", "AssimpZlib"
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

    -- 当前宿主部署运行配置；Application 按可执行目录读取，不依赖工作目录。
    filter { "system:windows", "files:Config/*.json" }
        buildmessage "Copying runtime configuration"
        buildcommands {
            '{MKDIR} "%{cfg.targetdir}/Resources/Config"',
            'if errorlevel 1 exit /b 1',
            '{COPYFILE} "%{file.abspath}" "%{cfg.targetdir}/Resources/Config/%{file.name}"',
            'if errorlevel 1 exit /b 1'
        }
        buildoutputs { "%{cfg.targetdir}/Resources/Config/%{file.name}" }

    filter {}
    CatalpaShaderBuild.AttachHostCook()

-- 共享作者工具核心：项目配置、Shader编译/校验/反射/Cook及命令协议；宿主只转交，不提供main。
filter {}
project "Tools"
    location "Tools"
    kind "StaticLib"

    files { "Tools/**.h", "Tools/**.cpp" }
    -- 导入/Cook的JSON适配及领域数学头由Tools直接消费，沿用现有依赖路径与ABI。
    includedirs { "Tools", "Catalpa/src", "%{IncludeDir.SPIRVReflect}",
        "%{IncludeDir.JSON}", "%{IncludeDir.GLM}",
        "%{IncludeDir.fastgltf}", "%{IncludeDir.simdjson}", "%{IncludeDir.stb}",
        "%{IncludeDir.MikkTSpace}", "%{IncludeDir.assimp}", "%{IncludeDir.AssimpConfig}",
        "%{IncludeDir.AssimpZlib}", "%{IncludeDir.AssimpZlibConfig}" }
    -- 与fastgltf静态实现的公开数值ABI保持一致；stb实现由后续唯一TextureImporter提供。
    defines { "FASTGLTF_USE_64BIT_FLOAT=0" }
    -- 反射是Tools的私有实现依赖；最终宿主通过项目引用链接所需静态实现。
    links { "Catalpa", "SPIRV-Reflect", "fastgltf", "MikkTSpace", "assimp", "AssimpZlib" }

    filter "system:windows"
        defines { "CATALPA_PLATFORM_WINDOW" }

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
        "Tools",
        "%{IncludeDir.imgui}",
        "%{IncludeDir.JSON}",
        "%{IncludeDir.GLM}"
    }

    links {
        "Catalpa",
        "Tools"
    }

    filter "system:windows"
        defines {
            "CATALPA_PLATFORM_WINDOW",
            "CATALPA_ENABLE_ASSERTS",
            -- 只调用 Catalpa.dll 中的唯一 ImGui 实现。
            "IMGUI_API=__declspec(dllimport)"
        }

-- 开发验证功能库：消费引擎、Tools及Editor会话接口，不提供进程入口或沙盒实现。
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
        "Tools",
        "CatalpaEditor/src",
        "Catalpa/src",
        "Catalpa/third_party/spdlog/include",
        "%{IncludeDir.GLM}"
    }

    links { "Catalpa", "Tools", "CatalpaEditor" }

    filter "system:windows"
        -- Fixture按__FILE__定位源根；/FC保证从任意cwd运行也得到绝对路径。
        buildoptions { "/FC" }
        -- Tests 是 Catalpa.dll 的消费者，不定义 CATALPA_BUILD_DLL。
        defines { "CATALPA_PLATFORM_WINDOW" }

    filter "configurations:Dist"
        excludefrombuild "On"

filter {}
