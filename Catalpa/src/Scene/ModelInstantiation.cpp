#include "catpch.h"
#include "Scene/ModelInstantiation.h"
#include "Scene/Components/MeshRendererComponent.h"
#include "Materials/MaterialValidation.h"
#include "Core/Serialization/Archive.h"

namespace Catalpa {
    namespace {
        ArchiveValue Vector(std::initializer_list<float> values) {
            ArchiveValue::Array array;
            for (auto v : values)
                array.push_back(ArchiveValue{double(v)});
            return ArchiveValue{std::move(array)};
        }

        struct PlannedNode {
            ModelNode source;
            PersistentEntityId persistent;
            std::optional<MeshRendererComponent> renderer;
        };

        // 在场景安全点之前解析材质槽，并为同一源节点上的额外网格创建子节点。
        bool PlanRenderers(const ModelAsset& model, std::span<const AssetVersionRef<MeshAsset>> meshes,
                           std::span<const AssetVersionRef<MaterialInstance>> materials,
                           std::span<const ModelMaterialOverride> overrides, std::vector<PlannedNode>& plan,
                           std::string& error) {
            plan.reserve(model.nodes.size() + model.meshes.size());
            std::map<AssetId, AssetVersionRef<MaterialInstance>> replacements;
            if (overrides.size() > 65536) {
                error = "Too many material overrides";
                return false;
            }
            std::set<AssetId> sourceMaterials;
            for (const auto& mesh : model.meshes)
                for (const auto material : mesh.materials)
                    sourceMaterials.insert(material.id);
            std::map<AssetId, const AssetVersionRef<MeshAsset>*> meshVersions;
            for (const auto& mesh : meshes)
                meshVersions.emplace(mesh.GetId(), &mesh);
            std::map<AssetId, const AssetVersionRef<MaterialInstance>*> materialVersions;
            for (const auto& material : materials) {
                if (!sourceMaterials.contains(material.GetId()))
                    continue;
                if (!material.HasValue() || !materialVersions.emplace(material.GetId(), &material).second) {
                    error = "Missing or ambiguous material version: " + material.GetId().ToString();
                    return false;
                }
                if (!ValidateMaterialInstance(*material.Get(), error))
                    return false;
            }
            for (const auto& overrideValue : overrides) {
                if (!sourceMaterials.contains(overrideValue.source) || !overrideValue.value.HasValue() ||
                    !replacements.emplace(overrideValue.source, overrideValue.value).second) {
                    error = "Invalid, foreign or duplicate explicit material override";
                    return false;
                }
                if (!ValidateMaterialInstance(*overrideValue.value.Get(), error))
                    return false;
            }
            for (const auto& node : model.nodes)
                plan.push_back({node, {}, {}});
            for (const auto& instance : model.meshes) {
                const auto& mesh = *meshVersions.at(instance.mesh.id);
                MeshRendererComponent renderer;
                renderer.meshVersion = mesh;
                renderer.mesh = mesh.Get()->mesh;
                for (const auto handle : instance.materials) {
                    const auto material = materialVersions.find(handle.id);
                    if (material == materialVersions.end()) {
                        error = "Missing model material: " + handle.id.ToString();
                        return false;
                    }
                    const auto replacement = replacements.find(handle.id);
                    renderer.materials.push_back(replacement == replacements.end() ? *material->second
                                                                                   : replacement->second);
                }
                if (!plan[instance.node].renderer)
                    plan[instance.node].renderer = std::move(renderer);
                else { // 同节点多个实例也完整保留，增加无变换子实体，不覆盖前一个组件。
                    ModelNode child;
                    child.name = plan[instance.node].source.name + "/mesh";
                    child.parent = int32_t(instance.node);
                    plan.push_back({std::move(child), {}, std::move(renderer)});
                }
            }
            return true;
        }
    }

    bool ModelInstantiation::Queue(Scene& scene, AssetVersionRef<ModelAsset> model,
                                   std::vector<AssetVersionRef<MeshAsset>> meshes,
                                   std::vector<AssetVersionRef<MaterialInstance>> materials,
                                   std::shared_ptr<ModelInstantiationResult>& output, std::string& error,
                                   EntityId parent, std::span<const ModelMaterialOverride> overrides) {
        try {
            if (!model.HasValue()) {
                error = "Model version is missing";
                return false;
            }
            if (!ValidateModelMeshBindings(*model.Get(), meshes, error))
                return false;
            if (parent != EntityId{} && !scene.ValidateEntity(parent).IsSuccess()) {
                error = "Instantiation parent is stale or foreign";
                return false;
            }
            std::vector<PlannedNode> plan;
            if (!PlanRenderers(*model.Get(), meshes, materials, overrides, plan, error))
                return false;

            std::set<PersistentEntityId> identities;
            for (auto& node : plan) {
                if (!GeneratePersistentEntityId(node.persistent, error))
                    return false;
                if (scene.m_Entities.contains(node.persistent) ||
                    !identities.insert(node.persistent).second) {
                    error = "Generated entity identity collision";
                    return false;
                }
            }
            std::vector<size_t> order;
            order.reserve(plan.size());
            std::vector<std::vector<size_t>> children(plan.size());
            for (size_t i = 0; i < plan.size(); ++i) {
                if (plan[i].source.parent < 0)
                    order.push_back(i);
                else
                    children[size_t(plan[i].source.parent)].push_back(i);
            }
            for (size_t i = 0; i < order.size(); ++i)
                for (auto child : children[order[i]])
                    order.push_back(child);
            if (order.size() != plan.size()) {
                error = "Model hierarchy cannot be ordered";
                return false;
            }
            // 入队前按组件 schema 校验整棵树。安全点只负责创建实体并应用已验证的值。
            IdentityComponent parentIdentity;
            if (parent != EntityId{}) {
                const auto status = scene.GetIdentity(parent, parentIdentity);
                if (!status.IsSuccess()) {
                    error = status.message;
                    return false;
                }
            }
            for (const auto& node : plan) {
                const auto& local = node.source.local;
                ArchiveValue identity{
                    ArchiveValue::Object{{"schemaVersion", ArchiveValue{uint64_t(1)}},
                                         {"persistent_id", ArchiveValue{node.persistent.ToString()}},
                                         {"name", ArchiveValue{node.source.name}}}};
                ArchiveValue transform{ArchiveValue::Object{
                    {"schemaVersion", ArchiveValue{uint64_t(1)}},
                    {"translation", Vector({local.translation.x, local.translation.y, local.translation.z})},
                    {"rotation",
                     Vector({local.rotation.w, local.rotation.x, local.rotation.y, local.rotation.z})},
                    {"scale", Vector({local.scale.x, local.scale.y, local.scale.z})}}};
                ArchiveValue::Object relation{{"schemaVersion", ArchiveValue{uint64_t(1)}}};
                if (node.source.parent >= 0)
                    relation.emplace("parent",
                                     ArchiveValue{plan[size_t(node.source.parent)].persistent.ToString()});
                else if (parent != EntityId{})
                    relation.emplace("parent", ArchiveValue{parentIdentity.persistent.ToString()});
                if (!scene.ValidateComponent("catalpa.identity", identity).IsSuccess() ||
                    !scene.ValidateComponent("catalpa.transform", transform).IsSuccess() ||
                    !scene.ValidateComponent("catalpa.relationship", ArchiveValue{std::move(relation)})
                         .IsSuccess()) {
                    error = scene.GetLastComponentDiagnostic();
                    return false;
                }
                if (node.renderer) {
                    ArchiveValue::Array ids;
                    for (const auto& m : node.renderer->materials)
                        ids.push_back(ArchiveValue{m.GetId().ToString()});
                    ArchiveValue fields{ArchiveValue::Object{
                        {"schemaVersion", ArchiveValue{uint64_t(1)}},
                        {"mesh", ArchiveValue{node.renderer->meshVersion.GetId().ToString()}},
                        {"materials", ArchiveValue{std::move(ids)}},
                        {"color", Vector({1, 1, 1, 1})},
                        {"visible", ArchiveValue{true}}}};
                    if (!scene.ValidateComponent("catalpa.mesh_renderer", fields).IsSuccess()) {
                        error = scene.GetLastComponentDiagnostic();
                        return false;
                    }
                }
            }

            auto result = std::make_shared<ModelInstantiationResult>();
            result->roots.reserve(plan.size());
            result->nodes.resize(model.Get()->nodes.size());
            std::vector<EntityId> created(plan.size());
            const auto status = scene.m_Commands.Enqueue(
                [&scene, plan = std::move(plan), order = std::move(order), created = std::move(created),
                 parent, result](World& world) mutable {
                    const bool wasDirty = scene.m_Dirty;
                    auto rollback = [&](WorldStatus reason) {
                        WorldStatus rollbackStatus;
                        // order 是实际创建顺序，子节点先撤销。仅移除此次成功登记的实体映射。
                        for (auto index = order.rbegin(); index != order.rend(); ++index) {
                            const size_t i = *index;
                            if (created[i] == EntityId{})
                                continue;
                            const auto destroyed = world.Destroy(created[i]);
                            if (!destroyed.IsSuccess()) {
                                rollbackStatus = destroyed;
                                continue;
                            }
                            const auto entry = scene.m_Entities.find(plan[i].persistent);
                            if (entry != scene.m_Entities.end() && entry->second == created[i])
                                scene.m_Entities.erase(entry);
                        }
                        scene.m_Dirty = wasDirty || !rollbackStatus.IsSuccess();
                        result->roots.clear();
                        result->nodes.clear();
                        result->error = reason.message;
                        result->complete = true;
                        result->success = false;
                        // 可恢复的工具事务失败由ticket报告；回滚成功不终止宿主模拟循环。
                        return rollbackStatus;
                    };
                    try {
                        if (result->cancelRequested)
                            return rollback({WorldError::CommandFailed, "Model instantiation cancelled"});
                        if (parent != EntityId{})
                            if (const auto valid = scene.ValidateEntity(parent); !valid.IsSuccess())
                                return rollback(valid);
                        for (const auto index : order) {
                            const auto& node = plan[index];
                            if (const auto s = world.Create(created[index]); !s.IsSuccess())
                                return rollback(s);
                            const auto nodeParent =
                                node.source.parent < 0 ? parent : created[size_t(node.source.parent)];
                            if (const auto s =
                                    scene.InitializeEntity(created[index], node.persistent, node.source.name,
                                                           node.source.local, nodeParent);
                                !s.IsSuccess())
                                return rollback(s);
                            if (node.renderer)
                                if (const auto s = world.Add(created[index], *node.renderer); !s.IsSuccess())
                                    return rollback(s);
                        }
                        for (size_t i = 0; i < created.size(); ++i) {
                            auto reference = scene.MakeReference(created[i]);
                            if (!reference.HasValue())
                                return rollback(
                                    {WorldError::CommandFailed, "Cannot resolve instantiated entity"});
                            if (i < result->nodes.size())
                                result->nodes[i] = {plan[i].source.id, reference};
                            if (plan[i].source.parent < 0)
                                result->roots.push_back(reference);
                        }
                        result->success = true;
                        result->complete = true;
                        return WorldStatus{};
                    } catch (const std::bad_alloc&) {
                        return rollback({WorldError::AllocationFailed, "Cannot instantiate complete model"});
                    } catch (...) {
                        return rollback({WorldError::CommandFailed, "Model transaction failed"});
                    }
                });
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            output = std::move(result);
            error.clear();
            return true;
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }
}
