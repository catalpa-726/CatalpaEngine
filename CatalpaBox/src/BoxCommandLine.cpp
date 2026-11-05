#include "BoxCommandLine.h"
#include "Commands/ToolCommandRunner.h"
#include "Log/Log.h"
#if !defined(CATALPA_DIST)
#include "TestRunner.h"
#endif
namespace Catalpa {
    ApplicationStartupResult BoxCommandLine::Dispatch(std::span<const std::string> args, BoxLaunchOptions& options)
    {
        options={};
        if(args.empty()) return {};
        if (args[0] == "--cook-shaders" || args[0] == "--build-assets" || args[0] == "--cook-assets") {
            const auto result=Tools::ToolCommandRunner::Run(args);
            for(const auto& diagnostic:result.diagnostics) {
                if(result.exitCode==0) CAT_INFO("{0}",diagnostic); else CAT_ERROR("{0}",diagnostic);
            }
            return {true,result.exitCode};
        }
        if(args.size()==2&&args[0]=="--project") {
            try {
                auto path=std::filesystem::u8path(args[1]);
                if(!path.is_absolute()) {CAT_ERROR("--project requires an absolute path.");return {true,2};}
                options.project=std::move(path);return {};
            } catch(const std::exception& exception) {CAT_ERROR("Invalid project path: {0}",exception.what());return {true,2};}
        }
        if(args.size()==1&&args[0]=="--sample-error-material") {options.errorMaterial=true;return {};}
        if(args.size()==1&&args[0]=="--sample-reversed-z") {options.reversedZ=true;return {};}
        if(args.size()==1&&args[0].starts_with("--test-")) {
#if !defined(CATALPA_DIST)
            if (args[0] == "--test-s05-s06")
                return {true, Tests::RunS05S06Tests()};
            if(args[0]=="--test-s04") return {true,Tests::RunS04Tests()};
            if(args[0]=="--test-s04-1") return {true,Tests::RunS041Tests()};
            if(args[0]=="--test-s03-4") return {true,Tests::RunS034Tests()};
            if(args[0]=="--test-s03-3") return {true,Tests::RunS033Tests()};
            if(args[0]=="--test-s03-2") return {true,Tests::RunS032Tests()};
            if(args[0]=="--test-s03-1") return {true,Tests::RunS031Tests()};
#else
            CAT_ERROR("Tests are available in Debug/Release only.");return {true,2};
#endif
        }
        CAT_INFO("Usage: CatalpaBox [--project <absolute-file> | --test-s05-s06 | --test-s04 | --sample-reversed-z | --sample-error-material | --cook-shaders <tool arguments> | --build-assets <tool arguments> | --cook-assets <tool arguments> | --help]. No arguments opens the workspace.");
        return {true,args.size()==1&&args[0]=="--help"?0:2};
    }
}
