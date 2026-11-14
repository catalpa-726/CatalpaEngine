#include "catpch.h"

#include "ECS/SystemScheduler.h"
#include "Scene/Scene.h"

namespace Catalpa {
	WorldStatus SystemScheduler::CloneGameplayTo(Scene& target) const {
		if (std::this_thread::get_id() != m_OwnerThread || m_Running)
			return {WorldError::Busy, "Play cloning requires the scheduler owner at a safe point"};
		for (const auto& system : m_Systems) {
			if (system.phase != SystemPhase::Gameplay)
				continue;
			if (!system.cloneForPlay)
				return {WorldError::InvalidArgument, "Native gameplay must provide an independent Play factory"};
			auto copy = system.cloneForPlay(target);
			if (copy.id != system.id || copy.phase != SystemPhase::Gameplay || !copy.execute)
				return {WorldError::InvalidArgument, "Play factory changed the gameplay contract"};
			const auto added = target.GetSystems().Add(std::move(copy));
			if (!added.IsSuccess())
				return added;
		}
		return target.GetSystems().Compile();
	}
}
