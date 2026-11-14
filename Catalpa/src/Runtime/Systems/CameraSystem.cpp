#include "catpch.h"

#include "Runtime/Systems/CameraSystem.h"

#include "Camera/CameraProjection.h"
#include "Scene/Scene.h"
#include "Scene/Components/CameraComponent.h"

namespace Catalpa {
	WorldStatus CameraSystem::Build(
	    const Scene& scene, EntityId camera, const CameraViewport& viewport, CameraMatrices& output) {
		CameraComponent component;
		if (const auto status = scene.GetWorld().Get(camera, component); !status.IsSuccess())
			return status;
		if (!component.enabled || component.viewId == 0 || component.historyRevision == 0)
			return {WorldError::InvalidArgument, "Camera is disabled or has invalid view identity"};
		TransformComponent transform;
		if (const auto status = scene.GetTransform(camera, transform); !status.IsSuccess())
			return status;
		if (!transform.worldValid)
			return {WorldError::InvalidArgument, "Camera world transform has not been updated"};

		/* 相机数学与独立预览共用；这里只负责从已提交Scene取得作者镜头和刚性世界姿态。 */
		if (const auto status = BuildCameraMatrices(transform.worldMatrix, component.lens, viewport, output);
		    !status.IsSuccess())
			return {WorldError::InvalidArgument, status.message};
		return {};
	}

}
