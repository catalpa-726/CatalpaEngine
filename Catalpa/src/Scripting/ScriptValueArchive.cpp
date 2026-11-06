#include "catpch.h"
#include "Scripting/ScriptValueArchive.h"

namespace Catalpa {
    bool EncodeScriptValues(const ScriptValues& values,ArchiveValue& output,std::string& error) try {
        if(!ValidateScriptValues(values,error)) return false;
        ArchiveValue::Object result;
        for(const auto& [name,value]:values) {
            ArchiveValue::Object tagged;
            std::visit([&](const auto& v) {
                using T=std::decay_t<decltype(v)>;
                if constexpr(std::is_same_v<T,Math::Vec3>) {
                    tagged["type"]={std::string("float3")};
                    tagged["value"]={ArchiveValue::Array{{double(v.x)},{double(v.y)},{double(v.z)}}};
                } else if constexpr(std::is_same_v<T,PersistentEntityId>) {
                    tagged["type"]={std::string("entity")};tagged["value"]={v.ToString()};
                } else {
                    tagged["type"]={std::string(std::is_same_v<T,bool>?"bool":std::is_same_v<T,int64_t>?"int64":std::is_same_v<T,double>?"number":"string")};
                    tagged["value"]={v};
                }
            },value);
            result.emplace(name,ArchiveValue{std::move(tagged)});
        }
        output={std::move(result)};error.clear();return true;
    } catch(const std::exception& exception) {error=exception.what();return false;}

    bool DecodeScriptValues(const ArchiveValue& archive,ScriptValues& output,std::string& error) try {
        const auto* fields=std::get_if<ArchiveValue::Object>(&archive.data);
        if(!fields || fields->size()>256) {error="Script properties require a bounded object";return false;}
        ScriptValues result;
        for(const auto& [name,entry]:*fields) {
            const auto* object=std::get_if<ArchiveValue::Object>(&entry.data);
            const auto* tag=entry.Find("type"),*value=entry.Find("value");
            const auto* type=tag?std::get_if<std::string>(&tag->data):nullptr;
            if(!object || object->size()!=2 || !type || !value) {error="Script value needs an explicit type and value";return false;}
            if(*type=="bool" && std::holds_alternative<bool>(value->data)) result[name]=std::get<bool>(value->data);
            else if(*type=="int64") {
                if(const auto* integer=std::get_if<int64_t>(&value->data)) result[name]=*integer;
                else if(const auto* integer=std::get_if<uint64_t>(&value->data);integer && *integer<=INT64_MAX) result[name]=int64_t(*integer);
                else {error="Script int64 range/type mismatch";return false;}
            } else if(*type=="number") {
                if(const auto* n=std::get_if<double>(&value->data)) result[name]=*n;
                else if(const auto* n=std::get_if<int64_t>(&value->data)) result[name]=double(*n);
                else if(const auto* n=std::get_if<uint64_t>(&value->data)) result[name]=double(*n);
                else {error="Script number type mismatch";return false;}
            } else if(*type=="string" && std::holds_alternative<std::string>(value->data)) result[name]=std::get<std::string>(value->data);
            else if(*type=="entity") {
                const auto* text=std::get_if<std::string>(&value->data);PersistentEntityId id;
                if(!text || !PersistentEntityId::TryParse(*text,id)) {error="Invalid script entity reference";return false;}
                result[name]=id;
            } else if(*type=="float3") {
                const auto* array=std::get_if<ArchiveValue::Array>(&value->data);
                if(!array || array->size()!=3) {error="Script float3 shape mismatch";return false;}
                Math::Vec3 vector;
                for(size_t i=0;i<3;++i) {
                    const auto& coordinate=(*array)[i].data;
                    double number=0;
                    if(const auto* n=std::get_if<double>(&coordinate)) number=*n;
                    else if(const auto* n=std::get_if<int64_t>(&coordinate)) number=double(*n);
                    else if(const auto* n=std::get_if<uint64_t>(&coordinate)) number=double(*n);
                    else {error="Script float3 coordinate is not a number";return false;}
                    if(!std::isfinite(number) || std::abs(number)>std::numeric_limits<float>::max() ||
                        (number != 0 && float(number) == 0)) {error="Script float3 range exceeded";return false;}
                    vector[i]=float(number);
                }
                result[name]=vector;
            } else {error="Unknown or mismatched script value type: "+*type;return false;}
        }
        if(!ValidateScriptValues(result,error)) return false;
        output=std::move(result);return true;
    } catch(const std::exception& exception) {error=exception.what();return false;}
}
