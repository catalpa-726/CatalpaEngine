#include "catpch.h"
#include "TestRunner.h"
#include "Particles/ParticleEffect.h"
#include "Materials/MaterialValidation.h"
#include "Assets/AssetManager.h"
#include "Runtime/Systems/ParticleSystem.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneDynamicComponents.h"
#include "Scene/Components/ParticleEmitterComponent.h"

namespace Catalpa::Tests {
    void RunParticleContractsTests(TestContext& test) {
        ParticleEffect effect;
        effect.material = AssetId{StableId::FromWords(0x5041525449434c45ULL, 1)};
        effect.maxParticles = 8;
        effect.emissionRate = 100;
        effect.minLifetime = effect.maxLifetime = 1;
        effect.gravity = {0, -2, 0};
        std::string error, text;
        test.Check(ValidateParticleEffect(effect, error),
                   "Particle configuration has bounded capacity and explicit units");
        auto invalid = effect;
        invalid.maxParticles = 16385;
        test.Check(!ValidateParticleEffect(invalid, error), "Oversized pools are rejected before allocation");
        invalid = effect;
        invalid.minLifetime = 0;
        test.Check(!ValidateParticleEffect(invalid, error), "Zero particle lifetime is rejected");
        invalid = effect;
        invalid.minVelocity.x = invalid.maxVelocity.x + 1;
        test.Check(!ValidateParticleEffect(invalid, error), "Velocity intervals cannot be reversed");
        test.Check(WriteParticleEffect(effect, text, error), "Particle author configuration serializes");
        ParticleEffect restored;
        test.Check(ReadParticleEffect(text, restored, error) && restored.maxParticles == 8 &&
                       restored.gravity.y == -2,
                   "Particle author configuration roundtrips units and capacity");
        ParticlePool first, second;
        test.Check(first.Reset(effect, 23, error) && second.Reset(effect, 23, error),
                   "Independent particle instances accept identical seeds");
        bool advanced = true;
        for (int i = 0; i < 10; ++i)
            advanced = advanced && first.Tick(.01, true, Math::Mat4{1}, error) &&
                       second.Tick(.01, true, Math::Mat4{1}, error);
        test.Check(advanced, "Bounded particle pools advance with fixed seconds");
        const auto a = first.Snapshot(Math::Mat4{1}), b = second.Snapshot(Math::Mat4{1});
        bool same = a.size() == b.size();
        for (size_t i = 0; i < a.size() && same; ++i)
            same = a[i].position == b[i].position && a[i].size == b[i].size && a[i].serial == b[i].serial;
        test.Check(same, "Same seed and fixed tick sequence produce identical instance snapshots");
        test.Check(first.AliveCount() <= 8 && first.DroppedCount() > 0,
                   "Full pools report dropped emissions without growing storage");
        test.Check(!first.Tick(.5, true, Math::Mat4{1}, error),
                   "Unbounded frame gaps do not silently integrate particles");
        test.Check(first.Reset(effect, 23, error) && first.AliveCount() == 0 && first.DroppedCount() == 0,
                   "Explicit restart clears pool state and drop diagnostics");
        test.Check(first.Tick(.01, false, Math::Mat4{1}, error) && first.AliveCount() == 0,
                   "Pausing emission does not create new particles");
        effect.localSpace = true;
        effect.emissionRate = 100;
        effect.minVelocity = effect.maxVelocity = {0, 0, 0};
        effect.gravity = {0, 0, 0};
        test.Check(first.Reset(effect, 1, error) && first.Tick(.01, true, Math::Mat4{1}, error),
                   "Local-space particles use emitter-relative storage");
        Math::Mat4 moved{1};
        moved[3][0] = 3;
        const auto local = first.Snapshot(moved);
        test.Check(!local.empty() && local.front().position.x == 3,
                   "Local-space particle snapshots follow the emitter's world transform");

        const auto id = [](uint64_t value) {
            return AssetId{StableId::FromWords(0x5041525449434c45ULL, value)};
        };
        std::shared_ptr<const MaterialDefinition> unlit;
        test.Check(CreateBuiltinMaterialDefinition(BuiltinMaterialKind::Unlit, id(2), unlit, error),
                   "Create a real domain material definition for particle system integration");
        if (!unlit)
            return;
        auto transparent = *unlit;
        transparent.alphaMode = MaterialAlphaMode::Blend;
        transparent.renderState.blend = MaterialBlendMode::Alpha;
        transparent.renderState.depthWrite = false;
        AssetVersionRef<MaterialDefinition> definition;
        std::shared_ptr<const MaterialInstance> material;
        AssetVersionRef<MaterialInstance> materialVersion;
        const bool materialReady =
            AssetVersionRef<MaterialDefinition>::TryCreate(
                id(3), 1, std::make_shared<const MaterialDefinition>(transparent), definition, error) &&
            CreateMaterialInstance(definition, {}, {}, 1, material, error) &&
            AssetVersionRef<MaterialInstance>::TryCreate(id(4), 1, material, materialVersion, error);
        test.Check(materialReady, "Particle integration pins a validated transparent material instance");
        if (!materialReady)
            return;
        auto small = effect, large = effect;
        small.material = large.material = materialVersion.GetId();
        small.materialVersion = large.materialVersion = materialVersion;
        small.emissionRate = large.emissionRate = 0;
        small.maxParticles = 4096;
        large.maxParticles = 12288;
        AssetVersionRef<ParticleEffect> smallVersion, largeVersion;
        const bool effectsReady =
            AssetVersionRef<ParticleEffect>::TryCreate(
                id(5), 1, std::make_shared<const ParticleEffect>(small), smallVersion, error) &&
            AssetVersionRef<ParticleEffect>::TryCreate(
                id(6), 1, std::make_shared<const ParticleEffect>(large), largeVersion, error);
        test.Check(effectsReady, "Two particle pools share the exact scene capacity budget");
        if (!effectsReady)
            return;
        JobSystem jobs;
        AssetManager assets(jobs);
        Scene scene("Particle capacity swap");
        const PersistentEntityId firstId{id(7).value}, secondId{id(8).value};
        bool ready = scene.QueueCreate(firstId, "First emitter", {}).IsSuccess() &&
                     scene.QueueCreate(secondId, "Second emitter", {}).IsSuccess() &&
                     scene.CommitPending().IsSuccess() && TransformSystem::Update(scene).IsSuccess();
        const auto assign = [&](PersistentEntityId identity, const AssetVersionRef<ParticleEffect>& fixed) {
            const auto entity = scene.Resolve(identity);
            ParticleEmitterComponent component;
            component.effect = fixed.GetId();
            component.effectVersion = fixed;
            return QueueDynamicComponent(scene, entity,
                                         [entity, component](World& world) {
                                             return world.Has<ParticleEmitterComponent>(entity)
                                                        ? world.Set(entity, component)
                                                        : world.Add(entity, component);
                                         })
                .IsSuccess();
        };
        ready = ready && assign(firstId, smallVersion) && assign(secondId, largeVersion) &&
                scene.CommitPending().IsSuccess();
        ParticleSystem system;
        test.Check(ready && system.Tick(scene, assets, 0, error),
                   "Both particle emitter pools initialize within the scene budget");
        ready = assign(firstId, largeVersion) && assign(secondId, smallVersion) &&
                scene.CommitPending().IsSuccess();
        test.Check(ready && system.Tick(scene, assets, 0, error),
                   "Same-tick expansion and shrink validate the final total independently of entity order");
    }
} // namespace Catalpa::Tests
