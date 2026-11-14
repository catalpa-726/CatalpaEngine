#include "catpch.h"

#include "Runtime/GameplayModule.h"

#include "Scene/Scene.h"

namespace Catalpa {
	WorldStatus GameplayModule::Install(Scene& scene, SystemDesc system) {
		if (IsInstalled())
			return {WorldError::AlreadyExists, "Gameplay module is already installed"};
		if (system.id.starts_with("engine."))
			return {WorldError::InvalidArgument, "Gameplay identity uses the engine namespace"};
		if (system.phase != SystemPhase::Gameplay)
			return {WorldError::InvalidArgument, "Gameplay modules require the Gameplay phase"};
		if (const auto status = scene.GetSystems().Compile(); !status.IsSuccess())
			return status;
		try {
			std::string id = system.id;
			if (const auto status = scene.GetSystems().Add(std::move(system)); !status.IsSuccess())
				return status;
			if (const auto status = scene.GetSystems().Compile(); !status.IsSuccess()) {
				const auto removed = scene.GetSystems().Remove(id);
				if (!removed.IsSuccess())
					return removed;
				const auto restored = scene.GetSystems().Compile();
				if (!restored.IsSuccess())
					return restored;
				return status;
			}
			m_SystemId.swap(id);
			m_WorldIdentity = scene.GetWorld().GetIdentity();
			return {};
		} catch (const std::bad_alloc&) {
			return {WorldError::AllocationFailed, "Cannot install gameplay registration"};
		}
	}

	WorldStatus GameplayModule::Uninstall(Scene& scene) {
		if (!IsInstalled())
			return {};
		if (m_WorldIdentity != scene.GetWorld().GetIdentity())
			return {WorldError::ForeignWorld, "Gameplay registration belongs to another scene"};
		if (const auto status = scene.GetSystems().Remove(m_SystemId); !status.IsSuccess())
			return status;
		ForgetUnloadedScene();
		return {};
	}

	void GameplayModule::ForgetUnloadedScene() noexcept {
		m_WorldIdentity = 0;
		m_SystemId.clear();
	}
}
