#include "catpch.h"
#include "AssetPipeline/Importers/CSharpCompiler.h"

#include "Core/Id/StableIdGenerator.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/PlatformServices.h"
#include "ShaderCompiler/ShaderCompileJob.h"
#include <Windows.h>
#include <ShlObj.h>

namespace Catalpa::Tools {
	namespace {
		namespace fs = std::filesystem;
		struct ScratchDirectory {
			fs::path path;
			~ScratchDirectory() {
				std::error_code ignored;
				fs::remove_all(path, ignored);
			}
		};

		fs::path ProgramFilesDirectory(REFKNOWNFOLDERID id) {
			PWSTR value = nullptr;
			if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &value)))
				return {};
			const fs::path path(value);
			CoTaskMemFree(value);
			return path;
		}

		/* VS 和独立 .NET SDK 均提供 Roslyn。导出的游戏仅使用预编译的程序集。 */
		bool FindCompiler(const fs::path& scratch, CancellationToken cancel, fs::path& executable,
		    std::vector<std::wstring>& args, std::string& error) {
			const auto vswhere =
			    ProgramFilesDirectory(FOLDERID_ProgramFilesX86) / L"Microsoft Visual Studio/Installer/vswhere.exe";
			std::string output;
			if (fs::is_regular_file(vswhere) &&
			    ShaderToolDetail::RunProcess(vswhere,
			        {L"-latest", L"-products", L"*", L"-utf8", L"-find", L"MSBuild/Current/Bin/Roslyn/csc.exe"},
			        scratch / "vs.log", cancel, output, error)) {
				const auto end = output.find_first_of("\r\n");
				const auto compiler = fs::u8path(output.substr(0, end));
				if (compiler.is_absolute() && fs::is_regular_file(compiler)) {
					executable = compiler;
					return true;
				}
			}
			if (cancel.IsCancellationRequested())
				return false;
			fs::path dotnet = ProgramFilesDirectory(FOLDERID_ProgramFiles) / L"dotnet/dotnet.exe";
			if (const auto* root = _wgetenv(L"DOTNET_ROOT");
			    root && fs::is_regular_file(fs::path(root) / L"dotnet.exe"))
				dotnet = fs::absolute(fs::path(root) / L"dotnet.exe");
			if (fs::is_regular_file(dotnet) &&
			    ShaderToolDetail::RunProcess(dotnet, {L"--list-sdks"}, scratch / "sdk.log", cancel, output, error)) {
				std::istringstream lines(output);
				std::string line;
				while (std::getline(lines, line)) {
					const auto open = line.find(" ["), close = line.rfind(']');
					if (open == std::string::npos || close == std::string::npos || open + 2 >= close)
						continue;
					const auto compiler = fs::u8path(line.substr(open + 2, close - open - 2)) /
					    fs::u8path(line.substr(0, open)) / "Roslyn/bincore/csc.dll";
					if (compiler.is_absolute() && fs::is_regular_file(compiler)) {
						args = {L"exec", compiler.wstring()};
						executable = dotnet;
					}
				}
				if (!executable.empty())
					return true;
			}
			error = "C# compilation requires Visual Studio with Roslyn or an installed .NET SDK";
			return false;
		}

		fs::path ReferenceDirectory() {
			const auto pack = ProgramFilesDirectory(FOLDERID_ProgramFilesX86) /
			    "Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8";
			if (fs::is_regular_file(pack / "mscorlib.dll"))
				return pack;
			std::array<wchar_t, MAX_PATH> windows{};
			const auto size = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
			if (!size || size >= windows.size())
				throw std::runtime_error("Cannot locate the .NET Framework reference assemblies");
			return fs::path(windows.data()) / "Microsoft.NET/Framework64/v4.0.30319";
		}
	}

	bool CompileCSharp(AssetBuildContext& context, std::span<const std::byte> source, std::vector<std::byte>& assembly,
	    std::string& error) {
		if (source.empty() || source.size() > 1024 * 1024) {
			error = "C# source must contain between 1 byte and 1 MiB";
			return false;
		}
		fs::path host;
		if (!GetExecutableDirectory(host, error))
			return false;
		const auto api = host / "Resources/Managed/Catalpa.Scripting.dll";
		if (!fs::is_regular_file(api)) {
			error = "Missing C# scripting API: " + AssetPathText(api);
			return false;
		}
		StableId unique;
		if (!GenerateStableId(unique, error))
			return false;
		const auto directory = fs::temp_directory_path() / ("CatalpaCSharp-" + unique.ToString());
		if (!fs::create_directory(directory)) {
			error = "Cannot create a private C# compiler directory";
			return false;
		}
		ScratchDirectory scratch{directory};
		fs::path executable;
		std::vector<std::wstring> args;
		if (!FindCompiler(directory, context.request.cancellation, executable, args, error))
			return false;

		/* 编译本次已经读取并计算哈希的源码快照；并行编辑不能改变正在打包的版本。 */
		const auto input = directory / context.request.source.filename();
		const auto dll = directory / ("Script_" + context.Identity("script").ToString() + ".dll");
		if (!AtomicFileWriter::Write(input, source, error))
			return false;
		args.insert(args.end(),
		    {L"/nologo", L"/noconfig", L"/nostdlib+", L"/target:library", L"/optimize+", L"/deterministic+",
		        L"/langversion:7.3", L"/utf8output", L"/codepage:65001", L"/errorendlocation",
		        L"/pathmap:" + directory.wstring() + L"=/_", L"/out:" + dll.wstring(), L"/reference:" + api.wstring()});
		const auto framework = ReferenceDirectory();
		for (const auto* library : {"mscorlib.dll", "System.dll", "System.Core.dll", "System.Numerics.dll"})
			args.push_back(L"/reference:" + (framework / library).wstring());
		args.push_back(input.wstring());
		std::string diagnostics;
		const bool ok = ShaderToolDetail::RunProcess(
		    executable, args, directory / "csc.log", context.request.cancellation, diagnostics, error);
		if (!ok && diagnostics.empty())
			return false;
		if (!diagnostics.empty()) {
			const auto temporaryName = AssetPathText(input.filename());
			const auto location = AssetPathText(context.request.source);
			std::istringstream lines(diagnostics);
			std::string line;
			while (std::getline(lines, line)) {
				const auto end = line.find(temporaryName + "(");
				if (end != std::string::npos)
					line.replace(0, end + temporaryName.size(), location);
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				context.Diagnose(ok ? AssetDiagnosticLevel::Warning : AssetDiagnosticLevel::Error, "C#", line);
				if (!ok && !line.empty())
					error = line;
			}
		}
		return ok && ShaderToolDetail::ReadFile(dll, 4 * 1024 * 1024, assembly, error) &&
		    context.ReserveWorking(assembly.size() * 3, error);
	}
}
