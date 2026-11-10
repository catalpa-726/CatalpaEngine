#pragma once
#include "Core/Application.h"
#include "catpch.h"
namespace Catalpa {
    // 宿主只保存启动选择和项目路径，不承载工具算法、资产格式或测试断言。
    struct BoxLaunchOptions {
        bool reversedZ = false;
        bool errorMaterial = false;
        bool testEditorThumbnails = false;
        bool testEditorGrove = false;
        std::filesystem::path testLanguageDirectory;
        bool testEditorDocking = false;
        bool testEditorPlatformWindows = false;
        std::filesystem::path project, package;
    };
    class BoxCommandLine final {
    public:
        static ApplicationStartupResult Dispatch(std::span<const std::string> arguments, BoxLaunchOptions&);
    };
}
