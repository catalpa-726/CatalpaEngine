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
                bool visible = false;
                if (const auto status = IntersectsFrustum(frustum, instances[index].worldBounds, visible); !status.IsSuccess()) return status;
                if (!visible) continue;
                candidate.instanceIndices.push_back(index);
                const auto [group, inserted] = groupIndices.try_emplace(instances[index].mesh.get(), candidate.groups.size());
                if (inserted) candidate.groups.push_back({ instances[index].mesh, {} });
                candidate.groups[group->second].instanceIndices.push_back(index);
            }
            output = std::move(candidate);
            return {};
        } catch (const std::bad_alloc&) {
            return { Math::MathError::OutOfRange, "Cannot allocate per-view visibility results" };
        }
    }
}
