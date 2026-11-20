#include "catpch.h"
#include "Platform/ExternalScriptEditor.h"
#include "Core/PlatformServices.h"
#include "Panels/EditorText.h"
#include <windows.h>
#include <cwchar>

namespace Catalpa::Editor {
	namespace fs = std::filesystem;
	namespace {
		/** @brief 从 Windows 注册的 App Paths 查找 IDE，包括用户选择的非默认安装位置。 */
		fs::path RegisteredApp(const wchar_t* name) {
			const auto key = std::wstring(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\") + name;
			for (auto hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
				std::wstring value(32768, L'\0');
				DWORD size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
				if (RegGetValueW(hive, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS)
					continue;
				value.resize(wcslen(value.c_str()));
				if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
					value = value.substr(1, value.size() - 2);
				std::error_code ec;
				if (fs::path(value).is_absolute() && fs::is_regular_file(value, ec))
					return value;
			}
			return {};
		}

		fs::path InstalledEditor(bool code) {
			if (auto path = RegisteredApp(code ? L"Code.exe" : L"devenv.exe"); !path.empty())
				return path;
			for (const auto* variable : {L"LOCALAPPDATA", L"ProgramFiles", L"ProgramFiles(x86)"}) {
				const auto* base = _wgetenv(variable);
				if (!base || !*base)
					continue;
				std::error_code ec;
				if (code) {
					for (const auto* sub : {L"Programs/Microsoft VS Code/Code.exe", L"Microsoft VS Code/Code.exe"}) {
						auto path = fs::path(base) / sub;
						if (fs::is_regular_file(path, ec))
							return path;
					}
				} else {
					for (const auto* year : {L"18", L"2022", L"2019"}) {
						for (const auto* edition : {L"Enterprise", L"Professional", L"Community", L"Preview"}) {
							auto path = fs::path(base) / L"Microsoft Visual Studio" / year / edition / L"Common7/IDE/devenv.exe";
							if (fs::is_regular_file(path, ec))
								return path;
						}
					}
				}
			}
			return {};
		}

		/** @brief 按 Windows argv 规则引用单个参数，保留空格、末尾反斜杠和内嵌双引号。 */
		std::wstring QuoteArgument(std::wstring_view argument) {
			std::wstring quoted = L"\"";
			size_t slashes = 0;
			for (wchar_t ch : argument) {
				if (ch == L'\\') {
					++slashes;
					continue;
				}
				quoted.append(ch == L'"' ? 2 * slashes + 1 : slashes, L'\\');
				quoted += ch;
				slashes = 0;
			}
			quoted.append(2 * slashes, L'\\');
			return quoted + L'"';
		}
	}

	bool CreateBehaviourFile(const fs::path& script, const fs::path& root, std::string& error) try {
		if (!script.is_absolute() || _wcsicmp(script.extension().c_str(), L".cs") ||
		    !IsPathWithinRoot(fs::canonical(script.parent_path()), fs::canonical(root))) {
			error = UiText("script.choose_cs", "Choose a .cs file inside the content folder.");
			return false;
		}
		std::wstring identifier;
		bool containsLetterOrDigit = false;
		for (const wchar_t ch : script.stem().wstring()) {
			WORD type = 0;
			const bool alphanumeric = GetStringTypeW(CT_CTYPE1, &ch, 1, &type) &&
			    (type & (C1_ALPHA | C1_DIGIT)) != 0 && !(ch >= 0xd800 && ch <= 0xdfff);
			identifier += alphanumeric ? ch : L'_';
			containsLetterOrDigit |= alphanumeric;
		}
		WORD firstType = 0;
		if (!containsLetterOrDigit)
			identifier = L"Script";
		else if (GetStringTypeW(CT_CTYPE1, identifier.data(), 1, &firstType) && (firstType & C1_DIGIT))
			identifier.insert(0, L"Script");
		const auto name = fs::path(identifier).u8string();
		const std::string source = "public sealed class @" + std::string(name.begin(), name.end()) +
		    " : global::Catalpa.Behaviour\n{\n"
		    "    public override void OnStart()\n    {\n    }\n\n"
		    "    public override void OnUpdate(double deltaTime)\n    {\n    }\n}\n";
		HANDLE handle = CreateFileW(script.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			error = UiText("script.create_failed", "Could not create the script. Choose a new file name.");
			return false;
		}
		DWORD written = 0;
		const bool ok = WriteFile(handle, source.data(), static_cast<DWORD>(source.size()), &written, nullptr) &&
		    written == source.size();
		if (!ok) {
			FILE_DISPOSITION_INFO remove{TRUE};
			SetFileInformationByHandle(handle, FileDispositionInfo, &remove, sizeof(remove));
			error = UiText("script.write_failed", "Could not write the script file.");
		}
		CloseHandle(handle);
		return ok;
	} catch (const std::exception& failure) {
		error = failure.what();
		return false;
	}

	bool LaunchScriptEditor(const std::string& preference, const fs::path& script, const fs::path& project,
	    std::string& error) try {
		const auto file = fs::canonical(script);
		fs::path exe;
		if (preference == "auto" || preference == "visual-studio")
			exe = InstalledEditor(false);
		if (preference == "vscode" || (preference == "auto" && exe.empty()))
			exe = InstalledEditor(true);
		if (preference != "auto" && preference != "visual-studio" && preference != "vscode")
			exe = fs::u8path(preference);
		if (!exe.is_absolute() || _wcsicmp(exe.extension().c_str(), L".exe") || !fs::is_regular_file(exe)) {
			error = UiText("script.ide_missing", "Select an installed script editor in Settings.");
			return false;
		}
		std::vector<std::wstring> args{exe.wstring()};
		if (!project.empty()) {
			/* VS 接收项目和源文件两个独立路径，可同时加载项目并打开外链脚本。 */
			if (_wcsicmp(exe.filename().c_str(), L"devenv.exe") == 0)
				args.push_back(project.wstring());
			else if (_wcsicmp(exe.filename().c_str(), L"Code.exe") == 0)
				args.insert(args.end(), {L"--reuse-window", project.parent_path().wstring()});
		}
		args.push_back(file.wstring());
		std::wstring command;
		for (const auto& arg : args) {
			if (!command.empty())
				command += L' ';
			command += QuoteArgument(arg);
		}
		STARTUPINFOW startup{sizeof(STARTUPINFOW)};
		PROCESS_INFORMATION process{};
		if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
		        exe.parent_path().c_str(), &startup, &process)) {
			error = UiText("script.launch_failed", "Could not launch the selected script editor.");
			return false;
		}
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return true;
	} catch (const std::exception& failure) {
		error = failure.what();
		return false;
	}
}
