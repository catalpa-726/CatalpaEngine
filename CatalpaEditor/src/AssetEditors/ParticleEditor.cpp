#include "catpch.h"

#include "AssetEditors/ParticleEditor.h"

#include "AssetEditors/AssetEditorRegistry.h"
#include "Commands/GraphCommands.h"
#include "Core/Application.h"
#include "Core/Id/StableIdGenerator.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Scene/SceneDynamicComponents.h"
#include "Scene/Components/ParticleEmitterComponent.h"
#include "Scene/Components/CameraComponent.h"
#include "Renderer/RenderSurface.h"
#include "imgui.h"

namespace Catalpa::Editor {
	namespace {
		constexpr uint64_t ParticleSurface = 0x4341545041525401ULL;

		std::unique_ptr<IAssetEditor> Factory() {
			return std::make_unique<ParticleEditor>();
		}

		AssetDocument* Open(AssetId id, const AssetManager& assets, DocumentManager& documents, std::string& error) {
			AssetVersionRef<ParticleEffect> value;
			bool fallback = false;
			std::string source;
			if (!assets.Acquire(id, value, fallback, error) || fallback ||
			    !WriteParticleEffect(*value.Get(), source, error))
				return nullptr;
			for (auto* document : documents.Assets())
				if (document->Asset() == id && document->TypeKey() == "particle_effect") {
					documents.Activate(document->Id());
					return document;
				}
			return documents.OpenSource(id, "particle_effect", std::move(source), {}, error);
		}

		bool Commit(AssetDocument& document, const ParticleEffect& value, std::string& error) {
			std::string text;
			return WriteParticleEffect(value, text, error) && ChangeAuthorSource(document, std::move(text), error);
		}
	} /* 命名空间 */
	bool RegisterParticleEditor(AssetEditorRegistry& registry, std::string& error) {
		return registry.Register("particle_effect", Factory, Open, error);
	}

	void ParticleEditor::Close(Application& app) {
		app.CloseEditorPreview(ParticleSurface);
		if (app.GetScenes().GetScene(m_Scene))
			(void)app.GetScenes().Unload(m_Scene);
		if (m_Effect.HasValue())
			app.GetAssets().Unload(AssetId{m_Effect});
		m_Scene = {};
		m_Document = {};
		m_Emitter = {};
		m_Effect = {};
		m_Published.reset();
		m_Revision = 0;
		m_Restart = 1;
	}

	void ParticleEditor::Draw(Application& app, AssetDocument& document, const PanelText& text, std::string& error) {
		if (!document.Source()) {
			error = "Particle editor needs an author draft";
			return;
		}
		if (m_Document != document.Id()) {
			Close(app);
			m_Document = document.Id();
		}
		ParticleEffect value;
		if (!ReadParticleEffect(*document.Source(), value, error))
			return;
		bool changed = false;
		std::array<char, 48> material{};
		const auto id = value.material.ToString();
		std::copy(id.begin(), id.end(), material.begin());
		if (ImGui::InputText(text("particle.material", "Billboard material", "ParticleMaterial").c_str(),
		        material.data(), material.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
			AssetId asset;
			if (AssetId::TryParse(material.data(), asset)) {
				value.material = asset;
				changed = true;
			} else
				error = "Invalid particle material identity";
		}
		changed |=
		    ImGui::InputScalar(text("particle.seed", "Seed", "ParticleSeed").c_str(), ImGuiDataType_U32, &value.seed);
		changed |= ImGui::InputScalar(text("particle.capacity", "Maximum particles", "ParticleCapacity").c_str(),
		    ImGuiDataType_U32, &value.maxParticles);
		changed |= ImGui::InputDouble(
		    text("particle.emission", "Emission / second", "ParticleRate").c_str(), &value.emissionRate, 0, 0, "%.3f");
		changed |= ImGui::InputDouble(
		    text("particle.duration", "Duration (seconds)", "ParticleDuration").c_str(), &value.duration, 0, 0, "%.3f");
		changed |= ImGui::Checkbox(text("particle.loop", "Loop", "ParticleLoop").c_str(), &value.loop);
		changed |= ImGui::Checkbox(
		    text("particle.local_space", "Local simulation space", "ParticleLocalSpace").c_str(), &value.localSpace);
		float lifetime[]{value.minLifetime, value.maxLifetime}, size[]{value.minSize, value.maxSize},
		    angular[]{value.minAngularVelocity, value.maxAngularVelocity};
		if (ImGui::InputFloat2(text("particle.lifetime", "Lifetime range", "ParticleLifetime").c_str(), lifetime)) {
			value.minLifetime = lifetime[0];
			value.maxLifetime = lifetime[1];
			changed = true;
		}
		if (ImGui::InputFloat2(text("particle.size", "Size range (metres)", "ParticleSize").c_str(), size)) {
			value.minSize = size[0];
			value.maxSize = size[1];
			changed = true;
		}
		if (ImGui::InputFloat2(
		        text("particle.angular_speed", "Angular speed (rad/s)", "ParticleAngularSpeed").c_str(), angular)) {
			value.minAngularVelocity = angular[0];
			value.maxAngularVelocity = angular[1];
			changed = true;
		}
		changed |=
		    ImGui::InputFloat3(text("particle.velocity_min", "Velocity minimum (m/s)", "ParticleVelocityMin").c_str(),
		        &value.minVelocity.x);
		changed |=
		    ImGui::InputFloat3(text("particle.velocity_max", "Velocity maximum (m/s)", "ParticleVelocityMax").c_str(),
		        &value.maxVelocity.x);
		changed |=
		    ImGui::InputFloat3(text("particle.gravity", "Gravity (m/s2)", "ParticleGravity").c_str(), &value.gravity.x);
		changed |= ImGui::ColorEdit4(text("particle.start_color", "Start linear RGBA", "ParticleStartColor").c_str(),
		    &value.startColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		changed |= ImGui::ColorEdit4(text("particle.end_color", "End linear RGBA", "ParticleEndColor").c_str(),
		    &value.endColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		changed |= ImGui::InputFloat(
		    text("particle.end_size", "End size multiplier", "ParticleEndSize").c_str(), &value.endSizeScale);
		if (changed && !Commit(document, value, error))
			return;
		std::array<float, 33> alphaCurve{}, sizeCurve{};
		for (size_t i = 0; i < alphaCurve.size(); ++i) {
			const float age = float(i) / float(alphaCurve.size() - 1);
			alphaCurve[i] = std::lerp(value.startColor.w, value.endColor.w, age);
			sizeCurve[i] = std::lerp(1.f, value.endSizeScale, age);
		}
		ImGui::PlotLines(text("particle.alpha_curve", "Alpha over normalized lifetime", "ParticleAlphaCurve").c_str(),
		    alphaCurve.data(), int(alphaCurve.size()), 0, nullptr, 0, 1, {0, 65});
		ImGui::PlotLines(
		    text("particle.size_curve", "Size scale over normalized lifetime", "ParticleSizeCurve").c_str(),
		    sizeCurve.data(), int(sizeCurve.size()), 0, nullptr, 0, std::max(1.f, value.endSizeScale), {0, 65});
		ImGui::Checkbox(text("particle.emit_preview", "Emit in preview", "ParticleEmitPreview").c_str(), &m_Emitting);
		ImGui::SameLine();
		if (ImGui::Button(text("particle.restart", "Restart preview", "ParticleRestart").c_str())) {
			if (m_Restart == UINT64_MAX) {
				error = "Particle preview revision exhausted";
				return;
			}
			++m_Restart;
		}
		ImGui::TextWrapped("%s",
		    text("particle.preview_note",
		        "Preview owns a separate Scene and deterministic particle pool. The transparent material "
		        "must disable depth writes. Editor changes use this document's Undo/Redo and Save.",
		        nullptr)
		        .c_str());
		(void)Preview(app, document, std::move(value), error);
	}

	/**
	 * @brief 把当前粒子效果草稿提交到独立的编辑预览场景。
	 *
	 * @param[in] app 提供活动的引擎服务和编辑预览资源。
	 * @param[in] document 持有作者数据与撤销历史的资源文档。
	 * @param[in] value 参与处理的粒子效果。
	 * @param[out] error 失败时写入诊断原因。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool ParticleEditor::Preview(
	    Application& app, AssetDocument& document, ParticleEffect value, std::string& error) try {
		const auto status = app.GetAssets().GetStatus(value.material);
		if (status == AssetStatus::Unloaded) {
			(void)app.GetAssets().Request(value.material, error);
			return false;
		}
		if (status != AssetStatus::Ready) {
			error = app.GetAssets().GetDiagnostic(value.material);
			return false;
		}
		bool fallback = false;
		if (!app.GetAssets().Acquire(value.material, value.materialVersion, fallback, error) || fallback ||
		    !ValidateParticleEffect(value, error))
			return false;
		if (!m_Effect.HasValue() && !GenerateStableId(m_Effect, error))
			return false;
		if (m_Published != document.Source()) {
			if (m_Revision == UINT64_MAX) {
				error = "Particle preview version exhausted";
				return false;
			}
			if (!app.GetAssets().PublishBuiltin(AssetId{m_Effect}, m_Revision + 1,
			        std::make_shared<const ParticleEffect>(std::move(value)), sizeof(ParticleEffect), error))
				return false;
			++m_Revision;
			m_Published = document.Source();
		}
		const auto check = [&](WorldStatus result) {
			if (!result.IsSuccess())
				error = result.message;
			return result.IsSuccess();
		};
		if (!app.GetScenes().GetScene(m_Scene)) {
			if (!check(app.GetScenes().CreateScene("Particle effect preview", m_Scene)))
				return false;
			auto* scene = app.GetScenes().GetScene(m_Scene);
			PersistentEntityId camera;
			if (!GeneratePersistentEntityId(m_Emitter, error) || !GeneratePersistentEntityId(camera, error))
				return false;
			Math::TransformTRS view;
			view.translation = {0, 1, 5};
			if (!check(scene->QueueCreate(m_Emitter, "Particle emitter", {})) ||
			    !check(scene->QueueCreate(camera, "Preview camera", view)) || !check(scene->CommitPending()))
				return false;
			CameraComponent lens;
			if (!check(scene->QueueSetCamera(scene->Resolve(camera), lens)) || !check(scene->CommitPending()))
				return false;
		}
		auto* scene = app.GetScenes().GetScene(m_Scene);
		const auto entity = scene->Resolve(m_Emitter);
		ParticleEmitterComponent emitter;
		emitter.effect = AssetId{m_Effect};
		emitter.emitting = m_Emitting;
		emitter.restartRevision = m_Restart;
		if (!check(QueueDynamicComponent(*scene, entity,
		        [entity, emitter](World& world) {
			        return world.Has<ParticleEmitterComponent>(entity) ? world.Set(entity, emitter)
			                                                           : world.Add(entity, emitter);
		        })) ||
		    !check(scene->CommitPending()))
			return false;
		const float extent = std::clamp(ImGui::GetContentRegionAvail().x, 64.f, 768.f);
		const auto pixels = uint32_t(std::clamp(extent * ImGui::GetWindowViewport()->FramebufferScale.x, 64.f, 2048.f));
		if (!app.SetEditorPreview(m_Scene, ParticleSurface, {pixels, pixels}, error))
			return false;
		RenderSurfaceSnapshot surface;
		if (app.GetEditorPreview(ParticleSurface, surface) && surface.textureToken)
			ImGui::Image(ImTextureRef(ImTextureID(surface.textureToken)), {extent, extent});
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
} /* 命名空间 Catalpa::Editor */
