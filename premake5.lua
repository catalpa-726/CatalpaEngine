newoption { trigger = "runtime-only", description = "Build the existing Box host without Editor, Tools or Tests" }
newoption { trigger = "no-sandbox", description = "Build libraries without the default Box host" }
local buildBox = _OPTIONS["no-sandbox"] == nil
local runtimeOnly = _OPTIONS["runtime-only"] ~= nil
CATALPA_RUNTIME_ONLY = runtimeOnly

outputdir = "%{cfg.system}-%{cfg.architecture}/%{cfg.buildcfg}"
local vulkanSDK = os.getenv("VULKAN_SDK") or ""
if not runtimeOnly then
if not vulkanSDK or vulkanSDK == "" then error("VULKAN_SDK is required for the locked shader toolchain.") end
for _, tool in ipairs { "Bin/slangc.exe", "Bin/slang-compiler.dll", "Bin/spirv-val.exe" } do
    if not os.isfile(path.join(vulkanSDK, tool)) then error("Missing SDK tool: " .. path.join(vulkanSDK, tool)) end
end

end

IncludeDir = {}
IncludeDir["GLFW"] = "Catalpa/third_party/glfw/include"
IncludeDir["GLAD"] = "Catalpa/third_party/glad/include"
IncludeDir["imgui"] = "Catalpa/third_party/imgui"
IncludeDir["ImGuizmo"] = "Catalpa/third_party/ImGuizmo/src"
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
IncludeDir["NodeEditor"] = "Catalpa/third_party/imgui-node-editor"
IncludeDir["NativeFileDialog"] = "Catalpa/third_party/nativefiledialog-extended/src/include"
-- Lua 官方发行包使用 src/，官方 Git 镜像把源码放在根目录。
IncludeDir["Lua"] = "Catalpa/third_party/lua/src"
if not os.isfile(path.join(_MAIN_SCRIPT_DIR, IncludeDir.Lua, "lua.h")) then
    IncludeDir["Lua"] = "Catalpa/third_party/lua"
end

-- 工程生成前检查外部源码依赖和生成的 GLAD；获取版本见 Dependencies.lock.json。
local requiredDependencies = {
    "Catalpa/third_party/glfw/include/GLFW/glfw3.h",
    "Catalpa/third_party/spdlog/include/spdlog/spdlog.h",
    "Catalpa/third_party/imgui/imgui.h",
    "Catalpa/third_party/ImGuizmo/src/ImGuizmo.h",
    "Catalpa/third_party/ImGuizmo/src/ImGuizmo.cpp",
    "Catalpa/third_party/ImGuizmo/LICENSE",
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
    "Catalpa/third_party/assimp/contrib/zlib/zconf.h.cmakein",
    "Catalpa/third_party/imgui-node-editor/imgui_node_editor.h",
    "Catalpa/third_party/imgui-node-editor/imgui_node_editor.cpp",
    "Catalpa/third_party/imgui-node-editor/imgui_node_editor_api.cpp",
    "Catalpa/third_party/imgui-node-editor/imgui_canvas.cpp",
    "Catalpa/third_party/imgui-node-editor/crude_json.cpp",
    "Catalpa/third_party/nativefiledialog-extended/src/include/nfd.h",
    "Catalpa/third_party/nativefiledialog-extended/src/nfd_win.cpp",
    path.join(IncludeDir.Lua, "lua.h"),
    path.join(IncludeDir.Lua, "luaconf.h"),
    path.join(IncludeDir.Lua, "lapi.c"),
    path.join(IncludeDir.Lua, "lauxlib.c"),
    path.join(IncludeDir.Lua, "linit.c")
}
for _, dependency in ipairs(requiredDependencies) do
    local runtimeDependency = dependency:find("/glfw/",1,true) or dependency:find("/spdlog/",1,true)
        or dependency:find("/imgui/",1,true) or dependency:find("/glad/",1,true)
        or dependency:find("/VulkanMemoryAllocator/",1,true) or dependency:find("/json/",1,true)
        or dependency:find("/glm/",1,true) or dependency:find("/entt/",1,true)
        or dependency:find("/enkiTS/",1,true) or dependency:find("/lua/",1,true)
    if (not runtimeOnly or runtimeDependency) and not os.isfile(path.join(_MAIN_SCRIPT_DIR, dependency)) then
        error("Missing dependency: " .. dependency ..
            ". Restore the listed dependency file before generating projects.")
    end
end

workspace "CatalpaEngine"
    configurations { "Debug", "Release", "Dist" }
    architecture "x86_64"
    -- CatalpaBox 是当前默认启动沙盒；功能库不依赖这个宿主。
    if buildBox then startproject "CatalpaBox" end

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
        runtime "Release"
        symbols "On"
        optimize "Speed"


filter {}
include "Script/Build/ImGui.lua"
include "Script/Build/GLFW.lua"
include "Script/Build/GLAD.lua"
include "Script/Build/EnkiTS.lua"
-- SPIR-V反射由Tools私有消费；Runtime只读取cooked ShaderAsset。
if not runtimeOnly then
include "Script/Build/SPIRVReflect.lua"
-- 工具依赖均由Premake直接编译；Assimp的派生头也由Lua生成。
include "Script/Build/AssetImport.lua"
include "Script/Build/Assimp.lua"
end
include "Script/Build/Authoring.lua"
include "Script/Build/StageDependencies.lua"
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
        "%{IncludeDir.Lua}"
    }

    links { 
        "GLFW",
        "GLAD",
        "imgui",
        "enkiTS",
        "Lua"
    }

    -- 第三方 Gizmo 实现与 ImGui 同属此 DLL；编辑器继续使用导入接口。
    if not runtimeOnly then CatalpaImGuiBuild.AttachGizmoImplementation() end

    include "Script/Build/Slang.lua"

    filter "system:windows"
        -- 系统随机源与Unicode命令行；系统库，不引入第三方源码。
        links { "bcrypt", "shell32", "imm32" }
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
if buildBox then
project "CatalpaBox"
    location "CatalpaBox"
    kind "ConsoleApp"

    files {
        "CatalpaBox/src/**.h",
        "CatalpaBox/src/**.cpp",
        -- 以公共头作为部署构建项，DLL 是额外输入，不编译此头。
        "Catalpa/src/Catalpa.h",
        "Catalpa/Resources/Localization/*.json",
        "Catalpa/Resources/Config/*.json",
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

    if runtimeOnly then
        links { "Catalpa" }
        defines { "CATALPA_RUNTIME_ONLY" }
        removefiles { "CatalpaBox/src/BoxDefaultEnvironment.cpp", "CatalpaBox/src/BoxGameplay.cpp" }
    else
        links {
            "Catalpa",
            "CatalpaEditor",
            "Tools",
            -- 最终宿主显式链接工具静态闭包，避免依赖StaticLib传递链接的差异。
            "fastgltf", "simdjson", "simdutf", "MikkTSpace", "assimp", "AssimpZlib",
            "imgui-node-editor", "nativefiledialog-extended", "Lua"
        }
    end

    -- 开发配置调用验证库；Dist 的入口由现有 CATALPA_DIST 分支禁用。
    filter "configurations:Debug or Release"
        if not runtimeOnly then
            includedirs { "Tests" }
            links { "Tests" }
        end
    filter {}

    filter "system:windows"
        links { "ole32", "uuid", "shell32" }
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
    filter { "system:windows", "files:**/Config/*.json" }
        buildmessage "Copying runtime configuration"
        buildcommands {
            '{MKDIR} "%{cfg.targetdir}/Resources/Config"',
            'if errorlevel 1 exit /b 1',
            '{COPYFILE} "%{file.abspath}" "%{cfg.targetdir}/Resources/Config/%{file.name}"',
            'if errorlevel 1 exit /b 1'
        }
        buildoutputs { "%{cfg.targetdir}/Resources/Config/%{file.name}" }

    filter {}
    if not runtimeOnly then
        CatalpaShaderBuild.AttachHostCook()
        -- Grove 使用普通作者资产流程。宿主完成链接且内置着色器就绪后再制作示例。
        local groveRoot = path.join(_MAIN_SCRIPT_DIR, "Projects/Grove")
        local deployGrove = path.join(_MAIN_SCRIPT_DIR, "Script/deploy_grove.py")
        local groveWork = path.join(_MAIN_SCRIPT_DIR, "bin-int", outputdir, "Grove")
        local projectFile = path.join(groveRoot, "Grove.catalpaproj")
        files { projectFile }
        filter { "system:windows", "files:**/Grove.catalpaproj" }
            buildmessage "Tracking Grove courtyard inputs"
            buildinputs { path.join(groveRoot, "Content/Grove.glb"), path.join(groveRoot, "Content/Courtyard.hdr"),
                          path.join(groveRoot, "Content/Grove.scene.json"), path.join(groveRoot, "Content/asset_metadata.json"),
                          path.join(groveRoot, "BuildProfiles.json"), deployGrove }
            buildcommands {
                '{MKDIR} "' .. groveWork .. '"',
                'if errorlevel 1 exit /b 1',
                'type nul > "' .. groveWork .. '/inputs.stamp"',
                'if errorlevel 1 exit /b 1'
            }
            buildoutputs { path.join(groveWork, "inputs.stamp") }
            compilebuildoutputs(false)
            linkbuildoutputs(false)
        filter "system:windows"
            postbuildcommands {
                'python "' .. deployGrove .. '" --host "%{!cfg.buildtarget.abspath}" --work "' ..
                    groveWork .. '" --configuration "%{cfg.buildcfg}"',
                'if errorlevel 1 exit /b 1'
            }
        filter {}
    end

end

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
        "%{IncludeDir.AssimpZlib}", "%{IncludeDir.AssimpZlibConfig}", "%{IncludeDir.Lua}" }
    -- 与fastgltf静态实现的公开数值ABI保持一致；stb实现由后续唯一TextureImporter提供。
    defines { "FASTGLTF_USE_64BIT_FLOAT=0" }
    -- 反射是Tools的私有实现依赖；最终宿主通过项目引用链接所需静态实现。
    links { "Catalpa", "SPIRV-Reflect", "fastgltf", "MikkTSpace", "assimp", "AssimpZlib", "Lua" }

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
        "%{IncludeDir.ImGuizmo}",
        "%{IncludeDir.NodeEditor}",
        "%{IncludeDir.NativeFileDialog}",
        "%{IncludeDir.JSON}",
        "%{IncludeDir.GLM}"
    }

    links {
        "Catalpa",
        "Tools",
        "imgui-node-editor",
        "nativefiledialog-extended"
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
        "%{IncludeDir.imgui}",
        -- 作者文档/图语义回归直接消费有界 JSON 适配；静态库不继承 Tools 的 include 路径。
        "%{IncludeDir.JSON}",
        "%{IncludeDir.GLM}"
    }

    links { "Catalpa", "Tools", "CatalpaEditor" }

    filter "system:windows"
        -- Fixture按__FILE__定位源根；/FC保证从任意cwd运行也得到绝对路径。
        buildoptions { "/FC" }
        -- Tests 是 Catalpa.dll 的消费者，GPU 缩略图用例消费同一个 ImGui 上下文。
        defines { "CATALPA_PLATFORM_WINDOW", "IMGUI_API=__declspec(dllimport)" }

    filter "configurations:Dist"
        excludefrombuild "On"

filter {}

-- 公开包含目录和静态库宏，供各阶段的真实消费者直接引用。
for _, consumer in ipairs { "Catalpa", "Tools", "CatalpaEditor", "CatalpaBox", "Tests" } do
    filter {}
    if buildBox or consumer ~= "CatalpaBox" then
    project(consumer)
        includedirs {
            "Catalpa/third_party/meshoptimizer/src", "Catalpa/third_party/zstd/lib",
            "Catalpa/third_party/KTX-Software/lib/include", "Catalpa/third_party/KTX-Software/external/dfdutils",
            "Catalpa/third_party/freetype/include", "Catalpa/third_party/ozz-animation/include",
            "Catalpa/third_party/JoltPhysics", "Catalpa/third_party/miniaudio", "Catalpa/third_party/RmlUi/Include",
            "Catalpa/third_party/recastnavigation/Recast/Include", "Catalpa/third_party/recastnavigation/Detour/Include",
            "Catalpa/third_party/recastnavigation/DetourCrowd/Include", "Catalpa/third_party/recastnavigation/DetourTileCache/Include"
        }
        defines { "RMLUI_STATIC_LIB", "KHRONOS_STATIC" }
    end
end
filter {}
if buildBox then
project "CatalpaBox"
    if not runtimeOnly then
        links { "meshoptimizer", "ktx", "astcenc", "zstd", "FreeType", "ozz_animation_offline", "ozz_animation",
            "ozz_base", "Jolt", "miniaudio", "RmlUi", "Recast", "DetourCrowd", "DetourTileCache", "Detour", "winmm" }
    end
filter {}

filter {}
end
filter {}
project "Catalpa"
    -- Runtime真实消费者在DLL内链接；runtime-only宿主不再依赖工具/编辑库补符号。
    links { "zstd", "FreeType", "Jolt", "Detour", "RmlUi", "miniaudio", "winmm" }
filter {}
project "Tools"
    links { "meshoptimizer", "Recast", "Detour", "miniaudio" }
filter {}

if runtimeOnly then
    for _, name in ipairs { "Tools", "CatalpaEditor", "Tests" } do
        filter {}
        project(name)
            excludefrombuild "On"
    end
end
filter {}

filter {}
if buildBox then
project "CatalpaBox"
    files {
        "Catalpa/Resources/Fonts/Inter/extras/ttf/Inter-Regular.ttf",
        "Catalpa/Resources/Fonts/Inter/InterVariable.ttf",
        "Catalpa/Resources/Fonts/Inter/LICENSE.txt",
        "Catalpa/Resources/Fonts/NotoSansSC/NotoSansSC-Regular.otf",
        "Catalpa/Resources/Fonts/NotoSansSC/LICENSE.txt"
    }
    -- 每个字体和许可文件有独立输出；两个字族可分别更新且不会覆盖同名许可证。
    for _, family in ipairs { "Inter", "NotoSansSC" } do
        local destination = "%{cfg.targetdir}/Resources/Fonts/" .. family
        filter { "system:windows", "files:**/Fonts/" .. family .. "/**" }
        buildmessage ("Deploying " .. family .. " font resource")
        buildcommands {
            '{MKDIR} "' .. destination .. '"',
            'if errorlevel 1 exit /b 1',
            '{COPYFILE} "%{file.abspath}" "' .. destination .. '/%{file.name}"',
            'if errorlevel 1 exit /b 1'
        }
        buildoutputs { destination .. "/%{file.name}" }
    end
filter {}

end
filter {}
