#include "catpch.h"
#include "Scripting/ScriptCommands.h"
#include "Scene/Scene.h"

namespace Catalpa {
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
        if (!Math::ValidateTRS(candidate).IsSuccess() || (found == m_Writes.end() && m_Writes.size() >= 256)) {
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
        const bool dirty = scene.m_Dirty;
        for (size_t i = 0; i < m_Writes.size(); ++i) {
            const auto status = scene.SetLocal(scene.Resolve(m_Writes[i].target), m_Writes[i].after);
            if (!status.IsSuccess()) {
                for (size_t previous = 0; previous < i; ++previous)
                    (void)scene.SetLocal(scene.Resolve(m_Writes[previous].target), m_Writes[previous].before);
                scene.m_Dirty = dirty;
                error = status.message;
                return false;
            }
        }
        m_Writes.clear();
        error.clear();
        return true;
    }
}
