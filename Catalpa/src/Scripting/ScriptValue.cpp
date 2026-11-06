#include "catpch.h"
#include "Scripting/ScriptValue.h"
#include "Core/Base.h"

namespace Catalpa {
    bool ValidateScriptValues(const ScriptValues& values,std::string& error) {
        if(values.size()>256) {error="Script property count exceeded";return false;}
        for(const auto& [name,value]:values) {
            if(name.empty() || name.size()>128 || name.find('\0')!=name.npos) {error="Invalid script property name";return false;}
            const bool valid=std::visit([](const auto& v) {
                using T=std::decay_t<decltype(v)>;
                if constexpr(std::is_same_v<T,double>) return std::isfinite(v) && std::abs(v)<=std::numeric_limits<float>::max();
                else if constexpr(std::is_same_v<T,Math::Vec3>) return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
                else if constexpr(std::is_same_v<T,std::string>) return v.size()<=65536 && v.find('\0')==v.npos;
                else if constexpr(std::is_same_v<T,PersistentEntityId>) return v.HasValue();
                else return true;
            },value);
            if(!valid) {error="Invalid script property value: "+name;return false;}
        }
        error.clear();return true;
    }
}
