#include "AssetEditors/MaterialEditor.h"

#include "Assets/BuiltinAssetIds.h"
#include "Camera/CameraProjection.h"
#include "Commands/AssetCommands.h"
#include "Core/Application.h"
#include "Core/Id/StableIdGenerator.h"
#include "Core/Math/CoordinateConvention.h"
#include "Geometry/PrimitiveMesh.h"
#include "Scene/Components/EnvironmentComponent.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Scene/SceneSubtree.h"
#include "Renderer/RenderSurface.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "Panels/EditorText.h"
#include "Panels/FormLayout.h"

namespace Catalpa::Editor {
	namespace {
		constexpr uint64_t MaterialSurface = 0x4341544d41544c01ULL;
		constexpr float PreviewSphereRadius = .82f, PreviewCubeSize = 1.f;

		/**
		 * @brief 在已呈现的无光照预览图上投影少量经纬线或可见棱边。
		 *
		 * @param[in] view 产出当前纹理的相机，用于消隐与透视投影。
		 * @param[in] imageMin 预览图左上角的屏幕坐标。
		 * @param[in] imageSize 预览图在界面中的逻辑尺寸。
		 * @param[in] sphere 为真时绘制球面经纬线，为假时绘制立方体的棱边。
		 */
		void DrawShapeGuides(const RenderView& view, ImVec2 imageMin, ImVec2 imageSize, bool sphere) {
			auto* draw = ImGui::GetWindowDrawList();
			draw->PushClipRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y), true);
			const float width = std::max(1.f, ImGui::GetFontSize() / 16.f);
			const auto line = [&](const Math::Vec3& a, const Math::Vec3& b) {
				const auto ca = view.matrices.viewProjection * Math::Vec4(a, 1);
				const auto cb = view.matrices.viewProjection * Math::Vec4(b, 1);
				if (ca.w <= 0 || cb.w <= 0)
					return;
				const auto screen = [&](const Math::Vec4& clip) {
					return ImVec2(imageMin.x + (.5f * clip.x / clip.w + .5f) * imageSize.x,
					    imageMin.y + (.5f * clip.y / clip.w + .5f) * imageSize.y);
				};
				const auto p = screen(ca), q = screen(cb);
				draw->AddLine(p, q, IM_COL32(15, 18, 20, 95), width * 2.f);
				draw->AddLine(p, q, IM_COL32(245, 248, 250, 105), width);
			};
			const auto& eye = view.matrices.position;
			if (sphere) {
				constexpr int rings = 24, sectors = 48;
				const auto point = [](int ring, int sector) {
					const float latitude = Math::Pi * float(ring) / rings;
					const float longitude = 2.f * Math::Pi * float(sector) / sectors;
					const float h = std::sin(latitude);
					return PreviewSphereRadius *
					    Math::Vec3(h * std::cos(longitude), std::cos(latitude), h * std::sin(longitude));
				};
				/* 球心在原点，可见半球满足 dot(eye - p, p) >= 0。在实际透视轮廓处截断线段。 */
				const auto arc = [&](Math::Vec3 a, Math::Vec3 b) {
					const float da = glm::dot(eye, a) - PreviewSphereRadius * PreviewSphereRadius;
					const float db = glm::dot(eye, b) - PreviewSphereRadius * PreviewSphereRadius;
					if (da < 0 && db < 0)
						return;
					if (da < 0)
						a = glm::mix(a, b, da / (da - db));
					else if (db < 0)
						b = glm::mix(a, b, da / (da - db));
					line(a, b);
				};
				for (int ring = rings / 4; ring < rings; ring += rings / 4)
					for (int sector = 0; sector < sectors; ++sector)
						arc(point(ring, sector), point(ring, sector + 1));
				for (int sector = 0; sector < sectors; sector += sectors / 6)
					for (int ring = 0; ring < rings; ++ring)
						arc(point(ring, sector), point(ring + 1, sector));
			} else {
				constexpr float half = PreviewCubeSize / 2;
				for (int axis = 0; axis < 3; ++axis) {
					const int u = (axis + 1) % 3, v = (axis + 2) % 3;
					for (int su : {-1, 1}) {
						for (int sv : {-1, 1}) {
							/* 相邻两面都背向相机时，这条棱被立方体本身遮挡。 */
							if (su * eye[u] <= half && sv * eye[v] <= half)
								continue;
							Math::Vec3 a{};
							a[u] = su * half;
							a[v] = sv * half;
							a[axis] = -half;
							auto b = a;
							b[axis] = half;
							line(a, b);
						}
					}
				}
			}
			draw->PopClipRect();
		}

		/**
		 * @brief 编辑值。
		 *
		 * @param[in] value 参与处理的 MaterialValue。
		 *
		 * @return 用户在本帧修改了该参数且控件接受新值时为 true。
		 */
		bool EditValue(MaterialValue& value) {
			const char* label = "##Value";
			const auto flags = ImGuiInputTextFlags_EnterReturnsTrue;
			return std::visit(
			    [&](auto& v) {
				    using T = std::decay_t<decltype(v)>;
				    if constexpr (std::is_same_v<T, float>)
					    return ImGui::InputFloat(label, &v, 0, 0, "%.6g", flags);
				    else if constexpr (std::is_same_v<T, bool>)
					    return ImGui::Checkbox(label, &v);
				    else if constexpr (std::is_same_v<T, int32_t>)
					    return ImGui::InputScalar(label, ImGuiDataType_S32, &v, nullptr, nullptr, nullptr, flags);
				    else if constexpr (std::is_same_v<T, uint32_t>)
					    return ImGui::InputScalar(label, ImGuiDataType_U32, &v, nullptr, nullptr, nullptr, flags);
				    else if constexpr (std::is_same_v<T, AssetHandle<ImageAsset>>)
					    return false;
				    else
					    return ImGui::InputScalarN(
					        label, ImGuiDataType_Float, v.data(), int(v.size()), nullptr, nullptr, "%.6g", flags);
			    },
			    value);
		}
	}

	bool SetMaterialTexture(AssetDocument& doc, AssetManager& assets, const MaterialParameter& parameter,
	    AssetHandle<ImageAsset> image, std::string& error) {
		const MaterialParameterOverride edit{parameter.id, image};
		return ChangeMaterial(doc, std::span{&edit, 1}, {}, error, &assets);
	}

	void MaterialEditor::Close(Application& app) {
		app.CloseEditorPreview(MaterialSurface);
		if (app.GetScenes().GetScene(m_Scene))
			(void)app.GetScenes().Unload(m_Scene);
		m_Scene = {};
		m_Document = {};
		m_MaterialKey = {};
		m_Object = {};
		m_Bound.reset();
		m_SliderSource.reset();
		m_SliderPreview.reset();
		m_Renderer = {};
		m_Version = 0;
	}

	/**
	 * @brief 更新场景。
	 *
	 * @param[in] app 提供场景、资产和渲染服务的应用对象。
	 * @param[in,out] doc 当前编辑的文档。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool MaterialEditor::UpdateScene(Application& app, AssetDocument& doc, std::string& error) {
		auto& scenes = app.GetScenes();
		auto check = [&](WorldStatus status) {
			if (!status.IsSuccess())
				error = status.message;
			return status.IsSuccess();
		};
		if (m_Document != doc.Id())
			Close(app);
		if (!m_Scene.HasValue()) {
			if (!check(scenes.CreateScene("Material preview", m_Scene)))
				return false;
			m_Document = doc.Id();
			auto* scene = scenes.GetScene(m_Scene);
			PersistentEntityId light;
			StableId meshId;
			if (!scene || !GeneratePersistentEntityId(m_Object, error) || !GeneratePersistentEntityId(light, error) ||
			    !GenerateStableId(meshId, error) || !GenerateStableId(m_MaterialKey, error))
				return false;
			Math::TransformTRS lightPose;
			lightPose.rotation = Math::Quat(Math::Vec3(-.68f, -.82f, 0.f));
			if (!check(scene->QueueCreate(light, "Preview light", lightPose)) ||
			    !check(scene->QueueCreate(m_Object, "Preview mesh", {})) || !check(scene->CommitPending()))
				return false;
			LightComponent sun;
			sun.parameters.intensity = 3.4f;
			/* 预览台以侧光呈现受光方向，暗部与金属反射保留 HDR 环境；背景采用预览表面的清屏色。 */
			EnvironmentComponent environment;
			environment.environment.id = BuiltinAssetIds::DefaultEnvironment;
			environment.settings.intensity = .42f;
			environment.settings.drawBackground = false;
			const auto lightEntity = scene->Resolve(light);
			if (!check(scene->QueueSetLight(lightEntity, sun)) ||
			    !check(scene->QueueSetEnvironment(lightEntity, environment)) || !check(scene->CommitPending()))
				return false;
			MeshData data;
			auto generated = m_Sphere ? CreateSphereMeshWithTangents(PreviewSphereRadius, data)
			                          : CreateCubeMeshWithTangents(PreviewCubeSize, data);
			if (generated.IsSuccess())
				generated = CreateMeshResource(data, m_Renderer.mesh);
			if (!generated.IsSuccess()) {
				error = generated.message;
				return false;
			}
			auto geometry = std::make_shared<MeshAsset>();
			geometry->materialSlots = {"preview"};
			geometry->mesh = m_Renderer.mesh;
			if (!AssetVersionRef<MeshAsset>::TryCreate(AssetId{meshId}, 1, geometry, m_Renderer.meshVersion, error))
				return false;
		}
		const auto& material = m_SliderDocument == doc.Id() && m_SliderSource == doc.Material() && m_SliderPreview
		    ? m_SliderPreview
		    : doc.Material();
		if (m_Bound != material) {
			auto* scene = scenes.GetScene(m_Scene);
			if (!scene || m_Version == UINT64_MAX) {
				error = "Preview scene expired";
				return false;
			}
			AssetVersionRef<MaterialInstance> fixed;
			if (!AssetVersionRef<MaterialInstance>::TryCreate(
			        AssetId{m_MaterialKey}, m_Version + 1, material, fixed, error))
				return false;
			m_Renderer.materials = {fixed};
			if (!check(scene->QueueSetMeshRenderer(scene->Resolve(m_Object), m_Renderer)) ||
			    !check(scene->CommitPending()))
				return false;
			++m_Version;
			m_Bound = material;
		}
		return true;
	}

	/** @brief 预览滑动中的线性因子，拖动结束或键盘确认时才返回最终修改。 */
	bool MaterialEditor::EditUnitScalar(
	    AssetDocument& doc, const MaterialParameter& parameter, float& value, std::string& error) {
		const auto id = ImGui::GetID("##Value");
		const bool staged =
		    m_SliderId == id && m_SliderDocument == doc.Id() && m_SliderSource == doc.Material() && m_SliderPreview;
		float edited = staged ? std::get<float>(*FindMaterialParameter(*m_SliderPreview, parameter.id)) : value;
		const float minimum = float(std::max(0., parameter.minimum.value_or(0.)));
		const float maximum = float(std::min(1., parameter.maximum.value_or(1.)));
		const bool changed =
		    ImGui::SliderFloat("##Value", &edited, minimum, maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("%s", UiText("material.scalar_help", "Drag to adjust, or Ctrl+click to enter a value."));
		if (ImGui::IsItemActive()) {
			if (changed) {
				MaterialParameterOverride edit{parameter.id, edited};
				if (UpdateMaterialInstance(*doc.Material(), std::span{&edit, 1}, {}, m_SliderPreview, error)) {
					m_SliderDocument = doc.Id();
					m_SliderSource = doc.Material();
					m_SliderId = id;
				}
			}
			return false;
		}
		if (staged) {
			m_SliderSource.reset();
			m_SliderPreview.reset();
		}
		if ((!changed && !(staged && ImGui::IsItemDeactivatedAfterEdit())) || value == edited)
			return false;
		value = edited;
		return true;
	}

	void MaterialEditor::DrawProperties(Application& app, AssetDocument& doc, std::string& error,
	    const MaterialTexturePicker& texturePicker) {
		const auto draft = doc.Material();
		ImGui::SeparatorText(UiText("material.properties", "Surface properties"));
		const bool pbr = draft->definition.Get()->shadingModel == MaterialShadingModel::Pbr;
		PlaceFormLabel(UiText("material.shading_model", "Shading model"));
		ImGui::TextWrapped(
		    "%s", pbr ? UiText("material.pbr", "Physically based (PBR)") : UiText("material.unlit", "Unlit"));
		const auto parameterLabel = [](const MaterialParameter& parameter) {
			std::string key = "material.parameter." + parameter.id;
			for (char& c : key)
				if (c >= 'A' && c <= 'Z')
					c = char(c - 'A' + 'a');
			const auto& fallback = parameter.displayName.empty() ? parameter.id : parameter.displayName;
			return UiText(key.c_str(), fallback.c_str());
		};
		for (const auto& parameter : draft->definition.Get()->parameters) {
			if (parameter.type == MaterialParameterType::Texture2D)
				continue;
			const auto* old = FindMaterialParameter(*draft, parameter.id);
			if (!old)
				continue;
			PlaceFormLabel(parameterLabel(parameter));
			/* 源采样状态不可由材质草稿覆盖；显示其含义，不把内部 bit 协议当作数值控件。 */
			if (const auto* source = std::get_if<uint32_t>(old); parameter.id.ends_with("_sampler") && source) {
				const char* address[]{UiText("sampler.repeat", "Repeat"), UiText("sampler.clamp", "Clamp"),
				    UiText("sampler.mirror", "Mirror"), UiText("sampler.invalid", "Invalid")};
				const auto* linear = UiText("sampler.linear", "Linear");
				const auto* nearest = UiText("sampler.nearest", "Nearest");
				ImGui::TextWrapped(UiText("sampler.settings", "U %s / V %s, min %s / mag %s / mip %s"),
				    address[*source & 3u], address[(*source >> 2) & 3u], (*source & 16u) ? linear : nearest,
				    (*source & 32u) ? linear : nearest,
				    !(*source & 128u)     ? UiText("sampler.off", "Off")
				        : (*source & 64u) ? linear
				                          : nearest);
				continue;
			}
			auto next = *old;
			ImGui::PushID(parameter.id.c_str());
			/* 色板使用禁用前的透明度；可编辑字段不进入 ImGui 的禁用作用域。 */
			if (!parameter.allowOverride)
				ImGui::BeginDisabled();
			try {
				bool changed = false;
				if (auto* rgba = std::get_if<std::array<float, 4>>(&next); parameter.id == "base_color" && rgba) {
					changed = m_ColorPicker.Draw(*rgba, true, pbr ? 1.f : 1e8f);
				} else if (auto* rgb = std::get_if<std::array<float, 3>>(&next); parameter.id == "emissive" && rgb) {
					std::array<float, 4> color{(*rgb)[0], (*rgb)[1], (*rgb)[2], 1.f};
					if (m_ColorPicker.Draw(color, false)) {
						std::copy_n(color.begin(), 3, rgb->begin());
						changed = true;
					}
					PlaceFormLabel(UiText("material.emissive_strength", "Emission strength"));
					const float peak = *std::max_element(rgb->begin(), rgb->end());
					float strength = peak;
					if (ImGui::InputFloat(
					        "##EmissionStrength", &strength, 0.f, 0.f, "%.7g", ImGuiInputTextFlags_EnterReturnsTrue)) {
						if (!(strength >= 0.f && strength <= 1e8f))
							error = UiText("material.emissive_invalid", "Emission strength must be in [0,1e8].");
						else if (strength != peak) {
							for (float& channel : *rgb)
								channel = peak > 0.f ? (channel / peak) * strength : strength;
							changed = true;
						}
					}
					ImGui::SetItemTooltip("%s",
					    UiText("material.emissive_strength_hint",
					        "Peak linear emission channel. Changing strength preserves hue. Raising strength from black starts with white light."));
				} else if (auto* scalar = std::get_if<float>(&next); scalar &&
				    (parameter.id == "metallic" || parameter.id == "roughness" ||
				        parameter.id == "occlusion_strength")) {
					changed = EditUnitScalar(doc, parameter, *scalar, error);
				} else
					changed = EditValue(next);
				if (changed) {
					MaterialParameterOverride edit{parameter.id, std::move(next)};
					ChangeMaterial(doc, std::span{&edit, 1}, {}, error, &app.GetAssets());
				}
			} catch (const std::invalid_argument& exception) {
				error = exception.what();
			}
			if (!parameter.allowOverride)
				ImGui::EndDisabled();
			ImGui::PopID();
		}
		bool firstMap = true;
		for (const auto& parameter : draft->definition.Get()->parameters) {
			if (parameter.type != MaterialParameterType::Texture2D)
				continue;
			const auto* value = FindMaterialParameter(*draft, parameter.id);
			const auto* image = value ? std::get_if<AssetHandle<ImageAsset>>(value) : nullptr;
			if (!image)
				continue;
			if (firstMap) {
				ImGui::SeparatorText(UiText("material.texture_maps", "Texture maps"));
				firstMap = false;
			}
			ImGui::PushID(parameter.id.c_str());
			PlaceFormLabel(parameterLabel(parameter));
			ImGui::BeginDisabled(!parameter.allowOverride);
			auto chosen = *image;
			if (texturePicker) {
				if (texturePicker(parameter, chosen) && chosen.id != image->id)
					SetMaterialTexture(doc, app.GetAssets(), parameter, chosen, error);
			} else {
				ImGui::TextUnformatted(image->HasValue() ? UiText("material.texture_assigned", "Assigned")
				                                         : UiText("material.texture_empty", "None"));
			}
			ImGui::EndDisabled();
			ImGui::PopID();
		}
		const auto allowed = draft->definition.Get()->allowedStateOverrides;
		auto permits = [allowed](MaterialStateOverride key) { return (allowed & static_cast<uint32_t>(key)) != 0; };
		MaterialStateOverrides states;
		int cull = static_cast<int>(draft->renderState.cull), blend = static_cast<int>(draft->renderState.blend);
		bool depthTest = draft->renderState.depthTest, depthWrite = draft->renderState.depthWrite;
		const char* culls[]{UiText("material.cull_none", "None"), UiText("material.cull_back", "Back"),
		    UiText("material.cull_front", "Front")};
		const char* blends[]{UiText("material.blend_opaque", "Opaque"), UiText("material.blend_alpha", "Alpha"),
		    UiText("material.blend_add", "Additive")};
		if (allowed)
			ImGui::SeparatorText(UiText("material.render_state", "Render state"));
		if (permits(MaterialStateOverride::Cull)) {
			PlaceFormLabel(UiText("material.cull", "Cull"));
			if (ImGui::Combo("###MaterialCull", &cull, culls, 3))
				states.cull = static_cast<MaterialCullMode>(cull);
		}
		if (permits(MaterialStateOverride::Blend)) {
			PlaceFormLabel(UiText("material.blend", "Blend"));
			if (ImGui::Combo("###MaterialBlend", &blend, blends, 3))
				states.blend = static_cast<MaterialBlendMode>(blend);
		}
		if (permits(MaterialStateOverride::DepthTest)) {
			PlaceFormLabel(UiText("material.depth_test", "Depth test"));
			if (ImGui::Checkbox("###MaterialDepthTest", &depthTest))
				states.depthTest = depthTest;
		}
		if (permits(MaterialStateOverride::DepthWrite)) {
			PlaceFormLabel(UiText("material.depth_write", "Depth write"));
			if (ImGui::Checkbox("###MaterialDepthWrite", &depthWrite))
				states.depthWrite = depthWrite;
		}
		if (states.cull || states.blend || states.depthTest || states.depthWrite)
			ChangeMaterial(doc, {}, states, error);
	}

	void MaterialEditor::Draw(Application& app, AssetDocument& doc, const PanelText& text, std::string& error,
	    const MaterialTexturePicker& texturePicker) {
		/* 按当前字号为两个区域预留可操作的宽度；窄窗口沿用同一组控件纵向排列。 */
		const bool columns = ImGui::GetContentRegionAvail().x >= ImGui::GetFontSize() * 42.f &&
		    ImGui::BeginTable("##MaterialLayout", 2,
		        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInnerV);
		if (columns) {
			ImGui::TableSetupColumn("##Properties", ImGuiTableColumnFlags_WidthStretch, .53f);
			ImGui::TableSetupColumn("##Preview", ImGuiTableColumnFlags_WidthStretch, .47f);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
		}
		DrawProperties(app, doc, error, texturePicker);
		if (columns)
			ImGui::TableNextColumn();
		ImGui::SeparatorText(text("material.preview", "Preview", nullptr).c_str());
		const auto available = ImGui::GetContentRegionAvail();
		const float previewSize = std::min(available.x,
		    std::clamp(available.y - 2.f * ImGui::GetFrameHeightWithSpacing(), ImGui::GetFontSize() * 6.f,
		        ImGui::GetFontSize() * 36.f));
		DrawPreview(app, doc, error, previewSize);
		if (columns)
			ImGui::EndTable();
	}

	void MaterialEditor::DrawPreviewControls(Application& app, const AssetDocument& doc) {
		const auto sphere = std::string(UiText("material.preview_sphere", "Sphere")) + "###Sphere";
		const auto cube = std::string(UiText("material.preview_cube", "Cube")) + "###Cube";
		if (ImGui::RadioButton(sphere.c_str(), m_Sphere) && !m_Sphere) {
			Close(app);
			m_Sphere = true;
		}
		const auto continueRow = [](const char* label) {
			const auto& style = ImGui::GetStyle();
			const float width =
			    ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label, nullptr, true).x;
			const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
			if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right)
				ImGui::SameLine();
		};
		continueRow(cube.c_str());
		if (ImGui::RadioButton(cube.c_str(), !m_Sphere) && m_Sphere) {
			Close(app);
			m_Sphere = false;
		}
		if (doc.Material()->definition.Get()->shadingModel == MaterialShadingModel::Unlit) {
			const auto guides = std::string(UiText("material.shape_guides", "Shape guides")) + "###MaterialShapeGuides";
			continueRow(guides.c_str());
			ImGui::Checkbox(guides.c_str(), &m_ShowShapeGuides);
			ImGui::SetItemTooltip("%s",
			    UiText("material.shape_guides_help",
			        "Show visible meridians or edges in the preview. Turn off to see the original unlit surface."));
		}
	}

	void MaterialEditor::DrawPreview(Application& app, AssetDocument& doc, std::string& error, float maxSize) {
		DrawPreviewControls(app, doc);
		std::string previewError;
		if (!UpdateScene(app, doc, previewError)) {
			error = std::move(previewError);
			Close(app);
			return;
		}
		const float size = std::clamp(ImGui::GetContentRegionAvail().x, 1.f, std::max(1.f, maxSize));
		const auto pixels =
		    static_cast<uint32_t>(std::clamp(size * ImGui::GetWindowViewport()->FramebufferScale.x, 1.f, 2048.f));
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, (ImGui::GetContentRegionAvail().x - size) * .5f));
		const ImVec2 imageSize{size, size};
		ImGui::InvisibleButton("##MaterialOrbit", imageSize);
		const auto imageMin = ImGui::GetItemRectMin();
		const auto imageMax = ImGui::GetItemRectMax();
		RenderSurfaceSnapshot surface;
		const bool hasImage = app.GetEditorPreview(MaterialSurface, surface) && surface.textureToken;
		if (hasImage)
			ImGui::GetWindowDrawList()->AddImage(
			    ImTextureRef(static_cast<ImTextureID>(surface.textureToken)), imageMin, imageMax);
		const auto& io = ImGui::GetIO();
		const bool hovered = ImGui::IsItemHovered();
		if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			m_Orbit = {};
			++m_ViewRevision;
		} else if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !io.AppFocusLost) {
			/* 同样比例的拖动在不同字号和尺寸的预览中转过同样的角度。 */
			const float degreesPerPixel = 180.f / size;
			m_Orbit.azimuth = std::remainder(m_Orbit.azimuth - io.MouseDelta.x * degreesPerPixel, 360.f);
			m_Orbit.elevation = std::clamp(m_Orbit.elevation + io.MouseDelta.y * degreesPerPixel, -85.f, 85.f);
		}
		if (hovered && !io.KeyShift && !io.KeyCtrl && ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY) &&
		    io.MouseWheel != 0 && ImGui::GetCurrentContext()->WheelingWindowScrolledFrame != ImGui::GetFrameCount())
			m_Orbit.distance = std::clamp(m_Orbit.distance * std::exp2(-io.MouseWheel / 6.f), 1.6f, 8.f);
		Math::TransformTRS camera;
		camera.rotation = glm::angleAxis(glm::radians(m_Orbit.azimuth), Math::Vec3(0, 1, 0)) *
		    glm::angleAxis(glm::radians(-m_Orbit.elevation), Math::Vec3(1, 0, 0));
		camera.translation = camera.rotation * Math::Vec3(0, 0, m_Orbit.distance);
		CameraLens lens;
		lens.nearPlane = .05f;
		lens.farPlane = 20.f;
		const bool submitted = app.SetEditorCameraPreview(m_Scene, MaterialSurface, {pixels, pixels}, camera, lens,
		    m_ViewRevision, previewError, {.showNormals = false});
		if (!submitted)
			error = std::move(previewError);
		if (hasImage && m_ShowShapeGuides &&
		    doc.Material()->definition.Get()->shadingModel == MaterialShadingModel::Unlit) {
			/* 同尺寸的纹理在 UI 采样前按新姿态重绘；尺寸变化时，UI 仍持有旧目标的纹理与相机。 */
			if (submitted && surface.extent == Extent2D{pixels, pixels} &&
			    surface.view.matrices.position != camera.translation) {
				Math::Mat4 world;
				const auto status = Math::ComposeTRS(camera, world);
				if (status.IsSuccess()) {
					const auto projection =
					    BuildCameraMatrices(world, lens, surface.view.viewport, surface.view.matrices);
					if (!projection.IsSuccess())
						error = projection.message;
				} else
					error = status.message;
			}
			DrawShapeGuides(surface.view, imageMin, imageSize, m_Sphere);
		}
		ImGui::TextWrapped(
		    "%s", UiText("material.preview_orbit_help", "Drag to orbit · Scroll to zoom · Double-click to reset"));
	}
}
