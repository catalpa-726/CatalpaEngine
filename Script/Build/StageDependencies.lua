-- S14-S17 源码库：不运行 CMake、不下载、不修改上游。
local root = _MAIN_SCRIPT_DIR
local function src(lib, p) return path.join(root, "Catalpa/third_party", lib, p) end
local function lib(name, package, patterns)
    filter {}
    project(name)
        kind "StaticLib"
        language "C++"
        cppdialect "C++20"
        staticruntime "Off"
        location(path.join(root, "out/projects/" .. name))
        targetdir(path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
        objdir(path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))
        for _, p in ipairs(patterns) do
            local matches = os.matchfiles(src(package, p))
            if #matches == 0 then error("Missing dependency source: " .. package .. "/" .. p) end
            files(matches)
        end
end
lib("meshoptimizer", "meshoptimizer", {"src/*.cpp"})
    includedirs {src("meshoptimizer", "src")}
lib("zstd", "zstd", {"lib/common/*.c", "lib/compress/*.c", "lib/decompress/*.c", "lib/dictBuilder/*.c"})
    language "C"
    cdialect "C11"
    includedirs {src("zstd", "lib"), src("zstd", "lib/common")}
    defines {"ZSTD_DISABLE_ASM", "ZSTD_MULTITHREAD", "_CRT_SECURE_NO_WARNINGS"}
lib("Jolt", "JoltPhysics", {"Jolt/**.cpp"})
    includedirs {src("JoltPhysics", "")}
    -- 单精度、16位ObjectLayer、SSE2基线；不启用额外调试绘制ABI。
    defines {"NOMINMAX", "WIN32_LEAN_AND_MEAN"}
lib("ozz_base", "ozz-animation", {"src/base/**.cc"})
    includedirs {src("ozz-animation", "include"), src("ozz-animation", "src")}
lib("ozz_animation", "ozz-animation", {"src/animation/runtime/*.cc"})
    includedirs {src("ozz-animation", "include"), src("ozz-animation", "src")}
    links {"ozz_base"}
lib("ozz_animation_offline", "ozz-animation", {"src/animation/offline/*.cc"})
    includedirs {src("ozz-animation", "include"), src("ozz-animation", "src")}
    links {"ozz_base", "ozz_animation"}

-- 使用模块聚合入口，避免内部.c被重复编译。
lib("FreeType", "freetype", {"src/base/ftbase.c", "src/base/ftbbox.c", "src/base/ftbdf.c", "src/base/ftbitmap.c", "src/base/ftcid.c", "src/base/ftdebug.c", "src/base/ftfstype.c", "src/base/ftgasp.c", "src/base/ftglyph.c", "src/base/ftgxval.c", "src/base/ftinit.c", "src/base/ftmm.c", "src/base/ftotval.c", "src/base/ftpatent.c", "src/base/ftpfr.c", "src/base/ftstroke.c", "src/base/ftsynth.c", "src/base/ftsystem.c", "src/base/fttype1.c", "src/base/ftwinfnt.c", "src/autofit/autofit.c", "src/bdf/bdf.c", "src/cff/cff.c", "src/cid/type1cid.c", "src/cache/ftcache.c", "src/gzip/ftgzip.c", "src/lzw/ftlzw.c", "src/pcf/pcf.c", "src/pfr/pfr.c", "src/psaux/psaux.c", "src/pshinter/pshinter.c", "src/psnames/psnames.c", "src/raster/raster.c", "src/sdf/sdf.c", "src/sfnt/sfnt.c", "src/smooth/smooth.c", "src/svg/svg.c", "src/truetype/truetype.c", "src/type1/type1.c", "src/type42/type42.c", "src/winfonts/winfnt.c", "src/hvf/hvf.c"})
    language "C"
    cdialect "C11"
    includedirs {src("freetype", "include")}
    defines {"FT2_BUILD_LIBRARY", "_CRT_SECURE_NO_WARNINGS"}
lib("RmlUi", "RmlUi", {"Source/Core/*.cpp", "Source/Core/Elements/*.cpp", "Source/Core/Layout/*.cpp", "Source/Core/FontEngineDefault/*.cpp"})
    includedirs {src("RmlUi", "Include"), src("freetype", "include")}
    defines {"RMLUI_STATIC_LIB", "RMLUI_FONT_ENGINE_FREETYPE"}
    links {"FreeType"}
lib("miniaudio", "miniaudio", {"miniaudio.h"})
    language "C"
    cdialect "C11"
    files {path.join(root, "Script/Build/miniaudio.c")}
    includedirs {src("miniaudio", "")}
    links {"ole32", "uuid", "winmm"}
lib("Recast", "recastnavigation", {"Recast/Source/*.cpp"})
    includedirs {src("recastnavigation", "Recast/Include"), src("recastnavigation", "Detour/Include")}
lib("Detour", "recastnavigation", {"Detour/Source/*.cpp"})
    includedirs {src("recastnavigation", "Detour/Include"), src("recastnavigation", "Detour/Include")}
lib("DetourCrowd", "recastnavigation", {"DetourCrowd/Source/*.cpp"})
    includedirs {src("recastnavigation", "DetourCrowd/Include"), src("recastnavigation", "Detour/Include")}
    links {"Detour"}
lib("DetourTileCache", "recastnavigation", {"DetourTileCache/Source/*.cpp"})
    includedirs {src("recastnavigation", "DetourTileCache/Include"), src("recastnavigation", "Detour/Include")}
    links {"Detour"}

-- KTX读取/转码核心；离线编码器将在S14纹理编码方案确定后接入。
-- dfdutils 与上游 add_library 的源清单一致；endswap.c 仅列入文档，不参与编译。
lib("ktx", "KTX-Software", {"lib/src/astc_codec.cpp", "lib/src/basis_transcode.cpp", "lib/src/miniz_wrapper.cpp", "lib/src/checkheader.c", "lib/src/etcunpack.cxx", "lib/src/filestream.c", "lib/src/glformat_str.c", "lib/src/hashlist.c", "lib/src/info.c", "lib/src/memstream.c", "lib/src/strings.c", "lib/src/swap.c", "lib/src/texture.c", "lib/src/texture2.c", "lib/src/vkformat_check.c", "lib/src/vkformat_check_variant.c", "lib/src/vkformat_str.c", "lib/src/vkformat_typesize.c", "lib/src/texture1.c", "external/basis_universal/transcoder/basisu_transcoder.cpp", "external/dfdutils/createdfd.c", "external/dfdutils/colourspaces.c", "external/dfdutils/interpretdfd.c", "external/dfdutils/printdfd.c", "external/dfdutils/queries.c", "external/dfdutils/vk2dfd.c", "external/dfdutils/dfd2vk.c"})
    includedirs {src("KTX-Software", "lib/include"), src("KTX-Software", "lib/src"),
        src("KTX-Software", "external"), src("KTX-Software", "external/dfdutils"),
        src("KTX-Software", "external/basis_universal"), src("KTX-Software", "external/basis_universal/zstd"),
        src("KTX-Software", "other_include"), src("KTX-Software", "utils"), src("zstd", "lib")}
    defines {"LIBKTX", "KHRONOS_STATIC", "KTX_FEATURE_KTX1", "KTX_FEATURE_KTX2", "KTX_FEATURE_WRITE=0",
        "KTX_OMIT_VULKAN=1", "SUPPORT_SOFTWARE_ETC_UNPACK=0", "BASISD_SUPPORT_KTX2_ZSTD=1", "BASISU_SUPPORT_SSE=0",
        "BASISD_SUPPORT_FXT1=0", "BASISU_NO_ITERATOR_DEBUG_LEVEL", "_CRT_SECURE_NO_WARNINGS"}
    links {"astcenc", "zstd"}
    -- dfdutils 按上游独立编译契约使用自带完整 Vulkan 枚举头。
    -- LIBKTX 的精简枚举头不含其生成映射所引用的部分 EXT 别名。
    filter "files:**/external/dfdutils/*.c"
        undefines {"LIBKTX"}
    filter {}
lib("astcenc", "KTX-Software", {"external/astc-encoder/Source/astcenc_*.cpp"})
    includedirs {src("KTX-Software", "external/astc-encoder/Source")}
    defines {"ASTCENC_SSE=20", "ASTCENC_AVX=0", "ASTCENC_NEON=0", "ASTCENC_SVE=0", "ASTCENC_POPCNT=0", "ASTCENC_F16C=0"}
filter {}
