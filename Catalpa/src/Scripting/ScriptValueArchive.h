#pragma once
#include "Scripting/ScriptValue.h"
#include "Core/Serialization/Archive.h"

namespace Catalpa {
    CATALPA_API bool EncodeScriptValues(const ScriptValues&,ArchiveValue&,std::string& error);
    CATALPA_API bool DecodeScriptValues(const ArchiveValue&,ScriptValues&,std::string& error);
}
