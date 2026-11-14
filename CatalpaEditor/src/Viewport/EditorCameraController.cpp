#include "Viewport/EditorCameraController.h"

#include "Camera/CameraProjection.h"
#include "Scene/Components/CameraComponent.h"

namespace Catalpa::Editor {
	namespace {
		constexpr float SpeedMetresPerSecond = 3.0f;
		constexpr float MouseRadiansPerPixel = 0.0025f;
		constexpr float WheelMetresPerStep = 0.5f;
		constexpr float WheelStepsPerOrthoDoubling = 4.0f;

		/**
		 * @brief 按照运行时优先级为编辑视口选择启用且姿态有效的相机。
		 * 与 Runtime 相机选择顺序相同。忽略不具备可用刚性世界姿态的相机，不修整作者数据。
		 *
		 * @param[in] scene 从中筛选可用相机及其刚体世界姿态的场景。
		 * @param[in] viewport 用于检查相机投影的视口尺寸。
		 * @param[in,out] pose 发现有效候选时接收其世界位姿，没有候选时沿用调用方传入的位姿。
		 * @param[in,out] lens 发现有效候选时接收镜头参数，没有候选时沿用调用方传入的镜头。
		 * @param[out] error 读取场景失败时写入错误；没有可用相机时，也可能写入改用默认姿态的说明。
		 *
		 * @return 遍历成功时为 true，即使没有可用相机；读取场景或实体组件失败时为 false。
		 */
		bool ReadSceneCamera(const Scene& scene, const CameraViewport& viewport, Math::TransformTRS& pose,
		    CameraLens& lens, std::string& error) {
			bool found = false;
			const char* invalidReason = nullptr;
			int32_t priority = std::numeric_limits<int32_t>::min();
			PersistentEntityId selected;
			const auto status = scene.GetWorld().ForEach([&](EntityId id) -> WorldStatus {
				if (!scene.GetWorld().Has<CameraComponent>(id))
					return {};
				CameraComponent camera;
				IdentityComponent identity;
				TransformComponent transform;
				if (auto s = scene.GetWorld().Get(id, camera); !s.IsSuccess())
					return s;
				if (!camera.enabled)
					return {};
				if (auto s = scene.GetIdentity(id, identity); !s.IsSuccess())
					return s;
				if (found &&
				    (camera.priority < priority || (camera.priority == priority && selected < identity.persistent)))
					return {};
				if (auto s = scene.GetTransform(id, transform); !s.IsSuccess())
					return s;
				auto matrix = transform.worldMatrix;
				if (!transform.worldValid) {
					/* 首帧在 Runtime 派生更新之前：无父节点的作者 TRS 即其世界姿态。 */
					RelationshipComponent relation;
					if (auto s = scene.GetRelationship(id, relation); !s.IsSuccess())
						return s;
					if (relation.parent.HasValue() || !Math::ComposeTRS(transform.local, matrix).IsSuccess())
						return {};
				}
				Math::TransformTRS worldPose;
				CameraMatrices candidate;
				if (auto s = BuildCameraMatrices(matrix, camera.lens, viewport, candidate); !s.IsSuccess()) {
					invalidReason = s.message;
					return {};
				}
				if (!Math::DecomposeTRS(matrix, worldPose).IsSuccess())
					return {};
				worldPose.scale = Math::Vec3{1};
				pose = worldPose;
				lens = camera.lens;
				priority = camera.priority;
				selected = identity.persistent;
				found = true;
				return {};
			});
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
			if (!found && invalidReason)
				error = std::string("Starting the editor view at its default pose: ") + invalidReason;
			return true;
		}
	}

	bool EditorCameraController::Reset(const Scene* scene, std::string& error, CameraViewport viewport) {
		if (m_HistoryRevision == UINT64_MAX) {
			error = "Editor camera history exhausted";
			return false;
		}
		Math::TransformTRS pose;
		pose.translation = {0, 2, 10};
		CameraLens lens;
		error.clear();
		if (scene && !ReadSceneCamera(*scene, viewport, pose, lens, error))
			return false;
		m_Pose = pose;
		m_Lens = lens;
		++m_HistoryRevision;
		return true;
	}

	bool EditorCameraController::SetProjection(ProjectionType projection, std::string& error) {
		if (m_Lens.projection == projection)
			return true;
		auto lens = m_Lens;
		lens.projection = projection;
		if (projection == ProjectionType::Orthographic) {
			lens.infiniteFar = false;
			if (!std::isfinite(lens.farPlane) || lens.farPlane <= lens.nearPlane)
				lens.farPlane = lens.nearPlane + 1000.f;
		} else if (lens.nearPlane == 0.f) {
			lens.nearPlane = 0.1f;
		}
		const auto status = ValidateCameraLens(lens);
		if (!status.IsSuccess() || m_HistoryRevision == UINT64_MAX) {
			error = status.IsSuccess() ? "Editor camera history exhausted" : status.message;
			return false;
		}
		m_Lens = lens;
		++m_HistoryRevision;
		return true;
	}

	bool EditorCameraController::AlignAxis(int axis, float sign, Math::Vec3 pivot, std::string& error) {
		if (axis < 0 || axis > 2 || (sign != 1.f && sign != -1.f) || m_HistoryRevision == UINT64_MAX) {
			error = "Invalid editor camera axis or revision";
			return false;
		}
		Math::Vec3 back{0.f};
		back[axis] = sign;
		const auto upHint = axis == 1 ? Math::Vec3{0, 0, -sign} : Math::Vec3{0, 1, 0};
		const auto right = glm::normalize(glm::cross(upHint, back));
		const auto up = glm::cross(back, right);
		const float distance = std::max(glm::length(m_Pose.translation - pivot), 1.f);
		auto pose = m_Pose;
		pose.translation = pivot + distance * back;
		pose.rotation = glm::quat_cast(Math::Mat3{right, up, back});
		if (const auto status = Math::ValidateTRS(pose); !status.IsSuccess()) {
			error = status.message;
			return false;
		}
		m_Pose = pose;
		++m_HistoryRevision;
		return true;
	}

	bool EditorCameraController::Navigate(const EditorCameraInput& input, std::string& error) {
		if (!std::isfinite(input.seconds) || input.seconds < 0 || !std::isfinite(input.right) ||
		    !std::isfinite(input.up) || !std::isfinite(input.forward) || !std::isfinite(input.lookX) ||
		    !std::isfinite(input.lookY) || !std::isfinite(input.wheel)) {
			error = "Editor camera input contains an invalid time or axis";
			return false;
		}
		auto pose = m_Pose;
		if (input.lookX || input.lookY) {
			const auto oldForward = pose.rotation * Math::Vec3{0, 0, -1};
			const float pitch = std::asin(std::clamp(oldForward.y, -1.f, 1.f));
			constexpr float maxPitch = Math::Pi * 0.5f - 0.01f;
			const float target = std::clamp(pitch - input.lookY * MouseRadiansPerPixel, -maxPitch, maxPitch);
			const auto yaw = glm::angleAxis(-input.lookX * MouseRadiansPerPixel, Math::Vec3{0, 1, 0});
			const auto tilt = glm::angleAxis(target - pitch, Math::Vec3{1, 0, 0});
			pose.rotation = glm::normalize(yaw * pose.rotation * tilt);
		}
		auto direction = pose.rotation * Math::Vec3{input.right, 0, -input.forward} + Math::Vec3{0, input.up, 0};
		const float length = glm::length(direction);
		if (length > 1.f)
			direction /= length;
		/* 避免窗口失焦、拖动或调试暂停之后的巨步长，不改变模拟时钟。 */
		pose.translation += direction * (std::min(input.seconds, 0.25f) * SpeedMetresPerSecond);
		auto lens = m_Lens;
		if (input.wheel != 0) {
			if (lens.projection == ProjectionType::Perspective) {
				pose.translation += pose.rotation * Math::Vec3{0, 0, -input.wheel * WheelMetresPerStep};
			} else {
				/* 正交画面中沿视线移动不会放大物体，缩放独立视口的可见高度。 */
				lens.orthographicHeight *= std::exp2(-input.wheel / WheelStepsPerOrthoDoubling);
				if (const auto status = ValidateCameraLens(lens); !status.IsSuccess()) {
					error = status.message;
					return false;
				}
			}
		}

		const auto status = Math::ValidateTRS(pose);
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		m_Pose = pose;
		m_Lens = lens;
		return true;
	}
}
