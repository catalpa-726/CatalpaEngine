#include "Application/WorkspaceEditing.h"
#include "Core/Application.h"
#include "Commands/SceneCommands.h"
#include "Commands/PrefabCommands.h"
#include "Documents/PrefabDocument.h"
#include "Scene/Components/PrefabInstanceComponent.h"
#include "Scene/PrefabReconciler.h"
#include "Scripting/BehaviorGraphAsset.h"
#include "Core/Serialization/AuthorJson.h"
#include "imgui.h"

namespace Catalpa::Editor {
    namespace {
        bool Properties(ScriptValues& values, std::string& error) {
            bool changed=false;
            std::optional<std::string> removed;
            for(auto& [name,value]:values) {
                ImGui::PushID(name.c_str());
                changed|=std::visit([&](auto& v)->bool {
                    using T=std::decay_t<decltype(v)>;
                    if constexpr(std::is_same_v<T,bool>) return ImGui::Checkbox(name.c_str(),&v);
                    else if constexpr(std::is_same_v<T,int64_t>) return ImGui::InputScalar(name.c_str(),ImGuiDataType_S64,&v,nullptr,nullptr,nullptr,ImGuiInputTextFlags_EnterReturnsTrue);
                    else if constexpr(std::is_same_v<T,double>) return ImGui::InputDouble(name.c_str(),&v,0,0,"%.8g",ImGuiInputTextFlags_EnterReturnsTrue);
                    else if constexpr(std::is_same_v<T,Math::Vec3>) return ImGui::InputFloat3(name.c_str(),&v.x,"%.6g",ImGuiInputTextFlags_EnterReturnsTrue);
                    else {
                        std::string text;
                        if constexpr(std::is_same_v<T,std::string>) text=v;else text=v.ToString();
                        std::vector<char> buffer(std::max<size_t>(text.size()+1,65537),0);
                        std::copy(text.begin(),text.end(),buffer.begin());
                        if(ImGui::InputText(name.c_str(),buffer.data(),buffer.size(),ImGuiInputTextFlags_EnterReturnsTrue)) {
                            if constexpr(std::is_same_v<T,std::string>) {v=buffer.data();return true;}
                            else {
                                PersistentEntityId id;
                                if (PersistentEntityId::TryParse(buffer.data(), id)) { v = id; return true; }
                                error = "Enter a nonzero persistent entity ID";
                            }
                        }
                        return false;
                    }
                },value);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove property")) removed = name;
                ImGui::PopID();
            }
            if (removed) { values.erase(*removed); changed = true; }
            return changed;
        }
    }

    void WorkspaceEditing::AuthorComponentControls(SceneDocument& doc) {
        auto* scene=doc.Resolve();const auto entity=m_Selection.Resolve(doc);
        if(!scene || !entity.HasValue() || doc.Playing()) return;
        const auto id=scene->MakeReference(entity).persistent;
        ImGui::Separator();
        ImGui::PushID("ScriptComponent");
        ScriptComponent script;
        const bool scripted=scene->GetWorld().Has<ScriptComponent>(entity) && scene->GetWorld().Get(entity,script).IsSuccess();
        if(scripted) {
            ImGui::TextWrapped("Script: %s",script.script.ToString().c_str());
            bool changed=ImGui::Checkbox("Script enabled",&script.enabled);
            changed|=Properties(script.properties, m_Error);
            ImGui::InputText("New script property",m_PropertyName.data(),m_PropertyName.size());
            ImGui::Combo("Property type", &m_PropertyType, "Bool\0Integer\0Number\0String\0Float3\0Entity\0");
            if (ImGui::Button("Add property") && m_PropertyName[0]) {
                ScriptValue value;
                switch (m_PropertyType) {
                    case 0: value = false; break;
                    case 1: value = int64_t(0); break;
                    case 2: value = 0.0; break;
                    case 3: value = std::string{}; break;
                    case 4: value = Math::Vec3{0}; break;
                    default: value = id; break;
                }
                if (!script.properties.emplace(m_PropertyName.data(), std::move(value)).second)
                    m_Error = "Script property already exists";
                else changed = true;
            }
            if(changed) EditScript(doc,id,script,m_Error);
            if(ImGui::Button("Remove script")) EditScript(doc,id,std::nullopt,m_Error);
        }
        ImGui::InputText("Script asset ID",m_ScriptAsset.data(),m_ScriptAsset.size());
        if(ImGui::Button(scripted?"Assign different script":"Add script")) {
            AssetId asset;
            if(!AssetId::TryParse(m_ScriptAsset.data(),asset)) m_Error="Enter a cooked script asset ID";
            else {script.script=asset;EditScript(doc,id,script,m_Error);}
        }
        ImGui::PopID();
        ImGui::PushID("BehaviorGraphComponent");
        BehaviorGraphComponent behavior;
        const bool graphed=scene->GetWorld().Has<BehaviorGraphComponent>(entity) && scene->GetWorld().Get(entity,behavior).IsSuccess();
        if(graphed) {
            ImGui::TextWrapped("Behavior: %s",behavior.graph.ToString().c_str());
            bool changed=ImGui::Checkbox("Behavior enabled",&behavior.enabled);
            changed|=Properties(behavior.variables, m_Error);
            AssetVersionRef<BehaviorGraphAsset> program;
            bool fallback = false;
            std::string ignored;
            if (m_App.GetAssets().Acquire(behavior.graph, program, fallback, ignored) && !fallback &&
                ImGui::BeginCombo("Add initial variable override", "Choose a declared variable")) {
                for (const auto& [name, value] : program.Get()->variables)
                    if (!behavior.variables.contains(name) && ImGui::Selectable(name.c_str())) {
                        behavior.variables.emplace(name, value);
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            if(changed) EditBehaviorGraph(doc,id,behavior,m_Error);
            if(ImGui::Button("Remove behavior")) EditBehaviorGraph(doc,id,std::nullopt,m_Error);
        }
        ImGui::InputText("Behavior asset ID",m_BehaviorAsset.data(),m_BehaviorAsset.size());
        if(ImGui::Button(graphed?"Assign different behavior":"Add behavior")) {
            AssetId asset;
            if(!AssetId::TryParse(m_BehaviorAsset.data(),asset)) m_Error="Enter a cooked behavior graph asset ID";
            else {behavior.graph=asset;EditBehaviorGraph(doc,id,behavior,m_Error);}
        }
        ImGui::PopID();
        PrefabInstanceComponent prefab;
        if(scene->GetWorld().Has<PrefabInstanceComponent>(entity) && scene->GetWorld().Get(entity,prefab).IsSuccess()) {
            ImGui::Separator();
            ImGui::TextWrapped("Prefab %s / version %llu",prefab.source.ToString().c_str(),static_cast<unsigned long long>(prefab.sourceVersion));
            if (ImGui::Button("Update base and preserve overrides") && UpdateAuthorPrefab(doc, m_App.GetAssets(), id, false, m_Error)) {
                m_Selection.Select(doc, scene->Resolve(id));
                m_OverrideRoot = {};
            }
            if (ImGui::Button("Revert supported overrides to loaded base") && UpdateAuthorPrefab(doc, m_App.GetAssets(), id, true, m_Error)) {
                m_Selection.Select(doc, scene->Resolve(id));
                m_OverrideRoot = {};
            }
            const bool refresh = ImGui::Button("Refresh current override paths");
            if (refresh || m_OverrideRoot != id) {
                m_OverrideRoot = id;
                m_OverridePaths.clear();
                (void)PrefabReconciler::DescribeOverrides(*scene, id, m_OverridePaths, m_Error);
            }
            for (const auto& path : m_OverridePaths) ImGui::TextWrapped("Override %s", path.c_str());
        }
    }

    void WorkspaceEditing::PrefabMenu() {
        auto* scene=ActiveScene();
        if(!scene || scene->Playing()) return;
        if(ImGui::MenuItem("Create prefab from selected subtree")) {
            const auto entity=m_Selection.Resolve(*scene);
            PrefabAsset prefab;std::string text;
            if(entity.HasValue() && PrefabDocument::FromSelection(*scene,scene->Resolve()->MakeReference(entity).persistent,prefab,m_Error) && WritePrefab(prefab,text,m_Error)) {
                StableId random;AssetId asset;
                if(GenerateStableId(random,m_Error) && AssetId::TryParse(random.ToString(),asset))
                    if(auto* doc=m_Documents.OpenSource(asset,"prefab",std::move(text),{},m_Error)) {
                        doc->History().MarkUnsaved();m_OpenAsset=doc->Id();ActivateAsset(*doc);
                    }
            } else if(m_Error.empty()) m_Error="Select an author entity first";
        }
        auto* prefab=dynamic_cast<AssetDocument*>(m_Documents.Find(m_OpenAsset));
        if(prefab && prefab->TypeKey()=="prefab" && ImGui::MenuItem("Instantiate opened cooked prefab")) {
            PersistentEntityId root;
            if(InstantiateAuthorPrefab(*scene,m_App.GetAssets(),prefab->Asset(),{},root,m_Error)) {
                m_Selection.Select(*scene,scene->Resolve()->Resolve(root));m_Documents.Activate(scene->Id());
            }
        }
    }
}
