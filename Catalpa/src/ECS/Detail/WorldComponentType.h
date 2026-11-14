#pragma once

/* 仅供 Catalpa 内部 .cpp 实例化。Box/Editor 和公共组件头不得包含此文件。 */
#include "ECS/World.h"

#include <entt/entity/registry.hpp>

namespace Catalpa {

	/* 每种类型只保留一份操作表，组件数据统一存储在 World 持有的 EnTT 注册表中。 */
	struct ComponentType {
		const void* (*find)(const entt::registry&, entt::entity);
		WorldStatus (*add)(entt::registry&, entt::entity, const void*);
		WorldStatus (*set)(entt::registry&, entt::entity, const void*);
		WorldStatus (*remove)(entt::registry&, entt::entity);
	};

	namespace Detail {

		/**
		 * @brief 构建组件类型。
		 *
		 * @return 组件类型 T 对应的只读静态操作表，表中保存查询、构造和变更等操作。
		 */
		template <typename T> const ComponentType& MakeComponentType() {
			static_assert(!std::is_empty_v<T>, "S03.1 component access requires a data component.");
			static_assert(std::is_copy_constructible_v<T> && std::is_nothrow_move_constructible_v<T> &&
			    std::is_nothrow_move_assignable_v<T> && std::is_nothrow_destructible_v<T>);
			static const ComponentType type{[](const entt::registry& registry, entt::entity entity) -> const void* {
				                                return registry.try_get<T>(entity);
			                                },
			    [](entt::registry& registry, entt::entity entity, const void* value) -> WorldStatus {
				    if (registry.all_of<T>(entity))
					    return {WorldError::AlreadyExists, "Component already exists"};
				    registry.emplace<T>(entity, *static_cast<const T*>(value));
				    return {};
			    },
			    [](entt::registry& registry, entt::entity entity, const void* value) -> WorldStatus {
				    T* current = registry.try_get<T>(entity);
				    if (!current)
					    return {WorldError::MissingComponent, "Component does not exist"};
				    T copy(*static_cast<const T*>(value));
				    *current = std::move(copy);
				    return {};
			    },
			    [](entt::registry& registry, entt::entity entity) -> WorldStatus {
				    if (!registry.all_of<T>(entity))
					    return {WorldError::MissingComponent, "Component does not exist"};
				    registry.remove<T>(entity);
				    return {};
			    }};
			return type;
		}

	}

}
