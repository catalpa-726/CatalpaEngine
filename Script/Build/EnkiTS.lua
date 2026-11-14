-- 只编译上游的 C++ 任务调度器；示例和 C 封装仅保留源码。
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

    --[[
    使用方应保留上游默认的三个任务优先级。
    引擎的预编译头仅用于 Catalpa 工程，此处单独编译调度器。
    运行时和优化选项沿用当前工作区配置。
    ]]

filter {}
