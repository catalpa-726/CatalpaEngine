#include "catpch.h"
#include "ShaderCompiler/SlangCompiler.h"
#include "Core/PlatformServices.h"
#include "Core/Serialization/JsonArchive.h"
#include <cctype>
namespace Catalpa::Tools {
    namespace {
        bool Dependencies(std::string_view text,const ShaderCompileRequest& request,std::vector<std::string>& output,std::string& error){
            // Make dependency syntax: escaped spaces/backslashes and continued lines; drive ':' is not the target separator.
            size_t split=std::string_view::npos;
            for(size_t i=0;i+1<text.size();++i)if(text[i]==':'&&(text[i+1]==' '||text[i+1]=='\t'||text[i+1]=='\r'||text[i+1]=='\n')&&(i==0||text[i-1]!='\\')){split=i;break;}
            if(split==text.npos){error="Slang dependency file has no target separator";return false;}
            std::vector<std::string> tokens;std::string token;
            for(size_t i=split+1;i<text.size();++i){char c=text[i];if(c=='\\'&&i+1<text.size()){
                    char next=text[i+1];if(next=='\r'||next=='\n'){++i;if(next=='\r'&&i+1<text.size()&&text[i+1]=='\n')++i;continue;}
                    if(next==' '||next=='\t'||next=='#'||next=='\\'||next==':'){token+=next;++i;continue;}
                }
                if(c==' '||c=='\t'||c=='\r'||c=='\n'){if(!token.empty()){tokens.push_back(std::move(token));token.clear();}}else token+=c;
            }
            if(!token.empty())tokens.push_back(std::move(token));if(tokens.empty()||tokens.size()>4096){error="Slang dependency list is empty or oversized";return false;}
            std::set<std::string> names;
            for(const auto& t:tokens){auto path=std::filesystem::u8path(t);if(!path.is_absolute())path=request.sdkRoot/L"Bin"/path;path=std::filesystem::canonical(path);
                if(!IsPathWithinRoot(path,request.sourceRoot)){error="compiler dependency escapes captured source root: "+ShaderToolDetail::PathText(path);return false;}
                names.insert(ShaderToolDetail::PathText(path.lexically_relative(request.sourceRoot)));
            }
            output.assign(names.begin(),names.end());return true;
        }
    }
    bool SlangCompiler::CheckEntryReflection(std::string_view text,const ShaderMetadata& metadata,std::string& error){
        std::unique_ptr<ArchiveValue> document;ArchiveError parseError;
        if(!JsonArchive::Parse(text,document,parseError)){error="Slang reflection JSON: "+parseError.ToString();return false;}
        const auto* field=document->Find("entryPoints");const auto* entries=field?std::get_if<ArchiveValue::Array>(&field->data):nullptr;
        if(!entries||entries->size()!=metadata.entries.size()){error="Slang/final SPIR-V entry count mismatch";return false;}
        std::set<std::pair<ShaderStage,std::string>> seen;
        for(const auto& entry:*entries){const auto* n=entry.Find("name"),*s=entry.Find("stage");const auto* name=n?std::get_if<std::string>(&n->data):nullptr;const auto* stage=s?std::get_if<std::string>(&s->data):nullptr;
            if(!name||!stage){error="Slang reflection entry requires name/stage";return false;}
            ShaderStage kind;if(*stage=="vertex")kind=ShaderStage::Vertex;else if(*stage=="fragment")kind=ShaderStage::Fragment;else if(*stage=="compute")kind=ShaderStage::Compute;else{error="unsupported Slang reflected stage";return false;}
            if(!seen.emplace(kind,*name).second||!FindShaderEntry(metadata,*name,kind)){error="Slang/final SPIR-V entry name/stage mismatch";return false;}
        }
        error.clear();return true;
    }
    bool SlangCompiler::Compile(const ShaderCompileRequest& job,const std::filesystem::path& temporary,std::vector<uint32_t>& output,
        std::vector<std::string>& dependencies,std::string& diagnostics,std::string& error){
        const auto spirv=temporary/L"module.spv",depfile=temporary/L"module.d";
        // No -entry/-stage: Slang discovers each [shader(...)] entry and emits one linked SPIR-V module.
        std::vector<std::wstring> args{job.sourceFile.wstring(),L"-I",job.sourceRoot.wstring(),L"-target",L"spirv",L"-profile",L"spirv_1_5",L"-fvk-use-entrypoint-name",L"-emit-spirv-directly",L"-depfile",depfile.wstring(),L"-reflection-json",(temporary/L"reflection.json").wstring(),L"-o",spirv.wstring()};
        if(job.defines.size()>128){error="too many shader defines";return false;}
        for(const auto& [name,value]:job.defines){
            if(name.empty()||name.size()>128||value.size()>4096||!(std::isalpha(static_cast<unsigned char>(name.front()))||name.front()=='_')||!std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c)||c=='_';})||value.find_first_of("\r\n\0",0,3)!=value.npos){error="invalid shader define";return false;}
            args.push_back(std::filesystem::u8path("-D"+name+"="+value).wstring());
        }
        if(!ShaderToolDetail::RunProcess(job.sdkRoot/L"Bin/slangc.exe",args,temporary/L"slang.log",job.cancellation,diagnostics,error))return false;
        std::vector<std::byte> bytes,deps;if(!ShaderToolDetail::ReadFile(spirv,16*1024*1024,bytes,error)||bytes.size()<20||bytes.size()%4!=0){error="Slang output is not bounded SPIR-V words: "+error;return false;}
        if(!ShaderToolDetail::ReadFile(depfile,4*1024*1024,deps,error))return false;
        const auto deptext=deps.empty()?std::string_view{}:std::string_view(reinterpret_cast<const char*>(deps.data()),deps.size());
        if(!Dependencies(deptext,job,dependencies,error))return false;
        std::vector<uint32_t> words(bytes.size()/4);for(size_t i=0;i<words.size();++i){uint32_t word=0;for(size_t b=0;b<4;++b)word|=uint32_t(std::to_integer<uint8_t>(bytes[i*4+b]))<<(b*8);words[i]=word;}output.swap(words);return true;
    }
}
