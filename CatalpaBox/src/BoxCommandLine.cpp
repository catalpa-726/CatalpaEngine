#include "BoxCommandLine.h"
#if !defined(CATALPA_RUNTIME_ONLY)
#include "Commands/ToolCommandRunner.h"
#endif
#include "Log/Log.h"
#if !defined(CATALPA_DIST) && !defined(CATALPA_RUNTIME_ONLY)
#include "TestRunner.h"
#endif
namespace Catalpa {
    ApplicationStartupResult BoxCommandLine::Dispatch(std::span<const std::string> args, BoxLaunchOptions& options)
    {
        options={};
        if (args.empty()) {
#if defined(CATALPA_RUNTIME_ONLY)
            CAT_ERROR("Runtime host requires --run-package <absolute-file>");
            return {true, 2};
#else
            return {};
#endif
        }
#if !defined(CATALPA_RUNTIME_ONLY)
        if (args[0] == "--cook-shaders" || args[0] == "--build-assets" || args[0] == "--cook-assets" ||
            args[0] == "--inspect" || args[0] == "--package") {
            const auto result=Tools::ToolCommandRunner::Run(args);
            for(const auto& diagnostic:result.diagnostics) {
                if(result.exitCode==0) CAT_INFO("{0}",diagnostic); else CAT_ERROR("{0}",diagnostic);
            }
            return {true,result.exitCode};
        }
#endif
        if (args.size() == 2 && args[0] == "--run-package") {
            try {
                options.package = std::filesystem::u8path(args[1]);
                if (!options.package.is_absolute()) {
                    CAT_ERROR("Package path must be absolute");
                    return {true, 2};
                }
                return {};
            } catch (const std::exception& e) {
                CAT_ERROR("Invalid package path: {0}", e.what());
                return {true, 2};
            }
        }
#if !defined(CATALPA_RUNTIME_ONLY)
        if(args.size()==2&&args[0]=="--project") {
            try {
                auto path=std::filesystem::u8path(args[1]);
                if(!path.is_absolute()) {CAT_ERROR("--project requires an absolute path.");return {true,2};}
                options.project=std::move(path);return {};
            } catch(const std::exception& exception) {CAT_ERROR("Invalid project path: {0}",exception.what());return {true,2};}
        }
        if(args.size()==1&&args[0]=="--sample-error-material") {options.errorMaterial=true;return {};}
        if(args.size()==1&&args[0]=="--sample-reversed-z") {options.reversedZ=true;return {};}
#endif
#if !defined(CATALPA_DIST) && !defined(CATALPA_RUNTIME_ONLY)
        if (args.size() == 2 && args[0] == "--test-editor-language") {
            options.testLanguageDirectory = std::filesystem::u8path(args[1]);
            if (!options.testLanguageDirectory.is_absolute()) {
                CAT_ERROR("--test-editor-language requires an absolute acceptance directory");
                return {true, 2};
            }
            return {};
        }
#endif
        if(args.size()==1&&args[0].starts_with("--test-")) {
#if !defined(CATALPA_DIST) && !defined(CATALPA_RUNTIME_ONLY)
            if (args[0] == "--test-editor-docking") { options.testEditorDocking = true; return {}; }
            if (args[0] == "--test-editor-platform-windows") { options.testEditorPlatformWindows = true; return {}; }
            if (args[0] == "--test-editor-thumbnails") { options.testEditorThumbnails = true; return {}; }
            if (args[0] == "--test-editor-grove") { options.testEditorGrove = true; return {}; }
            if (args[0] == "--test-s14-foundation")
                return {true, Tests::RunS14FoundationTests()};
            if (args[0] == "--test-s14-s17")
                return {true, Tests::RunS14S17Tests()};
            if (args[0] == "--test-s10-s13") return {true,Tests::RunS10S13Tests()};
            if (args[0] == "--test-s07-s09")
                return {true, Tests::RunS07S09Tests()};
            if (args[0] == "--test-s05-s06")
                return {true, Tests::RunS05S06Tests()};
            if(args[0]=="--test-s04") return {true,Tests::RunS04Tests()};
            if(args[0]=="--test-s04-1") return {true,Tests::RunS041Tests()};
            if(args[0]=="--test-s03-4") return {true,Tests::RunS034Tests()};
            if(args[0]=="--test-s03-3") return {true,Tests::RunS033Tests()};
            if(args[0]=="--test-s03-2") return {true,Tests::RunS032Tests()};
            if(args[0]=="--test-s03-1") return {true,Tests::RunS031Tests()};
#else
            CAT_ERROR("Tests require a development host in Debug/Release.");
            return {true, 2};
#endif
        }
#if defined(CATALPA_RUNTIME_ONLY)
        CAT_INFO("Usage: CatalpaBox --run-package <absolute-file> | --help");
#else
        CAT_INFO(
            "Usage: CatalpaBox [--project <absolute-file> | --run-package <absolute-file> | "
            "--inspect <absolute-file> | --package <tool arguments> | --test-s14-s17 | --test-s14-foundation "
            "| --test-editor-docking | --test-editor-platform-windows | --test-editor-thumbnails | --test-editor-grove | --test-s10-s13 | --test-s07-s09 | --test-s05-s06 | --test-s04 | --sample-reversed-z | "
            "--test-editor-language <absolute-directory> | --sample-error-material | --cook-shaders <tool arguments> | --build-assets <tool arguments> | "
            "--cook-assets <tool arguments> | --help]. No arguments opens the workspace.");
#endif
        return {true,args.size()==1&&args[0]=="--help"?0:2};
    }
}
