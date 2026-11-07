#include "catpch.h"
#include "Navigation/NavigationService.h"
#include "Core/Jobs/JobSystem.h"
#include "Assets/AssetManager.h"
#include "Scene/Scene.h"
#include "Runtime/Systems/TransformSystem.h"

namespace Catalpa
{
    struct NavigationService::Impl
    {
        struct Work
        {
            EntityReference owner;
            AssetId asset;
            uint64_t version = 0, revision = 0, ticket = 0;
            Math::Vec3 destination{};
            JobHandle job;
            NavigationPath result;
            std::string error;
            bool success = false;
        };
        struct Link
        {
            EntityReference owner;
            AssetId asset;
            uint64_t version = 0, revision = 0, ticket = 0;
            Math::Vec3 destination{};
            NavigationPath path;
            size_t point = 0;
        };
        JobSystem* jobs = nullptr;
        uint64_t world = 0, nextTicket = 1;
        std::vector<std::shared_ptr<Work>> pending;
        std::map<PersistentEntityId, Link> agents;
        std::vector<std::string> diagnostics;
    };
    NavigationService::NavigationService() : m_Impl(std::make_unique<Impl>())
    {
    }
    NavigationService::~NavigationService()
    {
        Stop();
    }
    void NavigationService::Stop() noexcept
    {
        for (auto& work : m_Impl->pending)
            work->job.Cancel();
        // owner先等待工作释放tile，Runtime随后才允许关闭Jobs或Scene。
        if (m_Impl->jobs)
            for (auto& work : m_Impl->pending)
                if (!m_Impl->jobs->IsComplete(work->job)) {
                    try {
                        std::string ignored;
                        (void)m_Impl->jobs->Wait(work->job, ignored);
                    }
                    catch (...) {
                        // 工作已join后，诊断字符串复制失败不改变关闭所需的完成证明。
                    }
                    if (!m_Impl->jobs->IsComplete(work->job))
                        std::terminate();
                }
        if (m_Impl->jobs)
            m_Impl->jobs->CollectCompleted();
        m_Impl->pending.clear();
        m_Impl->agents.clear();
        m_Impl->diagnostics.clear();
        m_Impl->jobs = nullptr;
        m_Impl->world = 0;
    }
    const std::vector<std::string>& NavigationService::Diagnostics() const noexcept
    {
        return m_Impl->diagnostics;
    }
    bool NavigationService::GetPath(PersistentEntityId id, NavigationPath& output) const
    {
        const auto i = m_Impl->agents.find(id);
        if (i == m_Impl->agents.end() || i->second.path.points.empty())
            return false;
        output = i->second.path;
        return true;
    }
    bool NavigationService::Tick(Scene& scene, AssetManager& assets, JobSystem& jobs, double dt,
                                 std::string& error)
    try {
        if (!std::isfinite(dt) || dt <= 0 || dt > 0.1 || !jobs.IsRunning()) {
            error = "Navigation requires running owner Jobs and bounded fixed step";
            return false;
        }
        if (m_Impl->world != scene.GetWorld().GetIdentity() || m_Impl->jobs != &jobs) {
            Stop();
            m_Impl->world = scene.GetWorld().GetIdentity();
            m_Impl->jobs = &jobs;
        }
        m_Impl->diagnostics.clear();
        auto status = scene.CommitPending();
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        status = TransformSystem::Update(scene);
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        for (auto it = m_Impl->pending.begin(); it != m_Impl->pending.end();) {
            const auto& work = *it;
            if (!jobs.IsComplete(work->job)) {
                ++it;
                continue;
            }
            const auto found = m_Impl->agents.find(work->owner.persistent);
            if (found != m_Impl->agents.end() && found->second.owner == work->owner &&
                found->second.ticket == work->ticket && scene.Resolve(work->owner).HasValue()) {
                if (work->success) {
                    found->second.path = std::move(work->result);
                    found->second.point = 0;
                }
                else if (m_Impl->diagnostics.size() < 256)
                    m_Impl->diagnostics.push_back(work->owner.persistent.ToString() + ": " +
                                                  (work->error.empty() ? work->job.GetError() : work->error));
            }
            it = m_Impl->pending.erase(it);
        }
        struct Input
        {
            EntityId entity;
            EntityReference owner;
            NavigationAgentComponent agent;
            CharacterControllerComponent character;
            Math::Vec3 position;
        };
        std::vector<Input> inputs;
        status = scene.GetWorld().ForEach([&](EntityId id) -> WorldStatus {
            const auto& w = scene.GetWorld();
            if (!w.Has<NavigationAgentComponent>(id))
                return {};
            NavigationAgentComponent agent;
            auto s = w.Get(id, agent);
            if (!s.IsSuccess())
                return s;
            if (!agent.enabled)
                return {};
            if (!w.Has<CharacterControllerComponent>(id)) {
                if (m_Impl->diagnostics.size() < 256)
                    m_Impl->diagnostics.push_back(
                        "Navigation agent requires CharacterController Transform authority");
                return {};
            }
            CharacterControllerComponent character;
            s = w.Get(id, character);
            if (!s.IsSuccess())
                return s;
            if (!character.enabled)
                return {};
            TransformComponent transform;
            s = w.Get(id, transform);
            if (!s.IsSuccess())
                return s;
            if (inputs.size() == 4096)
                return {WorldError::CapacityExceeded, "Navigation agent budget reached 4096"};
            inputs.push_back(
                {id, scene.MakeReference(id), agent, character, Math::Vec3(transform.worldMatrix[3])});
            return {};
        });
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        std::set<PersistentEntityId> alive;
        for (const auto& i : inputs) {
            alive.insert(i.owner.persistent);
            auto& link = m_Impl->agents[i.owner.persistent];
            const auto desired = i.agent;
            AssetVersionRef<NavMeshAsset> asset;
            bool fallback = false;
            const bool pinned =
                desired.meshVersion.HasValue() && desired.meshVersion.GetId() == desired.mesh.id;
            if (pinned)
                asset = desired.meshVersion;
            if (!pinned && ((assets.GetStatus(desired.mesh.id) == AssetStatus::Unloaded &&
                             !assets.Request(desired.mesh.id, error)) ||
                            !assets.Acquire(desired.mesh.id, asset, fallback, error) || fallback)) {
                if (m_Impl->diagnostics.size() < 256)
                    m_Impl->diagnostics.push_back("Navigation awaits exact tile: " + error);
                auto stopped = i.character;
                stopped.desiredVelocity.x = stopped.desiredVelocity.z = 0;
                status = SetCharacterController(scene, i.entity, stopped);
                if (!status.IsSuccess()) {
                    error = status.message;
                    return false;
                }
                continue;
            }
            if (!pinned) {
                auto bound = desired;
                bound.meshVersion = asset;
                status = SetNavigationAgent(scene, i.entity, bound);
                if (!status.IsSuccess()) {
                    error = status.message;
                    return false;
                }
            }
            const auto& build = asset.Get()->settings;
            const auto& capsule = i.character.settings;
            constexpr float tolerance = 1e-4f;
            if (capsule.radius > build.agentRadius + tolerance ||
                2 * (capsule.halfHeight + capsule.radius) > build.agentHeight + tolerance ||
                capsule.stepHeight + tolerance < build.maxClimb ||
                capsule.maxSlopeRadians + tolerance < build.maxSlopeDegrees * 0.01745329252f) {
                for (auto& pending : m_Impl->pending)
                    if (pending->owner == i.owner)
                        pending->job.Cancel();
                link.path = {};
                auto stopped = i.character;
                stopped.desiredVelocity.x = stopped.desiredVelocity.z = 0;
                status = SetCharacterController(scene, i.entity, stopped);
                if (!status.IsSuccess()) {
                    error = status.message;
                    return false;
                }
                if (m_Impl->diagnostics.size() < 256)
                    m_Impl->diagnostics.push_back("Navigation bake profile does not fit the character "
                                                  "radius/height/slope/step limits; movement intent stopped");
                continue;
            }
            const bool changed = link.owner != i.owner || link.asset != desired.mesh.id ||
                                 link.version != asset.GetVersion() ||
                                 link.revision != desired.requestRevision ||
                                 link.destination != desired.destination;
            if (changed) {
                for (auto& pending : m_Impl->pending)
                    if (pending->owner == link.owner && pending->ticket == link.ticket)
                        pending->job.Cancel();
                if (m_Impl->pending.size() >= 16) {
                    if (m_Impl->diagnostics.size() < 256)
                        m_Impl->diagnostics.push_back(
                            "Navigation pending-query budget reached 16; retry next tick");
                }
                else {
                    if (m_Impl->nextTicket == UINT64_MAX) {
                        error = "Navigation request identities exhausted";
                        return false;
                    }
                    auto work = std::make_shared<Impl::Work>();
                    work->owner = i.owner;
                    work->asset = desired.mesh.id;
                    work->version = asset.GetVersion();
                    work->revision = desired.requestRevision;
                    work->destination = desired.destination;
                    work->ticket = m_Impl->nextTicket++;
                    NavigationPathRequest request;
                    request.start = i.position;
                    request.end = desired.destination;
                    const auto owned = asset.GetShared();
                    // 提交后发布pending项不再分配，保证Stop能找到每一个被接受的工作。
                    m_Impl->pending.reserve(16);
                    if (!jobs.Submit(
                            [work, owned, request](const CancellationToken& token) {
                                NavigationWorld world;
                                if (token.IsCancellationRequested()) {
                                    work->error = "Navigation query cancelled before tile load";
                                    return;
                                }
                                work->success = world.Load(owned, work->version, work->error) &&
                                                world.FindPath(request, token, work->result, work->error);
                            },
                            work->job, error))
                        return false;
                    m_Impl->pending.push_back(work);
                    link = {i.owner,
                            desired.mesh.id,
                            asset.GetVersion(),
                            desired.requestRevision,
                            work->ticket,
                            desired.destination,
                            {},
                            0};
                }
            }
            auto motion = i.character;
            motion.desiredVelocity.x = motion.desiredVelocity.z = 0;
            if (!changed && !link.path.points.empty()) {
                while (link.point < link.path.points.size()) {
                    auto delta = link.path.points[link.point] - i.position;
                    delta.y = 0;
                    const auto distance = glm::length(delta);
                    if (distance <= desired.stoppingDistance) {
                        ++link.point;
                        continue;
                    }
                    const auto speed = std::min(desired.speed, distance / float(dt));
                    motion.desiredVelocity.x = delta.x / distance * speed;
                    motion.desiredVelocity.z = delta.z / distance * speed;
                    break;
                }
            }
            status = SetCharacterController(scene, i.entity, motion);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
        }
        for (auto it = m_Impl->agents.begin(); it != m_Impl->agents.end();)
            if (!alive.contains(it->first)) {
                for (auto& pending : m_Impl->pending)
                    if (pending->owner == it->second.owner)
                        pending->job.Cancel();
                // 禁用/移除导航不能留下上次的持续水平速度，由角色继续向前漂移。
                const auto entity = scene.Resolve(it->second.owner);
                CharacterControllerComponent motion;
                if (entity.HasValue() && scene.GetWorld().Has<CharacterControllerComponent>(entity) &&
                    scene.GetWorld().Get(entity, motion).IsSuccess()) {
                    motion.desiredVelocity.x = motion.desiredVelocity.z = 0;
                    const auto stopped = SetCharacterController(scene, entity, motion);
                    if (!stopped.IsSuccess()) {
                        error = stopped.message;
                        return false;
                    }
                }
                it = m_Impl->agents.erase(it);
            }
            else
                ++it;
        jobs.CollectCompleted();
        error.clear();
        return true;
    }
    catch (const std::exception& e) {
        error = e.what();
        return false;
    }
} // namespace Catalpa
