#pragma once

#include "Core/Base.h"
#include "ECS/EntityId.h"

#include <functional>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace Catalpa {

	enum class WorldError {
		None,
		InvalidArgument,
		InvalidEntity,
		ForeignWorld,
		Busy,
		AlreadyExists,
		MissingComponent,
		CapacityExceeded,
		AllocationFailed,
		CommandFailed,
		InvalidPresentationView
	};

	struct WorldStatus {
		WorldError code = WorldError::None;
		const char* message = "";
		[[nodiscard]] bool IsSuccess() const noexcept { return code == WorldError::None; }
	};

	/* 描述符的定义和 EnTT 适配只存在于 ECS/Detail；组件头仅声明 Get 的导出特化。 */
	struct ComponentType;
	template <typename T> struct ComponentTraits;
	class EntityCommandBuffer;

	/*
	 * 单线程组件所有者。只复制值或在 Read 回调内借用，不向应用暴露 registry/迭代器。
	 * Get 完成复制后才替换输出。Read 提供的引用只在本次回调内有效。
	 */
	class CATALPA_API World {
	public:
		World();
		~World();
		World(const World&) = delete;
		World& operator=(const World&) = delete;
		World(World&&) = delete;
		World& operator=(World&&) = delete;

		[[nodiscard]] uint64_t GetIdentity() const noexcept;
		[[nodiscard]] size_t GetEntityCount() const noexcept;
		/**
		 * @brief 统计实体当前持有的组件数，用于检查快照覆盖范围。
		 * 快照必须确认该实体的所有组件都有恢复策略。
		 *
		 * @param[in] entity 指定要统计组件的实体，身份与代次须属于当前世界。
		 * @param[out] count 成功时接收该实体具有的组件类型数量。
		 *
		 * @return 有效实体的组件统计完成时返回成功，否则返回实体校验状态。
		 */
		[[nodiscard]] WorldStatus CountComponents(EntityId entity, size_t& count) const;
		/**
		 * @brief 检查世界当前是否处于不能卸载的访问状态。
		 * 所有者卸载前查询：读回调、组件变更或命令提交期间不可销毁 World。
		 *
		 * @return 非所属线程，或存在读取、修改、命令提交时为 true。
		 */
		[[nodiscard]] bool IsAccessActive() const noexcept;
		[[nodiscard]] bool IsAlive(EntityId entity) const noexcept;
		/**
		 * @brief 在可修改的世界中分配新实体身份。
		 *
		 * @param[out] output 成功时写入新实体句柄。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus Create(EntityId& output);
		/**
		 * @brief 销毁当前世界内有效的实体并释放其组件。
		 *
		 * @param[in] entity 待销毁实体的句柄。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		[[nodiscard]] WorldStatus Destroy(EntityId entity);

		/**
		 * @brief 在受保护的只读作用域内遍历存活实体。
		 * 只访问当前实体身份；回调期间同 World 的增删/写入及命令提交返回 Busy。
		 *
		 * @param[in] visitor 接收当前实体并返回访问结果的回调。
		 *
		 * @return 全部访问成功时返回正常状态；回调或访问失败时返回对应状态。
		 */
		[[nodiscard]] WorldStatus ForEach(const std::function<WorldStatus(EntityId)>& visitor) const;

		template <typename T> [[nodiscard]] bool Has(EntityId entity) const noexcept {
			return HasComponent(entity, ComponentTraits<T>::Get());
		}

		/**
		 * @brief 复制实体的指定组件到调用方的输出对象。
		 *
		 * @param[in] entity 当前世界中的实体句柄。
		 * @param[out] output 成功时写入组件副本。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		template <typename T> [[nodiscard]] WorldStatus Get(EntityId entity, T& output) const {
			static_assert(std::is_copy_constructible_v<T> && std::is_nothrow_move_assignable_v<T>);
			return Read<T>(entity, [&output](const T& value) {
				T copy(value);
				output = std::move(copy);
				return WorldStatus{};
			});
		}

		/**
		 * @brief 在只读访问期间把实体上的组件借给回调。
		 *
		 * @param[in] entity 当前世界中的实体句柄。
		 * @param[in] reader 接收组件常量引用并返回 WorldStatus 的回调。
		 *
		 * @return 世界操作的状态，失败时包含对应的错误类别。
		 */
		template <typename T, typename Reader> [[nodiscard]] WorldStatus Read(EntityId entity, Reader&& reader) const {
			auto invoke = [&reader](const void* value) -> WorldStatus { return reader(*static_cast<const T*>(value)); };
			return ReadComponent(entity, ComponentTraits<T>::Get(), &invoke,
			    [](const void* value, void* callback) { return (*static_cast<decltype(invoke)*>(callback))(value); });
		}

		template <typename T> [[nodiscard]] WorldStatus Add(EntityId entity, const T& value) {
			return AddComponent(entity, ComponentTraits<T>::Get(), &value);
		}
		template <typename T> [[nodiscard]] WorldStatus Set(EntityId entity, const T& value) {
			return SetComponent(entity, ComponentTraits<T>::Get(), &value);
		}
		template <typename T> [[nodiscard]] WorldStatus Remove(EntityId entity) {
			return RemoveComponent(entity, ComponentTraits<T>::Get());
		}

	private:
		friend class EntityCommandBuffer;
		struct Impl;
		std::unique_ptr<Impl> m_Impl;

		[[nodiscard]] WorldStatus Validate(EntityId entity) const noexcept;
		[[nodiscard]] WorldStatus CanMutate() const noexcept;
		[[nodiscard]] bool HasComponent(EntityId entity, const ComponentType& type) const noexcept;
		[[nodiscard]] WorldStatus ReadComponent(
		    EntityId entity, const ComponentType& type, void* context, WorldStatus (*reader)(const void*, void*)) const;
		[[nodiscard]] WorldStatus AddComponent(EntityId entity, const ComponentType& type, const void* value);
		[[nodiscard]] WorldStatus SetComponent(EntityId entity, const ComponentType& type, const void* value);
		[[nodiscard]] WorldStatus RemoveComponent(EntityId entity, const ComponentType& type);
		[[nodiscard]] WorldStatus BeginCommands() noexcept;
		void EndCommands() noexcept;
	};

}
