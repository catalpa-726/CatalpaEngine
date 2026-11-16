#include "catpch.h"
#include "Viewport/ViewportPlacement.h"
#include "Scene/Components/MeshRendererComponent.h"

namespace Catalpa::Editor {
	namespace {
		/**
		 * @brief 用局部坐标中的射线与三个轴向区间相交，保留原世界射线的距离参数。
		 * @param[in] box 网格的局部包围盒。
		 * @param[in] origin 局部射线起点。
		 * @param[in] direction 对单位世界射线方向应用逆仿射变换后的方向。
		 * @param[in] farthest 世界空间中仍可见且未被更近命中遮挡的最大距离。
		 * @return 包围盒与可接受射线区间相交时为真。
		 */
		bool RayBox(const Aabb& box, glm::dvec3 origin, glm::dvec3 direction, double farthest) {
			double near = 0, far = farthest;
			for (int axis = 0; axis < 3; ++axis) {
				if (direction[axis] == 0) {
					if (origin[axis] < box.minimum[axis] || origin[axis] > box.maximum[axis])
						return false;
					continue;
				}
				double a = (box.minimum[axis] - origin[axis]) / direction[axis];
				double b = (box.maximum[axis] - origin[axis]) / direction[axis];
				if (a > b)
					std::swap(a, b);
				near = std::max(near, a);
				far = std::min(far, b);
				if (near > far)
					return false;
			}
			return !box.empty;
		}
	}

	bool FindViewportPlacement(const Scene& scene, const RenderView& view, float u, float v, Math::Vec3& position) {
		if (!std::isfinite(u) || !std::isfinite(v) || u < 0 || u > 1 || v < 0 || v > 1)
			return false;
		const glm::dmat4 inv(view.matrices.inverseViewProjection);
		const auto a = inv * glm::dvec4{2.0 * u - 1, 2.0 * v - 1, view.lens.reversedZ ? 1.0 : 0.0, 1};
		const auto b = inv * glm::dvec4{2.0 * u - 1, 2.0 * v - 1, .5, 1};
		if (a.w == 0 || b.w == 0)
			return false;
		const glm::dvec3 start = glm::dvec3(a) / double(a.w);
		const auto delta = glm::dvec3(b) / double(b.w) - start;
		const double length = glm::length(delta);
		if (!std::isfinite(length) || length < 1e-10)
			return false;
		const auto direction = delta / length;
		const double forwardRate = glm::dot(direction, glm::dvec3(view.matrices.forward));
		if (!(forwardRate > 0))
			return false;
		const double maxDistance = view.lens.infiniteFar
		    ? std::numeric_limits<double>::infinity()
		    : (double(view.lens.farPlane) - view.lens.nearPlane) / forwardRate;
		if (!(maxDistance > 0))
			return false;
		double nearest = maxDistance;
		bool found = false;
		/* 先以局部包围盒剔除，随后使用未归一化的局部射线保留世界距离参数 t。 */
		const auto status = scene.GetWorld().ForEach([&](EntityId entity) {
			MeshRendererComponent mesh;
			TransformComponent transform;
			if (!scene.GetWorld().Has<MeshRendererComponent>(entity) ||
			    !scene.GetWorld().Get(entity, mesh).IsSuccess() || !mesh.visible || !mesh.mesh ||
			    !scene.GetTransform(entity, transform).IsSuccess() || !transform.worldValid)
				return WorldStatus{};
			const auto matrix = glm::dmat4(transform.worldMatrix);
			if (glm::determinant(matrix) == 0)
				return WorldStatus{};
			const auto inverse = glm::inverse(matrix);
			const auto localStart = glm::dvec3(inverse * glm::dvec4(start, 1));
			const auto localDirection = glm::dvec3(inverse * glm::dvec4(direction, 0));
			const auto& data = mesh.mesh->GetData();
			if (!RayBox(data.localBounds, localStart, localDirection, nearest))
				return WorldStatus{};
			const auto attribute = std::find_if(data.attributes.begin(), data.attributes.end(), [](const auto& value) {
				return value.semantic == VertexSemantic::Position && value.format == VertexFormat::Float3;
			});
			if (attribute == data.attributes.end())
				return WorldStatus{};
			const auto vertex = [&](uint32_t raw, int32_t base) {
				float value[3];
				const auto index = size_t(int64_t(raw) + base);
				std::memcpy(
				    value, data.vertexData.data() + index * data.vertexStride + attribute->offset, sizeof(value));
				return glm::dvec3(value[0], value[1], value[2]);
			};
			const auto index = [&](size_t offset) -> uint32_t {
				if (data.indexType == MeshIndexType::UInt16) {
					uint16_t value;
					std::memcpy(&value, data.indexData.data() + offset * sizeof(value), sizeof(value));
					return value;
				}
				uint32_t value;
				std::memcpy(&value, data.indexData.data() + offset * sizeof(value), sizeof(value));
				return value;
			};
			for (const auto& part : data.submeshes)
				for (size_t i = 0; i < part.indexCount; i += 3) {
					const auto p0 = vertex(index(size_t(part.firstIndex) + i), part.baseVertex);
					const auto p1 = vertex(index(size_t(part.firstIndex) + i + 1), part.baseVertex);
					const auto p2 = vertex(index(size_t(part.firstIndex) + i + 2), part.baseVertex);
					const auto e1 = p1 - p0, e2 = p2 - p0;
					const auto cross = glm::cross(localDirection, e2);
					const double determinant = glm::dot(e1, cross);
					const double tolerance =
					    16 * std::numeric_limits<double>::epsilon() * glm::length(e1) * glm::length(cross);
					if (std::abs(determinant) <= tolerance)
						continue;
					const auto from = localStart - p0;
					const double x = glm::dot(from, cross) / determinant;
					if (x < 0 || x > 1)
						continue;
					const auto q = glm::cross(from, e1);
					const double y = glm::dot(localDirection, q) / determinant;
					if (y < 0 || x + y > 1)
						continue;
					const double t = glm::dot(e2, q) / determinant;
					if (t >= 0 && t <= nearest) {
						nearest = t;
						found = true;
					}
				}
			return WorldStatus{};
		});
		if (!status.IsSuccess())
			return false;
		if (!found) {
			const double ground = direction.y != 0 ? -start.y / direction.y : -1;
			nearest =
			    std::isfinite(ground) && ground >= 0 && ground <= maxDistance ? ground : std::min(5.0, maxDistance);
		}
		const auto candidate = Math::Vec3(start + direction * nearest);
		if (!std::isfinite(candidate.x) || !std::isfinite(candidate.y) || !std::isfinite(candidate.z))
			return false;
		position = candidate;
		return true;
	}

	bool SelectionBounds(const Scene& scene, EntityId root, Aabb& bounds, std::string& error) {
		std::map<uint32_t, std::vector<EntityId>> children;
		if (!scene.GetWorld().IsAlive(root)) {
			error = "Selection no longer exists";
			return false;
		}
		const auto status = scene.GetWorld().ForEach([&](EntityId entity) {
			RelationshipComponent relation;
			const auto result = scene.GetRelationship(entity, relation);
			if (result.IsSuccess())
				children[relation.parent.index].push_back(entity);
			return result;
		});
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		Aabb candidate;
		std::vector<EntityId> order{root};
		std::set<uint32_t> seen;
		for (size_t i = 0; i < order.size(); ++i) {
			const auto entity = order[i];
			if (!seen.insert(entity.index).second) {
				error = "Selection hierarchy contains a cycle";
				return false;
			}
			TransformComponent transform;
			if (auto result = scene.GetTransform(entity, transform); !result.IsSuccess() || !transform.worldValid) {
				error = "Selection has no valid world transform";
				return false;
			}
			Aabb local;
			MeshRendererComponent mesh;
			if (scene.GetWorld().Has<MeshRendererComponent>(entity) && scene.GetWorld().Get(entity, mesh).IsSuccess() &&
			    mesh.mesh)
				local = mesh.mesh->GetData().localBounds;
			if (local.empty) {
				local.empty = false;
				local.minimum = local.maximum = {};
			}
			Aabb world, combined;
			if (!TransformBounds(local, transform.worldMatrix, world).IsSuccess() ||
			    !MergeBounds(candidate, world, combined).IsSuccess()) {
				error = "Selection bounds exceed supported world coordinates";
				return false;
			}
			candidate = combined;
			const auto& descendants = children[entity.index];
			order.insert(order.end(), descendants.begin(), descendants.end());
		}
		bounds = candidate;
		error.clear();
		return true;
	}
}
