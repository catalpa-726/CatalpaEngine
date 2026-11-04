#pragma once

// 仅供 Catalpa 内部 .cpp 实例化。Box/Editor 和公共组件头不得包含此文件。
#include "ECS/World.h"
#include <entt/entity/registry.hpp>

namespace Catalpa {

    // 每类型仅有操作表，组件数据仍由 World 的唯一 EnTT registry 存储。
    struct ComponentType {
        const void* (*find)(const entt::registry&, entt::entity);
        WorldStatus (*add)(entt::registry&, entt::entity, const void*);
        WorldStatus (*set)(entt::registry&, entt::entity, const void*);
        WorldStatus (*remove)(entt::registry&, entt::entity);
    };

    namespace Detail {

        // 在所属领域 .cpp 中由 ComponentTraits<T>::Get 调用；不需要全局注册表或 schema。
        template<typename T> const ComponentType& MakeComponentType() {
            static_assert(!std::is_empty_v<T>, "S03.1 component access requires a data component.");
            static_assert(std::is_copy_constructible_v<T> && std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T> && std::is_nothrow_destructible_v<T>);
            static const ComponentType type{
                [](const entt::registry& registry, entt::entity entity) -> const void* { return registry.try_get<T>(entity); },
                [](entt::registry& registry, entt::entity entity, const void* value) -> WorldStatus {
                    if (registry.all_of<T>(entity)) return { WorldError::AlreadyExists, "Component already exists" };
                    registry.emplace<T>(entity, *static_cast<const T*>(value));
                    return {};
                },
                [](entt::registry& registry, entt::entity entity, const void* value) -> WorldStatus {
                    T* current = registry.try_get<T>(entity);
                    if (!current) return { WorldError::MissingComponent, "Component does not exist" };
                    T copy(*static_cast<const T*>(value));
                    *current = std::move(copy);
                    return {};
                },
                [](entt::registry& registry, entt::entity entity) -> WorldStatus {
                    if (!registry.all_of<T>(entity)) return { WorldError::MissingComponent, "Component does not exist" };
                    registry.remove<T>(entity);
                    return {};
                }
            };
            return type;
        }

    }

}
