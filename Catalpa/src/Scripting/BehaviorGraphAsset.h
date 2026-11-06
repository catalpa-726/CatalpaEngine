#pragma once
#include "Scripting/ScriptValue.h"
#include "Assets/AssetId.h"

namespace Catalpa {
    struct BehaviorNode {
        StableId id;
        std::string type, variable, event;
        double number = 0;
        Math::Vec3 vector{0};
        std::map<std::string, StableId, std::less<>> flow;
        float x = 0, y = 0;
    };
    struct BehaviorGraphAsset {
        std::vector<BehaviorNode> nodes;
        std::map<std::string, double, std::less<>> variables;
    };
    struct BehaviorNodeDescription {
        std::string_view name;
        std::vector<std::string_view> outputs;
        bool entry = false;
    };
    CATALPA_API std::span<const BehaviorNodeDescription> BehaviorNodeTypes();
    CATALPA_API bool ReadBehaviorGraph(std::string_view, BehaviorGraphAsset&, std::string& error);
    CATALPA_API bool WriteBehaviorGraph(const BehaviorGraphAsset&, std::string&, std::string& error);
    CATALPA_API bool ValidateBehaviorGraph(const BehaviorGraphAsset&, std::string& error);
    CATALPA_API bool ValidateBehaviorVariables(const ScriptValues&, std::string& error);
    CATALPA_API bool RegisterBehaviorGraphAssetLoader(class AssetManager&, std::string& error);
}
