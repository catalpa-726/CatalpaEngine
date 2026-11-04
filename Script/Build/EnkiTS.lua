-- Build only the upstream C++ scheduler; retain examples and the C wrapper as source only.
local engineRoot = _MAIN_SCRIPT_DIR
local enkiTSRoot = path.join(engineRoot, "Catalpa/third_party/enkiTS")

filter {}

project "enkiTS"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "Off"

    location (path.join(engineRoot, "out/projects/enkiTS"))
    targetdir (path.join(engineRoot, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(engineRoot, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files {
        path.join(enkiTSRoot, "src/TaskScheduler.cpp"),
        path.join(enkiTSRoot, "src/TaskScheduler.h"),
        path.join(enkiTSRoot, "src/LockLessMultiReadPipe.h")
    }

    includedirs {
        path.join(enkiTSRoot, "src")
    }

    -- Consumers must also retain the upstream default of three task priorities.
    -- No PCH is declared: the engine PCH belongs only to the Catalpa project.
    -- Runtime and optimization settings follow the workspace configuration.

filter {}
