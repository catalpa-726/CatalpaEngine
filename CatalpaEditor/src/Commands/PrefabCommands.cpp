#include "Commands/PrefabCommands.h"
#include "Scene/PrefabReconciler.h"

namespace Catalpa::Editor {
    namespace {
        PrefabResolver Resolver(AssetManager& assets) {
            return [&assets](AssetId id,std::shared_ptr<const PrefabAsset>& value,uint64_t& version,std::string& error) {
                AssetVersionRef<PrefabAsset> fixed;bool fallback=false;
                if(!assets.Acquire(id,fixed,fallback,error) || fallback) {
                    std::string ignored;(void)assets.Request(id,ignored);return false;
                }
                value=fixed.GetShared();version=fixed.GetVersion();return true;
            };
        }
        class PrefabEdit final : public EditorCommand {
          public:
            PrefabEdit(StableId owner,PersistentEntityId root,SceneSubtreeSnapshot candidate,bool replacing)
                :EditorCommand(owner, replacing ? "history.prefab_update" : "history.prefab_add"),m_Root(root),m_After(std::move(candidate)),m_Replacing(replacing){}
            bool Apply(Document& document,std::string& error) override {
                auto* doc=dynamic_cast<SceneDocument*>(&document);
                if(!doc || doc->Id()!=Owner() || !doc->Resolve() || doc->Playing()) {error="Prefab document is unavailable";return false;}
                auto& scene=*doc->Resolve();
                if (!doc->CheckTarget(m_After.Parent(), error) || (m_Replacing && !doc->CheckTarget(m_Root, error))) return false;
                if(m_Replacing) {
                    SceneSubtreeSnapshot previous;
                    if(!PrefabReconciler::Replace(scene,m_Root,m_After,previous,m_HasBefore?&m_Before:nullptr,error)) return false;
                    m_Before=std::move(previous);m_HasBefore=true;
                } else {
                    const auto status=scene.RestoreForUndo(m_After, scene.Resolve(m_After.Parent()));
                    if(!status.IsSuccess()) {error=status.message;return false;}
                }
                doc->RefreshDeletedTargets();doc->RefreshRestoredTargets(m_After);
                return true;
            }
            bool Revert(Document& document,std::string& error) override {
                auto* doc=dynamic_cast<SceneDocument*>(&document);
                if(!doc || !doc->Resolve() || doc->Playing()) {error="Prefab document is unavailable";return false;}
                if (!doc->CheckTarget(m_Root, error) || !doc->CheckTarget(m_After.Parent(), error)) return false;
                if(m_Replacing) {
                    SceneSubtreeSnapshot discarded;
                    if(!PrefabReconciler::Replace(*doc->Resolve(),m_Root,m_Before,discarded,&m_After,error)) return false;
                    doc->RefreshDeletedTargets();doc->RefreshRestoredTargets(m_Before);
                } else {
                    SceneSubtreeSnapshot discarded;
                    const auto status=doc->Resolve()->RemoveForUndo(doc->Resolve()->Resolve(m_Root),discarded,&m_After);
                    if(!status.IsSuccess()) {error=status.message;return false;}
                    doc->RefreshDeletedTargets();
                }
                return true;
            }
          private:
            PersistentEntityId m_Root;
            SceneSubtreeSnapshot m_Before,m_After;
            bool m_Replacing=false,m_HasBefore=false;
        };
    }

    bool InstantiateAuthorPrefab(SceneDocument& doc,AssetManager& assets,AssetId id,
        PersistentEntityId parent,PersistentEntityId& created,std::string& error) {
        if(!doc.Resolve() || doc.Playing()) {error="Stop Play before instantiating a prefab";return false;}
        std::shared_ptr<const PrefabAsset> source;uint64_t version=0;
        const auto resolver=Resolver(assets);
        if(!resolver(id,source,version,error)) return false;
        SceneSubtreeSnapshot candidate;PersistentEntityId root;
        if(!PrefabInstantiation::Prepare(*doc.Resolve(),id,version,*source,&assets,resolver,parent,{},candidate,root,error)) return false;
        candidate.ForEachIdentity([&](PersistentEntityId value) { doc.ReserveIdentity(value); });
        if(!doc.History().Execute(doc,std::make_unique<PrefabEdit>(doc.Id(),root,std::move(candidate),false),error)) return false;
        created=root;return true;
    }

    bool UpdateAuthorPrefab(SceneDocument& doc,AssetManager& assets,PersistentEntityId root,bool revert,std::string& error) {
        PrefabInstanceComponent provenance;
        auto* scene=doc.Resolve();
        if(!scene || doc.Playing() || !scene->GetWorld().Get(scene->Resolve(root),provenance).IsSuccess()) {
            error="Select a prefab instance root outside Play";return false;
        }
        std::shared_ptr<const PrefabAsset> source;uint64_t version=0;
        const auto resolver=Resolver(assets);
        if(!resolver(provenance.source,source,version,error)) return false;
        SceneSubtreeSnapshot candidate;
        if(!PrefabReconciler::Prepare(*scene,root,version,*source,&assets,resolver,revert,candidate,error)) return false;
        candidate.ForEachIdentity([&](PersistentEntityId value) { doc.ReserveIdentity(value); });
        return doc.History().Execute(doc,std::make_unique<PrefabEdit>(doc.Id(),root,std::move(candidate),true),error);
    }
}
