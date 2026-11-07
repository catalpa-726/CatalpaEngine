-- 作者工具与脚本依赖直接编译源码，复用工作区的配置和动态 CRT。
local root = _MAIN_SCRIPT_DIR
local nodeRoot = path.join(root, IncludeDir.NodeEditor)
local dialogRoot = path.join(root, "Catalpa/third_party/nativefiledialog-extended")
local luaSource = path.join(root, IncludeDir.Lua)

if not CATALPA_RUNTIME_ONLY then
filter {}
project "imgui-node-editor"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "Off"

    location(path.join(root, "out/projects/imgui-node-editor"))
    targetdir(path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir(path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files {
        path.join(nodeRoot, "*.h"),
        path.join(nodeRoot, "*.inl"),
        path.join(nodeRoot, "crude_json.cpp"),
        path.join(nodeRoot, "imgui_canvas.cpp"),
        path.join(nodeRoot, "imgui_node_editor.cpp"),
        path.join(nodeRoot, "imgui_node_editor_api.cpp")
    }
    includedirs { nodeRoot, path.join(root, IncludeDir.imgui) }

    -- 调用 Catalpa.dll 中的唯一 ImGui 实现，不链接示例自带的 ImGui。
    links { "Catalpa" }
    filter "system:windows"
        defines { "IMGUI_API=__declspec(dllimport)" }

filter {}
project "nativefiledialog-extended"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "Off"

    location(path.join(root, "out/projects/nativefiledialog-extended"))
    targetdir(path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir(path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files { path.join(dialogRoot, "src/include/*.h"), path.join(dialogRoot, "src/include/*.hpp") }
    includedirs { path.join(root, IncludeDir.NativeFileDialog) }
    filter "system:windows"
        files { path.join(dialogRoot, "src/nfd_win.cpp") }
        links { "ole32", "uuid", "shell32" }

end

filter {}
project "Lua"
    kind "StaticLib"
    language "C"
    cdialect "C11"
    staticruntime "Off"

    location(path.join(root, "out/projects/Lua"))
    targetdir(path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir(path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files { path.join(luaSource, "*.h"), path.join(luaSource, "*.hpp"), path.join(luaSource, "*.c") }
    -- 仅编译库；解释器/编译器 main、聚合源码和上游内部测试会重复定义实现。
    removefiles {
        path.join(luaSource, "lua.c"),
        path.join(luaSource, "luac.c"),
        path.join(luaSource, "onelua.c"),
        path.join(luaSource, "ltests.c")
    }
    includedirs { luaSource }

filter {}
