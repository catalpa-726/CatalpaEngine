#include "catpch.h"
#include "Scripting/Backends/CSharp/CSharpScriptBackend.h"
#include "Core/PlatformServices.h"
#include "Core/Serialization/JsonArchive.h"
#include "Scripting/ScriptValueArchive.h"

#define NOMINMAX
#include <windows.h>
#include <metahost.h>
#include <wrl/client.h>
#include <charconv>

namespace Catalpa {
	namespace {
		using Microsoft::WRL::ComPtr;
		using ManagedDispatch = int(__stdcall*)(int, double, const wchar_t*, const wchar_t*, int*, wchar_t**);
		struct Bridge {
			int(__stdcall* create)(
			    const void*, int, const wchar_t*, const wchar_t*, int*, ManagedDispatch*, wchar_t**) = nullptr;
			void(__stdcall* release)(int) = nullptr;
		};

		std::wstring Wide(std::string_view text) {
			if (text.empty())
				return {};
			const int size =
			    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
			if (!size)
				throw std::runtime_error("Invalid UTF-8 script data");
			std::wstring result(size, L'\0');
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), size);
			return result;
		}

		std::string TakeString(BSTR value) {
			if (!value)
				return {};
			const std::unique_ptr<wchar_t, decltype(&SysFreeString)> owner(value, SysFreeString);
			const int length = int(SysStringLen(value));
			const int size = WideCharToMultiByte(CP_UTF8, 0, value, length, nullptr, 0, nullptr, nullptr);
			std::string result(size, '\0');
			WideCharToMultiByte(CP_UTF8, 0, value, length, result.data(), size, nullptr, nullptr);
			return result;
		}

		/** @brief 首个托管行为按需启动 CLR，其余实体复用同一运行时。 */
		Bridge& Runtime() {
			static Bridge bridge = [] {
				std::filesystem::path dir;
				std::string error;
				if (!GetExecutableDirectory(dir, error))
					throw std::runtime_error(error);
				const auto host = dir / "Resources" / "Managed" / "Catalpa.Scripting.dll";
				if (!std::filesystem::is_regular_file(host))
					throw std::runtime_error("Missing Resources/Managed/Catalpa.Scripting.dll");
				ComPtr<ICLRMetaHost> meta;
				ComPtr<ICLRRuntimeInfo> info;
				ComPtr<ICLRRuntimeHost> runtime;
				HRESULT hr = CLRCreateInstance(CLSID_CLRMetaHost, IID_PPV_ARGS(meta.GetAddressOf()));
				if (SUCCEEDED(hr))
					hr = meta->GetRuntime(L"v4.0.30319", IID_PPV_ARGS(info.GetAddressOf()));
				if (SUCCEEDED(hr))
					hr = info->GetInterface(CLSID_CLRRuntimeHost, IID_PPV_ARGS(runtime.GetAddressOf()));
				if (SUCCEEDED(hr))
					hr = runtime->Start();
				Bridge table;
				const auto address = std::to_wstring(reinterpret_cast<uintptr_t>(&table));
				DWORD status = 1;
				if (SUCCEEDED(hr))
					hr = runtime->ExecuteInDefaultAppDomain(
					    host.c_str(), L"Catalpa.Hosting.ScriptHost", L"Bind", address.c_str(), &status);
				if (FAILED(hr) || status != 0 || !table.create || !table.release) {
					std::ostringstream message;
					message << "Cannot load Catalpa.Scripting.dll with .NET Framework 4.8 (HRESULT 0x" << std::hex
					        << uint32_t(hr) << ")";
					throw std::runtime_error(message.str());
				}
				return table;
			}();
			return bridge;
		}

		thread_local ScriptApi* currentApi = nullptr;
		struct CallbackScope {
			explicit CallbackScope(ScriptApi* api) : previous(std::exchange(currentApi, api)) {}
			~CallbackScope() { currentApi = previous; }
			ScriptApi* previous;
		};

		bool WriteValues(const ScriptValues& values, std::wstring& result, std::string& error) {
			ArchiveValue archive;
			ArchiveError reason;
			std::string json;
			if (!EncodeScriptValues(values, archive, error))
				return false;
			if (!JsonArchive::Write(archive, json, reason)) {
				error = reason.ToString();
				return false;
			}
			result = Wide(json);
			return true;
		}

		bool ValidPoint(double x, double y, double z) {
			return std::abs(x) <= FLT_MAX && std::abs(y) <= FLT_MAX && std::abs(z) <= FLT_MAX;
		}
	}

	bool ValidateCSharpAssembly(const ScriptAsset& asset, std::string& error) try {
		if (asset.language != ScriptLanguage::CSharp || asset.assembly.empty() || asset.assembly.size() > INT_MAX) {
			error = "C# behaviour requires a compiled assembly";
			return false;
		}
		BSTR message = nullptr;
		int unusedHandle = 0;
		ManagedDispatch unusedCall = nullptr;
		const int ok = Runtime().create(asset.assembly.data(), int(asset.assembly.size()), nullptr, nullptr,
		    &unusedHandle, &unusedCall, &message);
		error = TakeString(message);
		return ok != 0;
	} catch (const std::exception& ex) {
		error = ex.what();
		return false;
	}

	// 托管端通过以下 C ABI 操作当前回调的事务，后台线程无法借用场景指针。
	extern "C" CATALPA_API int __cdecl CatalpaScriptPosition(int op, double& x, double& y, double& z) noexcept try {
		if (!currentApi)
			return 0;
		std::string error;
		if (op == 0) {
			Math::Vec3 value;
			if (!currentApi->commands.ReadTranslation(currentApi->scene, currentApi->self, value, error))
				return 0;
			x = value.x;
			y = value.y;
			z = value.z;
			return 1;
		}
		if (!ValidPoint(x, y, z))
			return 0;
		const Math::Vec3 value{float(x), float(y), float(z)};
		return op == 1 ? currentApi->SetTranslation(value, error) : op == 2 && currentApi->Translate(value, error);
	} catch (...) {
		return 0;
	}

	extern "C" CATALPA_API int __cdecl CatalpaScriptCreate(
	    const char* name, int length, double x, double y, double z, char* id) noexcept try {
		if (!currentApi || !name || !id || length <= 0 || !ValidPoint(x, y, z))
			return 0;
		std::string error;
		PersistentEntityId created;
		Math::TransformTRS pose;
		pose.translation = {float(x), float(y), float(z)};
		if (!currentApi->commands.Create(
		        currentApi->scene, std::string_view(name, size_t(length)), pose, created, error))
			return 0;
		const auto text = created.ToString();
		std::memcpy(id, text.c_str(), 37);
		return 1;
	} catch (...) {
		return 0;
	}

	extern "C" CATALPA_API int64_t __cdecl CatalpaScriptAction(
	    int op, const char* a, int lengthA, const char* b, int lengthB) noexcept try {
		if (!currentApi || lengthA < 0 || lengthB < 0 || (!a && lengthA) || (!b && lengthB))
			return 0;
		const std::string_view first(a ? a : "", size_t(lengthA)), second(b ? b : "", size_t(lengthB));
		std::string error;
		switch (op) {
		case 0: {
			PersistentEntityId id;
			return PersistentEntityId::TryParse(first, id) &&
			    currentApi->commands.Destroy(currentApi->scene, id, error);
		}
		case 1:
			return currentApi->Emit(first, second, error);
		case 2: {
			uint64_t ticket = 0;
			return currentApi->AsyncChecksum(first, second, ticket, error) ? int64_t(ticket) : 0;
		}
		case 3: {
			uint64_t ticket = 0;
			const auto decoded = std::from_chars(first.data(), first.data() + first.size(), ticket);
			if (decoded.ec != std::errc{} || decoded.ptr != first.data() + first.size() || !ticket ||
			    currentApi->cancelledAsync.size() >= 16)
				return 0;
			currentApi->cancelledAsync.push_back(ticket);
			return 1;
		}
		case 4: {
			AssetId scene;
			if (!AssetId::TryParse(first, scene))
				return 0;
			currentApi->requestedScene = scene;
			return 1;
		}
		default:
			return 0;
		}
	} catch (...) {
		return 0;
	}

	CSharpScriptBackend::~CSharpScriptBackend() {
		if (m_Handle)
			Runtime().release(m_Handle);
	}

	bool CSharpScriptBackend::Start(
	    const ScriptAsset& asset, const ScriptValues& values, ScriptApi& api, std::string& error) try {
		m_SourceLabel = asset.sourceLabel;
		if (asset.assembly.empty() || asset.assembly.size() > INT_MAX || m_Handle) {
			error = "C# behaviour requires a compiled assembly";
			return false;
		}
		std::wstring data;
		if (!WriteValues(values, data, error))
			return false;
		BSTR message = nullptr;
		const auto entity = Wide(api.self.persistent.ToString());
		const int ok = Runtime().create(asset.assembly.data(), int(asset.assembly.size()), entity.c_str(), data.c_str(),
		    &m_Handle, &m_Dispatch, &message);
		auto detail = TakeString(message);
		if (!ok) {
			error = m_SourceLabel + ": " + detail;
			return false;
		}
		return api.reloading || Call("start", 0, api, error);
	} catch (const std::exception& ex) {
		error = m_SourceLabel + ": " + ex.what();
		return false;
	}

	bool CSharpScriptBackend::Invoke(int op, double time, const wchar_t* channel, const wchar_t* data, ScriptApi* api,
	    std::string& result, int& schema, std::string& error) {
		if (!m_Dispatch) {
			error = "C# behaviour is not active";
			return false;
		}
		CallbackScope scope(api);
		BSTR message = nullptr;
		const int ok = m_Dispatch(op, time, channel, data, &schema, &message);
		result = TakeString(message);
		if (!ok) {
			if (api) {
				api->commands.Clear();
				api->emissions.clear();
				api->asyncRequests.clear();
				api->cancelledAsync.clear();
				api->requestedScene.reset();
			}
			error = m_SourceLabel + ": " + result;
			return false;
		}
		return true;
	}

	bool CSharpScriptBackend::Call(std::string_view event, double seconds, ScriptApi& api, std::string& error) {
		const int op = event == "start" ? 0 : event == "update" ? 1 : event == "stop" ? 2 : event == "reload" ? 3 : -1;
		int unused = 0;
		std::string detail;
		return Invoke(op, seconds, nullptr, nullptr, &api, detail, unused, error);
	}

	bool CSharpScriptBackend::Event(
	    std::string_view channel, const ScriptValues& values, ScriptApi& api, std::string& error) try {
		std::wstring data;
		int unused = 0;
		std::string detail;
		return WriteValues(values, data, error) &&
		    Invoke(4, 0, Wide(channel).c_str(), data.c_str(), &api, detail, unused, error);
	} catch (const std::exception& ex) {
		error = ex.what();
		return false;
	}

	bool CSharpScriptBackend::ExportState(uint32_t& schema, ScriptValues& values, std::string& error) {
		std::string result;
		int version = 0;
		if (!Invoke(5, 0, nullptr, nullptr, nullptr, result, version, error))
			return false;
		std::unique_ptr<ArchiveValue> archive;
		ArchiveError reason;
		if (!JsonArchive::Parse(result, archive, reason)) {
			error = reason.ToString();
			return false;
		}
		if (!DecodeScriptValues(*archive, values, error))
			return false;
		schema = uint32_t(version);
		return true;
	}

	bool CSharpScriptBackend::ImportState(
	    uint32_t schema, const ScriptValues& values, ScriptApi& api, std::string& error) try {
		std::wstring data;
		std::string result;
		int unused = 0;
		return WriteValues(values, data, error) &&
		    Invoke(6, schema, nullptr, data.c_str(), &api, result, unused, error);
	} catch (const std::exception& ex) {
		error = ex.what();
		return false;
	}
}
