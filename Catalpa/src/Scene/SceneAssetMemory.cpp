#include "catpch.h"
#include "Scene/SceneAsset.h"

namespace Catalpa {
    namespace {
        constexpr size_t MapNodeOverhead = 4 * sizeof(void*);
        size_t ArchiveHeap(const ArchiveValue& value);
        size_t ObjectHeap(const ArchiveValue::Object& object) {
            size_t result = MapNodeOverhead;
            for (const auto& [key, value] : object)
                result += sizeof(ArchiveValue::Object::value_type) + MapNodeOverhead + key.capacity() + 1 +
                          ArchiveHeap(value);
            return result;
        }
        size_t ArchiveHeap(const ArchiveValue& value) {
            if (const auto* text = std::get_if<std::string>(&value.data))
                return text->capacity() + 1;
            if (const auto* array = std::get_if<ArchiveValue::Array>(&value.data)) {
                size_t result = array->capacity() * sizeof(ArchiveValue);
                for (const auto& child : *array)
                    result += ArchiveHeap(child);
                return result;
            }
            if (const auto* object = std::get_if<ArchiveValue::Object>(&value.data))
                return ObjectHeap(*object);
            return 0;
        }
        size_t ScriptHeap(const ScriptValues& values) {
            size_t result = sizeof(ScriptValues::Storage) + MapNodeOverhead;
            for (const auto& [key, value] : values) {
                result += sizeof(ScriptValues::value_type) + MapNodeOverhead + key.capacity() + 1;
                if (const auto* text = std::get_if<std::string>(&value))
                    result += text->capacity() + 1;
            }
            return result;
        }
    } // namespace
    size_t EstimateSceneAssetResidentBytes(const SceneAsset& scene) noexcept {
        size_t result = sizeof(SceneAsset) + scene.name.capacity() + 1 +
                        scene.entities.capacity() * sizeof(SceneEntityAsset);
        for (const auto& node : scene.entities) {
            result += node.name.capacity() + 1 + ObjectHeap(node.dynamicComponents) +
                      node.dynamicDependencies.capacity() * sizeof(AssetDependencyValue);
            if (node.script)
                result += ScriptHeap(node.script->properties);
            if (node.behavior)
                result += ScriptHeap(node.behavior->variables);
            if (node.prefab)
                result += sizeof(decltype(node.prefab->entities)::Storage) + MapNodeOverhead +
                          node.prefab->entities.size() *
                              (sizeof(decltype(node.prefab->entities)::value_type) + MapNodeOverhead) +
                          node.prefab->overrides.capacity() + 1;
            if (!node.mesh)
                continue;
            const auto& mesh = *node.mesh;
            result += mesh.materials.capacity() * sizeof(AssetId) + mesh.lods.capacity() * sizeof(AssetId) +
                      mesh.lodErrors.capacity() * sizeof(float);
            if (mesh.pinned)
                result += mesh.pinned->materials.capacity() * sizeof(AssetVersionRef<MaterialInstance>) +
                          mesh.pinned->lods.capacity() * sizeof(AssetVersionRef<MeshAsset>) +
                          mesh.pinned->lodErrors.capacity() * sizeof(float);
            if (mesh.procedural) {
                const auto& data = mesh.procedural->GetData();
                result += sizeof(MeshResource) + data.vertexData.capacity() + data.indexData.capacity() +
                          data.attributes.capacity() * sizeof(VertexAttribute) +
                          data.submeshes.capacity() * sizeof(Submesh);
                if (mesh.pinned && mesh.pinned->meshVersion.HasValue()) {
                    const auto& inlineAsset = *mesh.pinned->meshVersion.Get();
                    result += sizeof(MeshAsset) + inlineAsset.materialSlots.capacity() * sizeof(std::string);
                    for (const auto& slot : inlineAsset.materialSlots)
                        result += slot.capacity() + 1;
                }
            }
        }
        return result;
    }
} // namespace Catalpa
