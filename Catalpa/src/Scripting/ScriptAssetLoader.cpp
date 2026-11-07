#include "catpch.h"
#include "Scripting/ScriptAsset.h"
#include "Core/Serialization/AuthorJson.h"
#include "Assets/AssetManager.h"

namespace Catalpa {
    namespace {
        template <class Map> size_t ModuleMapHeap(const Map& values) {
            size_t bytes = 4 * sizeof(void*);
            for (const auto& [name, value] : values)
                bytes += sizeof(typename Map::value_type) + 4 * sizeof(void*) + name.capacity() + 1;
            return bytes;
        }
        size_t ScriptResidentBytes(const ScriptAsset& value) {
            // 只计本版本的源码与两张模块表；被固定引用的模块资产由其自己的版本额度保活。
            return sizeof(value) + value.source.capacity() + 1 + ModuleMapHeap(value.modules) +
                   ModuleMapHeap(value.moduleVersions);
        }
    } // namespace
    bool ReadScriptAsset(std::string_view text, ScriptAsset& output, std::string& error) try {
        using namespace AuthorJson;
        const auto v = Parse(text, 4 * 1024 * 1024);
        Fields(v, {"schemaVersion", "language", "source", "modules"});
        if (Unsigned(v.at("schemaVersion")) != 1 || v.at("language") != "lua")
            throw std::invalid_argument("Unsupported script language/version");
        ScriptAsset asset;
        asset.source = v.at("source").get<std::string>();
        if (asset.source.size() > 1024 * 1024 || asset.source.find('\0') != std::string::npos ||
            (!asset.source.empty() && asset.source[0] == '\x1b'))
            throw std::invalid_argument("Script requires bounded Lua source text");
        if (!v.at("modules").is_object() || v.at("modules").size() > 64)
            throw std::invalid_argument("Script module limit exceeded");
        for (auto it = v.at("modules").begin(); it != v.at("modules").end(); ++it) {
            if (it.key().empty() || it.key().size() > 128 || it.key().find('\0')!=std::string::npos) throw std::invalid_argument("Invalid module name");
            asset.modules.emplace(it.key(), Identity<AssetId>(it.value()));
        }
        output = std::move(asset);
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }

    bool WriteScriptAsset(const ScriptAsset& asset, std::string& output, std::string& error) {
        AuthorJson::Json modules = AuthorJson::Json::object();
        for (const auto& [key, id] : asset.modules) modules[key] = id.ToString();
        const auto text = AuthorJson::Json{{"schemaVersion", 1}, {"language", "lua"},
            {"source", asset.source}, {"modules", modules}}.dump(2);
        ScriptAsset checked;
        if (!ReadScriptAsset(text, checked, error)) return false;
        output = text;
        return true;
    }

    bool RegisterScriptAssetLoader(AssetManager& manager, std::string& error) {
        AssetLoader loader;
        if (!AssetType::TryParse("script", loader.type)) return false;
        loader.cppType = typeid(ScriptAsset);
        loader.decode = [](const AssetContainer& c, const AssetDecodeContext& context,
                           AssetLoadResult& output, std::string& error) {
            if (c.payloadVersion != 1 || context.cancellation.IsCancellationRequested() ||
                c.payload.size() > context.maxResidentBytes / 3) {
                error = "Script payload version, budget or cancellation";
                return false;
            }
            auto asset = std::make_shared<ScriptAsset>();
            if (!ReadScriptAsset(std::string_view(reinterpret_cast<const char*>(c.payload.data()), c.payload.size()),
                                 *asset, error)) return false;
            for (const auto& [name,id] : asset->modules) {
                const auto dependency=std::find_if(context.dependencies.begin(),context.dependencies.end(),[&](const auto& d){return d.id==id && d.cppType==typeid(ScriptAsset);});
                if(dependency==context.dependencies.end()) {error="Declared script module dependency is missing: "+name;return false;}
                AssetVersionRef<ScriptAsset> fixed;
                if(!AssetVersionRef<ScriptAsset>::TryCreate(id,dependency->version,std::static_pointer_cast<const ScriptAsset>(dependency->value),fixed,error)) return false;
                asset->moduleVersions.emplace(name,std::move(fixed));
            }
            const size_t resident = ScriptResidentBytes(*asset);
            if (resident > context.maxResidentBytes) {
                error = "Script module tables exceed resident budget";
                return false;
            }
            output = MakeAssetLoadResult<ScriptAsset>(asset, resident);
            error.clear();
            return true;
        };
        return manager.RegisterLoader(std::move(loader), error);
    }
}
