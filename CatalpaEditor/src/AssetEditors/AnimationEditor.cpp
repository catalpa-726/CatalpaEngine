#include "catpch.h"

#include "AssetEditors/AnimationEditor.h"

#include "AssetEditors/AssetEditorRegistry.h"
#include "Commands/GraphCommands.h"
#include "Core/Application.h"
#include "Core/Id/StableIdGenerator.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Scene/SceneDynamicComponents.h"
#include "Scene/Components/AnimationPreviewComponent.h"
#include "Scene/Components/SkinnedMeshRendererComponent.h"
#include "Scene/Components/CameraComponent.h"
#include "Scene/Components/MeshRendererComponent.h"
#include "Scene/Components/LightComponent.h"
#include "Renderer/RenderSurface.h"
#include "Assets/BuiltinAssetIds.h"
#include "imgui.h"

namespace Catalpa::Editor {
	namespace {
		constexpr uint64_t PreviewSurface = 0x434154414e494d01ULL;

		/**
		 * @brief 请求动画预览的依赖资产，并在就绪后固定其确切版本。
		 * 请求尚未加载的动画依赖，只有目标资源的确切版本就绪时才返回成功。
		 *
		 * @param[in,out] app 通过其资产管理器提交加载请求并获取指定版本。
		 * @param[in] id 要加载和固定的动画依赖资产标识。
		 * @param[out] value 成功时接收确切版本的不可变引用。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 取得指定资产的确切版本时为 true，仍在加载、加载失败或只得到回退版本时为 false。
		 */
		template <class T> bool Acquire(Application& app, AssetId id, AssetVersionRef<T>& value, std::string& error) {
			const auto status = app.GetAssets().GetStatus(id);
			if (status == AssetStatus::Unloaded) {
				if (!app.GetAssets().Request(id, error))
					return false;
				error = "Waiting for animation preview dependency";
				return false;
			}
			if (status != AssetStatus::Ready) {
				error = app.GetAssets().GetDiagnostic(id);
				if (error.empty())
					error = "Waiting for animation preview dependency";
				return false;
			}
			bool fallback = false;
			if (!app.GetAssets().Acquire(id, value, fallback, error) || fallback) {
				error = "Animation preview requires the selected exact asset version";
				return false;
			}
			return true;
		}

		template <class T>
		bool Fixed(AssetId id, uint64_t version, const std::shared_ptr<const T>& value, AssetVersionRef<T>& out,
		    std::string& error) {
			return AssetVersionRef<T>::TryCreate(id, version, value, out, error);
		}

		bool Commit(AssetDocument& document, const BlendTreeAsset& tree, std::string& error) {
			std::string text;
			return WriteBlendTree(tree, text, error) && ChangeAuthorSource(document, std::move(text), error);
		}

		std::unique_ptr<IAssetEditor> Factory() {
			return std::make_unique<AnimationEditor>();
		}

		/**
		 * @brief 按资源标识取得骨架、动画片段、蒙皮绑定和混合树的可编辑文档。
		 *
		 * @param[in] id 用于定位目标的资产标识。
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[in] documents 管理已打开的资产文档，并接收新建的动画资产文档。
		 * @param[out] error 失败时写入诊断原因。
		 *
		 * @return 文档管理器持有的动画资源文档，已打开时复用；找不到可编辑来源或打开失败时返回 nullptr。
		 */
		template <class T>
		AssetDocument* Open(AssetId id, const AssetManager& assets, DocumentManager& documents, std::string& error) {
			for (auto* document : documents.Assets())
				if (document->Asset() == id &&
				    (std::is_same_v<T, SkeletonAsset>            ? document->TypeKey() == "skeleton"
				            : std::is_same_v<T, AnimationClip>   ? document->TypeKey() == "animation_clip"
				            : std::is_same_v<T, MeshSkinBinding> ? document->TypeKey() == "mesh_skin_binding"
				                                                 : document->TypeKey() == "blend_tree")) {
					documents.Activate(document->Id());
					return document;
				}
			AssetVersionRef<T> fixed;
			bool fallback = false;
			if (!assets.Acquire(id, fixed, fallback, error) || fallback)
				return nullptr;
			std::string source;
			bool written = false;
			if constexpr (std::is_same_v<T, SkeletonAsset>)
				written = WriteSkeleton(*fixed.Get(), source, error);
			else if constexpr (std::is_same_v<T, AnimationClip>) {
				size_t keys = 0;
				for (const auto& track : fixed.Get()->tracks)
					keys += track.translation.keys.size() + track.rotation.keys.size() + track.scale.keys.size();
				written = keys > 4096 ? EncodeAnimationClip(*fixed.Get(), source, error)
				                      : WriteAnimationClip(*fixed.Get(), source, error);
			} else if constexpr (std::is_same_v<T, MeshSkinBinding>)
				written = fixed.Get()->influences.size() > 4096 ? EncodeMeshSkinBinding(*fixed.Get(), source, error)
				                                                : WriteMeshSkinBinding(*fixed.Get(), source, error);
			else
				written = WriteBlendTree(*fixed.Get(), source, error);
			if (!written)
				return nullptr;
			return documents.OpenSource(id,
			    std::is_same_v<T, SkeletonAsset>         ? "skeleton"
			        : std::is_same_v<T, AnimationClip>   ? "animation_clip"
			        : std::is_same_v<T, MeshSkinBinding> ? "mesh_skin_binding"
			                                             : "blend_tree",
			    std::move(source), {}, error);
		}
	} /* 命名空间 */

	bool RegisterAnimationEditors(AssetEditorRegistry& registry, std::string& error) {
		return registry.Register("skeleton", Factory, Open<SkeletonAsset>, error) &&
		    registry.Register("animation_clip", Factory, Open<AnimationClip>, error) &&
		    registry.Register("mesh_skin_binding", Factory, Open<MeshSkinBinding>, error) &&
		    registry.Register("blend_tree", Factory, Open<BlendTreeAsset>, error);
	}

	bool AnimationEditor::Accepts(const AssetDocument& doc) const {
		return doc.TypeKey() == "skeleton" || doc.TypeKey() == "animation_clip" ||
		    doc.TypeKey() == "mesh_skin_binding" || doc.TypeKey() == "blend_tree";
	}

	void AnimationEditor::Close(Application& app) {
		app.CloseEditorPreview(PreviewSurface);
		if (app.GetScenes().GetScene(m_Scene))
			(void)app.GetScenes().Unload(m_Scene);
		m_Scene = {};
		m_Document = {};
		m_Actor = {};
		m_Mesh = {};
		m_Camera = {};
		m_Frame = true;
		m_Time = 0;
		m_LastClock = 0;
		m_Playing = false;
		m_Canvas.Reset();
		m_CachedSource.reset();
		m_CachedPreview = {};
		m_CachedBinding = {};
		m_SkeletonSource = {};
		m_CachedBindingText.clear();
		m_CachedClipText.clear();
		m_ClipEvaluator.Clear();
		m_BlendPreview = {};
		m_DraftIdentity = {};
		m_DraftRevision = 0;
		m_Binding = {};
		m_Clip = {};
	}

	/**
	 * @brief 混合画布。
	 *
	 * @param[in,out] doc 当前编辑的文档。
	 * @param[in] tree 当前文档的动画混合树，提供要显示和编辑的节点及输入连接。
	 * @param[in] text 根据翻译键生成当前语言的界面文案。
	 * @param[out] error 失败时接收原因说明。
	 */
	void AnimationEditor::BlendCanvas(
	    AssetDocument& doc, BlendTreeAsset& tree, const PanelText& text, std::string& error) {
		std::vector<CanvasNode> nodes;
		std::vector<CanvasLink> links;
		for (size_t i = 0; i < tree.nodes.size(); ++i) {
			const auto& source = tree.nodes[i];
			CanvasNode node;
			node.id = source.id;
			node.title = source.kind == BlendNodeKind::Clip ? text("animation.node_clip", "Clip", nullptr)
			                                                : text("animation.node_blend_1d", "Blend 1D", nullptr);
			node.x = float(i % 4) * 230;
			node.y = float(i / 4) * 180;
			if (const auto saved = tree.layout.find(source.id); saved != tree.layout.end()) {
				node.x = saved->second.first;
				node.y = saved->second.second;
			}
			node.pins.push_back(
			    {CanvasIdentity(source.id, "pose"), source.id, text("animation.pose", "Pose", nullptr), false});
			for (size_t k = 0; k < source.children.size(); ++k) {
				const auto role = "child/" + std::to_string(k);
				node.pins.push_back(
				    {CanvasIdentity(source.id, role), source.id, std::to_string(source.thresholds[k]), true});
				links.push_back({CanvasIdentity(source.id, "link/" + std::to_string(k)),
				    CanvasIdentity(source.children[k], "pose"), CanvasIdentity(source.id, role)});
			}
			nodes.push_back(std::move(node));
		}
		const auto check = [&](const CanvasPin& from, const CanvasPin& to, std::string& diagnostic) {
			auto candidate = tree;
			const auto target = std::find_if(
			    candidate.nodes.begin(), candidate.nodes.end(), [&](const auto& node) { return node.id == to.node; });
			if (target == candidate.nodes.end() || target->kind != BlendNodeKind::Blend1D) {
				diagnostic = "A pose connects to a Blend1D child slot";
				return false;
			}
			for (size_t k = 0; k < target->children.size(); ++k)
				if (CanvasIdentity(target->id, "child/" + std::to_string(k)) == to.id) {
					target->children[k] = from.node;
					return ValidateBlendTree(candidate, diagnostic);
				}
			diagnostic = "Unknown blend child slot";
			return false;
		};
		auto changes = m_Canvas.Draw(doc.Id(), nodes, links, check, error);
		if (changes.selected.HasValue())
			m_Selected = changes.selected;
		if (!changes.positions.empty()) {
			for (const auto& [id, position] : changes.positions)
				tree.layout[id] = position;
			(void)Commit(doc, tree, error);
		}
		if (changes.connection) {
			const auto& [from, to] = *changes.connection;
			for (auto& node : tree.nodes)
				if (node.id == to.node)
					for (size_t k = 0; k < node.children.size(); ++k)
						if (CanvasIdentity(node.id, "child/" + std::to_string(k)) == to.id)
							node.children[k] = from.node;
			(void)Commit(doc, tree, error);
		}
		for (auto& node : tree.nodes)
			if (node.id == m_Selected) {
				ImGui::TextUnformatted(node.id.ToString().c_str());
				if (node.kind == BlendNodeKind::Clip) {
					std::array<char, 48> id{};
					const auto current = node.clip.ToString();
					std::copy(current.begin(), current.end(), id.begin());
					if (ImGui::InputText(text("animation.clip_asset", "Clip asset", "AnimationClipAsset").c_str(),
					        id.data(), id.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
						AssetId value;
						if (AssetId::TryParse(id.data(), value)) {
							node.clip = value;
							(void)Commit(doc, tree, error);
						} else
							error = "Invalid animation clip identity";
					}
					float speed = float(node.speed);
					if (ImGui::SliderFloat(
					        text("animation.node_speed", "Node speed", "AnimationNodeSpeed").c_str(), &speed, -8, 8)) {
						node.speed = speed;
						(void)Commit(doc, tree, error);
					}
				} else
					for (size_t k = 0; k < node.thresholds.size(); ++k) {
						ImGui::PushID(int(k));
						float threshold = node.thresholds[k];
						if (ImGui::InputFloat(text("animation.threshold", "Threshold", "AnimationThreshold").c_str(),
						        &threshold, 0, 0, "%.5g", ImGuiInputTextFlags_EnterReturnsTrue)) {
							node.thresholds[k] = threshold;
							(void)Commit(doc, tree, error);
						}
						ImGui::PopID();
					}
			}
	}

	void AnimationEditor::Draw(Application& app, AssetDocument& doc, const PanelText& text, std::string& error) {
		if (!Accepts(doc) || !doc.Source()) {
			error = "Animation editor needs an author document";
			return;
		}
		if (m_Document != doc.Id()) {
			Close(app);
			m_Document = doc.Id();
			m_Source.reset();
			m_Parameters.clear();
		}
		if (m_Source != doc.Source()) {
			const bool binary = doc.Source()->starts_with("CANI1") || doc.Source()->starts_with("CSKN");
			m_Buffer.clear();
			if (!binary && doc.Source()->size() <= 4 * 1024 * 1024) {
				m_Buffer.assign(std::max(size_t(65536), doc.Source()->size() + 65536), 0);
				std::copy(doc.Source()->begin(), doc.Source()->end(), m_Buffer.begin());
			}
			m_Source = doc.Source();
		}
		if (ImGui::CollapsingHeader(text("animation.author_data", "Author data", "AnimationAuthorData").c_str())) {
			if (m_Buffer.empty())
				ImGui::TextWrapped("%s",
				    text("animation.binary_author_note",
				        "Large/binary animation channels remain exact. Preview and metadata "
				        "editing are available; Save preserves the full stream.",
				        nullptr)
				        .c_str());
			else if (ImGui::InputTextMultiline(text("animation.source", "Source", "AnimationSource").c_str(),
			             m_Buffer.data(), m_Buffer.size(), {-1, 220}, ImGuiInputTextFlags_AllowTabInput)) {
				if (ChangeAuthorSource(doc, std::string(m_Buffer.data()), error))
					m_Source = doc.Source();
			}
			ImGui::TextWrapped("%s",
			    text("animation.author_note",
			        "Edits use this document's Undo/Redo and Save. Build the author "
			        "asset to publish its new runtime version.",
			        nullptr)
			        .c_str());
		}
		if (doc.TypeKey() == "blend_tree") {
			BlendTreeAsset tree;
			if (!ReadBlendTree(*doc.Source(), tree, error))
				return;
			BlendCanvas(doc, tree, text, error);
			for (const auto& [name, value] : tree.parameters) {
				auto [entry, created] = m_Parameters.try_emplace(name, value);
				ImGui::InputFloat(name.c_str(), &entry->second);
			}
			ImGui::Text("%s: %zu, %s: %zu", text("animation.states", "States", nullptr).c_str(), tree.states.size(),
			    text("animation.transitions", "Transitions", nullptr).c_str(), tree.transitions.size());
		}
		if (ImGui::Button(text(
		        m_Playing ? "animation.pause" : "animation.play", m_Playing ? "Pause" : "Play", "AnimationPlayback")
		            .c_str()))
			m_Playing = !m_Playing;
		ImGui::SameLine();
		if (ImGui::Button(text("animation.restart", "Restart", "AnimationRestart").c_str()))
			m_Time = 0;
		ImGui::SameLine();
		if (ImGui::Button(text("animation.frame_content", "Frame content", "AnimationFrame").c_str()))
			m_Frame = true;
		const double clock = ImGui::GetTime();
		if (m_Playing && m_LastClock > 0)
			m_Time += std::clamp(clock - m_LastClock, 0.0, .1);
		m_LastClock = clock;
		ImGui::InputDouble(
		    text("animation.preview_seconds", "Preview seconds", "AnimationSeconds").c_str(), &m_Time, 0, 0, "%.4f");
		if (!std::isfinite(m_Time) || m_Time < 0)
			m_Time = 0;
		ImGui::InputText(text("animation.skin_binding", "Skin binding (optional)", "AnimationBinding").c_str(),
		    m_Binding.data(), m_Binding.size());
		ImGui::InputText(text("animation.preview_clip", "Clip for skeleton/skin preview", "AnimationClip").c_str(),
		    m_Clip.data(), m_Clip.size());
		(void)Preview(app, doc, text, error);
		if (doc.TypeKey() == "animation_clip" && m_CachedSource == doc.Source() && m_CachedPreview.clip.HasValue()) {
			const auto& clip = *m_CachedPreview.clip.Get();
			const auto& skeleton = *m_CachedPreview.skeleton.Get();
			const auto commitClip = [&](const AnimationClip& changed) {
				std::string source;
				const bool binary = doc.Source()->starts_with("CANI1");
				return (binary ? EncodeAnimationClip(changed, source, error)
				               : WriteAnimationClip(changed, source, error)) &&
				    ChangeAuthorSource(doc, std::move(source), error);
			};
			std::string selected = text("animation.none", "None", nullptr);
			for (const auto& joint : skeleton.joints)
				if (joint.id == clip.rootMotionJoint)
					selected = joint.name;
			const auto change = [&](StableId id) {
				auto changed = clip;
				changed.rootMotionJoint = id;
				(void)commitClip(changed);
			};
			if (ImGui::BeginCombo(text("animation.root_motion", "Root motion joint", "AnimationRootMotion").c_str(),
			        selected.c_str())) {
				if (ImGui::Selectable(
				        text("animation.none", "None", "AnimationNoMotion").c_str(), !clip.rootMotionJoint.HasValue()))
					change({});
				for (const auto& joint : skeleton.joints)
					if (ImGui::Selectable(
					        (joint.name + "##" + joint.id.ToString()).c_str(), clip.rootMotionJoint == joint.id))
						change(joint.id);
				ImGui::EndCombo();
			}
			if (ImGui::CollapsingHeader(text("animation.events", "Events", "AnimationEvents").c_str())) {
				ImGui::BeginDisabled(clip.events.size() >= 4096);
				if (ImGui::Button(
				        text("animation.add_event", "Add event at preview time", "AnimationAddEvent").c_str())) {
					auto changed = clip;
					const double time = m_Playing ? std::fmod(m_Time, clip.duration) : m_Time;
					const double at = std::clamp(time, 0.0, std::nextafter(clip.duration, 0.0));
					const auto position = std::upper_bound(changed.events.begin(), changed.events.end(), at,
					    [](double time, const auto& event) { return time < event.time; });
					changed.events.insert(position, {at, "event", ""});
					(void)commitClip(changed);
				}
				ImGui::EndDisabled();
				ImGui::BeginChild("AnimationEventList", {0, 220}, true);
				ImGuiListClipper rows;
				rows.Begin(int(clip.events.size()), ImGui::GetFrameHeightWithSpacing() * 4);
				while (rows.Step())
					for (int row = rows.DisplayStart; row < rows.DisplayEnd; ++row) {
						ImGui::PushID(row);
						const auto& event = clip.events[size_t(row)];
						double time = event.time;
						std::array<char, 129> name{};
						std::array<char, 4097> payload{};
						std::copy(event.name.begin(), event.name.end(), name.begin());
						std::copy(event.payload.begin(), event.payload.end(), payload.begin());
						bool edited =
						    ImGui::InputDouble(text("animation.event_time", "Seconds", "AnimationEventTime").c_str(),
						        &time, 0, 0, "%.4f", ImGuiInputTextFlags_EnterReturnsTrue);
						edited |= ImGui::InputText(text("animation.event_name", "Name", "AnimationEventName").c_str(),
						    name.data(), name.size(), ImGuiInputTextFlags_EnterReturnsTrue);
						edited |= ImGui::InputText(
						    text("animation.event_payload", "Payload", "AnimationEventPayload").c_str(), payload.data(),
						    payload.size(), ImGuiInputTextFlags_EnterReturnsTrue);
						if (edited) {
							auto changed = clip;
							changed.events[size_t(row)] = {time, name.data(), payload.data()};
							(void)commitClip(changed);
						}
						if (ImGui::Button(
						        text("animation.remove_event", "Remove event", "AnimationRemoveEvent").c_str())) {
							auto changed = clip;
							changed.events.erase(changed.events.begin() + row);
							(void)commitClip(changed);
						}
						ImGui::PopID();
					}
				ImGui::EndChild();
			}
		}
	}

	/**
	 * @brief 把当前骨架、动画片段、蒙皮绑定和混合树草稿提交到独立的编辑预览场景。
	 *
	 * @param[in] app 提供活动的引擎服务和编辑预览资源。
	 * @param[in] doc 当前激活的资源文档。
	 * @param[in] text 根据翻译键生成当前语言的界面文案。
	 * @param[out] error 失败时写入诊断原因。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool AnimationEditor::Preview(Application& app, AssetDocument& doc, const PanelText& text, std::string& error) try {
		AnimationPreviewComponent preview = m_CachedPreview;
		AssetVersionRef<MeshSkinBinding> binding = m_CachedBinding;
		AssetVersionRef<SkeletonAsset> skeletonSource;
		if (doc.TypeKey() == "skeleton") {
			std::string ignored;
			bool fallback = false;
			const auto state = app.GetAssets().GetStatus(doc.Asset());
			if (state == AssetStatus::Ready) {
				if (!app.GetAssets().Acquire(doc.Asset(), skeletonSource, fallback, ignored) || fallback)
					skeletonSource = {};
			} else if (state == AssetStatus::Unloaded && (m_Clip[0] || m_Binding[0]))
				(void)app.GetAssets().Request(doc.Asset(), ignored);
		}
		if (m_CachedSource != doc.Source() || m_CachedBindingText != m_Binding.data() ||
		    m_CachedClipText != m_Clip.data() || skeletonSource.Get() != m_SkeletonSource.Get() ||
		    skeletonSource.GetVersion() != m_SkeletonSource.GetVersion()) {
			if (!m_DraftIdentity.HasValue() && !GenerateStableId(m_DraftIdentity.value, error))
				return false;
			if (m_DraftRevision == UINT64_MAX) {
				error = "Animation preview revision exhausted";
				return false;
			}
			const auto draftVersion = m_DraftRevision + 1;
			preview = {};
			binding = {};
			if (doc.TypeKey() == "skeleton") {
				SkeletonAsset value;
				if (!ReadSkeleton(*doc.Source(), value, error))
					return false;
				std::string authored, cooked;
				/* 文本排版不影响兼容性；仅语义完全相同的草稿才能借用真实已发布版本。 */
				if (skeletonSource.HasValue() && WriteSkeleton(value, authored, error) &&
				    WriteSkeleton(*skeletonSource.Get(), cooked, error) && authored == cooked)
					preview.skeleton = skeletonSource;
				else if (!Fixed(m_DraftIdentity, draftVersion, std::make_shared<const SkeletonAsset>(std::move(value)),
				             preview.skeleton, error))
					return false;
			} else if (doc.TypeKey() == "animation_clip") {
				AnimationClip value;
				if (!ReadAnimationClip(*doc.Source(), value, error) ||
				    !Acquire(app, value.skeleton, value.skeletonVersion, error))
					return false;
				preview.skeleton = value.skeletonVersion;
				if (!ValidateAnimationClip(value, preview.skeleton.Get(), error) ||
				    !Fixed(m_DraftIdentity, draftVersion, std::make_shared<const AnimationClip>(std::move(value)),
				        preview.clip, error))
					return false;
			} else if (doc.TypeKey() == "mesh_skin_binding") {
				MeshSkinBinding value;
				if (!ReadMeshSkinBinding(*doc.Source(), value, error) ||
				    !Acquire(app, value.skeleton, value.skeletonVersion, error) ||
				    !Acquire(app, value.mesh, value.meshVersion, error) ||
				    !Fixed(m_DraftIdentity, draftVersion, std::make_shared<const MeshSkinBinding>(std::move(value)),
				        binding, error))
					return false;
				preview.skeleton = binding.Get()->skeletonVersion;
			} else {
				BlendTreeAsset value;
				if (!ReadBlendTree(*doc.Source(), value, error) ||
				    !Acquire(app, value.skeleton, value.skeletonVersion, error))
					return false;
				preview.skeleton = value.skeletonVersion;
				for (auto& node : value.nodes)
					if (node.kind == BlendNodeKind::Clip && !Acquire(app, node.clip, node.clipVersion, error))
						return false;
				if (!ValidateBlendTree(value, error) ||
				    !Fixed(m_DraftIdentity, draftVersion, std::make_shared<const BlendTreeAsset>(std::move(value)),
				        preview.tree, error))
					return false;
			}
			if (m_Clip[0] && !preview.clip.HasValue() && !preview.tree.HasValue()) {
				AssetId id;
				if (!AssetId::TryParse(m_Clip.data(), id) || !Acquire(app, id, preview.clip, error))
					return false;
				const auto& clipSkeleton = preview.clip.Get()->skeletonVersion;
				if (doc.TypeKey() == "skeleton") {
					if (!clipSkeleton.HasValue() || clipSkeleton.GetId() != preview.skeleton.GetId() ||
					    clipSkeleton.GetVersion() != preview.skeleton.GetVersion() ||
					    clipSkeleton.Get() != preview.skeleton.Get()) {
						error = "The skeleton draft and selected clip do not share one exact cooked skeleton "
						        "version. Publish the skeleton and rebuild its dependent clip; edited draft "
						        "joints are never silently replaced.";
						return false;
					}
				} else
					preview.skeleton = clipSkeleton;
			}
			if (m_Binding[0] && doc.TypeKey() != "mesh_skin_binding") {
				AssetId id;
				if (!AssetId::TryParse(m_Binding.data(), id) || !Acquire(app, id, binding, error))
					return false;
			}
			if (binding.HasValue() &&
			    (binding.Get()->skeleton != preview.skeleton.GetId() ||
			        binding.Get()->skeletonVersion.GetVersion() != preview.skeleton.GetVersion())) {
				error = "Preview skin and animation must use the same fixed skeleton version";
				return false;
			}
			m_CachedPreview = preview;
			m_CachedBinding = binding;
			m_SkeletonSource = skeletonSource;
			m_CachedSource = doc.Source();
			m_CachedBindingText = m_Binding.data();
			m_CachedClipText = m_Clip.data();
			m_DraftRevision = draftVersion;
			m_ClipEvaluator.Clear();
			m_BlendPreview = {};
			m_Frame = true;
		}
		const double duration = preview.clip.HasValue() ? preview.clip.Get()->duration : 60.0;
		preview.time = m_Playing ? std::fmod(m_Time, duration) : std::min(m_Time, duration);
		for (const auto& [name, value] : m_Parameters)
			preview.parameters.emplace(name, value);
		auto check = [&](WorldStatus status) {
			if (!status.IsSuccess())
				error = status.message;
			return status.IsSuccess();
		};
		if (!app.GetScenes().GetScene(m_Scene)) {
			if (!check(app.GetScenes().CreateScene("Animation preview", m_Scene)))
				return false;
			auto* scene = app.GetScenes().GetScene(m_Scene);
			PersistentEntityId light;
			if (!GeneratePersistentEntityId(m_Actor, error) || !GeneratePersistentEntityId(m_Mesh, error) ||
			    !GeneratePersistentEntityId(m_Camera, error) || !GeneratePersistentEntityId(light, error))
				return false;
			Math::TransformTRS cameraPose;
			cameraPose.translation = {0, 1, 4};
			if (!check(scene->QueueCreate(m_Actor, "Preview animator", {})) ||
			    !check(scene->QueueCreate(m_Mesh, "Preview skinned mesh", {})) ||
			    !check(scene->QueueCreate(m_Camera, "Preview camera", cameraPose)) ||
			    !check(scene->QueueCreate(light, "Preview light", {})) || !check(scene->CommitPending()))
				return false;
			CameraComponent lens;
			lens.lens.farPlane = 1000;
			LightComponent sun;
			sun.parameters.intensity = 1000;
			if (!check(scene->QueueSetCamera(scene->Resolve(m_Camera), lens)) ||
			    !check(scene->QueueSetLight(scene->Resolve(light), sun)) || !check(scene->CommitPending()))
				return false;
		}
		auto* scene = app.GetScenes().GetScene(m_Scene);
		const auto actor = scene->Resolve(m_Actor), mesh = scene->Resolve(m_Mesh);
		if (!check(QueueDynamicComponent(*scene, actor, [actor, preview](World& world) {
			    return world.Has<AnimationPreviewComponent>(actor) ? world.Set(actor, preview)
			                                                       : world.Add(actor, preview);
		    })))
			return false;
		if (binding.HasValue()) {
			MeshRendererComponent renderer;
			renderer.mesh = binding.Get()->meshVersion.Get()->mesh;
			renderer.meshVersion = binding.Get()->meshVersion;
			AssetVersionRef<MaterialInstance> neutral;
			if (!Acquire(app, BuiltinAssetIds::DefaultUnlitInstance, neutral, error))
				return false;
			renderer.materials.assign(renderer.meshVersion.Get()->materialSlots.size(), neutral);
			SkinnedMeshRendererComponent skin;
			skin.binding = binding.GetId();
			skin.bindingVersion = binding;
			skin.animator = m_Actor;
			if (!check(scene->QueueSetMeshRenderer(mesh, renderer)) ||
			    !check(QueueDynamicComponent(*scene, mesh, [mesh, skin](World& world) {
				    return world.Has<SkinnedMeshRendererComponent>(mesh) ? world.Set(mesh, skin)
				                                                         : world.Add(mesh, skin);
			    })))
				return false;
			ImGui::TextWrapped("%s",
			    text("animation.skin_preview_note",
			        "Skin geometry preview uses a separate neutral Unlit material; "
			        "model author material slots stay unchanged.",
			        nullptr)
			        .c_str());
		} else if (scene->GetWorld().Has<MeshRendererComponent>(mesh)) {
			if (!check(scene->QueueRemoveMeshRenderer(mesh)))
				return false;
			if (scene->GetWorld().Has<SkinnedMeshRendererComponent>(mesh) &&
			    !check(QueueDynamicComponent(
			        *scene, mesh, [mesh](World& world) { return world.Remove<SkinnedMeshRendererComponent>(mesh); })))
				return false;
		}
		if (!check(scene->CommitPending()))
			return false;
		AnimationPose pose;
		if (preview.clip.HasValue()) {
			if (!m_ClipEvaluator.Bind(preview.clip, error) || !m_ClipEvaluator.Sample(preview.time, pose, error))
				return false;
		} else if (preview.tree.HasValue()) {
			BlendTreeRuntime runtime;
			runtime.evaluators = std::move(m_BlendPreview.evaluators);
			for (const auto& node : preview.tree.Get()->nodes)
				if (node.clipVersion.HasValue()) {
					AnimatorState clock;
					clock.initialized = true;
					clock.time = std::fmod(preview.time * node.speed, node.clipVersion.Get()->duration);
					if (clock.time < 0)
						clock.time += node.clipVersion.Get()->duration;
					runtime.players.emplace(node.id, clock);
				}
			AnimationAdvance sampled;
			const std::map<std::string, float, std::less<>> parameters(
			    preview.parameters.begin(), preview.parameters.end());
			if (!EvaluateBlendTree(*preview.tree.Get(), parameters, 0, runtime, sampled, error))
				return false;
			pose = std::move(sampled.pose);
			m_BlendPreview.evaluators = std::move(runtime.evaluators);
		} else {
			for (const auto& joint : preview.skeleton.Get()->joints)
				pose.local.push_back(joint.bindLocal);
			if (!BuildModelPose(*preview.skeleton.Get(), pose.local, pose.model, error))
				return false;
		}
		Math::Mat4 display{1};
		if (binding.HasValue() && !Math::InverseAffine(binding.Get()->meshToSkeleton, display).IsSuccess()) {
			error = "Preview mesh-to-skeleton inverse is invalid";
			return false;
		}
		std::vector<Math::Vec3> points;
		for (const auto& matrix : pose.model)
			points.push_back(Math::Vec3(display * matrix[3]));
		Aabb framing;
		if (!ComputeBounds(points, framing).IsSuccess()) {
			error = "Invalid animation preview bounds";
			return false;
		}
		if (binding.HasValue()) {
			Aabb combined;
			const auto status =
			    MergeBounds(framing, binding.Get()->meshVersion.Get()->mesh->GetData().localBounds, combined);
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
			framing = combined;
		}
		if (m_Frame && !framing.empty) {
			const auto center = (framing.minimum + framing.maximum) * .5f;
			const float radius = std::max(.25f, glm::length(framing.maximum - framing.minimum) * .5f);
			Math::TransformTRS camera;
			camera.translation = center + Math::Vec3(0, 0, radius * 2.5f);
			CameraComponent lens;
			lens.lens.nearPlane = radius * .02f;
			lens.lens.farPlane = radius * 20;
			if (!check(scene->QueueSetLocal(scene->Resolve(m_Camera), camera)) ||
			    !check(scene->QueueSetCamera(scene->Resolve(m_Camera), lens)) || !check(scene->CommitPending()))
				return false;
			m_Frame = false;
		}
		/* 使用引擎的调试绘制显示骨骼端点和连线包围体，蒙皮预览采用网格局部坐标。 */
		for (size_t i = 0; i < pose.model.size(); ++i) {
			const Math::Vec3 point = Math::Vec3(display * pose.model[i][3]);
			Aabb bounds{point - Math::Vec3(.012f), point + Math::Vec3(.012f), false};
			if (!app.AddDebugBounds(bounds, {.2f, 1, .3f, 1}, error, PreviewSurface))
				return false;
			const auto parent = preview.skeleton.Get()->joints[i].parent;
			if (parent >= 0) {
				const Math::Vec3 other = Math::Vec3(display * pose.model[size_t(parent)][3]);
				Aabb bone{
				    glm::min(point, other) - Math::Vec3(.003f), glm::max(point, other) + Math::Vec3(.003f), false};
				if (!app.AddDebugBounds(bone, {.4f, .7f, 1, 1}, error, PreviewSurface))
					return false;
			}
		}
		const float size = std::clamp(ImGui::GetContentRegionAvail().x, 64.f, 768.f);
		const auto pixels = uint32_t(std::clamp(size * ImGui::GetWindowViewport()->FramebufferScale.x, 64.f, 2048.f));
		if (!app.SetEditorPreview(m_Scene, PreviewSurface, {pixels, pixels}, error))
			return false;
		RenderSurfaceSnapshot surface;
		if (app.GetEditorPreview(PreviewSurface, surface) && surface.textureToken)
			ImGui::Image(ImTextureRef(ImTextureID(surface.textureToken)), {size, size});
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
} /* 命名空间 Catalpa::Editor */
