#include "catpch.h"

#include "Runtime/Systems/AnimationSystem.h"

#include "Assets/AssetManager.h"
#include "Scene/Scene.h"
#include "Scene/Components/AnimatorComponent.h"
#include "Scene/Components/SkinnedMeshRendererComponent.h"
#include "Scene/Components/MeshRendererComponent.h"
#include "Scene/Components/RigidBodyComponent.h"
#include "Scene/Components/CharacterControllerComponent.h"
#include "Scene/Components/AnimationPreviewComponent.h"
#include "ECS/Detail/WorldComponentType.h"

namespace Catalpa {
	const ComponentType& ComponentTraits<AnimationPreviewComponent>::Get() {
		return Detail::MakeComponentType<AnimationPreviewComponent>();
	}
	namespace {
		bool PendingAsset(AssetManager& assets, AssetId id, std::string& error) {
			const auto status = assets.GetStatus(id);
			if (status == AssetStatus::Unloaded)
				return assets.Request(id, error);
			if (status == AssetStatus::Failed || status == AssetStatus::Cancelled) {
				error = assets.GetDiagnostic(id);
				return false;
			}
			return true;
		}
	} /* 命名空间 */

	bool AnimationSystem::Tick(Scene& scene, AssetManager& assets, double dt, std::string& error) try {
		if (!std::isfinite(dt) || dt < 0 || dt > 1) {
			error = "Animation tick must use bounded seconds in [0,1]";
			return false;
		}
		const auto world = scene.GetWorld().GetIdentity();
		/* 先收集此世界的动画器、蒙皮和预览组件，完成遍历后再推进实例。 */
		std::vector<std::pair<EntityId, AnimatorComponent>> actors;
		std::vector<std::pair<EntityId, SkinnedMeshRendererComponent>> skins;
		std::vector<std::pair<EntityId, AnimationPreviewComponent>> previews;
		const auto scanned = scene.GetWorld().ForEach([&](EntityId id) {
			if (scene.GetWorld().Has<AnimatorComponent>(id)) {
				AnimatorComponent value;
				const auto status = scene.GetWorld().Get(id, value);
				if (!status.IsSuccess())
					return status;
				actors.emplace_back(id, std::move(value));
			}
			if (scene.GetWorld().Has<SkinnedMeshRendererComponent>(id)) {
				SkinnedMeshRendererComponent value;
				const auto status = scene.GetWorld().Get(id, value);
				if (!status.IsSuccess())
					return status;
				skins.emplace_back(id, std::move(value));
			}
			if (scene.GetWorld().Has<AnimationPreviewComponent>(id)) {
				AnimationPreviewComponent value;
				const auto status = scene.GetWorld().Get(id, value);
				if (!status.IsSuccess())
					return status;
				previews.emplace_back(id, std::move(value));
			}
			return WorldStatus{};
		});
		if (!scanned.IsSuccess()) {
			error = scanned.message;
			return false;
		}
		if (actors.size() + previews.size() > 1024 || skins.size() > 4096) {
			error = "Animation instance budget exceeded";
			return false;
		}

		/* 按实体代数与资产版本复用播放状态，分别求值片段、混合树或骨架绑定姿态。 */
		std::set<Key> alive;
		for (const auto& [entity, component] : actors) {
			const Key key{world, entity.index, entity.generation};
			alive.insert(key);
			if (!component.enabled)
				continue;
			if (!component.animation.HasValue() || !std::isfinite(component.speed) || component.speed < -8 ||
			    component.speed > 8) {
				error = "Animator asset identity or speed is invalid";
				return false;
			}
			AssetVersionRef<AnimationClip> clip = component.clipVersion;
			AssetVersionRef<BlendTreeAsset> tree = component.treeVersion;
			AssetVersionRef<SkeletonAsset> bindSkeleton = component.skeletonVersion;
			bool fallback = false;
			if (clip.HasValue() && clip.GetId() != component.animation)
				clip = {};
			if (tree.HasValue() && tree.GetId() != component.animation)
				tree = {};
			if (bindSkeleton.HasValue() && bindSkeleton.GetId() != component.animation)
				bindSkeleton = {};
			const bool pinned = clip.HasValue() || tree.HasValue() || bindSkeleton.HasValue();
			if (!pinned) {
				if (!PendingAsset(assets, component.animation, error))
					return false;
				if (assets.GetStatus(component.animation) != AssetStatus::Ready)
					continue;
			}
			std::string ignored;
			const bool isClip =
			    clip.HasValue() || (!pinned && assets.Acquire(component.animation, clip, fallback, ignored));
			const bool isTree = !isClip &&
			    (tree.HasValue() || (!pinned && assets.Acquire(component.animation, tree, fallback, ignored)));
			if (fallback ||
			    (!isClip && !isTree && !bindSkeleton.HasValue() &&
			        (!assets.Acquire(component.animation, bindSkeleton, fallback, error) || fallback))) {
				error = "Animator requires an exact animation_clip, blend_tree or skeleton version";
				return false;
			}
			const uint64_t version =
			    isClip ? clip.GetVersion() : (isTree ? tree.GetVersion() : bindSkeleton.GetVersion());
			Instance next;
			if (const auto found = m_Instances.find(key); found != m_Instances.end() &&
			    found->second.animation == component.animation && found->second.version == version)
				next = found->second;
			next.entity = scene.MakeReference(entity);
			next.animation = component.animation;
			next.version = version;
			AnimationAdvance advance;
			if (isClip) {
				next.skeleton = clip.Get()->skeletonVersion;
				if (!next.skeleton.HasValue() || !next.evaluator.Bind(clip, error) ||
				    !next.evaluator.Advance(
				        dt, component.speed, component.loop, component.rootMotion, next.player, advance, error))
					return false;
			} else if (isTree) {
				next.skeleton = tree.Get()->skeletonVersion;
				const std::map<std::string, float, std::less<>> parameters(
				    component.parameters.begin(), component.parameters.end());
				if (!EvaluateBlendTree(
				        *tree.Get(), parameters, dt, next.tree, advance, error, component.rootMotion, component.speed))
					return false;
			} else {
				next.skeleton = bindSkeleton;
				for (const auto& joint : bindSkeleton.Get()->joints)
					advance.pose.local.push_back(joint.bindLocal);
				if (!BuildModelPose(*bindSkeleton.Get(), advance.pose.local, advance.pose.model, error))
					return false;
			}
			/* 实例只持有运行时姿态和固定版本；作者组件及动画源数据不写入采样时钟。 */
			next.pose = std::move(advance.pose);
			if (component.rootMotion &&
			    (glm::dot(advance.rootTranslation, advance.rootTranslation) > 0 ||
			        advance.rootRotation != Math::Quat{1, 0, 0, 0})) {
				if (scene.GetWorld().Has<RigidBodyComponent>(entity)) {
					RigidBodyComponent body;
					const auto status = scene.GetWorld().Get(entity, body);
					if (!status.IsSuccess()) {
						error = status.message;
						return false;
					}
					if (body.enabled && body.motion == PhysicsMotion::Dynamic) {
						error = "Root motion cannot write a dynamic body's physics-owned transform";
						return false;
					}
				}
				if (scene.GetWorld().Has<CharacterControllerComponent>(entity)) {
					CharacterControllerComponent controller;
					const auto status = scene.GetWorld().Get(entity, controller);
					if (!status.IsSuccess()) {
						error = status.message;
						return false;
					}
					if (controller.enabled) {
						error = "Character root motion requires an explicit movement-intent policy; direct "
						        "transform writes are forbidden";
						return false;
					}
				}
				TransformComponent transform;
				const auto status = scene.GetTransform(entity, transform);
				if (!status.IsSuccess()) {
					error = status.message;
					return false;
				}
				const auto& scale = transform.local.scale;
				if (scale.x <= 0 || std::abs(scale.x - scale.y) > 1e-5f * scale.x ||
				    std::abs(scale.x - scale.z) > 1e-5f * scale.x) {
					error = "Root-motion actors require a positive uniform local scale";
					return false;
				}
				transform.local.translation +=
				    transform.local.rotation * (transform.local.scale * advance.rootTranslation);
				transform.local.rotation = glm::normalize(transform.local.rotation * advance.rootRotation);
				const auto queued = scene.QueueSetLocal(entity, transform.local);
				if (!queued.IsSuccess()) {
					error = queued.message;
					return false;
				}
			}
			for (auto& event : advance.events) {
				if (m_Events.size() >= 4096) {
					error = "Animation event delivery budget exceeded; consume events each frame";
					return false;
				}
				m_Events.push_back({next.entity, std::move(event)});
			}
			m_Instances[key] = std::move(next);
		}
		/* 预览按指定时间采样，其姿态独立于普通动画器的推进时钟。 */
		for (const auto& [entity, preview] : previews) {
			const Key key{world, entity.index, entity.generation};
			alive.insert(key);
			if (!preview.skeleton.HasValue() || !std::isfinite(preview.time) || preview.time < 0) {
				error = "Invalid animation preview snapshot";
				return false;
			}
			Instance next;
			if (const auto prior = m_Instances.find(key);
			    prior != m_Instances.end() && prior->second.skeleton.Get() == preview.skeleton.Get())
				next = prior->second;
			next.entity = scene.MakeReference(entity);
			next.skeleton = preview.skeleton;
			next.pose = {};
			if (preview.clip.HasValue()) {
				if (next.evaluator.Clip().Get() != preview.clip.Get())
					next.evaluator.Clear();
				if (!next.evaluator.Bind(preview.clip, error) || !next.evaluator.Sample(preview.time, next.pose, error))
					return false;
			} else if (preview.tree.HasValue()) {
				BlendTreeRuntime runtime;
				runtime.evaluators = std::move(next.tree.evaluators);
				for (const auto& node : preview.tree.Get()->nodes)
					if (node.clipVersion.HasValue()) {
						AnimatorState clock;
						clock.initialized = true;
						clock.time = std::fmod(preview.time * node.speed, node.clipVersion.Get()->duration);
						if (clock.time < 0)
							clock.time += node.clipVersion.Get()->duration;
						runtime.players.emplace(node.id, clock);
					}
				AnimationAdvance advance;
				const std::map<std::string, float, std::less<>> parameters(
				    preview.parameters.begin(), preview.parameters.end());
				if (!EvaluateBlendTree(*preview.tree.Get(), parameters, 0, runtime, advance, error))
					return false;
				next.pose = std::move(advance.pose);
				next.tree.evaluators = std::move(runtime.evaluators);
			} else {
				for (const auto& joint : preview.skeleton.Get()->joints)
					next.pose.local.push_back(joint.bindLocal);
				if (!BuildModelPose(*preview.skeleton.Get(), next.pose.local, next.pose.model, error))
					return false;
			}
			m_Instances[key] = std::move(next);
		}
		std::erase_if(m_Instances,
		    [&](const auto& item) { return std::get<0>(item.first) == world && !alive.contains(item.first); });

		/* 将可见蒙皮与同一骨架版本的姿态配对，生成调色板和变形后的包围盒。 */
		std::set<Key> visible;
		for (const auto& [entity, component] : skins) {
			if (!component.enabled)
				continue;
			const Key key{world, entity.index, entity.generation};
			visible.insert(key);
			AssetVersionRef<MeshSkinBinding> binding = component.bindingVersion;
			bool fallback = false;
			if (binding.HasValue() && binding.GetId() != component.binding)
				binding = {};
			if (!binding.HasValue()) {
				if (!PendingAsset(assets, component.binding, error))
					return false;
				if (assets.GetStatus(component.binding) != AssetStatus::Ready) {
					m_Meshes.erase(key);
					continue;
				}
				if (!assets.Acquire(component.binding, binding, fallback, error) || fallback) {
					error = "Skin requires an exact mesh_skin_binding version";
					return false;
				}
			}
			const auto actor = component.animator.HasValue() ? scene.Resolve(component.animator) : entity;
			const auto pose = m_Instances.find(Key{world, actor.index, actor.generation});
			if (pose == m_Instances.end()) {
				m_Meshes.erase(key);
				continue;
			}
			const auto& data = *binding.Get();
			if (!data.skeletonVersion.HasValue() || !data.meshVersion.HasValue() ||
			    data.skeleton != pose->second.skeleton.GetId() ||
			    data.skeletonVersion.GetVersion() != pose->second.skeleton.GetVersion() ||
			    data.skeletonVersion.Get() != pose->second.skeleton.Get()) {
				error = "Skin and animator must use the same exact skeleton content version";
				return false;
			}
			MeshRendererComponent renderer;
			if (!scene.GetWorld().Has<MeshRendererComponent>(entity)) {
				m_Meshes.erase(key);
				continue;
			}
			const auto meshStatus = scene.GetWorld().Get(entity, renderer);
			if (!meshStatus.IsSuccess()) {
				error = meshStatus.message;
				return false;
			}
			if (renderer.mesh != data.meshVersion.Get()->mesh ||
			    (renderer.meshVersion.HasValue() &&
			        (renderer.meshVersion.GetId() != data.mesh ||
			            renderer.meshVersion.GetVersion() != data.meshVersion.GetVersion()))) {
				error = "Skinned renderer geometry differs from its exact skin-binding mesh version";
				return false;
			}
			AnimatedMeshSnapshot snapshot{scene.MakeReference(entity), binding};
			if (!m_Skins[key].Bind(binding, error) ||
			    !m_Skins[key].BuildPalette(pose->second.pose, snapshot.palette, error))
				return false;
			for (const auto& matrix : snapshot.palette) {
				Aabb transformed, merged;
				auto status = TransformBounds(data.meshVersion.Get()->mesh->GetData().localBounds, matrix, transformed);
				if (!status.IsSuccess()) {
					error = status.message;
					return false;
				}
				status = MergeBounds(snapshot.bounds, transformed, merged);
				if (!status.IsSuccess()) {
					error = status.message;
					return false;
				}
				snapshot.bounds = merged;
			}
			m_Meshes[key] = std::move(snapshot);
		}
		std::erase_if(m_Meshes,
		    [&](const auto& item) { return std::get<0>(item.first) == world && !visible.contains(item.first); });
		std::erase_if(m_Skins,
		    [&](const auto& item) { return std::get<0>(item.first) == world && !visible.contains(item.first); });
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	std::vector<AnimatedMeshSnapshot> AnimationSystem::Snapshot(uint64_t world) const {
		std::vector<AnimatedMeshSnapshot> result;
		for (const auto& [key, value] : m_Meshes)
			if (std::get<0>(key) == world)
				result.push_back(value);
		return result;
	}

	std::vector<AnimationRuntimeEvent> AnimationSystem::TakeEvents() {
		auto result = std::move(m_Events);
		m_Events.clear();
		return result;
	}

	void AnimationSystem::ReleaseWorld(uint64_t world) noexcept {
		std::erase_if(m_Instances, [&](const auto& item) { return std::get<0>(item.first) == world; });
		std::erase_if(m_Meshes, [&](const auto& item) { return std::get<0>(item.first) == world; });
		std::erase_if(m_Skins, [&](const auto& item) { return std::get<0>(item.first) == world; });
		std::erase_if(m_Events, [&](const auto& item) { return item.entity.world == world; });
	}
} /* 命名空间 Catalpa */
