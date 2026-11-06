#include "Commands/SceneCommands.h"
#include "Scene/SceneSubtree.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Camera/CameraProjection.h"

namespace Catalpa::Editor {
    namespace {
        bool Status(WorldStatus value, std::string& error) {
            if (value.IsSuccess()) {
                error.clear();
                return true;
            }
            error = value.message;
            return false;
        }

        // 用实际世界姿态和单位视口预检候选投影，避免合法标量在组合 float VP 中失效。
        // 最终目标尺寸/抖动仍由 Runtime 逐帧校验；此处不修改相机作者值。
        WorldStatus CheckCameraPresentation(Scene& scene, EntityId entity, const CameraComponent& camera) {
            if (!camera.enabled) return {};
            if (auto status = TransformSystem::Update(scene); !status.IsSuccess()) return status;
            TransformComponent transform;
            if (auto status = scene.GetTransform(entity, transform); !status.IsSuccess()) return status;
            if (camera.viewId == 0 || camera.historyRevision == 0)
                return { WorldError::InvalidArgument, "Camera view identity and history revision must be nonzero" };
            CameraMatrices candidate;
            if (const auto status = BuildCameraMatrices(transform.worldMatrix, camera.lens, {1, 1}, candidate); !status.IsSuccess())
                return { WorldError::InvalidArgument, status.message };
            return {};
        }

        // Scene 的公共队列逐命令提交。拒绝夹带待处理命令，防止局部失败误记为完整事务。
        class SceneCommand final : public EditorCommand {
          public:
            using Step = std::function<WorldStatus(Scene&)>;

            SceneCommand(StableId id, Step apply, Step revert, std::vector<PersistentEntityId> targets = {})
                : EditorCommand(id), m_Apply(std::move(apply)), m_Revert(std::move(revert)),
                  m_Targets(std::move(targets)) {}

            bool Apply(Document& doc, std::string& e) override {
                return Run(doc, m_Apply, e);
            }

            bool Revert(Document& doc, std::string& e) override {
                return Run(doc, m_Revert, e);
            }

          private:
            bool Run(Document& doc, const Step& step, std::string& error) {
                auto* sceneDoc = dynamic_cast<SceneDocument*>(&doc);
                auto* scene = sceneDoc && doc.Id() == Owner() ? sceneDoc->Resolve() : nullptr;
                if (!scene || sceneDoc->Playing() || !scene->CanEditNow()) {
                    error = "Scene is unavailable or has pending work";
                    return false;
                }
                for (auto id : m_Targets)
                    if (!sceneDoc->CheckTarget(id, error))
                        return false;
                return Status(step(*scene), error);
            }

            Step m_Apply, m_Revert;
            std::vector<PersistentEntityId> m_Targets;
        };

        WorldStatus Commit(Scene& scene, WorldStatus queued) {
            return queued.IsSuccess() ? scene.CommitPending() : queued;
        }

        WorldStatus ParentId(const Scene& scene, EntityId id, PersistentEntityId& parent) {
            RelationshipComponent relation;
            if (auto s = scene.GetRelationship(id, relation); !s.IsSuccess())
                return s;
            parent = {};
            if (!relation.parent.HasValue())
                return {};
            IdentityComponent identity;
            if (auto s = scene.GetIdentity(relation.parent, identity); !s.IsSuccess())
                return s;
            parent = identity.persistent;
            return {};
        }
    }

    template<class T>
    static bool EditAuthorComponent(SceneDocument& doc,PersistentEntityId id,std::optional<T> next,std::string& error) {
        auto* scene=doc.Resolve();
        if(!scene || doc.Playing()) {error="Author scene is unavailable during Play";return false;}
        const auto entity=scene->Resolve(id);
        std::optional<T> before;
        if(scene->GetWorld().Has<T>(entity)) {
            if(const auto status=scene->GetWorld().Get(entity,before.emplace());!status.IsSuccess()) {error=status.message;return false;}
        }
        const auto step=[id](std::optional<T> expected,std::optional<T> value) {
            return [id,expected=std::move(expected),value=std::move(value)](Scene& scene)->WorldStatus {
                const auto target=scene.Resolve(id);
                if(scene.GetWorld().Has<T>(target)!=expected.has_value()) return {WorldError::Busy,"Author component presence changed"};
                if(expected) {T actual;const auto status=scene.GetWorld().Get(target,actual);if(!status.IsSuccess())return status;
                    if(actual!=*expected) return {WorldError::Busy,"Author component changed outside this command"};}
                if constexpr(std::is_same_v<T,ScriptComponent>) return Commit(scene,value?scene.QueueSetScript(target,*value):scene.QueueRemoveScript(target));
                else return Commit(scene,value?scene.QueueSetBehaviorGraph(target,*value):scene.QueueRemoveBehaviorGraph(target));
            };
        };
        return doc.History().Execute(doc,std::make_unique<SceneCommand>(doc.Id(),step(before,next),step(next,before),std::vector{id}),error);
    }

    bool EditScript(SceneDocument& doc,PersistentEntityId id,std::optional<ScriptComponent> next,std::string& error) {
        return EditAuthorComponent(doc,id,std::move(next),error);
    }
    bool EditBehaviorGraph(SceneDocument& doc,PersistentEntityId id,std::optional<BehaviorGraphComponent> next,std::string& error) {
        return EditAuthorComponent(doc,id,std::move(next),error);
    }

    bool CreateAuthorEntity(SceneDocument& doc, std::string name, PersistentEntityId parent,
                            PersistentEntityId& created, std::string& error) {
        PersistentEntityId id;
        if (!GeneratePersistentEntityId(id, error))
            return false;
        doc.ReserveIdentity(id);
        auto create = [id, parent, name, &doc](Scene& scene) -> WorldStatus {
            const auto parentEntity = scene.Resolve(parent);
            if (parent.HasValue() && !parentEntity.HasValue())
                return {WorldError::InvalidEntity, "Parent was removed"};
            const auto s = Commit(scene, scene.QueueCreate(id, name, {}, parentEntity));
            if (s.IsSuccess())
                doc.TrackCreated(id);
            return s;
        };
        auto remove = [id, parent, name, &doc](Scene& scene) -> WorldStatus {
            std::string ignored;
            if (!doc.CheckTarget(id, ignored))
                return {WorldError::InvalidEntity, "Created entity no longer matches its generation"};
            const auto entity = scene.Resolve(id);
            size_t components = 0;
            if (auto s = scene.GetWorld().CountComponents(entity, components); !s.IsSuccess())
                return s;
            if (components != 3)
                return {WorldError::Busy, "Created entity acquired other components"};
            IdentityComponent identity;
            TransformComponent transform;
            PersistentEntityId currentParent;
            if (auto s = scene.GetIdentity(entity, identity); !s.IsSuccess())
                return s;
            if (auto s = scene.GetTransform(entity, transform); !s.IsSuccess())
                return s;
            if (auto s = ParentId(scene, entity, currentParent); !s.IsSuccess())
                return s;
            const Math::TransformTRS initial;
            if (identity.name != name || parent != currentParent ||
                transform.local.translation != initial.translation ||
                transform.local.rotation != initial.rotation || transform.local.scale != initial.scale)
                return {WorldError::Busy, "Created entity was edited outside this transaction"};
            auto checked = scene.GetWorld().ForEach([&](EntityId other) {
                RelationshipComponent relation;
                if (auto s = scene.GetRelationship(other, relation); !s.IsSuccess())
                    return s;
                return relation.parent == entity
                           ? WorldStatus{WorldError::Busy, "Created entity gained children"}
                           : WorldStatus{};
            });
            const auto s = checked.IsSuccess() ? Commit(scene, scene.QueueDestroySubtree(entity)) : checked;
            if (s.IsSuccess())
                doc.RefreshDeletedTargets();
            return s;
        };
        if (!doc.History().Execute(
                doc, std::make_unique<SceneCommand>(doc.Id(), create, remove, std::vector{parent}), error))
            return false;
        created = id;
        return true;
    }

    bool DeleteAuthorEntity(SceneDocument& doc, PersistentEntityId id, std::string& error) {
        auto* scene = doc.Resolve();
        PersistentEntityId parent;
        if (!scene || !Status(ParentId(*scene, scene->Resolve(id), parent), error) ||
            !doc.CheckTarget(parent, error))
            return false;
        auto snapshot = std::make_shared<SceneSubtreeSnapshot>();
        auto remove = [id, parent, snapshot, captured = false, &doc](Scene& scene) mutable -> WorldStatus {
            std::string ignored;
            if (!doc.CheckTarget(id, ignored))
                return {WorldError::InvalidEntity, "Delete target is stale"};
            PersistentEntityId currentParent;
            if (auto s = ParentId(scene, scene.Resolve(id), currentParent); !s.IsSuccess())
                return s;
            if (currentParent != parent)
                return {WorldError::Busy, "Subtree parent changed"};
            const auto s =
                scene.RemoveForUndo(scene.Resolve(id), *snapshot, captured ? snapshot.get() : nullptr);
            if (s.IsSuccess()) {
                doc.RefreshDeletedTargets();
                captured = true;
            }
            return s;
        };
        auto restore = [snapshot, parent, &doc](Scene& scene) {
            const auto s = scene.RestoreForUndo(*snapshot, scene.Resolve(parent));
            if (s.IsSuccess())
                doc.RefreshRestoredTargets(*snapshot);
            return s;
        };
        return doc.History().Execute(
            doc, std::make_unique<SceneCommand>(doc.Id(), remove, restore, std::vector{parent}), error);
    }

    bool RenameEntity(SceneDocument& doc, PersistentEntityId id, std::string name,
                      const std::string& expected, std::string& error) {
        if (name == expected)
            return true;
        auto set = [id](std::string oldName, std::string newName) {
            return [=](Scene& scene) -> WorldStatus {
                const auto entity = scene.Resolve(id);
                IdentityComponent value;
                if (auto status = scene.GetIdentity(entity, value); !status.IsSuccess())
                    return status;
                if (value.name != oldName)
                    return {WorldError::Busy, "Name changed outside this edit"};
                return Commit(scene, scene.QueueSetName(entity, newName));
            };
        };
        return doc.History().Execute(doc,
                                     std::make_unique<SceneCommand>(doc.Id(), set(expected, name),
                                                                    set(name, expected), std::vector{id}),
                                     error);
    }

    bool EditCamera(SceneDocument& doc, PersistentEntityId id, const CameraComponent& next,
                    std::string& error) {
        auto* scene = doc.Resolve();
        if (!scene) {
            error = "Camera scene was unloaded";
            return false;
        }
        CameraComponent before;
        if (!Status(scene->GetWorld().Get(scene->Resolve(id), before), error))
            return false;
        if (next.viewId != before.viewId || next.historyRevision != before.historyRevision) {
            error = "Camera view ID and history revision are read-only";
            return false;
        }
        const auto same = [](const CameraComponent& a, const CameraComponent& b) {
            return a.lens == b.lens && a.enabled == b.enabled && a.priority == b.priority &&
                   a.viewId == b.viewId;
        };
        if (same(before, next))
            return true;
        if (before.historyRevision >= UINT64_MAX - 1) {
            error = "Camera revision has no room for an edit and its undo";
            return false;
        }
        auto set = [id, same](CameraComponent expected, CameraComponent value) {
            return [=](Scene& scene) -> WorldStatus {
                const auto entity = scene.Resolve(id);
                CameraComponent current;
                if (auto s = scene.GetWorld().Get(entity, current); !s.IsSuccess())
                    return s;
                if (!same(current, expected))
                    return {WorldError::Busy, "Camera changed outside this edit"};
                if (current.historyRevision == UINT64_MAX)
                    return {WorldError::CapacityExceeded, "Camera history revision exhausted"};
                auto published = value;
                // 还原旧镜头值也会改变投影历史；应用、撤销和重做均使用新的修订。
                published.historyRevision = current.historyRevision + 1;
                if (const auto status = CheckCameraPresentation(scene, entity, published); !status.IsSuccess()) return status;
                return Commit(scene, scene.QueueSetCamera(entity, published));
            };
        };
        return doc.History().Execute(
            doc,
            std::make_unique<SceneCommand>(doc.Id(), set(before, next), set(next, before), std::vector{id}),
            error);
    }

    bool ChangeLocal(SceneDocument& doc, PersistentEntityId id, const Math::TransformTRS& before,
                     const Math::TransformTRS& after, std::string& error) {
        auto set = [id](Math::TransformTRS expected, Math::TransformTRS next) {
            return [=](Scene& scene) -> WorldStatus {
                const auto entity = scene.Resolve(id);
                TransformComponent transform;
                if (auto s = scene.GetTransform(entity, transform); !s.IsSuccess())
                    return s;
                const auto& v = transform.local;
                if (v.translation != expected.translation || v.rotation != expected.rotation ||
                    v.scale != expected.scale)
                    return {WorldError::InvalidArgument, "Transform changed outside this edit"};
                return Commit(scene, scene.QueueSetLocal(entity, next));
            };
        };
        return doc.History().Execute(
            doc,
            std::make_unique<SceneCommand>(doc.Id(), set(before, after), set(after, before), std::vector{id}),
            error);
    }

    namespace {
        bool Same(const LightComponent& a, const LightComponent& b) {
            const auto& x = a.parameters;
            const auto& y = b.parameters;
            return a.enabled == b.enabled && x.type == y.type && x.color == y.color &&
                   x.intensity == y.intensity && x.range == y.range &&
                   x.innerConeRadians == y.innerConeRadians && x.outerConeRadians == y.outerConeRadians &&
                   x.castShadows == y.castShadows && x.shadowBiasMeters == y.shadowBiasMeters;
        }

        bool Same(const EnvironmentComponent& a, const EnvironmentComponent& b) {
            const auto& x = a.settings;
            const auto& y = b.settings;
            return a.enabled == b.enabled && a.environment == b.environment && x.intensity == y.intensity &&
                   x.exposureEV == y.exposureEV && x.rotationRadians == y.rotationRadians &&
                   x.drawBackground == y.drawBackground && x.fogEnabled == y.fogEnabled &&
                   x.fogColor == y.fogColor && x.fogDensityPerMeter == y.fogDensityPerMeter;
        }

        template <class T>
        bool EditOptional(SceneDocument& doc, PersistentEntityId id, const T& next,
                          WorldStatus (Scene::*set)(EntityId, const T&),
                          WorldStatus (Scene::*remove)(EntityId), std::string& error) {
            auto* scene = doc.Resolve();
            if (!scene) {
                error = "Scene was unloaded";
                return false;
            }
            const auto entity = scene->Resolve(id);
            std::optional<T> before;
            if (scene->GetWorld().Has<T>(entity) &&
                !Status(scene->GetWorld().Get(entity, before.emplace()), error))
                return false;
            auto update = [=](std::optional<T> expected, std::optional<T> value) {
                return [=](Scene& scene) -> WorldStatus {
                    const auto target = scene.Resolve(id);
                    if (scene.GetWorld().Has<T>(target) != expected.has_value())
                        return {WorldError::Busy, "Component set changed"};
                    if (expected) {
                        T current;
                        if (auto s = scene.GetWorld().Get(target, current); !s.IsSuccess())
                            return s;
                        if (!Same(*expected, current))
                            return {WorldError::Busy, "Component changed outside this edit"};
                    }
                    return Commit(scene, value ? (scene.*set)(target, *value) : (scene.*remove)(target));
                };
            };
            return doc.History().Execute(doc,
                                         std::make_unique<SceneCommand>(doc.Id(), update(before, next),
                                                                        update(next, before),
                                                                        std::vector{id}),
                                         error);
        }
    }

    bool EditLight(SceneDocument& doc, PersistentEntityId id, const LightComponent& value,
                   std::string& error) {
        return EditOptional(doc, id, value, &Scene::QueueSetLight, &Scene::QueueRemoveLight, error);
    }

    bool EditEnvironment(SceneDocument& doc, PersistentEntityId id, const EnvironmentComponent& value,
                         std::string& error) {
        return EditOptional(doc, id, value, &Scene::QueueSetEnvironment, &Scene::QueueRemoveEnvironment,
                            error);
    }

    bool EditMeshDisplay(SceneDocument& doc, PersistentEntityId id, Math::Vec4 color, bool visible,
                         std::string& error) {
        auto* scene = doc.Resolve();
        if (!scene) {
            error = "Scene was unloaded";
            return false;
        }
        MeshRendererComponent before;
        if (!Status(scene->GetWorld().Get(scene->Resolve(id), before), error))
            return false;
        auto update = [id](Math::Vec4 expected, bool wasVisible, Math::Vec4 next, bool enabled) {
            return [=](Scene& scene) -> WorldStatus {
                const auto entity = scene.Resolve(id);
                MeshRendererComponent mesh;
                if (auto s = scene.GetWorld().Get(entity, mesh); !s.IsSuccess())
                    return s;
                if (mesh.color != expected || mesh.visible != wasVisible)
                    return {WorldError::Busy, "Mesh display values changed"};
                mesh.color = next;
                mesh.visible = enabled;
                return Commit(scene, scene.QueueSetMeshRenderer(entity, mesh));
            };
        };
        return doc.History().Execute(
            doc,
            std::make_unique<SceneCommand>(doc.Id(), update(before.color, before.visible, color, visible),
                                           update(color, visible, before.color, before.visible),
                                           std::vector{id}),
            error);
    }

    bool ReparentEntity(SceneDocument& doc, PersistentEntityId child, PersistentEntityId parent,
                        std::string& error) {
        auto* scene = doc.Resolve();
        if (!scene) {
            error = "Scene was unloaded";
            return false;
        }
        PersistentEntityId before;
        if (!Status(ParentId(*scene, scene->Resolve(child), before), error))
            return false;
        if (before == parent)
            return true;
        auto move = [child](PersistentEntityId expected, PersistentEntityId next) {
            return [=](Scene& scene) -> WorldStatus {
                const auto entity = scene.Resolve(child);
                PersistentEntityId current;
                if (auto s = ParentId(scene, entity, current); !s.IsSuccess())
                    return s;
                const auto target = scene.Resolve(next);
                if (current != expected || (next.HasValue() && !target.HasValue()))
                    return {WorldError::InvalidArgument, "Parent no longer matches this edit"};
                return Commit(scene, scene.QueueSetParent(entity, target, false));
            };
        };
        return doc.History().Execute(doc,
                                     std::make_unique<SceneCommand>(doc.Id(), move(before, parent),
                                                                    move(parent, before),
                                                                    std::vector{child, parent, before}),
                                     error);
    }
}
