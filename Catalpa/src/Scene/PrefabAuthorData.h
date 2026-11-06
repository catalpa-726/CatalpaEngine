#pragma once
#include "Scene/SceneSerializer.h"
#include "Core/Serialization/AuthorJson.h"

// 实现私有的比较表示；固定版本引用转换为资产身份，不比较 GPU 或 C++ 对象地址。
namespace Catalpa::PrefabDetail {
    inline auto Inverse(const std::map<PersistentEntityId, PersistentEntityId>& mapping) {
        std::map<PersistentEntityId,PersistentEntityId> inverse;
        for(const auto& [local,instance]:mapping) inverse.emplace(instance,local);
        return inverse;
    }
    inline SceneEntityAsset LocalizeWithInverse(SceneEntityAsset node,
        const std::map<PersistentEntityId, PersistentEntityId>& inverse) {
        if(const auto id=inverse.find(node.id);id!=inverse.end()) node.id=id->second;
        const auto values=[&](ScriptValues& fields) {
            for(auto& [name,value]:fields) if(auto* id=std::get_if<PersistentEntityId>(&value))
                if(const auto local=inverse.find(*id);local!=inverse.end()) *id=local->second;
        };
        if(node.script) values(node.script->properties);
        if(node.behavior) values(node.behavior->variables);
        return node;
    }
    inline SceneEntityAsset Localize(SceneEntityAsset node, const std::map<PersistentEntityId, PersistentEntityId>& mapping) {
        return LocalizeWithInverse(std::move(node), Inverse(mapping));
    }
    inline AuthorJson::Json EntityText(SceneEntityAsset node) {
        node.parent = {};
        node.prefab.reset();
        SceneAsset one;
        one.entities.push_back(std::move(node));
        std::string text, error;
        if (!SceneSerializer::Write(one, text, error)) throw std::invalid_argument(error);
        return AuthorJson::Json::parse(text).at("entities").front();
    }
    inline bool AllowedPath(std::string_view path) {
        return path == "/name" || path == "/translation" || path.starts_with("/translation/") ||
               path == "/rotation" || path.starts_with("/rotation/") || path == "/scale" ||
               path.starts_with("/scale/") || path == "/mesh/color" || path.starts_with("/mesh/color/") ||
               path == "/mesh/visible";
    }
}
