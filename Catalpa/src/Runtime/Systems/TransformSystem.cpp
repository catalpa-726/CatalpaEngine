#include "catpch.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Core/Math/TransformMath.h"
#include "Scene/Scene.h"

#include <limits>
#include <map>
#include <vector>

namespace Catalpa {

    WorldStatus TransformSystem::Compute(const Scene& scene, std::vector<ComputedTransform>& output) {
        try {
            struct Node { EntityId entity; EntityId parent; Math::Mat4 local{ 1.0f }; Math::Mat4 world{ 1.0f }; };
            std::vector<Node> nodes;
            nodes.reserve(scene.m_World.GetEntityCount());
            const auto status = scene.m_World.ForEach([&](EntityId entity) {
                TransformComponent transform;
                RelationshipComponent relationship;
                if (const auto read = scene.m_World.Get(entity, transform); !read.IsSuccess()) return read;
                if (const auto read = scene.m_World.Get(entity, relationship); !read.IsSuccess()) return read;
                Node node{ entity, relationship.parent };
                if (const auto math = Math::ComposeTRS(transform.local, node.local); !math.IsSuccess()) return WorldStatus{ WorldError::InvalidArgument, math.message };
                nodes.push_back(node);
                return WorldStatus{};
            });
            if (!status.IsSuccess()) return status;

            constexpr size_t absent = std::numeric_limits<size_t>::max();
            std::map<uint32_t, size_t> indices;
            std::vector<size_t> firstChild(nodes.size(), absent), nextSibling(nodes.size(), absent), parents(nodes.size(), absent), order;
            order.reserve(nodes.size());
            for (size_t i = 0; i < nodes.size(); ++i) indices.emplace(nodes[i].entity.index, i);
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (nodes[i].parent == EntityId{}) { order.push_back(i); continue; }
                const auto parent = indices.find(nodes[i].parent.index);
                if (parent == indices.end() || nodes[parent->second].entity != nodes[i].parent) return { WorldError::InvalidEntity, "Transform hierarchy contains a stale or foreign parent" };
                parents[i] = parent->second;
                nextSibling[i] = firstChild[parent->second];
                firstChild[parent->second] = i;
            }
            // 每个节点至多有一个父；从根迭代遍历覆盖不足即存在环，无递归栈深度限制。
            for (size_t head = 0; head < order.size(); ++head) {
                const size_t current = order[head];
                if (parents[current] == absent) nodes[current].world = nodes[current].local;
                else if (const auto math = Math::ComposeWorld(nodes[parents[current]].world, nodes[current].local, nodes[current].world); !math.IsSuccess()) return { WorldError::InvalidArgument, math.message };
                for (size_t child = firstChild[current]; child != absent; child = nextSibling[child]) order.push_back(child);
            }
            if (order.size() != nodes.size()) return { WorldError::InvalidArgument, "Transform hierarchy contains a cycle" };
            std::vector<ComputedTransform> computed;
            computed.reserve(nodes.size());
            for (const auto& node : nodes) computed.push_back({ node.entity, node.world });
            output.swap(computed);
            return {};
        } catch (const std::bad_alloc&) { return { WorldError::AllocationFailed, "Cannot allocate transform snapshot" }; }
    }

    WorldStatus TransformSystem::Update(Scene& scene) {
        if (scene.m_Committing || scene.m_UpdatingTransforms || scene.m_World.IsAccessActive()) return { WorldError::Busy, "Scene is currently borrowed or committing" };
        if (!scene.m_Dirty) return {};
        scene.m_UpdatingTransforms = true;
        // 防止计算或暂存失败使更新标记残留；整个发布阶段只处理不分配的值组件。
        struct ResetFlag { bool& flag; ~ResetFlag() { flag = false; } } reset{ scene.m_UpdatingTransforms };
        std::vector<ComputedTransform> computed;
        if (const auto status = Compute(scene, computed); !status.IsSuccess()) return status;
        try {
            std::vector<TransformComponent> values;
            values.reserve(computed.size());
            for (const auto& item : computed) {
                TransformComponent transform;
                if (const auto status = scene.m_World.Get(item.entity, transform); !status.IsSuccess()) return status;
                transform.worldMatrix = item.worldMatrix;
                transform.worldValid = true;
                values.push_back(transform);
            }
            for (size_t i = 0; i < computed.size(); ++i) {
                if (const auto status = scene.m_World.Set(computed[i].entity, values[i]); !status.IsSuccess()) return status;
            }
            scene.m_Dirty = false;
            return {};
        } catch (const std::bad_alloc&) { return { WorldError::AllocationFailed, "Cannot allocate transform publication values" }; }
    }

}
