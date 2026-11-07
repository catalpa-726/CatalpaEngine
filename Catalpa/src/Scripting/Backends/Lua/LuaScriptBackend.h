#pragma once
#include "Scripting/Backends/IScriptBackend.h"

namespace Catalpa {
    struct ScriptMemoryBudget {
        size_t used = 0, limit = 64 * 1024 * 1024;
        std::thread::id owner = std::this_thread::get_id();
    };
    // Lua C API、VM 内存与 hook 均留在实现中，外部只能交付独占作者值。
    class CATALPA_API LuaScriptBackend final : public IScriptBackend {
      public:
        LuaScriptBackend();
        explicit LuaScriptBackend(std::shared_ptr<ScriptMemoryBudget>);
        ~LuaScriptBackend() override;
        LuaScriptBackend(const LuaScriptBackend&) = delete;
        LuaScriptBackend& operator=(const LuaScriptBackend&) = delete;
        bool Start(const ScriptAsset&, const ScriptValues&, ScriptApi&, std::string&) override;
        bool Call(std::string_view, double, ScriptApi&, std::string&) override;
        bool Event(std::string_view, const ScriptValues&, ScriptApi&, std::string&) override;
        bool ExportState(uint32_t&, ScriptValues&, std::string&) override;
        bool ImportState(uint32_t, const ScriptValues&, ScriptApi&, std::string&) override;
        bool SetModules(std::map<std::string, std::shared_ptr<const ScriptAsset>, std::less<>>, std::string&);
        static bool CheckSource(std::string_view source, std::string& error);
      private:
        struct State;
        std::unique_ptr<State> m_State;
    };
}
