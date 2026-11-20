#pragma once
#include "Scripting/Backends/IScriptBackend.h"

namespace Catalpa {
	/** @brief 为一个实体持有托管行为实例，程序集版本的运行域由同版本实例共享。 */
	class CSharpScriptBackend final : public IScriptBackend {
	public:
		~CSharpScriptBackend() override;
		bool Start(const ScriptAsset&, const ScriptValues&, ScriptApi&, std::string& error) override;
		bool Call(std::string_view event, double seconds, ScriptApi&, std::string& error) override;
		bool Event(std::string_view channel, const ScriptValues&, ScriptApi&, std::string& error) override;
		bool ExportState(uint32_t& schema, ScriptValues&, std::string& error) override;
		bool ImportState(uint32_t schema, const ScriptValues&, ScriptApi&, std::string& error) override;

	private:
		using Dispatch = int(__stdcall*)(int, double, const wchar_t*, const wchar_t*, int*, wchar_t**);
		bool Invoke(int op, double time, const wchar_t* channel, const wchar_t* data, ScriptApi* api,
		    std::string& result, int& schema, std::string& error);
		int m_Handle = 0;
		Dispatch m_Dispatch = nullptr;
		std::string m_SourceLabel;
	};
}
