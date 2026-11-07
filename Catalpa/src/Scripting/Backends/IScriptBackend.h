#pragma once
#include "Scripting/ScriptAsset.h"
#include "Scripting/ScriptApi.h"
#include "Scripting/ScriptValue.h"

namespace Catalpa {
    class IScriptBackend {
      public:
        virtual ~IScriptBackend() = default;
        virtual bool Start(const ScriptAsset&, const ScriptValues&, ScriptApi&, std::string& error) = 0;
        virtual bool Call(std::string_view event, double seconds, ScriptApi&, std::string& error) = 0;
        virtual bool Event(std::string_view channel, const ScriptValues&, ScriptApi&, std::string& error) = 0;
        virtual bool ExportState(uint32_t& schema, ScriptValues&, std::string& error) = 0;
        virtual bool ImportState(uint32_t schema, const ScriptValues&, ScriptApi&, std::string& error) = 0;
    };
}
