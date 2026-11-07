#include "catpch.h"
#include "ShaderCompiler/ShaderCompileJob.h"
#include "ShaderCompiler/SlangCompiler.h"
#include "ShaderCompiler/SpirvValidator.h"
#include "ShaderCompiler/SpirvReflection.h"
#include "Shaders/ShaderLibrary.h"
#include "Assets/AssetContainerReader.h"
#include "Core/Id/StableIdGenerator.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Core/PlatformServices.h"
#include "Core/Serialization/BinaryArchive.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
namespace Catalpa::Tools {
    namespace ShaderToolDetail {
        std::string PathText(const std::filesystem::path& p){auto u=p.generic_u8string();return {reinterpret_cast<const char*>(u.data()),u.size()};}
        bool ReadFile(const std::filesystem::path& p,size_t limit,std::vector<std::byte>& output,std::string& error){
            if(!p.is_absolute()){error="tool input must be absolute";return false;}VirtualFileSystem fs;VirtualPath v;
            return fs.MountReadOnly("tool-file",p.parent_path(),0,error)&&VirtualPath::Parse("tool-file:/"+PathText(p.filename()),v,error)&&fs.ReadFile(v,limit,output,error);
        }
        namespace {
            struct Handle { HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}Handle()=default;explicit Handle(HANDLE h):value(h){}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete; };
            std::wstring Quote(std::wstring_view text){std::wstring result=L"\"";size_t slash=0;for(auto c:text){if(c==L'\\'){++slash;continue;}result.append(slash*(c==L'"'?2:1),L'\\');slash=0;if(c==L'"')result+=L'\\';result+=c;}result.append(slash*2,L'\\');result+=L'"';return result;}
        }
        bool RunProcess(const std::filesystem::path& exe,const std::vector<std::wstring>& args,const std::filesystem::path& log,CancellationToken cancel,std::string& diagnostics,std::string& error){
            if(!exe.is_absolute()||!log.is_absolute()){error="tool executable/log must be absolute";return false;}
            if(cancel.IsCancellationRequested()){error="shader job cancelled";return false;}
            std::wstring command=Quote(exe.wstring());for(const auto& arg:args){if(arg.find(L'\0')!=arg.npos){error="NUL in process argument";return false;}command+=L" "+Quote(arg);}if(command.size()>=32767){error="tool command line exceeds Windows limit";return false;}
            SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
            Handle file(CreateFileW(log.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,nullptr));
            Handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));
            if(file.value==INVALID_HANDLE_VALUE||input.value==INVALID_HANDLE_VALUE){error="cannot open owned tool diagnostic handles";return false;}
            Handle job(CreateJobObjectW(nullptr,nullptr));JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if(!job.value||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){error="cannot create bounded tool process job";return false;}
            SIZE_T listBytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&listBytes);std::vector<std::byte> storage(listBytes);
            auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
            if(!InitializeProcThreadAttributeList(attributes,1,0,&listBytes)){error="cannot initialize process handle list";return false;}
            struct AttributeOwner{LPPROC_THREAD_ATTRIBUTE_LIST p;~AttributeOwner(){DeleteProcThreadAttributeList(p);}} attributeOwner{attributes};
            HANDLE allowed[]{file.value,input.value};
            if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,allowed,sizeof(allowed),nullptr,nullptr)){error="cannot restrict child handle inheritance";return false;}
            STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdOutput=file.value;startup.StartupInfo.hStdError=file.value;startup.StartupInfo.hStdInput=input.value;startup.lpAttributeList=attributes;
            PROCESS_INFORMATION pi{};const auto working=exe.parent_path();
            if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,working.c_str(),&startup.StartupInfo,&pi)){error="CreateProcess failed: "+std::to_string(GetLastError());return false;}
            Handle process(pi.hProcess),thread(pi.hThread);
            if(!AssignProcessToJobObject(job.value,process.value)||ResumeThread(thread.value)==DWORD(-1)){TerminateProcess(process.value,1);WaitForSingleObject(process.value,INFINITE);error="cannot own/resume shader tool process";return false;}
            const auto start=std::chrono::steady_clock::now();bool interrupted=false;
            while(true){const auto wait=WaitForSingleObject(process.value,25);if(wait==WAIT_OBJECT_0)break;
                LARGE_INTEGER length{};const bool oversized=!GetFileSizeEx(file.value,&length)||length.QuadPart>4*1024*1024;
                if(wait==WAIT_FAILED||cancel.IsCancellationRequested()||oversized||std::chrono::steady_clock::now()-start>std::chrono::minutes(5)){
                    error=cancel.IsCancellationRequested()?"shader job cancelled":oversized?"tool diagnostics exceeded 4MiB":"shader tool wait failed or exceeded five minutes";
                    TerminateJobObject(job.value,1);WaitForSingleObject(process.value,INFINITE);interrupted=true;break;
                }
            }
            DWORD code=1;if(!GetExitCodeProcess(process.value,&code)){error="cannot read shader tool exit code";return false;}
            // The job owns all descendants; close it before reading diagnostics so no writer survives publication.
            TerminateJobObject(job.value,code);CloseHandle(file.value);file.value=nullptr;
            std::vector<std::byte> bytes;std::string readError;if(ReadFile(log,4*1024*1024,bytes,readError))diagnostics=bytes.empty()?std::string{}:std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size());
            else if(!interrupted){error="cannot read tool diagnostics: "+readError;return false;}
            if(interrupted)return false;if(code!=0){error=PathText(exe.filename())+" failed with exit code "+std::to_string(code)+": "+diagnostics;return false;}error.clear();return true;
        }
        bool AddPositionInvariant(std::span<const uint32_t> words,std::vector<uint32_t>& output,std::string& error){
            if(words.size()<5||words[0]!=0x07230203){error="invalid SPIR-V pre-validation header";return false;}
            std::set<std::pair<uint32_t,uint32_t>> positions,invariants;size_t insert=0;
            for(size_t p=5;p<words.size();){const auto count=words[p]>>16,op=words[p]&65535;if(count==0||count>words.size()-p){error="truncated SPIR-V before validation";return false;}
                if(op>=19&&op<=39&&insert==0)insert=p;
                if(op==71&&count>=3){if(words[p+2]==11&&count==4&&words[p+3]==0)positions.emplace(words[p+1],UINT32_MAX);if(words[p+2]==18&&count==3)invariants.emplace(words[p+1],UINT32_MAX);}
                if(op==72&&count>=4){if(words[p+3]==11&&count==5&&words[p+4]==0)positions.emplace(words[p+1],words[p+2]);if(words[p+3]==18&&count==4)invariants.emplace(words[p+1],words[p+2]);}
                // Grouped decorations would hide Position from this bounded transform; fail rather than claim invariance.
                if(op>=73&&op<=75){error="group SPIR-V decorations unsupported by invariant preparation";return false;}p+=count;
            }
            if(insert==0){error="SPIR-V has no type section";return false;}std::vector<uint32_t> added;
            for(const auto& [id,member]:positions)if(!invariants.contains({id,member})){if(member==UINT32_MAX)added.insert(added.end(),{(3u<<16)|71u,id,18u});else added.insert(added.end(),{(4u<<16)|72u,id,member,18u});}
            std::vector<uint32_t> result;result.reserve(words.size()+added.size());result.insert(result.end(),words.begin(),words.begin()+insert);result.insert(result.end(),added.begin(),added.end());result.insert(result.end(),words.begin()+insert,words.end());output.swap(result);error.clear();return true;
        }
    }
    namespace {
        // 指纹预检与实际编译共用适配器版本，反射规则变化后不再命中旧布局缓存。
        constexpr std::string_view ShaderVariantIdentity =
            "Catalpa.ShaderVariant/v2;target=vulkan1.3;profile=spirv_1_5;direct=1;invariant=1;"
            "slang-reflection=1;reflection-adapter=4;image-dimension=2;reflect=vulkan-sdk-1.4.363.0";

        bool Snapshot(const std::filesystem::path& root,std::map<std::string,std::string,std::less<>>& output,std::string& error){
            VirtualFileSystem files;if(!files.MountReadOnly("shader-source",root,0,error))return false;size_t total=0;
            std::map<std::string,std::string,std::less<>> result;
            for(const auto& item:std::filesystem::recursive_directory_iterator(root)){
                if(item.is_symlink()){error="shader sources must not contain symlinks";return false;}if(!item.is_regular_file())continue;
                if(result.size()>=4096){error="shader source file count exceeds 4096";return false;}const auto relative=ShaderToolDetail::PathText(item.path().lexically_relative(root));VirtualPath path;std::vector<std::byte> bytes;
                if(!VirtualPath::Parse("shader-source:/"+relative,path,error)||!files.ReadFile(path,16*1024*1024,bytes,error))return false;
                if(bytes.size()>64*1024*1024-total){error="shader source snapshot exceeds 64MiB";return false;}total+=bytes.size();std::string hash;if(!ComputeAssetContentHash(bytes,hash,error))return false;result.emplace(relative,std::move(hash));
            }
            output.swap(result);return true;
        }
        bool LockedSdk(const std::filesystem::path& root,BinaryWriter& identity,std::string& error){
            struct Lock{const char* name;const char* hash;};
            constexpr Lock locks[]{
                {"slangc.exe","306cf17c6b9c795ecf7b5d951414e6a41ae0e6d1442e94eb8e965524a82d702d"},
                {"slang-compiler.dll","5f80a76029bed0b1a92d17ea7fb92989be33d3bfb16e9fb6a4f06749b9b448c0"},
                {"spirv-val.exe","ea84bd4b277f344d57fd05971603189df00b33184010d968bfd648bb46b61a82"}};
            for(const auto& lock:locks){std::vector<std::byte> bytes;std::string hash;if(!ShaderToolDetail::ReadFile(root/L"Bin"/lock.name,64*1024*1024,bytes,error)||!ComputeAssetContentHash(bytes,hash,error))return false;
                if(hash!=lock.hash){error=std::string("SDK tool differs from Dependencies.lock.json: ")+lock.name;return false;}if(!identity.String(lock.name)||!identity.String(hash)){error=identity.Error();return false;}}
            return true;
        }
    }
    bool ShaderCompileJob::Fingerprint(const ShaderCompileRequest& request,std::string& output,std::string& error){
        try {
            if(!request.sourceRoot.is_absolute()||!request.sourceFile.is_absolute()||!request.sdkRoot.is_absolute()){error="shader fingerprint requires absolute source/SDK paths";return false;}
            const auto root=std::filesystem::canonical(request.sourceRoot),file=std::filesystem::canonical(request.sourceFile),sdk=std::filesystem::canonical(request.sdkRoot);
            if(!IsPathWithinRoot(file,root)||file.extension()!=L".slang"){error="shader fingerprint source escapes root";return false;}
            BinaryWriter identity;identity.String(ShaderVariantIdentity);
            if(!LockedSdk(sdk,identity,error))return false;std::map<std::string,std::string,std::less<>> sources;if(!Snapshot(root,sources,error))return false;
            const auto relative=ShaderToolDetail::PathText(file.lexically_relative(root));if(!sources.contains(relative)){error="source missing from fingerprint snapshot";return false;}identity.String(relative);
            identity.U32(uint32_t(sources.size()));for(const auto& [name,hash]:sources){identity.String(name);identity.String(hash);}identity.U32(uint32_t(request.defines.size()));for(const auto& [name,value]:request.defines){identity.String(name);identity.String(value);}
            if(!identity.Error().empty()){error=identity.Error();return false;}return ComputeAssetContentHash(identity.Data(),output,error);
        }catch(const std::exception& e){error=e.what();return false;}
    }
    bool ShaderCompileJob::Run(const ShaderCompileRequest& request,ShaderCompileResult& output,std::string& error){
        try {
            if(!request.sourceRoot.is_absolute()||!request.sourceFile.is_absolute()||!request.sdkRoot.is_absolute()){error="shader compile requires explicit absolute source and SDK paths";return false;}
            const auto root=std::filesystem::canonical(request.sourceRoot),file=std::filesystem::canonical(request.sourceFile),sdk=std::filesystem::canonical(request.sdkRoot);
            if(!IsPathWithinRoot(file,root)||file.extension()!=L".slang"){error="shader source is outside root or not .slang";return false;}
            ShaderCompileRequest job=request;job.sourceRoot=root;job.sourceFile=file;job.sdkRoot=sdk;
            BinaryWriter identity;identity.String(ShaderVariantIdentity);
            if(!LockedSdk(sdk,identity,error))return false;
            std::map<std::string,std::string,std::less<>> before,after;if(!Snapshot(root,before,error))return false;
            const auto relative=ShaderToolDetail::PathText(file.lexically_relative(root));if(!before.contains(relative)){error="source not in captured dependency root";return false;}identity.String(relative);
            identity.U32(uint32_t(before.size()));for(const auto& [name,hash]:before){identity.String(name);identity.String(hash);}identity.U32(uint32_t(request.defines.size()));for(const auto& [name,value]:request.defines){identity.String(name);identity.String(value);}
            std::string inputFingerprint;if(!ComputeAssetContentHash(identity.Data(),inputFingerprint,error))return false;
            StableId tempId;if(!GenerateStableId(tempId,error))return false;const auto temporary=std::filesystem::temp_directory_path()/std::filesystem::u8path("CatalpaShader-"+tempId.ToString());
            if(IsPathWithinRoot(std::filesystem::weakly_canonical(temporary),root)||IsPathWithinRoot(std::filesystem::weakly_canonical(temporary),sdk)){error="owned temporary output would overlap a readonly input root";return false;}
            if(!std::filesystem::create_directory(temporary)){error="cannot create exclusive shader temporary root";return false;}
            // Only this newly-created exclusive directory is removed. It never aliases an input/output root.
            struct Temporary{std::filesystem::path path;~Temporary(){std::error_code ec;std::filesystem::remove_all(path,ec);}} temporaryOwner{temporary};
            std::vector<uint32_t> compiled,finalWords;ShaderCompileResult result;
            if(!SlangCompiler::Compile(job,temporary,compiled,result.dependencies,result.diagnostics,error)||!ShaderToolDetail::AddPositionInvariant(compiled,finalWords,error))return false;
            const auto finalFile=temporary/L"final.spv";if(!AtomicFileWriter::Write(finalFile,std::as_bytes(std::span(finalWords)),error))return false;
            std::string validation;if(!SpirvValidator::Validate(sdk,finalFile,temporary/L"validator.log",job.cancellation,validation,error))return false;result.diagnostics+=validation;
            auto asset=std::make_shared<ShaderAsset>();asset->variant.inputFingerprint=std::move(inputFingerprint);asset->spirv=std::move(finalWords);if(!SpirvReflection::Reflect(asset->spirv,asset->metadata,error))return false;
            std::vector<std::byte> reflectionJson;if(!ShaderToolDetail::ReadFile(temporary/L"reflection.json",4*1024*1024,reflectionJson,error))return false;
            const auto reflectionText=reflectionJson.empty()?std::string_view{}:std::string_view(reinterpret_cast<const char*>(reflectionJson.data()),reflectionJson.size());
            if(!SlangCompiler::CheckEntryReflection(reflectionText,asset->metadata,error))return false;
            identity.U32(uint32_t(asset->metadata.entries.size()));for(const auto& entry:asset->metadata.entries){identity.String(entry.name);identity.U32(uint32_t(entry.stage));}identity.String(asset->metadata.bytecodeSha256);
            if(!identity.Error().empty()){error=identity.Error();return false;}if(!ComputeAssetContentHash(identity.Data(),asset->variant.sha256,error))return false;
            if(!Snapshot(root,after,error)||before!=after){error="shader source dependencies changed during compilation";return false;}
            if(job.cancellation.IsCancellationRequested()){error="shader job cancelled before publication";return false;}
            if(!ValidateShaderAsset(*asset,error))return false;result.asset=std::move(asset);output=std::move(result);error.clear();return true;
        }catch(const std::exception& e){error=e.what();return false;}
    }
}
