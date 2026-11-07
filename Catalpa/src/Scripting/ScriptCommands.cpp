#include "catpch.h"
#include "Scripting/ScriptCommands.h"
#include "Scene/Scene.h"
#include "Scene/SceneSubtree.h"

namespace Catalpa {
    bool ScriptCommands::Create(Scene& scene, std::string_view name, const Math::TransformTRS& local,
                                PersistentEntityId& output, std::string& error) {
        if (name.size() > 1024 || name.find('\0') != name.npos || !Math::ValidateTRS(local).IsSuccess() ||
            m_Creates.size() + m_Destroys.size() + m_Writes.size() >= 256 ||
            scene.GetWorld().GetEntityCount() + m_Creates.size() >= 65536) {
            error = "Script entity creation exceeds name/transform/entity/command bounds";
            return false;
        }
        PersistentEntityId id;
        if (!GeneratePersistentEntityId(id, error))
            return false;
        m_Creates.push_back({id, std::string(name), local});
        output = id;
        error.clear();
        return true;
    }
    bool ScriptCommands::Destroy(Scene& scene, PersistentEntityId id, std::string& error) {
        const auto pending = std::find_if(m_Creates.begin(), m_Creates.end(),
                                          [&](const auto& value) { return value.id == id; });
        if (pending != m_Creates.end()) {
            m_Creates.erase(pending);
            error.clear();
            return true;
        }
        const auto entity = scene.Resolve(id);
        if (!entity.HasValue() || m_Creates.size() + m_Destroys.size() + m_Writes.size() >= 256) {
            error = "Script destroy target or command capacity rejected";
            return false;
        }
        const auto reference = scene.MakeReference(entity);
        if (std::find(m_Destroys.begin(), m_Destroys.end(), reference) == m_Destroys.end())
            m_Destroys.push_back(reference);
        error.clear();
        return true;
    }
    bool ScriptCommands::SetTranslation(Scene& scene, EntityReference ref, Math::Vec3 value, std::string& error) {
        const auto entity = scene.Resolve(ref);
        TransformComponent transform;
        if (!entity.HasValue() || !scene.GetTransform(entity, transform).IsSuccess()) {
            error = "Script entity reference expired";
            return false;
        }
        const auto found = std::find_if(m_Writes.begin(), m_Writes.end(),
                                       [&](const auto& w) { return w.target == ref; });
        auto candidate = found == m_Writes.end() ? transform.local : found->after;
        candidate.translation = value;
        if (!Math::ValidateTRS(candidate).IsSuccess() ||
            (found == m_Writes.end() && m_Writes.size() + m_Creates.size() + m_Destroys.size() >= 256)) {
            error = "Script command limit or invalid transform";
            return false;
        }
        if (found == m_Writes.end()) m_Writes.push_back({ref, transform.local, candidate});
        else found->after = candidate;
        return true;
    }

    bool ScriptCommands::Translate(Scene& scene, EntityReference ref, Math::Vec3 delta, std::string& error) {
        TransformComponent transform;
        const auto found = std::find_if(m_Writes.begin(), m_Writes.end(),
                                       [&](const auto& w) { return w.target == ref; });
        if (found != m_Writes.end()) return SetTranslation(scene, ref, found->after.translation + delta, error);
        if (!scene.GetTransform(scene.Resolve(ref), transform).IsSuccess()) {
            error = "Script target is missing";
            return false;
        }
        return SetTranslation(scene, ref, transform.local.translation + delta, error);
    }

    bool ScriptCommands::ReadTranslation(const Scene& scene,EntityReference ref,Math::Vec3& output,std::string& error) const {
        TransformComponent transform;
        if(!scene.GetTransform(scene.Resolve(ref),transform).IsSuccess()) {error="Script entity expired";return false;}
        const auto found=std::find_if(m_Writes.begin(),m_Writes.end(),[&](const auto& write){return write.target==ref;});
        output=found==m_Writes.end()?transform.local.translation:found->after.translation;
        return true;
    }

    bool ScriptCommands::Commit(Scene& scene, std::string& error) {
        if (!scene.CanEditNow()) {
            error = "Script command publication requires a structure boundary";
            return false;
        }
        for (const auto& w : m_Writes) {
            TransformComponent current;
            const auto id = scene.Resolve(w.target);
            if (!scene.GetTransform(id, current).IsSuccess() || current.local.translation != w.before.translation ||
                current.local.rotation != w.before.rotation || current.local.scale != w.before.scale) {
                error = "Script target changed before publication";
                return false;
            }
        }
        std::vector<SceneSubtreeSnapshot> removed(m_Destroys.size());
        std::set<PersistentEntityId> destroyedIds;
        for (size_t i = 0; i < m_Destroys.size(); ++i) {
            const auto entity = scene.Resolve(m_Destroys[i]);
            const auto status = scene.CaptureForUndo(entity, removed[i]);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            bool overlap = false;
            removed[i].ForEachIdentity([&](PersistentEntityId id) {
                if (!destroyedIds.insert(id).second)
                    overlap = true;
            });
            if (overlap) {
                error = "Script destroy subtrees overlap";
                return false;
            }
        }
        for (const auto& write : m_Writes)
            if (destroyedIds.contains(write.target.persistent)) {
                error = "Script cannot transform and delete the same subtree in one call";
                return false;
            }
        for (const auto& create : m_Creates)
            if (scene.Resolve(create.id).HasValue()) {
                error = "Script created identity collided before publication";
                return false;
            }
        const bool dirty = scene.m_Dirty;
        std::vector<PersistentEntityId> created;
        created.reserve(m_Creates.size());
        for (const auto& create : m_Creates) {
            auto status = scene.QueueCreate(create.id, create.name, create.local);
            if (status.IsSuccess())
                status = scene.CommitPending();
            if (!status.IsSuccess()) {
                (void)scene.m_Commands.Clear();
                bool restored = true;
                for (const auto id : created)
                    restored &= scene.DestroySubtree(scene.Resolve(id)).IsSuccess();
                scene.m_Dirty = dirty;
                error = restored ? status.message
                                 : "Script creation rollback failed; scene requires controlled shutdown";
                return false;
            }
            created.push_back(create.id);
        }
        for (size_t i = 0; i < m_Writes.size(); ++i) {
            const auto status = scene.SetLocal(scene.Resolve(m_Writes[i].target), m_Writes[i].after);
            if (!status.IsSuccess()) {
                bool restored = true;
                for (size_t previous = 0; previous < i; ++previous)
                    restored &=
                        scene.SetLocal(scene.Resolve(m_Writes[previous].target), m_Writes[previous].before)
                            .IsSuccess();
                for (const auto id : created)
                    restored &= scene.DestroySubtree(scene.Resolve(id)).IsSuccess();
                scene.m_Dirty = dirty;
                error = restored ? status.message
                                 : "Script transform rollback failed; scene requires controlled shutdown";
                return false;
            }
        }
        for (size_t i = 0; i < m_Destroys.size(); ++i) {
            SceneSubtreeSnapshot snapshot;
            const auto status = scene.RemoveForUndo(scene.Resolve(m_Destroys[i]), snapshot, &removed[i]);
            if (!status.IsSuccess()) {
                bool restored = true;
                for (size_t previous = i; previous > 0; --previous)
                    restored &= scene.RestoreForUndo(removed[previous - 1]).IsSuccess();
                for (const auto& write : m_Writes)
                    restored &= scene.SetLocal(scene.Resolve(write.target), write.before).IsSuccess();
                for (const auto id : created)
                    restored &= scene.DestroySubtree(scene.Resolve(id)).IsSuccess();
                scene.m_Dirty = dirty;
                error = restored ? status.message
                                 : "Script structural rollback failed; scene requires controlled shutdown";
                return false;
            }
        }
        Clear();
        error.clear();
        return true;
    }
}
