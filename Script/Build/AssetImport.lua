-- Tools 使用的导入库。Assimp 的头文件配置和源码选择在 Assimp.lua 中。
local root = _MAIN_SCRIPT_DIR
local vendor = path.join(root, "Catalpa/third_party")

-- 继承当前工作区配置/动态CRT，工程文件放out而非上游目录。
local function staticProject(name, languageName)
    filter {}
    project(name)
    kind "StaticLib"
    language(languageName or "C++")
    staticruntime "Off"
    location(path.join(root, "out/projects/" .. name))
    targetdir(path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir(path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))
    -- PCH仅由Catalpa项目声明；此处不启用，也无需调用旧版flags接口。
    if languageName == "C" then cdialect "C11" end
end

staticProject("simdjson")
    files { path.join(vendor, "simdjson/singleheader/simdjson.cpp"),
        path.join(vendor, "simdjson/singleheader/simdjson.h") }
    includedirs { path.join(root, IncludeDir.simdjson) }

-- 用户提供的合并源码包直接编译，保留全部功能。
staticProject("simdutf")
    files { path.join(vendor, "simdutf/simdutf.cpp"), path.join(vendor, "simdutf/simdutf.h") }
    includedirs { path.join(root, IncludeDir.simdutf) }

staticProject("fastgltf")
    files { path.join(vendor, "fastgltf/include/**.hpp"),
        path.join(vendor, "fastgltf/src/base64.cpp"), path.join(vendor, "fastgltf/src/crc32.cpp"),
        path.join(vendor, "fastgltf/src/exporter.cpp"), path.join(vendor, "fastgltf/src/io.cpp"),
        path.join(vendor, "fastgltf/src/parser.cpp"), path.join(vendor, "fastgltf/src/uri.cpp"),
        path.join(vendor, "fastgltf/src/validate.cpp") }
    -- IncludeDir 相对仓库根目录。子脚本内直接传相对路径会多出 Script/Build。
    includedirs {
        path.join(root, IncludeDir.fastgltf),
        path.join(root, IncludeDir.simdjson),
        path.join(root, IncludeDir.simdutf)
    }
    defines { "FASTGLTF_USE_64BIT_FLOAT=0" }
    links { "simdjson", "simdutf" }

staticProject("MikkTSpace", "C")
    files { path.join(vendor, "MikkTSpace/mikktspace.c"), path.join(vendor, "MikkTSpace/mikktspace.h") }
    includedirs { path.join(root, IncludeDir.MikkTSpace) }

filter {}
