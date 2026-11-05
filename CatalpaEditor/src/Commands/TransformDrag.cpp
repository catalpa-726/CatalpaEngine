#include "Commands/TransformDrag.h"
#include "Commands/SceneCommands.h"

namespace Catalpa::Editor {
    namespace {
        bool SameLocal(const Math::TransformTRS& a, const Math::TransformTRS& b) {
            return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
        }
    }

    bool TransformDrag::Begin(SceneDocument& doc, EntityReference entity, std::string& error) {
        auto* scene = doc.Resolve();
        TransformComponent transform;
        if (Active() || !scene || !scene->CanEditNow() ||
            !scene->GetTransform(scene->Resolve(entity), transform).IsSuccess()) {
            error = "Cannot begin manipulation on a stale or busy entity";
            return false;
        }
        m_Handle = doc.Handle();
        m_Entity = entity;
        m_Document = doc.Id();
        m_Before = m_Current = transform.local;
        m_Scenes = &doc.Scenes();
        return true;
    }

    bool TransformDrag::Set(const Math::TransformTRS& value, std::string& error) {
        auto* scene = m_Scenes ? m_Scenes->GetScene(m_Handle) : nullptr;
        const auto entity = scene ? scene->Resolve(m_Entity) : EntityId{};
        if (!entity.HasValue()) {
            m_Scenes = nullptr;
            error = "Manipulated entity was removed";
            return false;
        }
        if (!scene->CanEditNow()) {
            error = "Scene has pending changes during manipulation";
            return false;
        }
        TransformComponent transform;
        if (!scene->GetTransform(entity, transform).IsSuccess() || !SameLocal(transform.local, m_Current)) {
            m_Scenes = nullptr;
            error = "Entity was edited outside this manipulation";
            return false;
        }
        if (SameLocal(value, m_Current))
            return true;
        auto status = scene->QueueSetLocal(entity, value);
        if (status.IsSuccess())
            status = scene->CommitPending();
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        m_Current = value;
        return true;
    }

    bool TransformDrag::Preview(const Math::TransformTRS& value, std::string& error) {
        return Set(value, error);
    }

    bool TransformDrag::Cancel(std::string& error) {
        if (!Active())
            return true;
        if (!Set(m_Before, error))
            return false;
        m_Scenes = nullptr;
        return true;
    }

    bool TransformDrag::Finish(SceneDocument& doc, std::string& error) {
        if (!Matches(doc, m_Entity)) {
            error = "Manipulation belongs to another document";
            return false;
        }
        const auto final = m_Current;
        // 回滚和记录位于同一 UI 安全点；失败时不留下无历史的已提交编辑。
        if (!Cancel(error))
            return false;
        if (SameLocal(m_Before, final))
            return true;
        return ChangeLocal(doc, m_Entity.persistent, m_Before, final, error);
    }
}
