#include "catpch.h"
#include "Renderer/Visibility/VisibilitySystem.h"
#include "Camera/Frustum.h"

namespace Catalpa {

    Math::MathStatus VisibilitySystem::Build(const RenderView& view,
        std::span<const RenderMeshInstance> instances, VisibilityResult& output)
    {
        try {
            VisibilityResult candidate;
            if (view.viewport.width == 0 || view.viewport.height == 0) {
                output = std::move(candidate);
                return {};
            }
            Frustum frustum;
            if (const auto status = BuildFrustum(view.matrices.viewProjection, view.lens, frustum); !status.IsSuccess()) return status;
            if (instances.size() > candidate.instanceIndices.max_size())
                return { Math::MathError::OutOfRange, "Visibility instance count exceeds index storage" };
            candidate.instanceIndices.reserve(instances.size());
            std::map<const MeshResource*, size_t> groupIndices;
            for (size_t index = 0; index < instances.size(); ++index) {
                if (!instances[index].mesh)
                    return { Math::MathError::OutOfRange, "Visibility requires a valid immutable mesh resource" };
                bool visible = false;
                if (const auto status = IntersectsFrustum(frustum, instances[index].worldBounds, visible); !status.IsSuccess()) return status;
                if (!visible) continue;
                candidate.instanceIndices.push_back(index);
                const auto [group, inserted] = groupIndices.try_emplace(instances[index].mesh.get(), candidate.groups.size());
                if (inserted) candidate.groups.push_back({ instances[index].mesh, {} });
                candidate.groups[group->second].instanceIndices.push_back(index);
                const auto& instance = instances[index];
                const auto center = (glm::dvec3(instance.worldBounds.minimum) + glm::dvec3(instance.worldBounds.maximum)) * 0.5;
                const auto cameraCenter = glm::dmat4(view.matrices.view) * glm::dvec4(center, 1.0);
                if (!std::isfinite(cameraCenter.z))
                    return { Math::MathError::NonFinite, "Transparent camera depth is not finite" };
                const auto& submeshes = instance.mesh->GetData().submeshes;
                for (size_t submesh = 0; submesh < submeshes.size(); ++submesh) {
                    VisibleSubmesh draw{ index, static_cast<uint32_t>(submesh), -cameraCenter.z };
                    const uint32_t slot = submeshes[submesh].materialSlot;
                    MaterialAlphaMode mode = MaterialAlphaMode::Opaque;
                    if (!instance.materials.empty()) {
                        if (slot >= instance.materials.size() || !instance.materials[slot].HasValue()
                            || !instance.materials[slot].Get()->definition.HasValue())
                            return { Math::MathError::OutOfRange, "Visible submesh material slot is invalid" };
                        mode = instance.materials[slot].Get()->definition.Get()->alphaMode;
                    }
                    switch (mode) {
                    case MaterialAlphaMode::Opaque: candidate.opaque.push_back(draw); break;
                    case MaterialAlphaMode::Mask: candidate.masked.push_back(draw); break;
                    case MaterialAlphaMode::Blend: candidate.transparent.push_back(draw); break;
                    default: return { Math::MathError::OutOfRange, "Visible material alpha mode is invalid" };
                    }
                }
            }
            std::sort(candidate.transparent.begin(), candidate.transparent.end(), [&](const auto& left, const auto& right) {
                if (left.cameraDepth != right.cameraDepth) return left.cameraDepth > right.cameraDepth;
                const auto leftId = instances[left.instanceIndex].id, rightId = instances[right.instanceIndex].id;
                if (leftId != rightId) return leftId < rightId;
                return left.submeshIndex < right.submeshIndex;
            });
            output = std::move(candidate);
            return {};
        } catch (const std::bad_alloc&) {
            return { Math::MathError::OutOfRange, "Cannot allocate per-view visibility results" };
        }
    }
}
