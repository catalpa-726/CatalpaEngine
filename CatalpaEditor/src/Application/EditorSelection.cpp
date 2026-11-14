#include "Application/EditorSelection.h"

namespace Catalpa::Editor {
	bool EditorSelection::Select(SceneDocument& doc, EntityId id) {
		const auto* scene = doc.Resolve();
		if (!scene || !scene->GetWorld().IsAlive(id)) {
			Clear();
			return false;
		}
		m_Entity = scene->MakeReference(id);
		m_Document = doc.Id();
		return m_Entity.HasValue();
	}

	EntityId EditorSelection::Resolve(const SceneDocument& doc) const {
		const auto* scene = doc.Resolve();
		return scene && m_Document == doc.Id() ? scene->Resolve(m_Entity) : EntityId{};
	}
}
