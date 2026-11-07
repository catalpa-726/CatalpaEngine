#include "catpch.h"
#include "TestRunner.h"
#include "Physics/PhysicsService.h"
#include "Navigation/NavigationWorld.h"
#include "AssetPipeline/Navigation/RecastNavMeshBaker.h"
#include "Scene/Scene.h"
#include "AssetPipeline/Navigation/NavigationBuildProfiles.h"

namespace Catalpa::Tests
{
    void RunPhysicsNavigationTests(TestContext& test)
    {
        std::string error;
        CollisionShapeAsset ground;
        ground.dimensions = {10, 0.5f, 10};
        CollisionShapeAsset box;
        box.dimensions = {0.5f, 0.5f, 0.5f};
        auto invalid = box;
        invalid.dimensions.x = std::numeric_limits<float>::quiet_NaN();
        test.Check(!ValidateCollisionShape(invalid, error), "Collider rejects nonfinite dimensions");
        std::string json;
        CollisionShapeAsset restored;
        test.Check(WriteCollisionShape(box, json, error) && ReadCollisionShape(json, restored, error) &&
                       restored.dimensions == box.dimensions,
                   "Collider author roundtrip preserves metre-space extents");
        const std::string defaults =
            R"({"schemaVersion":1,"maxBodies":4096,"maxContacts":8192,"maxEvents":8192,"scratchBytes":16777216,"gravity":[0,-9.81,0],"collisionMasks":[65535,65535,65535,65535,65535,65535,65535,65535,65535,65535,65535,65535,65535,65535,65535,65535]})";
        PhysicsService configured;
        test.Check(configured.ConfigureFromJson(defaults, error),
                   "Physics service consumes strict runtime defaults before creating an SDK world");
        auto tooSmall = PhysicsWorldConfig{};
        tooSmall.scratchBytes = 1024 * 1024;
        test.Check(
            !ValidatePhysicsWorldConfig(tooSmall, error),
            "Physics rejects a scratch block below its configured body/contact reserve before Jolt Update");
        test.Check(!configured.ConfigureFromJson(R"({"schemaVersion":999})", error),
                   "Bad physics configuration cannot silently replace a validated candidate");
        PhysicsWorld world;
        const bool started = world.Initialize({}, error);
        test.Check(started, "Real Jolt physics world initializes without window/GPU");
        if (started) {
            PhysicsBodyHandle floor, falling;
            PhysicsBodyDesc desc;
            desc.pose.position = {0, -0.5f, 0};
            test.Check(world.CreateBody(desc, ground, {}, floor, error), "Create static ground body");
            desc.motion = PhysicsMotion::Dynamic;
            desc.pose.position = {0, 3, 0};
            test.Check(world.CreateBody(desc, box, {}, falling, error), "Create dynamic kilogram body");
            PhysicsBodyState state;
            bool stepped = true;
            for (uint64_t tick = 1; tick <= 180 && stepped; ++tick)
                stepped = world.Step(1.0 / 60, tick, error);
            test.Check(stepped && world.ReadBody(falling, state, error) &&
                           std::abs(state.pose.position.y - 0.5f) < 0.08f,
                       "Actual dynamic collision settles above ground at fixed-step metre height");
            test.Check(!world.SetPose(falling, {{0, 2, 0}, {1, 0, 0, 0}}, false, 1.0 / 60, error),
                       "Dynamic Transform cannot be overwritten without explicit teleport");
            PhysicsPose teleported;
            teleported.position = {0, 4, 0};
            test.Check(world.SetPose(falling, teleported, true, 1.0 / 60, error),
                       "Explicit teleport is admitted for dynamic body");
            PhysicsQueryHit hit;
            test.Check(world.Raycast({0, 10, 0}, {0, -20, 0}, hit, error) && hit.hit && hit.body == falling,
                       "Actual Jolt ray returns validated closest body handle");
            test.Check(world.Raycast({0, 10, 0}, {0, -20, 0}, hit, error, 0xffff, falling) && hit.hit &&
                           hit.body == floor,
                       "Ray ignore excludes the specified live body");
            test.Check(world.Sweep(box, {{3, 3, 0}, {1, 0, 0, 0}}, {0, -5, 0}, hit, error) && hit.hit &&
                           hit.body == floor,
                       "Convex sweep queries actual ground geometry");
            std::vector<PhysicsQueryHit> overlaps;
            test.Check(
                world.Overlap(box, {{0, 4, 0}, {1, 0, 0, 0}}, overlaps, error) &&
                    std::any_of(overlaps.begin(), overlaps.end(), [&](auto h) { return h.body == falling; }),
                "Overlap returns owned hit values");
            test.Check(!world.Step(1.0 / 60, 180, error), "Physics rejects duplicate fixed tick");
            test.Check(!world.Step(std::numeric_limits<double>::denorm_min(), 181, error) &&
                           world.Step(1.0 / 60, 181, error),
                       "A step that underflows the SDK float is rejected without consuming its tick");
            test.Check(!world.Raycast({0, 10, 0}, {std::numeric_limits<float>::max(), 0, 0}, hit, error) &&
                           !world.Sweep(box, {{0, 2, 0}, {1, 0, 0, 0}},
                                        {std::numeric_limits<float>::max(), 0, 0}, hit, error),
                       "Extreme finite query segments are rejected before native arithmetic overflows");
            PhysicsBodyDesc moving;
            moving.motion = PhysicsMotion::Kinematic;
            moving.pose.position = {-3, 2, 0};
            PhysicsBodyHandle platform;
            PhysicsBodyState platformBefore, platformAfter;
            test.Check(world.CreateBody(moving, box, {}, platform, error) &&
                           world.ReadBody(platform, platformBefore, error) &&
                           !world.SetPose(platform, {{900000, 2, 0}, {1, 0, 0, 0}}, false, 1.0 / 60, error) &&
                           world.ReadBody(platform, platformAfter, error) &&
                           platformAfter.pose.position == platformBefore.pose.position &&
                           world.DestroyBody(platform, error),
                       "Unsafe kinematic target velocity is rejected before changing the native pose");
            std::vector<PhysicsContactEvent> events;
            world.TakeEvents(events);
            test.Check(std::any_of(events.begin(), events.end(),
                                   [](auto e) { return e.kind == PhysicsEventKind::Begin; }),
                       "Actual collision emits a value Begin event");
            test.Check(std::is_sorted(events.begin(), events.end(),
                                      [](const auto& a, const auto& b) {
                                          return std::tie(a.tick, a.firstBody.value, a.secondBody.value,
                                                          a.kind) < std::tie(b.tick, b.firstBody.value,
                                                                             b.secondBody.value, b.kind);
                                      }),
                       "Physics dispatch values are ordered by tick, stable body handles and event kind");
            PhysicsBodyHandle character;
            PhysicsPose pose;
            pose.position = {2, 1, 0};
            test.Check(world.CreateCharacter({}, pose, character, error),
                       "Real Jolt virtual capsule controller initializes");
            CharacterMotion movement;
            bool moved = true;
            for (int n = 0; n < 120 && moved; ++n)
                moved = world.MoveCharacter(character, {1, 0, 0}, 1.0 / 60, movement, error);
            test.Check(moved && movement.grounded && movement.pose.position.x > 3.5f,
                       "Character walks and grounds using real Jolt collision queries");
            test.Check(world.DestroyBody(falling, error) && !world.ReadBody(falling, state, error),
                       "Destroyed body handle cannot resolve");
            test.Check(!world.Raycast({0, 10, 0}, {0, -20, 0}, hit, error, 0xffff, falling),
                       "Query rejects stale ignore handle");
        }
        NavigationBuildInput input;
        input.vertices = {{-10, 0, -10}, {10, 0, -10}, {10, 0, 10}, {-10, 0, 10}};
        input.indices = {0, 2, 1, 0, 3, 2};
        NavMeshAsset mesh;
        const bool baked = Tools::RecastNavMeshBaker::Bake(input, {}, mesh, error);
        test.Check(baked,
                   "Real Recast raster/compact/regions/contours/poly/detail pipeline bakes a Detour tile");
        if (baked) {
            NavMeshAsset roundtrip;
            test.Check(WriteNavMeshAsset(mesh, json, error) && ReadNavMeshAsset(json, roundtrip, error) &&
                           roundtrip.tile == mesh.tile,
                       "Cooked navigation ABI roundtrip preserves tile bytes");
            NavigationWorld navigation;
            NavigationPathRequest request;
            request.start = {-5, 0, -5};
            request.end = {5, 0, 5};
            NavigationPath path;
            test.Check(navigation.Load(std::make_shared<const NavMeshAsset>(mesh), 7, error) &&
                           navigation.FindPath(request, {}, path, error) && path.version == 7 &&
                           path.points.size() >= 2,
                       "Real Detour finds an owned exact-version path");
            auto broken = mesh;
            broken.tile[0] = std::byte(0);
            test.Check(!navigation.Load(std::make_shared<const NavMeshAsset>(broken), 8, error) &&
                           navigation.FindPath(request, {}, path, error) && path.version == 7,
                       "Invalid tile replacement preserves the previous world");
            auto wrongProfile = mesh;
            wrongProfile.settings.agentRadius += 0.1f;
            test.Check(!ValidateNavMeshAsset(wrongProfile, error),
                       "Cooked navigation profile cannot claim a different agent clearance than its tile");
            // 固定cooked ABI为Detour-v7/le/polyref32：header100、poly32、link12字节。
            // 删除整个link段，构造尺寸自洽但link容量为零的载荷，必须在进入SDK前拒绝。
            auto noLinks = mesh;
            const auto headerWord = [&](size_t index) {
                uint32_t value = 0;
                std::memcpy(&value, noLinks.tile.data() + index * 4, 4);
                return value;
            };
            const auto polyCount = headerWord(6), vertexCount = headerWord(7), linkCount = headerWord(8);
            const size_t linkOffset = 100 + size_t(vertexCount) * 12 + size_t(polyCount) * 32;
            const size_t linkBytes = size_t(linkCount) * 12;
            if (linkCount && linkOffset <= noLinks.tile.size() &&
                linkBytes <= noLinks.tile.size() - linkOffset) {
                noLinks.tile.erase(noLinks.tile.begin() + static_cast<std::ptrdiff_t>(linkOffset),
                                   noLinks.tile.begin() +
                                       static_cast<std::ptrdiff_t>(linkOffset + linkBytes));
                std::fill_n(noLinks.tile.begin() + 32, 4, std::byte{0});
                test.Check(!ValidateNavMeshAsset(noLinks, error) && error.find("header") != std::string::npos,
                           "A self-consistent zero-link tile is rejected before Detour indexes links[-1]");
            }
            else
                test.Check(false, "Navigation fixture matches its declared Detour-v7/polyref32 ABI");
            request.end = {1000, 0, 1000};
            test.Check(!navigation.FindPath(request, {}, path, error),
                       "Unreachable destination is reported explicitly");
            request.end = {5, 0, 5};
            request.areaCosts[0] = 0;
            test.Check(!navigation.FindPath(request, {}, path, error),
                       "Area costs below admissible A-star minimum are rejected");
            CancellationSource cancelled;
            cancelled.Cancel();
            request.areaCosts[0] = 1;
            test.Check(!navigation.FindPath(request, cancelled.Token(), path, error),
                       "Cancelled query does not publish a path");
            NavMeshAsset preserved = mesh;
            test.Check(!Tools::RecastNavMeshBaker::Bake(input, cancelled.Token(), preserved, error) &&
                           preserved.tile == mesh.tile,
                       "Cancelled bake preserves old asset bytes");
        }
        input.indices[0] = 999;
        test.Check(!ValidateNavigationInput(input, error),
                   "Navigation rejects invalid source indices rather than repairing geometry");
        const std::string profiles =
            R"({"schemaVersion":1,"default":"small","profiles":{"small":{"cellSize":0.15,"cellHeight":0.1,"agentHeight":0.9,"agentRadius":0.18,"maxClimb":0.15,"maxSlopeDegrees":40,"maxCells":200000}}})";
        const std::string author =
            R"({"schemaVersion":1,"settings":{"agentRadius":0.25},"vertices":[[0,0,0],[1,0,0],[0,0,1]],"indices":[0,2,1]})";
        NavigationBuildInput profiled;
        test.Check(
            Tools::ResolveNavigationBuildSource(author, profiles, profiled, error) &&
                profiled.buildProfile == "small" && profiled.settings.agentHeight == 0.9f &&
                profiled.settings.cellSize == 0.15f && profiled.settings.agentRadius == 0.25f,
            "Navigation selects the actual default build profile and applies only explicit author overrides");
        NavigationBuildInput restoredNavigation;
        test.Check(WriteNavigationSource(profiled, json, error) &&
                       ReadNavigationSource(json, restoredNavigation, error) &&
                       restoredNavigation.buildProfile == profiled.buildProfile &&
                       restoredNavigation.settings.agentRadius == profiled.settings.agentRadius,
                   "Explicit navigation source roundtrip retains resolved settings and selected profile");
    }
} // namespace Catalpa::Tests
